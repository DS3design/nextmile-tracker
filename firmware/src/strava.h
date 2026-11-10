#pragma once
#include <Arduino.h>
#include "ui.h"

namespace strava {
enum Result { OK, NET, AUTH, RATE, SCOPE, CONFIG };

struct Totals {
  int year = 0;  // 0 = nothing synced yet
  float meters = 0;
  uint16_t runs = 0;
  RunInfo last;
  int lastYear = 0;  // year lastYearMeters belongs to; 0 = unknown
  float lastYearMeters = 0;
};

void begin();
bool configured();
bool linked();
String athleteName();
String authorizeUrl(const String& state);
Result exchange(const String& code, const String& scope);
// full=true re-reads the whole year (catches edits/deletes); false only checks the latest 30 activities.
Result sync(int year, bool full, bool walks);
Result syncLastYear(int year, bool walks);
const Totals& totals();
void unlink();
const char* describe(Result r);
}
