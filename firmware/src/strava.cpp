#include "strava.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <vector>

#include "certs_gen.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {

struct Run {
  int64_t id;
  float m;
  uint32_t s;
  int32_t local;  // start time as local wall-clock seconds since epoch
};

struct Latest {
  int64_t id = 0;
  int32_t local = INT32_MIN;
  char name[48] = "";
};

const char* API = "https://www.strava.com/api/v3";
const char* TOKEN_URL = "https://www.strava.com/oauth/token";
const char* RUNS_FILE = "/runs.bin";
const uint32_t MAGIC = 0x4E4D5232;

Preferences prefs;
String accessToken, refreshToken, name;
uint32_t expiresAt = 0;
std::vector<Run> runs;
Latest latest;
strava::Totals tot;

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// "2026-11-10T07:00:00Z" -> seconds since epoch, ignoring the zone suffix.
int32_t parseIso(const char* s) {
  int y, mo, d, h, mi, se;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  return (int32_t)(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se);
}

bool countable(const char* t, bool walks) {
  if (!t) return false;
  if (!strcmp(t, "Run") || !strcmp(t, "TrailRun") || !strcmp(t, "VirtualRun")) return true;
  return walks && (!strcmp(t, "Walk") || !strcmp(t, "Hike"));
}

time_t localYearStart(int year) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mday = 1;
  t.tm_isdst = -1;
  return mktime(&t);
}

void saveTokens() {
  prefs.putString("at", accessToken);
  prefs.putString("rt", refreshToken);
  prefs.putUInt("exp", expiresAt);
}

void recompute() {
  tot.meters = 0;
  tot.runs = runs.size();
  const Run* best = nullptr;
  for (const Run& r : runs) {
    tot.meters += r.m;
    if (!best || r.local > best->local) best = &r;
  }
  tot.last = RunInfo();
  if (best) {
    tot.last.valid = true;
    tot.last.meters = best->m;
    tot.last.moving_s = best->s;
    tot.last.start_local = best->local;
    strlcpy(tot.last.name, best->id == latest.id && latest.name[0] ? latest.name : "Run", sizeof tot.last.name);
  }
}

void saveRuns() {
  File f = LittleFS.open(RUNS_FILE, "w");
  if (!f) return;
  uint32_t hdr[3] = {MAGIC, (uint32_t)tot.year, (uint32_t)runs.size()};
  f.write((const uint8_t*)hdr, sizeof hdr);
  if (!runs.empty()) f.write((const uint8_t*)runs.data(), runs.size() * sizeof(Run));
  f.write((const uint8_t*)&latest, sizeof latest);
  f.close();
}

void loadRuns() {
  File f = LittleFS.open(RUNS_FILE, "r");
  if (!f) return;
  uint32_t hdr[3];
  if (f.read((uint8_t*)hdr, sizeof hdr) == sizeof hdr && hdr[0] == MAGIC && hdr[2] < 5000) {
    runs.resize(hdr[2]);
    size_t want = hdr[2] * sizeof(Run);
    if (f.read((uint8_t*)runs.data(), want) == want && f.read((uint8_t*)&latest, sizeof latest) == sizeof latest) {
      tot.year = hdr[1];
    } else {
      runs.clear();
      latest = Latest();
    }
  }
  f.close();
}

int call(bool post, const String& url, const String& form, JsonDocument& doc, JsonDocument* filter) {
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  client.setTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be parsed straight off the socket
  http.setTimeout(15000);
  if (!http.begin(client, url)) return -1;
  if (!post && accessToken.length()) http.addHeader("Authorization", "Bearer " + accessToken);
  int code;
  if (post) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST(form);
  } else {
    code = http.GET();
  }
  if (code == 200) {
    DeserializationError e = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                    : deserializeJson(doc, http.getStream());
    if (e) code = -2;
  }
  http.end();
  return code;
}

strava::Result fromCode(int code) {
  if (code == 200) return strava::OK;
  if (code == 401 || code == 400) return strava::AUTH;
  if (code == 429) return strava::RATE;
  return strava::NET;
}

String tokenForm(const char* grant) {
  return String("client_id=") + STRAVA_CLIENT_ID + "&client_secret=" + STRAVA_CLIENT_SECRET + "&grant_type=" + grant;
}

strava::Result ensureToken(bool force) {
  if (refreshToken.isEmpty()) return strava::AUTH;
  if (!force && accessToken.length() && time(nullptr) < (time_t)expiresAt - 300) return strava::OK;
  JsonDocument d;
  int code = call(true, TOKEN_URL, tokenForm("refresh_token") + "&refresh_token=" + refreshToken, d, nullptr);
  if (code != 200) return fromCode(code);
  accessToken = d["access_token"] | "";
  refreshToken = d["refresh_token"] | refreshToken.c_str();
  expiresAt = d["expires_at"] | 0;
  saveTokens();
  return strava::OK;
}

strava::Result getJson(const String& url, JsonDocument& doc, JsonDocument* filter) {
  strava::Result r = ensureToken(false);
  if (r != strava::OK) return r;
  int code = call(false, url, "", doc, filter);
  if (code == 401) {
    r = ensureToken(true);
    if (r != strava::OK) return r;
    doc.clear();
    code = call(false, url, "", doc, filter);
  }
  return fromCode(code);
}

JsonDocument& activityFilter() {
  static JsonDocument f;
  if (f.isNull()) {
    JsonObject o = f[0].to<JsonObject>();
    for (const char* k : {"id", "name", "distance", "moving_time", "start_date_local", "sport_type", "type"}) o[k] = true;
  }
  return f;
}

struct Item {
  Run run;
  int year;
  bool counts;
  const char* name;
};

Item parse(JsonObject o, bool walks) {
  Item it;
  const char* local = o["start_date_local"] | "";
  it.run.id = o["id"] | (int64_t)0;
  it.run.m = o["distance"] | 0.0f;
  it.run.s = o["moving_time"] | 0;
  it.run.local = parseIso(local);
  it.year = atoi(local);
  const char* t = o["sport_type"] | (const char*)nullptr;
  if (!t) t = o["type"] | "";
  it.counts = countable(t, walks);
  it.name = o["name"] | "Run";
  return it;
}

void consider(const Item& it) {
  if (it.run.local >= latest.local) {
    latest.id = it.run.id;
    latest.local = it.run.local;
    strlcpy(latest.name, it.name, sizeof latest.name);
  }
}

}  // namespace

namespace strava {

void begin() {
  prefs.begin("strava", false);
  accessToken = prefs.getString("at", "");
  refreshToken = prefs.getString("rt", "");
  expiresAt = prefs.getUInt("exp", 0);
  name = prefs.getString("name", "");
  tot.lastYear = prefs.getInt("lyY", 0);
  tot.lastYearMeters = prefs.getFloat("lyM", 0);
  loadRuns();
  recompute();
}

bool configured() { return strlen(STRAVA_CLIENT_ID) > 0 && strlen(STRAVA_CLIENT_SECRET) > 0; }
bool linked() { return refreshToken.length() > 0; }
String athleteName() { return name; }
const Totals& totals() { return tot; }

String authorizeUrl(const String& state) {
  String redirect = LINK_PAGE_URL;
  redirect.replace(":", "%3A");
  redirect.replace("/", "%2F");
  return String("https://www.strava.com/oauth/mobile/authorize?client_id=") + STRAVA_CLIENT_ID +
         "&redirect_uri=" + redirect + "&response_type=code&approval_prompt=auto&scope=read,activity:read_all&state=" +
         state;
}

Result exchange(const String& code, const String& scope) {
  if (!configured()) return CONFIG;
  if (scope.indexOf("activity:read") < 0) return SCOPE;
  JsonDocument d;
  int c = call(true, TOKEN_URL, tokenForm("authorization_code") + "&code=" + code, d, nullptr);
  if (c != 200) return fromCode(c);
  accessToken = d["access_token"] | "";
  refreshToken = d["refresh_token"] | "";
  expiresAt = d["expires_at"] | 0;
  String first = d["athlete"]["firstname"] | "";
  String last = d["athlete"]["lastname"] | "";
  first.trim();
  last.trim();
  name = first;
  if (last.length()) name += " " + last.substring(0, 1) + ".";
  saveTokens();
  prefs.putString("name", name);
  runs.clear();
  latest = Latest();
  tot = Totals();
  prefs.putInt("lyY", 0);
  LittleFS.remove(RUNS_FILE);
  recompute();
  return linked() ? OK : AUTH;
}

Result sync(int year, bool full, bool walks) {
  if (!linked()) return AUTH;
  if (tot.year != year) full = true;
  Result r;
  if (full) {
    std::vector<Run> fresh;
    Latest keep = latest;
    latest = Latest();
    time_t after = localYearStart(year) - 86400;
    for (int page = 1; page <= 30; page++) {
      JsonDocument doc;
      r = getJson(String(API) + "/athlete/activities?per_page=100&page=" + page + "&after=" + (long)after, doc,
                  &activityFilter());
      if (r != OK) {
        latest = keep;
        return r;
      }
      JsonArray a = doc.as<JsonArray>();
      for (JsonObject o : a) {
        Item it = parse(o, walks);
        if (it.counts && it.year == year) {
          fresh.push_back(it.run);
          consider(it);
        }
      }
      if (a.size() < 100) break;
    }
    runs.swap(fresh);
    tot.year = year;
  } else {
    JsonDocument doc;
    r = getJson(String(API) + "/athlete/activities?per_page=30", doc, &activityFilter());
    if (r != OK) return r;
    JsonArray a = doc.as<JsonArray>();
    if (a.size() == 0) {
      runs.clear();
    } else {
      int32_t oldest = INT32_MAX;
      std::vector<int64_t> seen;
      for (JsonObject o : a) {
        Item it = parse(o, walks);
        if (it.run.local < oldest) oldest = it.run.local;
        if (!it.counts || it.year != year) continue;
        seen.push_back(it.run.id);
        bool found = false;
        for (Run& x : runs) {
          if (x.id == it.run.id) {
            x = it.run;
            found = true;
          }
        }
        if (!found) runs.push_back(it.run);
        consider(it);
      }
      // Anything we hold inside the window Strava just returned, but that it didn't list, was deleted or changed type.
      for (size_t i = 0; i < runs.size();) {
        bool keep = runs[i].local < oldest;
        for (int64_t id : seen) keep = keep || id == runs[i].id;
        if (keep) i++;
        else runs.erase(runs.begin() + i);
      }
    }
  }
  recompute();
  saveRuns();
  return OK;
}

Result syncLastYear(int year, bool walks) {
  if (!linked()) return AUTH;
  float meters = 0;
  time_t after = localYearStart(year) - 86400, before = localYearStart(year + 1) + 86400;
  for (int page = 1; page <= 30; page++) {
    JsonDocument doc;
    Result r = getJson(String(API) + "/athlete/activities?per_page=100&page=" + page + "&after=" + (long)after +
                           "&before=" + (long)before,
                       doc, &activityFilter());
    if (r != OK) return r;
    JsonArray a = doc.as<JsonArray>();
    for (JsonObject o : a) {
      Item it = parse(o, walks);
      if (it.counts && it.year == year) meters += it.run.m;
    }
    if (a.size() < 100) break;
  }
  tot.lastYear = year;
  tot.lastYearMeters = meters;
  prefs.putInt("lyY", year);
  prefs.putFloat("lyM", meters);
  return OK;
}

void unlink() {
  if (accessToken.length() && WiFi.status() == WL_CONNECTED) {
    JsonDocument d;
    call(true, "https://www.strava.com/oauth/deauthorize", "access_token=" + accessToken, d, nullptr);
  }
  prefs.clear();
  accessToken = refreshToken = name = "";
  expiresAt = 0;
  runs.clear();
  latest = Latest();
  tot = Totals();
  LittleFS.remove(RUNS_FILE);
  recompute();
}

const char* describe(Result r) {
  switch (r) {
    case OK: return "OK";
    case NET: return "Couldn't reach Strava";
    case AUTH: return "Strava needs to be linked again";
    case RATE: return "Strava is busy, will retry";
    case SCOPE: return "Activity access wasn't allowed";
    case CONFIG: return "Strava keys missing in firmware";
  }
  return "";
}

}  // namespace strava
