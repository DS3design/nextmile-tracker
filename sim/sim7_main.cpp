// CrowPanel 7" desktop renderer (800x480): draws each tracker screen into an off-screen sprite and saves a PPM.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../firmware-crowpanel7/src/ui.h"
#include "../firmware-crowpanel7/src/ledfx.h"
#include <math.h>
#include <vector>

static void save(LGFX_Sprite& s, const char* path) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", s.width(), s.height());
  for (int y = 0; y < s.height(); y++)
    for (int x = 0; x < s.width(); x++) {
      uint16_t p = s.readPixel(x, y);
      unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 31) * 255 / 31),
                              (unsigned char)(((p >> 5) & 63) * 255 / 63),
                              (unsigned char)((p & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

int main() {
  LGFX_Sprite s;
  s.setColorDepth(16);
  s.createSprite(800, 480);
  ui::init();

  UiData d;
  strcpy(d.athlete, "Don S.");
  d.year = 2026; d.ytd_m = 412.6f * 1609.344f; d.ytd_runs = 87;
  d.last_year = 2025; d.last_year_mi = 545; d.goal_mi = 600; d.goal_auto = true;
  d.last.valid = true; strcpy(d.last.name, "Morning Run"); d.last.meters = 5.21f * 1609.344f;
  d.last.moving_s = 50 * 60 + 22; d.last.start_local = 1794470400 + 7 * 3600;  // Tue Nov 10 2026
  d.wx_valid = true; d.wx_temp = 53; d.wx_rain = 0; d.wx_desc = "CLEAR";
  ui::main(s, d); save(s, "out7/main_nofc.ppm");
  const char* dn[3] = {"TODAY", "TUE", "WED"};
  const int hi[3] = {58, 61, 49}, lo[3] = {41, 44, 39}, rn[3] = {0, 10, 70};
  const char* ds[3] = {"CLEAR", "PT CLOUDY", "RAIN"};
  for (int i = 0; i < 3; i++) { d.fc[i].valid = true; d.fc[i].hi = hi[i]; d.fc[i].lo = lo[i]; d.fc[i].rain = rn[i]; d.fc[i].desc = ds[i]; strcpy(d.fc[i].name, dn[i]); }
  d.today_m = 3.1f * 1609.344f; d.week_m = 14.2f * 1609.344f; d.month_m = 41.8f * 1609.344f;
  ui::main(s, d); save(s, "out7/main.ppm");
  for (uint32_t ms = 1000; ms <= 3000; ms += 50) ui::confettiStep(s, ms);
  save(s, "out7/main_confetti.ppm");
  ui::confettiClear(s); save(s, "out7/main_confetti_cleared.ppm");
  ui::confettiReset();
  UiData dr = d; dr.wx_temp = 61; dr.wx_rain = 70; ui::main(s, dr); save(s, "out7/main_rain.ppm");

  UiData d2 = d; strcpy(d2.athlete, "Jessica Martinez-Thompson"); d2.ytd_m = 1288.4f * 1609.344f; d2.ytd_runs = 241;
  d2.goal_auto = false; d2.goal_mi = 1200; strcpy(d2.last.name, "Pacegiving 2026 - Roseville Charity Run 10K");
  d2.last.meters = 6.21f * 1609.344f; d2.last.moving_s = 3725; d2.wifi_ok = false;
  ui::main(s, d2); save(s, "out7/main_goal_reached.ppm");
  UiData d4 = d; strcpy(d4.athlete, "Don"); d4.ytd_m = 95.8f * 1609.344f; d4.ytd_runs = 91; d4.kinds = 3; d4.goal_mi = 90; d4.goal_auto = true; strcpy(d4.last.name, "Walk");
  ui::main(s, d4); save(s, "out7/main_goal_reached_auto.ppm");
  UiData d5 = d; d5.last_year_mi = 374.4f; d5.goal_mi = 374.4f; d5.ytd_m = 301.7f * 1609.344f; ui::main(s, d5); save(s, "out7/main_goal_exact.ppm");
  d5.ytd_m = 380.2f * 1609.344f; ui::main(s, d5); save(s, "out7/main_goal_beat.ppm");

  UiData d3; strcpy(d3.athlete, "Alex R."); d3.year = 2027; d3.goal_mi = 0;
  ui::main(s, d3); save(s, "out7/main_new_year_no_goal.ppm");

  {
    UiData a = d, b = d;
    strcpy(b.athlete, "Jamie S.");
    b.ytd_m = 287.3f * 1609.344f; b.ytd_runs = 61; b.goal_mi = 412.0f; b.last_year_mi = 412.0f;
    strcpy(b.last.name, "Sunday Long Run"); b.last.meters = 16093; b.last.moving_s = 5520;
    a.theme = 0; b.theme = 2;  // each runner picks their own colors
    ui::duo(s, a, b); save(s, "out7/duo.ppm");
    for (uint32_t ms = 1000; ms <= 3000; ms += 50) ui::confettiStep(s, ms);
    save(s, "out7/duo_confetti.ppm");
    ui::confettiClear(s); ui::confettiReset();
    b.today_m = 0; b.week_m = 18.4f * 1609.344f; b.month_m = 41.8f * 1609.344f;
    ui::together(s, a, b); save(s, "out7/together.ppm");
    ui::together(s, a, b, 1000); save(s, "out7/together_shared.ppm");
    UiData an = a, bn = b; an.fc[0].valid = false;
    ui::together(s, an, bn); save(s, "out7/together_nofc.ppm");
    UiData aa = a, ab = b; aa.totals_only = true;  // Apple runner: year-only race
    ui::together(s, aa, ab); save(s, "out7/together_apple.ppm");
    UiData both[2] = {a, b};
    ui::night(s, both, 2, "10:42", "PM", "MONDAY, NOVEMBER 16"); save(s, "out7/night_duo.ppm");
    ui::night(s, both, 1, "6:05", "AM", "TUESDAY, NOVEMBER 17"); save(s, "out7/night.ppm");
    // Photo behind the miles: a made-up sunset gradient with a hill.
    std::vector<uint16_t> ph(800 * 480);
    for (int y = 0; y < 480; y++) for (int x = 0; x < 800; x++) {
      bool hill = y > 300 + 60 * sinf(x / 130.0f);
      int r = hill ? 30 : 255 - y / 3, g = hill ? 70 : 120 + y / 4, bl = hill ? 30 : 200 - y / 3;
      ph[y * 800 + x] = (r >> 3) << 11 | (g >> 2) << 5 | bl >> 3;
    }
    ui::setBackdrop(ph.data());
    ui::main(s, d); save(s, "out7/photo_main.ppm");
    ui::duo(s, a, b); save(s, "out7/photo_duo.ppm");
    ui::setBackdrop(nullptr);
  }
  {
    // LED strip preview: each row is one frame (40 ms), each column one of 10 LEDs, drawn 16 px wide.
    for (int e = 0; e < ledfx::EFFECT_N - 1; e++) {
      LGFX_Sprite st; st.setColorDepth(16); st.createSprite(10 * 16, 150 * 3);
      ledfx::reset(); ledfx::Px px[10];
      for (int f = 0; f < 150; f++) {
        ledfx::step(px, 10, 20000 + f * 40, e, 0xFF0000);
        for (int i = 0; i < 10; i++) st.fillRect(i * 16, f * 3, 15, 3, st.color888(px[i].r * 255, px[i].g * 255, px[i].b * 255));
      }
      char p[40]; snprintf(p, sizeof p, "out7/led_%d.ppm", e); save(st, p);
    }
  }
  ui::WifiNet nets[] = {{"DS3DESIGN", -48, false}, {"Schomer-Home", -63, false}, {"NETGEAR-Guest-Network-Extended", -70, false}, {"xfinitywifi", -80, true}, {"Linksys", -85, false}};
  ui::wifiList(s, nets, 5, 0, false, nullptr); save(s, "out7/setup_wifi.ppm");
  ui::wifiList(s, nets, 0, 0, true, nullptr); save(s, "out7/setup_wifi_scanning.ppm");
  ui::wifiList(s, nets, 5, 4, false, "Couldn't reach DS3DESIGN. Move closer to the router."); save(s, "out7/setup_wifi_more.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "", 0, nullptr); save(s, "out7/setup_wifi_keys.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "Running2026!", 2, "That password didn't work. Try again."); save(s, "out7/setup_wifi_keys_sym.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "Run", 1, nullptr); save(s, "out7/setup_wifi_keys_caps.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "Run2026", 3, nullptr); save(s, "out7/setup_wifi_keys_sym2.ppm");
  ui::wifiPhone(s, "NextMile-Setup", false); save(s, "out7/setup_wifi_phone.ppm");
  ui::wifiRetry(s, "Schomer-Home"); save(s, "out7/wifi_retry.ppm");
  ui::linkStrava(s, "http://192.168.1.42/link", "192.168.1.42", false); save(s, "out7/setup_strava.ppm");
  ui::message(s, "Loading your runs...", "Adding up this year's miles.", nullptr); save(s, "out7/loading.ppm");
  {
    const char* tabs[] = {"GOAL", "RUNNERS", "SCREEN", "LIGHTS", "WEATHER", "SYSTEM"};
    ui::SetRow g[] = {
      {0, ui::ROW_CHOICE, "Editing", "DON"},
      {1, ui::ROW_TEXT, "Name on screen", "Don"},
      {2, ui::ROW_STEP, "Yearly goal", "500 MI", false, "Blank = beat last year"},
      {3, ui::ROW_CHOICE, "Count my", "ALL"},
      {4, ui::ROW_ACTION, "Link a watch or app"},
    };
    ui::settings(s, tabs, 6, 0, g, 5, ""); save(s, "out7/set_goal.ppm");
    ui::SetRow rr[] = {
      {5, ui::ROW_TOGGLE, "Two runners", "", true, "Each links their own watch"},
      {6, ui::ROW_STEP, "Shared goal", "ADDED UP", false, "Blank = your goals added up"},
      {0, ui::ROW_LINE, "Tap the miles to see both runners together."},
    };
    ui::settings(s, tabs, 6, 1, rr, 3, "Now link runner 2's watch: GOAL tab, Link a watch"); save(s, "out7/set_runners.ppm");
    ui::SetRow sc[] = {
      {7, ui::ROW_CHOICE, "Colors, Don", "EMBER"},
      {7, ui::ROW_CHOICE, "Colors, Jamie", "OCEAN"},
      {8, ui::ROW_STEP, "Brightness", "100%"},
      {9, ui::ROW_TOGGLE, "Dim in a dark room", "", true},
      {10, ui::ROW_TOGGLE, "Night clock", "", true, "Dim clock instead of off"},
      {11, ui::ROW_ACTION, "Photo behind the miles"},
    };
    ui::settings(s, tabs, 6, 2, sc, 5, ""); save(s, "out7/set_screen.ppm");
    ui::SetRow le[] = {
      {12, ui::ROW_TOGGLE, "Lights always on", "", false, "Off: glow 6 h on new miles"},
      {13, ui::ROW_CHOICE, "New-miles effect", "MIX"},
      {14, ui::ROW_STEP, "Lights on the strip", "10"},
    };
    ui::settings(s, tabs, 6, 3, le, 3, ""); save(s, "out7/set_lights.ppm");
    ui::SetRow wx[] = {
      {15, ui::ROW_TEXT, "ZIP or town", "Roseville"},
      {16, ui::ROW_CHOICE, "Time zone", "Pacific", false, "For the new-year reset"},
    };
    ui::settings(s, tabs, 6, 4, wx, 2, ""); save(s, "out7/set_weather.ppm");
    ui::SetRow sy[] = {
      {17, ui::ROW_ACTION, "Change WiFi"},
      {18, ui::ROW_ACTION, "Refresh miles now"},
      {19, ui::ROW_ACTION, "Check for update"},
      {20, ui::ROW_ACTION, "Phone settings (QR)"},
      {21, ui::ROW_ACTION, "Erase everything", "", false, "", true},
    };
    ui::settings(s, tabs, 6, 5, sy, 5, ""); save(s, "out7/set_system.ppm");
    ui::scan(s, "Link a watch", "http://192.168.1.42/link?r=1",
             "Point your phone's camera at this code and pick your watch.",
             "Your phone needs to be on your home WiFi.", nullptr); save(s, "out7/scan_link.ppm");
    ui::scan(s, "Add a photo", "http://192.168.1.42/",
             "Open the settings page on your phone, tap Photo, and pick a picture.",
             "It shows faintly behind the miles.", "REMOVE PHOTO"); save(s, "out7/scan_photo.ppm");
    ui::textEntry(s, "Name on the screen", "Don", 0); save(s, "out7/entry_name.ppm");
  }
  for (int t = 0; t < ui::themeCount(); t++) {
    char p[40];
    snprintf(p, sizeof p, "out7/theme_%d.ppm", t);
    ui::setTheme(t);
    ui::main(s, d);
    save(s, p);
  }
  ui::setTheme(1);
  // info screen replaced by the Settings pages above
  ui::setTheme(0);
  ui::resetHold(s, 6); save(s, "out7/reset.ppm");
  ui::wifiRetry(s, "Schomer-Home"); save(s, "out7/wifi_retry.ppm");
  return 0;
}
