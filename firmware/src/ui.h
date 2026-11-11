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
  uint8_t kinds = 1;  // 1 runs, 2 walks and hikes, 3 both
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
// WiFi setup on the touchscreen: a list of nearby WiFi, then an on-screen keyboard for the password.
struct WifiNet { char ssid[33]; int rssi; bool open; };
constexpr int NET_ROWS = 4;
enum { LIST_NONE = -1, LIST_AGAIN = -2, LIST_MORE = -3, LIST_PHONE = -4 };
enum { KB_NONE = 0, KB_SHIFT = -1, KB_SYM = -2, KB_DEL = -3, KB_OK = -4, KB_BACK = -5 };
void wifiList(lgfx::LovyanGFX& g, const WifiNet* nets, int n, int first, bool scanning, const char* error);
int wifiListHit(int x, int y, int n, int first);  // net index, or LIST_*
void wifiPassword(lgfx::LovyanGFX& g, const char* ssid, const char* pass, int layer, const char* error);
void wifiPasswordField(lgfx::LovyanGFX& g, const char* pass);
void wifiKeyboard(lgfx::LovyanGFX& g, int layer);  // layer 0 lower, 1 upper, 2 symbols
int wifiKeyHit(int x, int y, int layer);            // character, or KB_*
void wifiKeyFlash(lgfx::LovyanGFX& g, int x, int y, int layer, bool down);
void wifiPhone(lgfx::LovyanGFX& g, const char* apName, bool failed);  // WiFi code for the phone setup page
void wifiRetry(lgfx::LovyanGFX& g, const char* ssid);
void linkStrava(lgfx::LovyanGFX& g, const char* url, const char* ip, bool relink);
void message(lgfx::LovyanGFX& g, const char* title, const char* line1, const char* line2);
void info(lgfx::LovyanGFX& g, const char* url, const char* ip, const char* ssid, bool ledAlways = false);
void resetHold(lgfx::LovyanGFX& g, int secondsLeft);
void formatMiles(char* out, size_t n, float miles, int decimals);
}
