#include "source.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include <algorithm>

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
String at, rt, name, garminKey, polarUser, appleTok, verifier, mcpSession;
uint32_t expAt = 0;

#ifndef WHOOP_CLIENT_ID
#define WHOOP_CLIENT_ID ""
#define WHOOP_CLIENT_SECRET ""
#endif
#ifndef OURA_CLIENT_ID
#define OURA_CLIENT_ID ""
#define OURA_CLIENT_SECRET ""
#endif
#ifndef WITHINGS_CLIENT_ID
#define WITHINGS_CLIENT_ID ""
#define WITHINGS_CLIENT_SECRET ""
#endif
#ifndef WAHOO_CLIENT_ID
#define WAHOO_CLIENT_ID ""
#define WAHOO_CLIENT_SECRET ""
#endif

const char* IV_API = "https://intervals.icu/api/v1/athlete/0";
const char* WHOOP_API = "https://api.prod.whoop.com";
const char* OURA_API = "https://api.ouraring.com";
const char* WITHINGS_API = "https://wbsapi.withings.net";
const char* WAHOO_API = "https://api.wahooligan.com";
const char* GOOGLE_SCOPE = "https://www.googleapis.com/auth/googlehealth.activity_and_fitness.readonly";
const char* GARMIN = "https://personal.connectapi.garmin.com";
const char* POLAR = "https://www.polaraccesslink.com/v3";
const char* GHEALTH = "https://health.googleapis.com/v4/users/me/dataTypes/exercise/dataPoints";
const char* STRAVA = "https://www.strava.com/api/v3";
// COROS MCP: self-service OAuth 2.0 + PKCE, no partner approval. US region server.
// Public client from dynamic registration (redirect = LINK_PAGE_URL); it has no secret.
const char* COROS_ISSUER = "https://mcpus.coros.com";
const char* COROS_MCP = "https://mcpus.coros.com/mcp";
const char* COROS_CLIENT_ID = "4bdc75ae-07b7-4c5c-bd0f-27c0d96d9538";

void save() {
  prefs.putUChar("k", k);
  prefs.putString("at", at);
  prefs.putString("rt", rt);
  prefs.putUInt("exp", expAt);
  prefs.putString("name", name);
  prefs.putString("gk", garminKey);
  prefs.putString("pu", polarUser);
  prefs.putString("ap", appleTok);
  prefs.putString("cv", verifier);
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

// ---- PKCE (COROS) -------------------------------------------------------------

String b64url(const uint8_t* d, size_t n) {
  unsigned char out[64];
  size_t len = 0;
  mbedtls_base64_encode(out, sizeof out, &len, d, n);
  String s;
  for (size_t i = 0; i < len; i++) {
    char c = out[i];
    if (c == '=') break;
    s += c == '+' ? '-' : c == '/' ? '_' : c;
  }
  return s;
}

String newVerifier() {
  String v;
  char b[9];
  for (int i = 0; i < 6; i++) {
    snprintf(b, sizeof b, "%08lx", (unsigned long)esp_random());
    v += b;
  }
  return v;
}

String pkceChallenge(const String& v) {
  uint8_t h[32];
  mbedtls_sha256_ret((const unsigned char*)v.c_str(), v.length(), h, 0);
  return b64url(h, 32);
}

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
  } else if (k == src::COROS) {
    r.url = String(COROS_ISSUER) + "/oauth2/token";
    r.body += String("&client_id=") + COROS_CLIENT_ID;
  } else if (k == src::WHOOP) {
    r.url = String(WHOOP_API) + "/oauth/oauth2/token";
    r.body += String("&client_id=") + WHOOP_CLIENT_ID + "&client_secret=" + WHOOP_CLIENT_SECRET;
    if (form.startsWith("grant_type=refresh_token")) r.body += "&scope=offline";
  } else if (k == src::OURA) {
    r.url = String(OURA_API) + "/oauth/token";
    r.body += String("&client_id=") + OURA_CLIENT_ID + "&client_secret=" + OURA_CLIENT_SECRET;
  } else if (k == src::WAHOO) {
    r.url = String(WAHOO_API) + "/oauth/token";
    r.body += String("&client_id=") + WAHOO_CLIENT_ID + "&client_secret=" + WAHOO_CLIENT_SECRET;
  } else if (k == src::WITHINGS) {
    r.url = String(WITHINGS_API) + "/v2/oauth2";
    r.body = "action=requesttoken&" + form + "&client_id=" + WITHINGS_CLIENT_ID + "&client_secret=" + WITHINGS_CLIENT_SECRET;
  } else {
    return src::CONFIG;
  }
  JsonDocument d;
  int c = net::call(r, d);
  if (c != 200) return fromCode(c);
  JsonVariantConst t = k == src::WITHINGS ? d["body"].as<JsonVariantConst>() : d.as<JsonVariantConst>();  // Withings wraps it
  at = t["access_token"] | "";
  rt = t["refresh_token"] | rt.c_str();
  uint32_t exp = t["expires_at"] | 0;
  if (!exp) exp = time(nullptr) + (uint32_t)(t["expires_in"] | 3600);
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
  if (k == src::INTERVALS) return garminKey.length() ? src::OK : src::AUTH;
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
  q.auth = k == src::INTERVALS ? basic("API_KEY", garminKey.c_str()) : "Bearer " + at;
  int c = net::call(q, doc);
  if (c == 401 && k != src::POLAR && k != src::INTERVALS) {
    if ((r = ensureToken(true)) != src::OK) return r;
    q.auth = "Bearer " + at;
    doc.clear();
    c = net::call(q, doc);
  }
  return fromCode(c);
}

bool isRun(String t, uint8_t kinds) {
  t.toLowerCase();
  bool run = t.indexOf("run") >= 0;  // Run, TrailRun, trail_running, RUNNING, TREADMILL_RUNNING...
  bool walk = t.indexOf("walk") >= 0 || t.indexOf("hik") >= 0;
  return (run && (kinds & 1)) || (walk && (kinds & 2));
}

// ---- per-source fetchers ----------------------------------------------------
// Each returns activities; `windowed` tells the caller the list is the latest N (deletions detectable).

src::Result fetchStrava(time_t after, time_t before, int maxPages, uint8_t kinds, std::vector<Act>& out) {
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
      x.counts = isRun(t ? t : (o["type"] | ""), kinds);
      strlcpy(x.name, o["name"] | "Run", sizeof x.name);
      out.push_back(x);
    }
    if (a.size() < (maxPages == 1 ? 30u : 100u)) break;
  }
  return src::OK;
}

src::Result fetchGarmin(const String& from, const String& to, int maxPages, uint8_t kinds, std::vector<Act>& out) {
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
      x.counts = isRun(o["activityType"]["typeKey"] | "", kinds);
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

src::Result fetchPolar(uint8_t kinds, std::vector<Act>& out) {
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
    x.counts = isRun(sport, kinds);
    strlcpy(x.name, sport.indexOf("TRAIL") >= 0 ? "Trail Run" : sport.indexOf("TREADMILL") >= 0 ? "Treadmill Run" : "Run",
            sizeof x.name);
    out.push_back(x);
  }
  return src::OK;
}

src::Result fetchGoogle(const String& from, const String& to, uint8_t kinds, std::vector<Act>& out) {
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
      x.counts = isRun(e["exerciseType"] | "", kinds);
      strlcpy(x.name, e["displayName"] | "Run", sizeof x.name);
      out.push_back(x);
    }
    token = doc["nextPageToken"] | "";
    if (!token.length()) break;
  }
  return src::OK;
}

// ---- intervals.icu, WHOOP, Oura, Withings ----------------------------------

String isoUtc(time_t t) {
  char b[32];
  strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&t));
  return b;
}

String localDay(time_t t) {
  struct tm m;
  localtime_r(&t, &m);
  return ymd(m.tm_year + 1900, m.tm_mon + 1, m.tm_mday);
}

int32_t tzOffset(const char* s) {  // "-05:00"
  if (!s || (s[0] != '+' && s[0] != '-')) return 0;
  int h = 0, m = 0;
  sscanf(s + 1, "%d:%d", &h, &m);
  return (s[0] == '-' ? -1 : 1) * (h * 3600 + m * 60);
}

void setYear(Act& x) {
  time_t t = x.local;
  x.year = gmtime(&t)->tm_year + 1900;
}

const char* runName(const String& sport, bool counts) {
  String s = sport;
  s.toLowerCase();
  if (s.indexOf("trail") >= 0) return "Trail Run";
  if (s.indexOf("treadmill") >= 0 || s.indexOf("virtual") >= 0 || s.indexOf("indoor") >= 0) return "Treadmill Run";
  if (s.indexOf("run") >= 0) return "Run";
  if (s.indexOf("hik") >= 0) return "Hike";
  return counts ? "Walk" : "Workout";
}

src::Result fetchIntervals(const String& from, const String& to, uint8_t kinds, std::vector<Act>& out) {
  JsonDocument doc;
  src::Result r = get(String(IV_API) + "/activities?oldest=" + from + "&newest=" + to +
                          "&fields=id,start_date_local,type,distance,moving_time,name",
                      doc);
  if (r != src::OK) return r;
  for (JsonObject o : doc.as<JsonArray>()) {
    Act x;
    const char* local = o["start_date_local"] | "";
    x.id = hash64(o["id"].as<String>().c_str());
    x.m = o["distance"] | 0.0f;
    x.s = o["moving_time"] | 0;
    x.local = acts::parseIso(local);
    x.year = atoi(local);
    x.counts = isRun(o["type"] | "", kinds);
    strlcpy(x.name, o["name"] | runName(o["type"] | "", x.counts), sizeof x.name);
    out.push_back(x);
  }
  return src::OK;
}

src::Result fetchWhoop(const String& start, const String& end, uint8_t kinds, std::vector<Act>& out) {
  String token;
  for (int page = 0; page < 60; page++) {
    JsonDocument doc;
    String u = String(WHOOP_API) + "/developer/v2/activity/workout?limit=25&start=" + net::urlEncode(start) + "&end=" +
               net::urlEncode(end);
    if (token.length()) u += "&nextToken=" + net::urlEncode(token);
    src::Result r = get(u, doc);
    if (r != src::OK) return r;
    for (JsonObject o : doc["records"].as<JsonArray>()) {
      Act x;
      int32_t a = acts::parseIso(o["start"] | ""), b = acts::parseIso(o["end"] | "");
      x.id = hash64(o["id"] | "");
      x.m = o["score"]["distance_meter"] | 0.0f;
      x.s = b > a ? b - a : 0;
      x.local = a + tzOffset(o["timezone_offset"] | "");
      setYear(x);
      String sport = o["sport_name"] | "";
      x.counts = isRun(sport, kinds);
      strlcpy(x.name, runName(sport, x.counts), sizeof x.name);
      out.push_back(x);
    }
    token = doc["next_token"] | "";
    if (!token.length()) break;
  }
  return src::OK;
}

src::Result fetchOura(const String& from, const String& to, uint8_t kinds, std::vector<Act>& out) {
  String token;
  for (int page = 0; page < 40; page++) {
    JsonDocument doc;
    String u = String(OURA_API) + "/v2/usercollection/workout?start_date=" + from + "&end_date=" + to;
    if (token.length()) u += "&next_token=" + net::urlEncode(token);
    src::Result r = get(u, doc);
    if (r != src::OK) return r;
    for (JsonObject o : doc["data"].as<JsonArray>()) {
      Act x;
      const char* st = o["start_datetime"] | "";
      int32_t a = acts::parseIso(st), b = acts::parseIso(o["end_datetime"] | "");
      x.id = hash64(o["id"] | "");
      x.m = o["distance"] | 0.0f;
      x.s = b > a ? b - a : 0;
      x.local = a;  // already local wall clock
      x.year = atoi(st);
      String sport = o["activity"] | "";
      x.counts = isRun(sport, kinds);
      strlcpy(x.name, runName(sport, x.counts), sizeof x.name);
      out.push_back(x);
    }
    token = doc["next_token"] | "";
    if (!token.length()) break;
  }
  return src::OK;
}

// Withings workout categories: 1 walk, 2 run, 3 hiking.
src::Result fetchWithings(const String& from, const String& to, uint8_t kinds, std::vector<Act>& out) {
  src::Result r = ensureToken(false);
  if (r != src::OK) return r;
  int offset = 0;
  bool retried = false;
  for (int page = 0; page < 30; page++) {
    net::Req q;
    q.method = "POST";
    q.url = String(WITHINGS_API) + "/v2/measure";
    q.auth = "Bearer " + at;
    q.body = "action=getworkouts&startdateymd=" + from + "&enddateymd=" + to + "&data_fields=distance,effduration&offset=" + offset;
    JsonDocument d;
    int c = net::call(q, d);
    if (c != 200) return fromCode(c);
    int st = d["status"] | -1;
    if (st == 401 && !retried) {
      retried = true;
      if ((r = ensureToken(true)) != src::OK) return r;
      page--;
      continue;
    }
    if (st != 0) return st == 401 ? src::AUTH : st == 601 ? src::RATE : src::NET;
    for (JsonObject o : d["body"]["series"].as<JsonArray>()) {
      Act x;
      int cat = o["category"] | 0;
      time_t t = o["startdate"] | 0;
      struct tm lt;
      localtime_r(&t, &lt);
      x.id = o["id"] | 0LL;
      x.m = o["data"]["distance"] | 0.0f;
      x.s = o["data"]["effduration"] | 0;
      x.local = (int32_t)(acts::daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 + lt.tm_hour * 3600 +
                          lt.tm_min * 60 + lt.tm_sec);
      x.year = lt.tm_year + 1900;
      x.counts = ((kinds & 1) && cat == 2) || ((kinds & 2) && (cat == 1 || cat == 3));
      strlcpy(x.name, cat == 2 ? "Run" : cat == 3 ? "Hike" : cat == 1 ? "Walk" : "Workout", sizeof x.name);
      out.push_back(x);
    }
    if (!(d["body"]["more"] | false)) break;
    offset = d["body"]["offset"] | 0;
  }
  return src::OK;
}

float num(JsonVariantConst v) { return v.is<const char*>() ? atof(v.as<const char*>()) : v.as<float>(); }

// Wahoo workout_type_id: 1 running, 3 track, 4 trail, 5 treadmill; 6-8 walking, 9 hiking.
// The list is newest first and has no date filter, so page back until we pass `from`.
src::Result fetchWahoo(time_t from, time_t to, uint8_t kinds, std::vector<Act>& out) {
  JsonDocument filter;
  JsonObject f = filter["workouts"][0].to<JsonObject>();
  f["id"] = f["starts"] = f["name"] = f["workout_type_id"] = true;
  f["workout_summary"]["distance_accum"] = f["workout_summary"]["duration_active_accum"] = true;
  const int per = 50;
  for (int page = 1; page <= 40; page++) {
    JsonDocument doc;
    src::Result r = get(String(WAHOO_API) + "/v1/workouts?per_page=" + per + "&page=" + page, doc, &filter);
    if (r != src::OK) return r;
    JsonArray a = doc["workouts"].as<JsonArray>();
    bool older = false;
    for (JsonObject o : a) {
      time_t utc = acts::parseIso(o["starts"] | "");
      if (utc < from) {
        older = true;
        continue;
      }
      JsonObjectConst s = o["workout_summary"];
      if (utc > to || s.isNull()) continue;  // planned workouts have no summary
      int t = o["workout_type_id"] | -1;
      bool run = t == 1 || t == 3 || t == 4 || t == 5, walk = t >= 6 && t <= 9;
      Act x;
      struct tm lt;
      localtime_r(&utc, &lt);
      x.id = o["id"] | 0LL;
      x.m = num(s["distance_accum"]);
      x.s = (uint32_t)num(s["duration_active_accum"]);
      x.local = (int32_t)(acts::daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 + lt.tm_hour * 3600 +
                          lt.tm_min * 60 + lt.tm_sec);
      x.year = lt.tm_year + 1900;
      x.counts = ((kinds & 1) && run) || ((kinds & 2) && walk);
      const char* def = t == 5 ? "Treadmill Run" : t == 4 ? "Trail Run" : run ? "Run" : t == 9 ? "Hike" : walk ? "Walk" : "Workout";
      const char* n = o["name"] | "";
      strlcpy(x.name, *n ? n : def, sizeof x.name);
      out.push_back(x);
    }
    if (older || a.size() < (size_t)per) break;
  }
  return src::OK;
}

src::Result fetchRange(time_t from, time_t to, uint8_t kinds, std::vector<Act>& out) {
  src::Result r = src::OK;
  switch (k) {
    case src::INTERVALS:
      for (time_t a = from; a <= to && r == src::OK; a += 91 * 86400)
        r = fetchIntervals(localDay(a), localDay(std::min(to, a + 90 * 86400)), kinds, out);
      return r;
    case src::WHOOP: return fetchWhoop(isoUtc(from - 86400), isoUtc(to + 86400), kinds, out);
    case src::OURA: return fetchOura(localDay(from), localDay(to + 86400), kinds, out);
    case src::WITHINGS: return fetchWithings(localDay(from), localDay(to), kinds, out);
    case src::WAHOO: return fetchWahoo(from, to + 86400, kinds, out);
    default: return src::CONFIG;
  }
}

// ---- COROS MCP (JSON-RPC over HTTP) -------------------------------------------

src::Result mcpPost(const String& body, JsonDocument& doc) {
  net::Req q;
  q.method = "POST";
  q.url = COROS_MCP;
  q.contentType = "application/json";
  q.accept = "application/json, text/event-stream";
  q.auth = "Bearer " + at;
  q.session = mcpSession;
  q.body = body;
  String text, ctype, sess;
  int c = net::callText(q, text, &ctype, &sess);
  if (sess.length()) mcpSession = sess;
  if (c == 202) return src::OK;  // notification accepted
  if (c != 200) return fromCode(c);
  if (ctype.indexOf("event-stream") >= 0) {  // take the JSON-RPC message from the last "data:" line
    String msg;
    int p = 0;
    while (p < (int)text.length()) {
      int e = text.indexOf('\n', p);
      if (e < 0) e = text.length();
      String line = text.substring(p, e);
      line.trim();
      if (line.startsWith("data:")) {
        String d = line.substring(5);
        d.trim();
        if (d.indexOf("jsonrpc") >= 0) msg = d;
      }
      p = e + 1;
    }
    text = msg;
  }
  if (!text.length()) return src::OK;
  if (deserializeJson(doc, text)) return src::NET;
  if (!doc["error"].isNull()) return src::NET;
  return src::OK;
}

src::Result corosSession() {
  mcpSession = "";
  JsonDocument d;
  src::Result r = mcpPost(F("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":"
                            "\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"NextMileTracker\",\"version\":\"1\"}}}"),
                          d);
  if (r != src::OK) return r;
  JsonDocument n;
  mcpPost(F("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"), n);
  return src::OK;
}

// Seconds from "2:12:38" or "45:10".
uint32_t clockSecs(const char* s) {
  uint32_t parts[3] = {0, 0, 0};
  int n = 0;
  while (*s == ' ') s++;
  while (n < 3 && isdigit(*s)) {
    parts[n++] = strtoul(s, (char**)&s, 10);
    if (*s != ':') break;
    s++;
  }
  return n == 3 ? parts[0] * 3600 + parts[1] * 60 + parts[2] : n == 2 ? parts[0] * 60 + parts[1] : parts[0];
}

const char* corosName(int code) {
  switch (code) {
    case 100: return "Run";
    case 101: return "Indoor Run";
    case 102: return "Trail Run";
    case 103: return "Track Run";
    case 104: return "Hike";
    default: return nullptr;
  }
}

// querySportRecords answers in text:
//   1. Trail Run(102) — 2026-10-03
//      Duration: 2:12:38 | Distance: 12.03 km
void parseCoros(const String& t, uint8_t kinds, std::vector<Act>& out) {
  std::vector<String> keys;
  size_t first = out.size();
  int p = 0;
  bool open = false;
  while (p < (int)t.length()) {
    int e = t.indexOf('\n', p);
    if (e < 0) e = t.length();
    String line = t.substring(p, e);
    p = e + 1;
    line.trim();
    int dot = line.indexOf(". ");
    int di = -1;
    for (int i = 0; i + 10 <= (int)line.length(); i++) {
      const char* c = line.c_str() + i;
      if (isdigit(c[0]) && isdigit(c[1]) && isdigit(c[2]) && isdigit(c[3]) && c[4] == '-' && isdigit(c[5]) && isdigit(c[6]) &&
          c[7] == '-' && isdigit(c[8]) && isdigit(c[9])) {
        di = i;
        break;
      }
    }
    if (dot > 0 && dot <= 4 && isdigit(line[0]) && di > dot) {
      Act x;
      String sport = line.substring(dot + 2, di);
      while (sport.length() && !isalnum(sport[sport.length() - 1]) && sport[sport.length() - 1] != ')')
        sport.remove(sport.length() - 1);
      sport.trim();
      int code = 0, lp = sport.lastIndexOf('(');
      if (lp >= 0) code = sport.substring(lp + 1).toInt();
      String label = lp > 0 ? sport.substring(0, lp) : (lp < 0 ? sport : String());
      label.trim();
      String when = line.substring(di, di + 10);
      if ((int)line.length() > di + 15 && line[di + 10] == ' ' && isdigit(line[di + 11])) when += " " + line.substring(di + 11, di + 16) + ":00";
      x.local = acts::parseIso(when.c_str());
      x.year = atoi(when.c_str());
      x.counts = ((kinds & 1) && code >= 100 && code <= 103) || ((kinds & 2) && code == 104) || isRun(label, kinds);
      const char* known = corosName(code);
      strlcpy(x.name, known ? known : (label.length() ? label.c_str() : "Run"), sizeof x.name);
      out.push_back(x);
      keys.push_back(line);
      open = true;
      continue;
    }
    if (!open) continue;
    Act& x = out.back();
    int d = line.indexOf("Duration:");
    if (d >= 0) x.s = clockSecs(line.c_str() + d + 9);
    int m = line.indexOf("Distance:");
    if (m >= 0) {
      const char* s = line.c_str() + m + 9;
      char* end;
      float v = strtof(s, &end);
      while (*end == ' ') end++;
      x.m = strncmp(end, "km", 2) == 0 ? v * 1000 : strncmp(end, "mi", 2) == 0 ? v * 1609.344f : v;
    }
    if (d >= 0 || m >= 0) keys.back() += "|" + line;
  }
  for (size_t i = 0; i < keys.size(); i++) out[first + i].id = hash64(keys[i].c_str());
}

src::Result corosRecords(const String& from, const String& to, uint8_t kinds, std::vector<Act>& out) {
  String body = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"querySportRecords\","
                "\"arguments\":{\"startDate\":\"" + from + "\",\"endDate\":\"" + to + "\",\"limit\":50}}}";
  JsonDocument d;
  src::Result r = mcpPost(body, d);
  if (r != src::OK) return r;
  JsonObject res = d["result"];
  if (res["isError"] | false) return src::NET;
  String text;
  for (JsonObject c : res["content"].as<JsonArray>()) text += String(c["text"] | "") + "\n";
  parseCoros(text, kinds, out);
  return src::OK;
}

String ymdc(int y, int m, int d) {
  char b[10];
  snprintf(b, sizeof b, "%04d%02d%02d", y, m, d);
  return b;
}

// Half-month windows (50 records each) from `from` through `to`, local dates.
src::Result fetchCoros(time_t from, time_t to, uint8_t kinds, std::vector<Act>& out) {
  src::Result r = ensureToken(false);
  if (r != src::OK) return r;
  r = corosSession();
  if (r == src::AUTH) {
    if ((r = ensureToken(true)) != src::OK) return r;
    r = corosSession();
  }
  if (r != src::OK) return r;
  struct tm a, b;
  localtime_r(&from, &a);
  localtime_r(&to, &b);
  int y = a.tm_year + 1900, mo = a.tm_mon + 1, half = a.tm_mday > 15;
  const int ey = b.tm_year + 1900, em = b.tm_mon + 1, ehalf = b.tm_mday > 15;
  while (y < ey || (y == ey && (mo < em || (mo == em && half <= ehalf)))) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int last = days[mo - 1] + (mo == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
    r = corosRecords(ymdc(y, mo, half ? 16 : 1), ymdc(y, mo, half ? last : 15), kinds, out);
    if (r != src::OK) return r;
    if (half && ++mo > 12) mo = 1, y++;
    half = !half;
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
  verifier = prefs.getString("cv", "");
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
    case COROS: return "COROS";
    case INTERVALS: return "intervals.icu (Suunto, Zwift & more)";
    case WHOOP: return "WHOOP";
    case OURA: return "Oura Ring";
    case WITHINGS: return "Withings";
    case WAHOO: return "Wahoo";
    default: return "your watch";
  }
}

bool available(Kind x) {
  switch (x) {
    case STRAVA: return strlen(STRAVA_CLIENT_ID) && strlen(STRAVA_CLIENT_SECRET);
    case POLAR: return strlen(POLAR_CLIENT_ID) && strlen(POLAR_CLIENT_SECRET);
    case GOOGLE: return strlen(GOOGLE_CLIENT_ID) && strlen(GOOGLE_CLIENT_SECRET);
    case WHOOP: return strlen(WHOOP_CLIENT_ID) && strlen(WHOOP_CLIENT_SECRET);
    case OURA: return strlen(OURA_CLIENT_ID) && strlen(OURA_CLIENT_SECRET);
    case WITHINGS: return strlen(WITHINGS_CLIENT_ID) && strlen(WITHINGS_CLIENT_SECRET);
    case WAHOO: return strlen(WAHOO_CLIENT_ID) && strlen(WAHOO_CLIENT_SECRET);
    case GARMIN:
    case APPLE:
    case COROS:
    case INTERVALS: return true;
    default: return false;
  }
}

bool linked() {
  switch (k) {
    case GARMIN:
    case INTERVALS: return garminKey.length() > 0;
    case POLAR: return at.length() > 0;
    case APPLE: return appleTok.length() > 0;
    case STRAVA:
    case GOOGLE:
    case COROS:
    case WHOOP:
    case OURA:
    case WITHINGS:
    case WAHOO: return rt.length() > 0;
    default: return false;
  }
}

bool hasHistory() { return k != NONE && k != POLAR && k != APPLE; }
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
    case COROS:
      verifier = newVerifier();
      prefs.putString("cv", verifier);
      return String(COROS_ISSUER) + "/oauth2/authorize?response_type=code&client_id=" + COROS_CLIENT_ID +
             "&redirect_uri=" + redirectUri() + "&scope=" + net::urlEncode("openid offline_access mcp.tools") +
             "&code_challenge=" + pkceChallenge(verifier) + "&code_challenge_method=S256&resource=" +
             net::urlEncode(COROS_MCP) + "&state=" + st;
    case WHOOP:
      return String(WHOOP_API) + "/oauth/oauth2/auth?response_type=code&client_id=" + WHOOP_CLIENT_ID + "&redirect_uri=" +
             redirectUri() + "&scope=" + net::urlEncode("read:workout read:profile offline") + "&state=" + st;
    case OURA:
      return String("https://cloud.ouraring.com/oauth/authorize?response_type=code&client_id=") + OURA_CLIENT_ID +
             "&redirect_uri=" + redirectUri() + "&scope=workout&state=" + st;
    case WITHINGS:
      return String("https://account.withings.com/oauth2_user/authorize2?response_type=code&client_id=") + WITHINGS_CLIENT_ID +
             "&redirect_uri=" + redirectUri() + "&scope=user.activity&state=" + st;
    case WAHOO:
      return String(WAHOO_API) + "/oauth/authorize?response_type=code&client_id=" + WAHOO_CLIENT_ID + "&redirect_uri=" +
             redirectUri() + "&scope=" + net::urlEncode("user_read workouts_read offline_data") + "&state=" + st;
    default: return "";
  }
}

void resetFor(Kind x) {
  unlink();
  k = x;
}

Result exchange(Kind x, const String& code, const String& scope) {
  if (!available(x) || (x == NONE || x == GARMIN || x == APPLE || x == INTERVALS)) return CONFIG;
  if (x == COROS && !verifier.length()) return CONFIG;
  String cv = verifier;  // resetFor() wipes it
  if (x == STRAVA && scope.indexOf("activity:read") < 0) return SCOPE;
  if (x == GOOGLE && scope.length() && scope.indexOf("activity_and_fitness") < 0) return SCOPE;
  resetFor(x);
  Result r = oauthToken("grant_type=authorization_code&code=" + net::urlEncode(code) + "&redirect_uri=" + redirectUri() +
                        (x == COROS ? "&code_verifier=" + cv : String()));
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
  if (r == OK && x == WHOOP) {
    JsonDocument d;
    if (get(String(WHOOP_API) + "/developer/v2/user/profile/basic", d) == OK) {
      String f = d["first_name"] | "", l = d["last_name"] | "";
      if (f.length()) name = f + (l.length() ? " " + l.substring(0, 1) + "." : "");
    }
  }
  if (r == OK && x == WAHOO) {
    JsonDocument d;
    if (get(String(WAHOO_API) + "/v1/user", d) == OK) {
      String f = d["first"] | "", l = d["last"] | "";
      f.trim();
      l.trim();
      if (f.length()) name = f + (l.length() ? " " + l.substring(0, 1) + "." : "");
    }
  }
  if (r != OK) {
    k = NONE;
    at = rt = "";
  }
  save();
  return r;
}

Result linkIntervals(const String& apiKey) {
  resetFor(INTERVALS);
  garminKey = apiKey;
  garminKey.trim();
  JsonDocument filter, d;
  filter["firstname"] = true;
  filter["name"] = true;
  Result r = get(String(IV_API), d, &filter);
  if (r == OK) {
    String n = d["firstname"] | "";
    if (!n.length()) n = d["name"] | "";
    name = n;
  } else {
    garminKey = "";
    k = NONE;
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

Result sync(int year, bool full, uint8_t kinds) {
  if (!linked()) return AUTH;
  if (k == APPLE) return OK;  // pushed from the iPhone
  if (acts::totals().year != year) full = true;
  std::vector<Act> v;
  Result r;
  switch (k) {
    case STRAVA:
      r = full ? fetchStrava(acts::localYearStart(year) - 86400, 0, 30, kinds, v) : fetchStrava(0, 0, 1, kinds, v);
      break;
    case GARMIN:
      r = full ? fetchGarmin(ymd(year, 1, 1), ymd(year, 12, 31), 30, kinds, v) : fetchGarmin("", "", 1, kinds, v);
      break;
    case GOOGLE: {
      time_t from = full ? acts::localYearStart(year) : time(nullptr) - 30 * 86400;
      struct tm t;
      localtime_r(&from, &t);
      r = fetchGoogle(ymd(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday), ymd(year + 1, 1, 1), kinds, v);
      if (r == OK && !full) {
        acts::mergeWindow(year, v);
        return OK;
      }
      break;
    }
    case COROS: {
      time_t now = time(nullptr);
      r = fetchCoros(full ? acts::localYearStart(year) : now - 30 * 86400, now, kinds, v);
      if (r == OK && !full) {
        acts::mergeWindow(year, v);
        return OK;
      }
      break;
    }
    case INTERVALS:
    case WHOOP:
    case OURA:
    case WITHINGS:
    case WAHOO: {
      time_t now = time(nullptr);
      r = fetchRange(full ? acts::localYearStart(year) : now - 30 * 86400, now, kinds, v);
      if (r == OK && !full) {
        acts::mergeWindow(year, v);
        return OK;
      }
      break;
    }
    case POLAR:
      r = fetchPolar(kinds, v);
      if (r == OK) acts::merge(year, v);
      return r;
    default: return CONFIG;
  }
  if (r != OK) return r;
  if (full) acts::replaceYear(year, v);
  else acts::mergeWindow(year, v);
  return OK;
}

Result syncLastYear(int year, uint8_t kinds) {
  if (!linked()) return AUTH;
  if (!hasHistory()) {
    acts::setLastYear(year, acts::totals().lastYear == year ? acts::totals().lastYearMeters : 0);
    return OK;
  }
  std::vector<Act> v;
  Result r;
  if (k == STRAVA) r = fetchStrava(acts::localYearStart(year) - 86400, acts::localYearStart(year + 1) + 86400, 30, kinds, v);
  else if (k == GARMIN) r = fetchGarmin(ymd(year, 1, 1), ymd(year, 12, 31), 30, kinds, v);
  else if (k == INTERVALS || k == WHOOP || k == OURA || k == WITHINGS || k == WAHOO)
    r = fetchRange(acts::localYearStart(year), acts::localYearStart(year + 1) - 86400, kinds, v);
  else if (k == COROS) r = fetchCoros(acts::localYearStart(year), acts::localYearStart(year + 1) - 86400, kinds, v);
  else r = fetchGoogle(ymd(year, 1, 1), ymd(year + 1, 1, 1), kinds, v);
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
    } else if (k == COROS) {
      q.url = String(COROS_ISSUER) + "/oauth2/revoke";
      q.body = "token=" + net::urlEncode(rt.length() ? rt : at) + "&client_id=" + COROS_CLIENT_ID;
      net::call(q, d);
    } else if (k == WAHOO) {
      q.method = "DELETE";
      q.url = String(WAHOO_API) + "/v1/permissions";
      q.auth = "Bearer " + at;
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
  at = rt = name = garminKey = polarUser = appleTok = verifier = mcpSession = "";
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
