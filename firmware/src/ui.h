#pragma once
// Screen drawing shared by the firmware and the desktop simulator (sim/).
#include <stdint.h>
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

struct RunInfo {
  bool valid = false;
  char name[48] = "";
  float meters = 0;
  uint32_t moving_s = 0;
  int32_t start_local = 0;  // seconds since epoch in the runner's local time
};

struct UiData {
  char athlete[40] = "";
  int year = 2026;
  float ytd_m = 0;
  uint16_t ytd_runs = 0;
  float goal_mi = 0;     // 0 = no goal yet
  bool goal_auto = true; // true = "beat last year"
  int last_year = 2025;
  float last_year_mi = 0;
  RunInfo last;
  bool wifi_ok = true;
  bool sync_error = false;
  const char* source = "STRAVA";
  bool wx_valid = false;
  int wx_temp = 0, wx_rain = 0;  // F, today's max rain chance %
  const char* wx_desc = "";
};

namespace ui {
constexpr int THEME_BTN_Y = 172, THEME_BTN_H = 26;  // "Colors" button on the settings screen
int themeCount();
const char* themeName(int i);
void setTheme(int i);
uint32_t accent();  // current theme's accent colour, 0xRRGGBB
void init();
void main(lgfx::LovyanGFX& g, const UiData& d);
void wifiSetup(lgfx::LovyanGFX& g, const char* apName);
void wifiRetry(lgfx::LovyanGFX& g, const char* ssid);
void linkStrava(lgfx::LovyanGFX& g, const char* url, const char* ip, bool relink);
void message(lgfx::LovyanGFX& g, const char* title, const char* line1, const char* line2);
void info(lgfx::LovyanGFX& g, const char* url, const char* ip, const char* ssid, bool ledAlways = false);
void resetHold(lgfx::LovyanGFX& g, int secondsLeft);
void formatMiles(char* out, size_t n, float miles, int decimals);
}
