#include "activities.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

namespace {

struct Run {
  int64_t id;
  float m;
  uint32_t s;
  int32_t local;
};

struct Latest {
  int64_t id = 0;
  int32_t local = INT32_MIN;
  char name[40] = "";
};

const char* RUNS_FILE = "/runs.bin";
const uint32_t MAGIC = 0x4E4D5233;

Preferences prefs;
std::vector<Run> runs;
Latest latest;
acts::Totals tot;
float carry = 0;
uint16_t countOverride = 0;

void consider(const acts::Act& a) {
  if (a.local >= latest.local) {
    latest.id = a.id;
    latest.local = a.local;
    strlcpy(latest.name, a.name, sizeof latest.name);
  }
}

void recompute() {
  tot.meters = carry;
  tot.runs = countOverride ? countOverride : runs.size();
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

void save() {
  File f = LittleFS.open(RUNS_FILE, "w");
  if (!f) return;
  uint32_t hdr[3] = {MAGIC, (uint32_t)tot.year, (uint32_t)runs.size()};
  f.write((const uint8_t*)hdr, sizeof hdr);
  if (!runs.empty()) f.write((const uint8_t*)runs.data(), runs.size() * sizeof(Run));
  f.write((const uint8_t*)&latest, sizeof latest);
  f.close();
}

void load() {
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

void newYear(int year) {
  if (tot.year == year) return;
  runs.clear();
  latest = Latest();
  tot.year = year;
  if (prefs.getInt("cyY", 0) != year) carry = 0;
}

void upsert(const acts::Act& a) {
  for (Run& x : runs) {
    if (x.id == a.id) {
      x = {a.id, a.m, a.s, a.local};
      consider(a);
      return;
    }
  }
  runs.push_back({a.id, a.m, a.s, a.local});
  consider(a);
}

}  // namespace

namespace acts {

void begin() {
  prefs.begin("acts", false);
  tot.lastYear = prefs.getInt("lyY", 0);
  tot.lastYearMeters = prefs.getFloat("lyM", 0);
  load();
  carry = prefs.getInt("cyY", 0) == tot.year ? prefs.getFloat("cyM", 0) : 0;
  countOverride = prefs.getInt("cyY", 0) == tot.year ? prefs.getUShort("cnt", 0) : 0;
  recompute();
}

const Totals& totals() { return tot; }

void clear() {
  prefs.clear();
  runs.clear();
  latest = Latest();
  tot = Totals();
  carry = 0;
  LittleFS.remove(RUNS_FILE);
  recompute();
}

void replaceYear(int year, const std::vector<Act>& all) {
  runs.clear();
  latest = Latest();
  tot.year = year;
  carry = prefs.getInt("cyY", 0) == year ? prefs.getFloat("cyM", 0) : 0;
  for (const Act& a : all)
    if (a.counts && a.year == year) upsert(a);
  recompute();
  save();
}

void mergeWindow(int year, const std::vector<Act>& recent) {
  newYear(year);
  if (recent.empty()) {
    runs.clear();
  } else {
    int32_t oldest = INT32_MAX;
    for (const Act& a : recent) oldest = min(oldest, a.local);
    for (size_t i = 0; i < runs.size();) {
      bool keep = runs[i].local < oldest;
      for (const Act& a : recent) keep = keep || (a.id == runs[i].id && a.counts && a.year == year);
      if (keep) i++;
      else runs.erase(runs.begin() + i);
    }
    for (const Act& a : recent)
      if (a.counts && a.year == year) upsert(a);
  }
  recompute();
  save();
}

void merge(int year, const std::vector<Act>& add) {
  newYear(year);
  for (const Act& a : add)
    if (a.counts && a.year == year) upsert(a);
  recompute();
  save();
}

void setLastYear(int year, float meters) {
  tot.lastYear = year;
  tot.lastYearMeters = meters;
  prefs.putInt("lyY", year);
  prefs.putFloat("lyM", meters);
}

void setCarryIn(int year, float meters) {
  prefs.putInt("cyY", year);
  prefs.putFloat("cyM", meters);
  if (tot.year == year || tot.year == 0) {
    tot.year = year;
    carry = meters;
    recompute();
  }
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

int32_t parseIso(const char* s) {
  int y, mo, d, h = 0, mi = 0, se = 0;
  if (!s || sscanf(s, "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3) return 0;
  return (int32_t)(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se);
}

time_t localYearStart(int year) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mday = 1;
  t.tm_isdst = -1;
  return mktime(&t);
}

void setAbsolute(int year, float meters, uint16_t count, const Act& last) {
  runs.clear();
  latest = Latest();
  tot.year = year;
  if (last.year == year && last.m > 0) upsert(last);
  carry = max(0.0f, meters - (runs.empty() ? 0 : last.m));
  countOverride = count ? count : 1;
  prefs.putInt("cyY", year);
  prefs.putFloat("cyM", carry);
  prefs.putUShort("cnt", countOverride);
  recompute();
  save();
}

}  // namespace acts
