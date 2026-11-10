#include "source.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <time.h>

#include <vector>

#include "activities.h"
#include "net.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

using acts::Act;

namespace {

Preferences prefs;
src::Kind k = src::NONE;
String at, rt, name, garminKey, polarUser, appleTok;
uint32_t expAt = 0;

const char* GOOGLE_SCOPE = "https://www.googleapis.com/auth/googlehealth.activity_and_fitness.readonly";
const char* GARMIN = "https://personal.connectapi.garmin.com";
const char* POLAR = "https://www.polaraccesslink.com/v3";
const char* GHEALTH = "https://health.googleapis.com/v4/users/me/dataTypes/exercise/dataPoints";
const char* STRAVA = "https://www.strava.com/api/v3";

void save() {
  prefs.putUChar("k", k);
  prefs.putString("at", at);
  prefs.putString("rt", rt);
  prefs.putUInt("exp", expAt);
  prefs.putString("name", name);
  prefs.putString("gk", garminKey);
  prefs.putString("pu", polarUser);
  prefs.putString("ap", appleTok);
}

src::Result fromCode(int c) {
  if (c == 200 || c == 201 || c == 204) return src::OK;
  if (c == 401 || c == 400 || c == 403) return src::AUTH;
  if (c == 429) return src::RATE;
  return src::NET;
}

int64_t hash64(const char* s) {
  uint64_t h = 1469598103934665603ULL;
  while (s && *s) h = (h ^ (uint8_t)*s++) * 1099511628211ULL;
  return (int64_t)(h >> 1);
}

// "PT2H44M10S" or "1800s"
uint32_t parseDuration(const char* s) {
  if (!s) return 0;
  if (s[0] != 'P') return (uint32_t)atof(s);
  uint32_t total = 0;
  float n = 0;
  for (const char* p = s; *p; p++) {
    if (isdigit(*p) || *p == '.') {
      n = atof(p);
      while (isdigit(p[1]) || p[1] == '.') p++;
    } else if (*p == 'H') total += n * 3600, n = 0;
    else if (*p == 'M') total += n * 60, n = 0;
    else if (*p == 'S') total += n, n = 0;
  }
  return total;
}

String ymd(int y, int m, int d) {
  char b[12];
  snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
  return b;
}

String redirectUri() { return net::urlEncode(LINK_PAGE_URL); }

String basic(const char* id, const char* secret) {
  String raw = String(id) + ":" + secret;
  unsigned char out[200];
  size_t n = 0;
  mbedtls_base64_encode(out, sizeof out, &n, (const unsigned char*)raw.c_str(), raw.length());
  return "Basic " + String((const char*)out).substring(0, n);
}

// ---- token handling -------------------------------------------------------

src::Result oauthToken(const String& form) {
  net::Req r;
  r.method = "POST";
  r.body = form;
  if (k == src::STRAVA) {
    r.url = "https://www.strava.com/oauth/token";
    r.body += String("&client_id=") + STRAVA_CLIENT_ID + "&client_secret=" + STRAVA_CLIENT_SECRET;
  } else if (k == src::GOOGLE) {
    r.url = "https://oauth2.googleapis.com/token";
    r.body += String("&client_id=") + net::urlEncode(GOOGLE_CLIENT_ID) + "&client_secret=" + GOOGLE_CLIENT_SECRET;
  } else if (k == src::POLAR) {
    r.url = "https://polarremote.com/v2/oauth2/token";
    r.auth = basic(POLAR_CLIENT_ID, POLAR_CLIENT_SECRET);
  } else {
    return src::CONFIG;
  }
  JsonDocument d;
  int c = net::call(r, d);
  if (c != 200) return fromCode(c);
  at = d["access_token"] | "";
  rt = d["refresh_token"] | rt.c_str();
  uint32_t exp = d["expires_at"] | 0;
  if (!exp) exp = time(nullptr) + (uint32_t)(d["expires_in"] | 3600);
  expAt = exp;
  if (k == src::POLAR) polarUser = String((long long)(d["x_user_id"] | 0LL));
  if (k == src::STRAVA) {
    String f = d["athlete"]["firstname"] | "", l = d["athlete"]["lastname"] | "";
    f.trim();
    l.trim();
    if (f.length()) name = f + (l.length() ? " " + l.substring(0, 1) + "." : "");
  }
  return at.length() ? src::OK : src::AUTH;
}

src::Result garminToken() {
  net::Req r;
  r.method = "POST";
  r.url = String(GARMIN) + "/personalapitoken-service/personal-api/personal-api-key/token";
  r.auth = "ApiKey " + garminKey;
  JsonDocument d;
  int c = net::call(r, d);
  if (c != 200) return fromCode(c);
  const char* t = d["access_token"] | (const char*)nullptr;
  if (!t) t = d["accessToken"] | (const char*)nullptr;
  if (!t) t = d["token"] | "";
  at = t;
  expAt = time(nullptr) + 23 * 3600;  // valid 24 h per Garmin's FAQ
  return at.length() ? src::OK : src::AUTH;
}

src::Result ensureToken(bool force) {
  if (k == src::APPLE) return src::OK;
  if (k == src::POLAR) return at.length() ? src::OK : src::AUTH;  // Polar tokens don't expire
  if (!force && at.length() && time(nullptr) < (time_t)expAt - 300) return src::OK;
  src::Result r = k == src::GARMIN ? garminToken() : oauthToken("grant_type=refresh_token&refresh_token=" + net::urlEncode(rt));
  if (r == src::OK) save();
  return r;
}

src::Result get(const String& url, JsonDocument& doc, JsonDocument* filter = nullptr) {
  src::Result r = ensureToken(false);
  if (r != src::OK) return r;
  net::Req q;
  q.url = url;
  q.filter = filter;
  q.auth = "Bearer " + at;
  int c = net::call(q, doc);
  if (c == 401 && k != src::POLAR) {
    if ((r = ensureToken(true)) != src::OK) return r;
    q.auth = "Bearer " + at;
    doc.clear();
    c = net::call(q, doc);
  }
  return fromCode(c);
}

bool isRun(String t, bool walks) {
  t.toLowerCase();
  if (t.indexOf("run") >= 0) return true;  // Run, TrailRun, trail_running, RUNNING, TREADMILL_RUNNING...
  return walks && (t.indexOf("walk") >= 0 || t.indexOf("hik") >= 0);
}

// ---- per-source fetchers ----------------------------------------------------
// Each returns activities; `windowed` tells the caller the list is the latest N (deletions detectable).

src::Result fetchStrava(time_t after, time_t before, int maxPages, bool walks, std::vector<Act>& out) {
  for (int page = 1; page <= maxPages; page++) {
    JsonDocument doc;
    String u = String(STRAVA) + "/athlete/activities?per_page=" + (maxPages == 1 ? 30 : 100) + "&page=" + page;
    if (after) u += "&after=" + String((long)after);
    if (before) u += "&before=" + String((long)before);
    src::Result r = get(u, doc);
    if (r != src::OK) return r;
    JsonArray a = doc.as<JsonArray>();
    for (JsonObject o : a) {
      Act x;
      const char* local = o["start_date_local"] | "";
      x.id = o["id"] | 0LL;
      x.m = o["distance"] | 0.0f;
      x.s = o["moving_time"] | 0;
      x.local = acts::parseIso(local);
      x.year = atoi(local);
      const char* t = o["sport_type"] | (const char*)nullptr;
      x.counts = isRun(t ? t : (o["type"] | ""), walks);
      strlcpy(x.name, o["name"] | "Run", sizeof x.name);
      out.push_back(x);
    }
    if (a.size() < (maxPages == 1 ? 30u : 100u)) break;
  }
  return src::OK;
}

src::Result fetchGarmin(const String& from, const String& to, int maxPages, bool walks, std::vector<Act>& out) {
  const int per = maxPages == 1 ? 30 : 100;
  for (int page = 0; page < maxPages; page++) {
    JsonDocument doc;
    String u = String(GARMIN) + "/activitylist-service/activities/search/activities?limit=" + per + "&start=" + page * per;
    if (from.length()) u += "&startDate=" + from + "&endDate=" + to;
    src::Result r = get(u, doc);
    if (r != src::OK) return r;
    JsonArray a = doc.as<JsonArray>();
    for (JsonObject o : a) {
      Act x;
      const char* local = o["startTimeLocal"] | "";
      x.id = o["activityId"] | 0LL;
      x.m = o["distance"] | 0.0f;
      float mv = o["movingDuration"] | 0.0f;
      x.s = mv > 0 ? mv : (o["duration"] | 0.0f);
      x.local = acts::parseIso(local);
      x.year = atoi(local);
      x.counts = isRun(o["activityType"]["typeKey"] | "", walks);
      strlcpy(x.name, o["activityName"] | "Run", sizeof x.name);
      out.push_back(x);
      if (!name.length()) {
        String n = o["ownerFullName"] | "";
        int sp = n.indexOf(' ');
        if (sp > 0) n = n.substring(0, sp + 2) + ".";
        name = n;
        prefs.putString("name", name);
      }
    }
    if (a.size() < (size_t)per) break;
  }
  return src::OK;
}

src::Result fetchPolar(bool walks, std::vector<Act>& out) {
  JsonDocument doc;
  src::Result r = get(String(POLAR) + "/exercises", doc);
  if (r != src::OK) return r;
  for (JsonObject o : doc.as<JsonArray>()) {
    Act x;
    const char* local = o["start_time"] | "";
    x.id = hash64(o["id"] | "");
    x.m = o["distance"] | 0.0f;
    x.s = parseDuration(o["duration"] | "");
    x.local = acts::parseIso(local);
    x.year = atoi(local);
    String sport = o["detailed_sport_info"] | (o["sport"] | "");
    x.counts = isRun(sport, walks);
    strlcpy(x.name, sport.indexOf("TRAIL") >= 0 ? "Trail Run" : sport.indexOf("TREADMILL") >= 0 ? "Treadmill Run" : "Run",
            sizeof x.name);
    out.push_back(x);
  }
  return src::OK;
}

src::Result fetchGoogle(const String& from, const String& to, bool walks, std::vector<Act>& out) {
  String filter = "exercise.interval.civil_start_time >= \"" + from + "\" AND exercise.interval.civil_start_time < \"" + to + "\"";
  String token;
  for (int page = 0; page < 40; page++) {
    JsonDocument doc;
    String u = String(GHEALTH) + "?pageSize=25&filter=" + net::urlEncode(filter);
    if (token.length()) u += "&pageToken=" + net::urlEncode(token);
    src::Result r = get(u, doc);
    if (r != src::OK) return r;
    for (JsonObject o : doc["dataPoints"].as<JsonArray>()) {
      JsonObject e = o["exercise"];
      Act x;
      x.id = hash64(o["name"] | "");
      x.m = (e["metricsSummary"]["distanceMillimeters"] | 0.0) / 1000.0;
      x.s = parseDuration(e["activeDuration"] | "");
      x.local = acts::parseIso(e["interval"]["startTime"] | "") + (int32_t)parseDuration(e["interval"]["startUtcOffset"] | "0s");
      const char* off = e["interval"]["startUtcOffset"] | "0s";
      if (off[0] == '-') x.local -= 2 * (int32_t)parseDuration(off + 1);
      time_t t = x.local;
      x.year = gmtime(&t)->tm_year + 1900;
      x.counts = isRun(e["exerciseType"] | "", walks);
      strlcpy(x.name, e["displayName"] | "Run", sizeof x.name);
      out.push_back(x);
    }
    token = doc["nextPageToken"] | "";
    if (!token.length()) break;
  }
  return src::OK;
}

}  // namespace

namespace src {

void begin() {
  prefs.begin("src", false);
  k = (Kind)prefs.getUChar("k", NONE);
  at = prefs.getString("at", "");
  rt = prefs.getString("rt", "");
  expAt = prefs.getUInt("exp", 0);
  name = prefs.getString("name", "");
  garminKey = prefs.getString("gk", "");
  polarUser = prefs.getString("pu", "");
  appleTok = prefs.getString("ap", "");
  acts::begin();
}

Kind kind() { return k; }

const char* label(Kind x) {
  switch (x) {
    case STRAVA: return "Strava";
    case GARMIN: return "Garmin";
    case POLAR: return "Polar";
    case GOOGLE: return "Fitbit / Pixel Watch";
    case APPLE: return "Apple Watch";
    default: return "your watch";
  }
}

bool available(Kind x) {
  switch (x) {
    case STRAVA: return strlen(STRAVA_CLIENT_ID) && strlen(STRAVA_CLIENT_SECRET);
    case POLAR: return strlen(POLAR_CLIENT_ID) && strlen(POLAR_CLIENT_SECRET);
    case GOOGLE: return strlen(GOOGLE_CLIENT_ID) && strlen(GOOGLE_CLIENT_SECRET);
    case GARMIN:
    case APPLE: return true;
    default: return false;
  }
}

bool linked() {
  switch (k) {
    case GARMIN: return garminKey.length() > 0;
    case POLAR: return at.length() > 0;
    case APPLE: return appleTok.length() > 0;
    case STRAVA:
    case GOOGLE: return rt.length() > 0;
    default: return false;
  }
}

bool hasHistory() { return k == STRAVA || k == GARMIN || k == GOOGLE; }
String athleteName() { return name; }

String authorizeUrl(Kind x, const String& state) {
  String st = net::urlEncode(state + "|" + String((int)x));
  switch (x) {
    case STRAVA:
      return String("https://www.strava.com/oauth/mobile/authorize?client_id=") + STRAVA_CLIENT_ID + "&redirect_uri=" +
             redirectUri() + "&response_type=code&approval_prompt=auto&scope=read,activity:read_all&state=" + st;
    case POLAR:
      return String("https://flow.polar.com/oauth2/authorization?response_type=code&client_id=") + POLAR_CLIENT_ID +
             "&redirect_uri=" + redirectUri() + "&state=" + st;
    case GOOGLE:
      return String("https://accounts.google.com/o/oauth2/v2/auth?response_type=code&access_type=offline&prompt=consent&client_id=") +
             net::urlEncode(GOOGLE_CLIENT_ID) + "&redirect_uri=" + redirectUri() + "&scope=" + net::urlEncode(GOOGLE_SCOPE) +
             "&state=" + st;
    default: return "";
  }
}

void resetFor(Kind x) {
  unlink();
  k = x;
}

Result exchange(Kind x, const String& code, const String& scope) {
  if (!available(x) || (x != STRAVA && x != POLAR && x != GOOGLE)) return CONFIG;
  if (x == STRAVA && scope.indexOf("activity:read") < 0) return SCOPE;
  if (x == GOOGLE && scope.length() && scope.indexOf("activity_and_fitness") < 0) return SCOPE;
  resetFor(x);
  Result r = oauthToken("grant_type=authorization_code&code=" + net::urlEncode(code) + "&redirect_uri=" + redirectUri());
  if (r == OK && x == POLAR) {
    net::Req q;
    q.method = "POST";
    q.url = String(POLAR) + "/users";
    q.contentType = "application/json";
    q.body = "{\"member-id\":\"nextmile-" + WiFi.macAddress() + "\"}";
    q.auth = "Bearer " + at;
    JsonDocument d;
    int c = net::call(q, d);
    if (c != 200 && c != 201 && c != 409) r = fromCode(c);  // 409: already registered
  }
  if (r != OK) {
    k = NONE;
    at = rt = "";
  }
  save();
  return r;
}

Result linkGarmin(const String& apiKey) {
  resetFor(GARMIN);
  garminKey = apiKey;
  garminKey.trim();
  Result r = garminToken();
  if (r != OK) {
    garminKey = "";
    k = NONE;
  }
  save();
  return r;
}

void linkApple() {
  resetFor(APPLE);
  char b[9];
  snprintf(b, sizeof b, "%08lx", (unsigned long)esp_random());
  appleTok = b;
  save();
}

String appleToken() { return appleTok; }

// {"year":2026,"miles":412.6,"runs":87,"last":{"name":"Morning Run","miles":5.2,"minutes":50.4,"start":"2026-11-12 07:00"},"last_year_miles":545}
Result applePush(const String& token, const String& json) {
  if (k != APPLE || !appleTok.length() || token != appleTok) return AUTH;
  JsonDocument d;
  if (deserializeJson(d, json)) return NET;
  Act last;
  JsonObject l = d["last"];
  const char* start = l["start"] | "";
  last.id = 1;
  last.m = (l["miles"] | 0.0f) * 1609.344f;
  last.s = (l["minutes"] | 0.0f) * 60;
  last.local = acts::parseIso(start);
  last.year = atoi(start);
  last.counts = true;
  strlcpy(last.name, l["name"] | "Run", sizeof last.name);
  int year = d["year"] | last.year;
  if (!year) return NET;
  acts::setAbsolute(year, (d["miles"] | 0.0f) * 1609.344f, d["runs"] | 0, last);
  float ly = d["last_year_miles"] | -1.0f;
  if (ly >= 0) acts::setLastYear(year - 1, ly * 1609.344f);
  return OK;
}

Result sync(int year, bool full, bool walks) {
  if (!linked()) return AUTH;
  if (k == APPLE) return OK;  // pushed from the iPhone
  if (acts::totals().year != year) full = true;
  std::vector<Act> v;
  Result r;
  switch (k) {
    case STRAVA:
      r = full ? fetchStrava(acts::localYearStart(year) - 86400, 0, 30, walks, v) : fetchStrava(0, 0, 1, walks, v);
      break;
    case GARMIN:
      r = full ? fetchGarmin(ymd(year, 1, 1), ymd(year, 12, 31), 30, walks, v) : fetchGarmin("", "", 1, walks, v);
      break;
    case GOOGLE: {
      time_t from = full ? acts::localYearStart(year) : time(nullptr) - 30 * 86400;
      struct tm t;
      localtime_r(&from, &t);
      r = fetchGoogle(ymd(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday), ymd(year + 1, 1, 1), walks, v);
      if (r == OK && !full) {
        acts::mergeWindow(year, v);
        return OK;
      }
      break;
    }
    case POLAR:
      r = fetchPolar(walks, v);
      if (r == OK) acts::merge(year, v);
      return r;
    default: return CONFIG;
  }
  if (r != OK) return r;
  if (full) acts::replaceYear(year, v);
  else acts::mergeWindow(year, v);
  return OK;
}

Result syncLastYear(int year, bool walks) {
  if (!linked()) return AUTH;
  if (!hasHistory()) {
    acts::setLastYear(year, acts::totals().lastYear == year ? acts::totals().lastYearMeters : 0);
    return OK;
  }
  std::vector<Act> v;
  Result r;
  if (k == STRAVA) r = fetchStrava(acts::localYearStart(year) - 86400, acts::localYearStart(year + 1) + 86400, 30, walks, v);
  else if (k == GARMIN) r = fetchGarmin(ymd(year, 1, 1), ymd(year, 12, 31), 30, walks, v);
  else r = fetchGoogle(ymd(year, 1, 1), ymd(year + 1, 1, 1), walks, v);
  if (r != OK) return r;
  float m = 0;
  for (const Act& a : v)
    if (a.counts && a.year == year) m += a.m;
  acts::setLastYear(year, m);
  return OK;
}

void unlink() {
  if (WiFi.status() == WL_CONNECTED && at.length()) {
    net::Req q;
    q.method = "POST";
    JsonDocument d;
    if (k == STRAVA) {
      q.url = "https://www.strava.com/oauth/deauthorize";
      q.body = "access_token=" + at;
      net::call(q, d);
    } else if (k == GOOGLE) {
      q.url = "https://oauth2.googleapis.com/revoke";
      q.body = "token=" + net::urlEncode(rt.length() ? rt : at);
      net::call(q, d);
    } else if (k == POLAR && polarUser.length()) {
      q.method = "DELETE";
      q.url = String(POLAR) + "/users/" + polarUser;
      q.auth = "Bearer " + at;
      net::call(q, d);
    }
  }
  prefs.clear();
  k = NONE;
  at = rt = name = garminKey = polarUser = appleTok = "";
  expAt = 0;
  acts::clear();
}

const char* describe(Result r) {
  switch (r) {
    case OK: return "OK";
    case NET: return "Couldn't reach the watch service";
    case AUTH: return "Your watch account needs to be linked again";
    case RATE: return "The watch service is busy, will retry";
    case SCOPE: return "Activity access wasn't allowed";
    case CONFIG: return "This brand isn't set up in the firmware yet";
  }
  return "";
}

}  // namespace src
