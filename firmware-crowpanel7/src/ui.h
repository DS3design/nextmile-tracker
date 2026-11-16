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

struct DayFc {  // one day of the running forecast
  bool valid = false;
  int hi = 0, lo = 0, rain = 0;  // F, max rain chance %
  const char* desc = "";
  char name[8] = "";  // "TODAY", "TUE"
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
  DayFc fc[3];
  float today_m = 0, week_m = 0, month_m = 0;  // this runner's miles today, since Monday, this month
};

namespace ui {
int themeCount();
const char* themeName(int i);
void setTheme(int i);
uint32_t accent();  // current theme's accent colour, 0xRRGGBB
void init();
void main(lgfx::LovyanGFX& g, const UiData& d);
// Two-runner mode: split down the middle, and a tap shows the pair's combined miles.
void duo(lgfx::LovyanGFX& g, const UiData& a, const UiData& b);
// sharedGoal: miles the pair aim for together; 0 = both goals added up.
void together(lgfx::LovyanGFX& g, const UiData& a, const UiData& b, float sharedGoal = 0);
// Dark room: a big dim clock and today's miles for each of the n runners.
void night(lgfx::LovyanGFX& g, const UiData* r, int n, const char* hhmm, const char* ampm, const char* date);
// A photo (800x480 RGB565) drawn faintly behind the miles screens; nullptr for none.
void setBackdrop(const uint16_t* rgb565);
// A few confetti flakes drifting down over the main screen; each flake saves and restores what's under it.
void confettiStep(lgfx::LovyanGFX& g, uint32_t ms);
void confettiClear(lgfx::LovyanGFX& g);  // put back what the flakes covered
void confettiReset();                    // the screen was redrawn: forget the flakes
// WiFi setup on the touchscreen: a list of nearby WiFi, then an on-screen keyboard for the password.
struct WifiNet { char ssid[33]; int rssi; bool open; };
constexpr int NET_ROWS = 3;
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
// On-screen settings. The firmware supplies rows; hit-testing tells it which row and which control was tapped.
enum RowKind { ROW_TITLE, ROW_TOGGLE, ROW_CHOICE, ROW_STEP, ROW_TEXT, ROW_ACTION, ROW_LINE };
struct SetRow {
  int id = 0;
  RowKind kind = ROW_TITLE;
  const char* label = "";    // left side (or centered for ROW_ACTION/ROW_LINE)
  const char* value = "";    // right side for CHOICE/STEP/TEXT/TOGGLE
  bool on = false;           // TOGGLE
  const char* hint = "";     // dim line under the label
  bool danger = false;       // ROW_ACTION in the warning colour
};
// Row index for a normal row. For CHOICE/STEP/TOGGLE: 2*index + (0 = minus/left/toggle, 1 = plus/right). -1 = none.
int settingsHit(int x, int y, int nRows);
int settingsTabAt(int x, int y);  // tab index, or -1
void settings(lgfx::LovyanGFX& g, const char* const* tabs, int nTabs, int tab, const SetRow* rows, int n,
              const char* status);
// "Scan this code" page: big QR, text, and an optional extra button. Tap anywhere else to go back.
void scan(lgfx::LovyanGFX& g, const char* title, const char* url, const char* line1, const char* line2,
          const char* btn);
int scanHit(int x, int y);  // 0 back, 1 the extra button
// Text entry on the keyboard (shared with the WiFi password screen).
void okLabel(const char* l);      // the keyboard's big key, e.g. "CONNECT" or "SAVE"
void fieldHint(const char* h);    // grey text in an empty field
void textEntry(lgfx::LovyanGFX& g, const char* title, const char* value, int layer);
void resetHold(lgfx::LovyanGFX& g, int secondsLeft);
void formatMiles(char* out, size_t n, float miles, int decimals);
}
