#pragma once
// Light sensor auto-dim, theme-coloured RGB LED, and today's running weather.
#include <Arduino.h>

namespace extras {
struct Weather {
  bool valid = false;
  int temp = 0, hi = 0, lo = 0, rain = 0, wind = 0, code = 0;
};

void begin();
void loop(bool netUp);

// Backlight: target brightness for the screen right now (0 = off in a dark room).
uint8_t backlight();
bool screenOff();
void wake();            // touch while dark: light the screen for a while
int lightRaw();         // smoothed sensor reading, for calibration on the settings page

// LED events
const uint32_t NEWRUN_MS = 6UL * 60 * 60 * 1000;  // how long new miles are celebrated
void newRun(uint32_t ageMs = 0);  // ageMs: how long ago the miles came in (resuming after a restart)
void milestone();
bool celebrating();     // within NEWRUN_MS of new miles
void dismiss();         // touch stops the milestone flashing
void setAhead(bool ahead);
void setSetup(bool on);  // rainbow while the tracker is being set up

// Weather
const Weather& weather();
const char* describe(int code);
void locationChanged(); // re-geocode cfg.place
String weatherStatus();
}
