#pragma once
// Shared run cache and yearly totals, fed by whichever watch/service the tracker is linked to.
#include <Arduino.h>

#include <vector>

#include "ui.h"

namespace acts {

struct Act {
  int64_t id = 0;
  float m = 0;          // meters
  uint32_t s = 0;       // moving seconds
  int32_t local = 0;    // start, local wall-clock seconds since epoch
  int year = 0;         // local year of start
  bool counts = false;  // a run (or walk/hike when enabled)
  char name[40] = "";
};

struct Totals {
  int year = 0;
  float meters = 0;
  uint16_t runs = 0;
  RunInfo last;
  int lastYear = 0;
  float lastYearMeters = 0;
};

void begin();
const Totals& totals();
void clear();

// Full re-read of `year`: replaces everything cached.
void replaceYear(int year, const std::vector<Act>& all);
// Latest N activities. Cached runs inside the returned window that are missing were deleted or retyped.
void mergeWindow(int year, const std::vector<Act>& recent);
// Append-only sources (Polar, Apple): add/update, never delete.
void merge(int year, const std::vector<Act>& add);
void setLastYear(int year, float meters);
// Miles the runner logged before linking a source with no history (Polar).
void setCarryIn(int year, float meters);
// Sources that only report totals (Apple Shortcut): year miles, run count and the latest run.
void setAbsolute(int year, float meters, uint16_t count, const Act& last);

int64_t daysFromCivil(int y, unsigned m, unsigned d);
int32_t parseIso(const char* s);  // "2026-11-10T07:00:00" or "2026-11-10 07:00:00" -> seconds, zone ignored
time_t localYearStart(int year);

}  // namespace acts
