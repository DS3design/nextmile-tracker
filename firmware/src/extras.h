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
void newRun();
void milestone();
void dismiss();         // touch clears the new-run glow
void setAhead(bool ahead);
void setSetup(bool on);  // rainbow while the tracker is being set up

// Weather
const Weather& weather();
const char* describe(int code);
void locationChanged(); // re-geocode cfg.place
String weatherStatus();
}
