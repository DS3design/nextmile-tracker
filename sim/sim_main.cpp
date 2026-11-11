// Desktop renderer: draws each tracker screen into an off-screen sprite and saves a PPM.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../firmware/src/ui.h"

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
  s.createSprite(320, 240);
  ui::init();

  UiData d;
  strcpy(d.athlete, "Don S.");
  d.year = 2026; d.ytd_m = 412.6f * 1609.344f; d.ytd_runs = 87;
  d.last_year = 2025; d.last_year_mi = 545; d.goal_mi = 600; d.goal_auto = true;
  d.last.valid = true; strcpy(d.last.name, "Morning Run"); d.last.meters = 5.21f * 1609.344f;
  d.last.moving_s = 50 * 60 + 22; d.last.start_local = 1794470400 + 7 * 3600;  // Tue Nov 10 2026
  d.wx_valid = true; d.wx_temp = 53; d.wx_rain = 0; d.wx_desc = "CLEAR";
  ui::main(s, d); save(s, "out/main.ppm");
  UiData dr = d; dr.wx_temp = 61; dr.wx_rain = 70; ui::main(s, dr); save(s, "out/main_rain.ppm");

  UiData d2 = d; strcpy(d2.athlete, "Jessica Martinez-Thompson"); d2.ytd_m = 1288.4f * 1609.344f; d2.ytd_runs = 241;
  d2.goal_auto = false; d2.goal_mi = 1200; strcpy(d2.last.name, "Pacegiving 2026 - Roseville Charity Run 10K");
  d2.last.meters = 6.21f * 1609.344f; d2.last.moving_s = 3725; d2.wifi_ok = false;
  ui::main(s, d2); save(s, "out/main_goal_reached.ppm");

  UiData d3; strcpy(d3.athlete, "Alex R."); d3.year = 2027; d3.goal_mi = 0;
  ui::main(s, d3); save(s, "out/main_new_year_no_goal.ppm");

  ui::WifiNet nets[] = {{"DS3DESIGN", -48, false}, {"Schomer-Home", -63, false}, {"NETGEAR-Guest-Network-Extended", -70, false}, {"xfinitywifi", -80, true}, {"Linksys", -85, false}};
  ui::wifiList(s, nets, 5, 0, false, nullptr); save(s, "out/setup_wifi.ppm");
  ui::wifiList(s, nets, 0, 0, true, nullptr); save(s, "out/setup_wifi_scanning.ppm");
  ui::wifiList(s, nets, 5, 4, false, "Couldn't reach DS3DESIGN. Move closer to the router."); save(s, "out/setup_wifi_more.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "", 0, nullptr); save(s, "out/setup_wifi_keys.ppm");
  ui::wifiPassword(s, "DS3DESIGN", "Running2026!", 2, "That password didn't work. Try again."); save(s, "out/setup_wifi_keys_sym.ppm");
  ui::wifiPhone(s, "NextMile-Setup", false); save(s, "out/setup_wifi_phone.ppm");
  ui::wifiRetry(s, "Schomer-Home"); save(s, "out/wifi_retry.ppm");
  ui::linkStrava(s, "http://192.168.1.42/link", "192.168.1.42", false); save(s, "out/setup_strava.ppm");
  ui::message(s, "Loading your runs...", "Adding up this year's miles.", nullptr); save(s, "out/loading.ppm");
  ui::info(s, "http://192.168.1.42/", "192.168.1.42", "Schomer-Home"); save(s, "out/info.ppm");
  for (int t = 0; t < ui::themeCount(); t++) {
    char p[40];
    snprintf(p, sizeof p, "out/theme_%d.ppm", t);
    ui::setTheme(t);
    ui::main(s, d);
    save(s, p);
  }
  ui::setTheme(1);
  ui::info(s, "http://192.168.1.42/", "192.168.1.42", "Schomer-Home"); save(s, "out/info_daylight.ppm");
  ui::setTheme(0);
  ui::resetHold(s, 6); save(s, "out/reset.ppm");
  ui::wifiRetry(s, "Schomer-Home"); save(s, "out/wifi_retry.ppm");
  return 0;
}
