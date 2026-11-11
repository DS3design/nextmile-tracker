#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fonts_gen.h"
#include "icon_gen.h"

namespace {

struct Theme {
  const char* name;
  uint32_t bg, fg, grey, dim, panel, track, accent;
};
const Theme THEMES[] = {
    {"Next Mile", 0x000000, 0xFFFFFF, 0x9A9A9A, 0x5A5A5A, 0x161616, 0x2C2C2C, 0xFF0000},
    {"Daylight", 0xFFFFFF, 0x000000, 0x555555, 0x8A8A8A, 0xEDEDED, 0xD4D4D4, 0xE00000},
    {"Ocean", 0x000A1A, 0xFFFFFF, 0x8FA6C4, 0x4C5F7A, 0x0D1D33, 0x1B2F4D, 0x1E90FF},
    {"Neon", 0x000000, 0xFFFFFF, 0x9A9A9A, 0x5A5A5A, 0x0F1A0F, 0x1F301F, 0x39FF14},
    {"Sunset", 0x120600, 0xFFFFFF, 0xB09A8A, 0x6A5444, 0x221208, 0x3A2212, 0xFF7A00},
    {"Ultraviolet", 0x07000F, 0xFFFFFF, 0xA89ABF, 0x5E4F78, 0x170D26, 0x2A1A42, 0xB24BFF},
    {"Flamingo", 0x000000, 0xFFFFFF, 0x9A9A9A, 0x5A5A5A, 0x1A0C14, 0x33182A, 0xFF3FA4},
    {"Gold Medal", 0x000000, 0xFFFFFF, 0x9A9A9A, 0x5A5A5A, 0x171308, 0x2E2610, 0xFFC400},
};
constexpr int THEME_N = sizeof THEMES / sizeof THEMES[0];
int cur = 0;
#define BLACK (THEMES[cur].bg)
#define WHITE (THEMES[cur].fg)
#define GREY (THEMES[cur].grey)
#define DIM (THEMES[cur].dim)
#define PANEL (THEMES[cur].panel)
#define TRACK (THEMES[cur].track)
#define RED (THEMES[cur].accent)

lgfx::PointerWrapper w_big, w_title, w_mid, w_label, w_body;
lgfx::VLWfont f_big, f_title, f_mid, f_label, f_body;
bool fontsReady = false;

void load(lgfx::VLWfont& f, lgfx::PointerWrapper& w, const uint8_t* data, size_t len) {
  w.set(data, len);
  f.loadFont(&w);
}

void text(lgfx::LovyanGFX& g, const lgfx::IFont* f, uint32_t fg, uint32_t bg, textdatum_t datum,
          const char* s, int x, int y) {
  g.setFont(f);
  g.setTextColor(g.color888(fg >> 16, (fg >> 8) & 0xFF, fg & 0xFF),
                 g.color888(bg >> 16, (bg >> 8) & 0xFF, bg & 0xFF));
  g.setTextDatum(datum);
  g.drawString(s, x, y);
}

uint32_t c(lgfx::LovyanGFX& g, uint32_t rgb) {
  return g.color888(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void upper(char* dst, size_t n, const char* src) {
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
  dst[i] = 0;
}

// Shortens s with "..." until it fits in maxW pixels using the current font.
void fit(lgfx::LovyanGFX& g, const lgfx::IFont* f, char* s, int maxW) {
  g.setFont(f);
  size_t len = strlen(s);
  if (g.textWidth(s) <= maxW) return;
  while (len > 1) {
    s[--len] = 0;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s...", s);
    if (g.textWidth(tmp) <= maxW) {
      strcpy(s, tmp);
      return;
    }
  }
}

void drawIcon(lgfx::LovyanGFX& g, int x0, int y0, uint32_t bg) {
  int br = bg >> 16, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
  int fr = WHITE >> 16, fg = (WHITE >> 8) & 0xFF, fb = WHITE & 0xFF;
  for (int y = 0; y < ICON_H; y++) {
    for (int x = 0; x < ICON_W; x++) {
      uint8_t a = icon_mtn_a8[y * ICON_W + x];
      if (a < 8) continue;
      int r = br + (fr - br) * a / 255, gg = bgc + (fg - bgc) * a / 255, b = bb + (fb - bb) * a / 255;
      g.drawPixel(x0 + x, y0 + y, g.color888(r, gg, b));
    }
  }
}

void header(lgfx::LovyanGFX& g, const char* title, bool wifiOk, const char* right = nullptr) {
  char t[48];
  upper(t, sizeof t, title);
  int room = 320 - 24 - ICON_W - 12;
  int rx = 320 - 18 - ICON_W;
  g.setFont(&f_label);
  if (!wifiOk) {
    int w = g.textWidth("NO WIFI");
    room -= w + 8;
    text(g, &f_label, RED, BLACK, top_right, "NO WIFI", rx, 10);
  } else if (right && *right) {
    g.setFont(&f_label);
    room -= g.textWidth(right) + 20;
    text(g, &f_label, GREY, BLACK, top_right, right, rx - 8, 10);
  }
  fit(g, &f_title, t, room);
  text(g, &f_title, WHITE, BLACK, top_left, t, 12, 4);
  drawIcon(g, 320 - 12 - ICON_W, 6, BLACK);
  g.fillRect(12, 40, 296, 2, c(g, RED));
}

void qr(lgfx::LovyanGFX& g, const char* s, int x, int y, int size) {
  g.fillRoundRect(x - 6, y - 6, size + 12, size + 12, 6, c(g, 0xFFFFFF));  // QR stays dark-on-light in every theme
  g.qrcode(s, x, y, size, 1, false);
}

void lines(lgfx::LovyanGFX& g, const lgfx::IFont* f, uint32_t fg, int x, int y, int lh,
           const char* const* ls, int n) {
  for (int i = 0; i < n; i++) text(g, f, fg, BLACK, top_left, ls[i], x, y + i * lh);
}

}  // namespace

namespace ui {

int themeCount() { return THEME_N; }
const char* themeName(int i) { return THEMES[i >= 0 && i < THEME_N ? i : 0].name; }
void setTheme(int i) { cur = i >= 0 && i < THEME_N ? i : 0; }
uint32_t accent() { return THEMES[cur].accent; }

void init() {
  if (fontsReady) return;
  load(f_big, w_big, font_big_vlw, sizeof font_big_vlw);
  load(f_title, w_title, font_title_vlw, sizeof font_title_vlw);
  load(f_mid, w_mid, font_mid_vlw, sizeof font_mid_vlw);
  load(f_label, w_label, font_label_vlw, sizeof font_label_vlw);
  load(f_body, w_body, font_body_vlw, sizeof font_body_vlw);
  fontsReady = true;
}

void formatMiles(char* out, size_t n, float miles, int decimals) {
  if (miles < 0) miles = 0;
  char raw[24];
  snprintf(raw, sizeof raw, "%.*f", decimals, miles);
  char* dot = strchr(raw, '.');
  int intLen = dot ? (int)(dot - raw) : (int)strlen(raw);
  size_t o = 0;
  for (int i = 0; i < intLen && o + 1 < n; i++) {
    out[o++] = raw[i];
    int left = intLen - i - 1;
    if (left > 0 && left % 3 == 0 && o + 1 < n) out[o++] = ',';
  }
  if (dot) for (const char* p = dot; *p && o + 1 < n; p++) out[o++] = *p;
  out[o] = 0;
}

void main(lgfx::LovyanGFX& g, const UiData& d) {
  g.fillScreen(c(g, BLACK));
  const float miles = d.ytd_m / 1609.344f;
  char big[24];
  formatMiles(big, sizeof big, miles, 1);
  text(g, &f_big, WHITE, BLACK, top_center, big, 160, 32);
  // header after the number: the big glyph boxes reach up into the header band
  char wx[40] = "";
  if (d.wx_valid) {
    if (d.wx_rain >= 30) snprintf(wx, sizeof wx, "%d\u00b0  %d%% RAIN", d.wx_temp, d.wx_rain);
    else snprintf(wx, sizeof wx, "%d\u00b0  %s", d.wx_temp, d.wx_desc);
  }
  header(g, d.athlete[0] ? d.athlete : "Next Mile Tracker", d.wifi_ok, wx);

  char lbl[64];
  snprintf(lbl, sizeof lbl, "MILES IN %d  -  %u RUN%s", d.year, d.ytd_runs, d.ytd_runs == 1 ? "" : "S");
  text(g, &f_label, GREY, BLACK, top_center, lbl, 160, 123);

  // goal bar
  const int bx = 12, by = 162, bw = 296, bh = 14;
  g.fillRoundRect(bx, by, bw, bh, bh / 2, c(g, TRACK));
  if (d.goal_mi > 0) {
    float pct = miles / d.goal_mi;
    int fw = (int)(bw * (pct > 1 ? 1 : pct));
    if (fw > 0) g.fillRoundRect(bx, by, fw < bh ? bh : fw, bh, bh / 2, c(g, RED));
    char gm[20], left[64], right[16];
    formatMiles(gm, sizeof gm, d.goal_mi, 0);
    if (pct >= 1) {
      snprintf(left, sizeof left, "GOAL %s MI  -  REACHED!", gm);
    } else if (d.goal_auto) {
      snprintf(left, sizeof left, "GOAL %s MI  -  BEAT %d", gm, d.last_year);
    } else {
      snprintf(left, sizeof left, "GOAL %s MI", gm);
    }
    snprintf(right, sizeof right, "%d%%", (int)(pct * 100 + 0.5f));
    text(g, &f_label, WHITE, BLACK, top_left, left, bx, 141);
    text(g, &f_label, pct >= 1 ? RED : WHITE, BLACK, top_right, right, bx + bw, 141);
  } else {
    text(g, &f_label, GREY, BLACK, top_left, "TAP, THEN SCAN THE CODE TO SET A YEARLY GOAL", bx, 141);
  }

  // last run panel
  const int px = 12, py = 186, pw = 296, ph = 48;
  g.fillRoundRect(px, py, pw, ph, 6, c(g, PANEL));
  if (d.last.valid) {
    char date[24] = "";
    time_t t = (time_t)d.last.start_local;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(date, sizeof date, "%a %b %d", &tmv);
    char dateU[24];
    upper(dateU, sizeof dateU, date);
    g.setFont(&f_label);
    int dw = g.textWidth(dateU);
    char name[64];
    snprintf(name, sizeof name, "LAST RUN  -  %s", d.last.name);
    char nameU[64];
    upper(nameU, sizeof nameU, name);
    fit(g, &f_label, nameU, pw - 24 - dw - 10);
    text(g, &f_label, GREY, PANEL, top_left, nameU, px + 10, py + 5);
    text(g, &f_label, GREY, PANEL, top_right, dateU, px + pw - 10, py + 5);

    float mi = d.last.meters / 1609.344f;
    char dist[20], pace[20], dur[20];
    snprintf(dist, sizeof dist, "%.2f MI", mi);
    if (mi > 0.05f) {
      int sp = (int)(d.last.moving_s / mi + 0.5f);
      snprintf(pace, sizeof pace, "%d:%02d /MI", sp / 60, sp % 60);
    } else {
      snprintf(pace, sizeof pace, "--");
    }
    uint32_t s = d.last.moving_s;
    if (s >= 3600) snprintf(dur, sizeof dur, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    else snprintf(dur, sizeof dur, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    text(g, &f_mid, WHITE, PANEL, top_left, dist, px + 10, py + 22);
    text(g, &f_mid, WHITE, PANEL, top_center, pace, px + pw / 2 + 6, py + 22);
    text(g, &f_mid, WHITE, PANEL, top_right, dur, px + pw - 10, py + 22);
  } else {
    text(g, &f_label, GREY, PANEL, top_left, "LAST RUN", px + 10, py + 5);
    text(g, &f_mid, WHITE, PANEL, top_left, "No runs yet this year. Go get one!", px + 10, py + 22);
  }
  if (d.sync_error) g.fillCircle(312, 232, 3, c(g, RED));
}

void wifiSetup(lgfx::LovyanGFX& g, const char* apName) {
  g.fillScreen(c(g, BLACK));
  header(g, "Let's get connected", true);
  text(g, &f_label, RED, BLACK, top_left, "STEP 1 OF 2  -  WIFI", 12, 46);
  char wifiQr[64];
  snprintf(wifiQr, sizeof wifiQr, "WIFI:T:nopass;S:%s;;", apName);
  qr(g, wifiQr, 18, 84, 104);
  const char* ls[] = {"Scan with your phone", "camera, or open WiFi", "settings and join:"};
  lines(g, &f_body, WHITE, 140, 72, 19, ls, 3);
  text(g, &f_mid, WHITE, BLACK, top_left, apName, 140, 130);
  const char* ls2[] = {"A setup page opens.", "Pick your home WiFi", "(must be 2.4 GHz)."};
  lines(g, &f_body, GREY, 140, 162, 19, ls2, 3);
}

void wifiRetry(lgfx::LovyanGFX& g, const char* ssid) {
  g.fillScreen(c(g, BLACK));
  header(g, "WiFi not found", false);
  text(g, &f_mid, WHITE, BLACK, top_center, "Looking for your WiFi:", 160, 70);
  char s[48];
  snprintf(s, sizeof s, "\"%s\"", ssid);
  text(g, &f_title, WHITE, BLACK, top_center, s, 160, 100);
  text(g, &f_body, GREY, BLACK, top_center, "Still trying in the background.", 160, 150);
  text(g, &f_body, GREY, BLACK, top_center, "New router or moved? Tap the screen", 160, 180);
  text(g, &f_body, GREY, BLACK, top_center, "to set up WiFi again.", 160, 200);
}

void linkStrava(lgfx::LovyanGFX& g, const char* url, const char* ip, bool relink) {
  g.fillScreen(c(g, BLACK));
  header(g, relink ? "Reconnect your watch" : "Link your watch", true);
  text(g, &f_label, RED, BLACK, top_left, relink ? "YOUR WATCH ACCOUNT NEEDS TO RELINK" : "STEP 2 OF 2  -  YOUR WATCH", 12, 46);
  qr(g, url, 18, 84, 104);
  const char* ls[] = {"Scan with your phone", "camera, then pick", "your watch brand."};
  lines(g, &f_body, WHITE, 140, 72, 19, ls, 3);
  const char* ls2[] = {"Phone must be on the", "same WiFi as this", "tracker. Or open:"};
  lines(g, &f_body, GREY, 140, 138, 19, ls2, 3);
  char u[40];
  snprintf(u, sizeof u, "http://%s", ip);
  text(g, &f_body, WHITE, BLACK, top_left, u, 140, 197);
}

void message(lgfx::LovyanGFX& g, const char* title, const char* line1, const char* line2) {
  g.fillScreen(c(g, BLACK));
  header(g, "Next Mile Tracker", true);
  text(g, &f_title, WHITE, BLACK, top_center, title, 160, 90);
  if (line1) text(g, &f_body, GREY, BLACK, top_center, line1, 160, 136);
  if (line2) text(g, &f_body, GREY, BLACK, top_center, line2, 160, 158);
}

void info(lgfx::LovyanGFX& g, const char* url, const char* ip, const char* ssid, bool ledAlways) {
  g.fillScreen(c(g, BLACK));
  header(g, "Settings", true);
  qr(g, url, 18, 58, 104);
  const char* ls[] = {"Scan to change your", "goal, screen or WiFi."};
  lines(g, &f_body, WHITE, 140, 54, 19, ls, 2);
  char u[40];
  snprintf(u, sizeof u, "http://%s", ip);
  text(g, &f_body, WHITE, BLACK, top_left, u, 140, 102);
  char w[48];
  snprintf(w, sizeof w, "WiFi: %s", ssid);
  char wf[48];
  strcpy(wf, w);
  fit(g, &f_body, wf, 168);
  text(g, &f_body, GREY, BLACK, top_left, wf, 140, 126);
  text(g, &f_body, GREY, BLACK, top_left, "Data from your watch", 140, 150);
  for (int x : {18, 164}) {
    g.fillRoundRect(x, THEME_BTN_Y, 138, THEME_BTN_H, 6, c(g, PANEL));
    g.drawRoundRect(x, THEME_BTN_Y, 138, THEME_BTN_H, 6, c(g, RED));
  }
  char t[48], tn[24];
  upper(tn, sizeof tn, THEMES[cur].name);
  snprintf(t, sizeof t, "COLORS: %s", tn);
  fit(g, &f_label, t, 130);
  text(g, &f_label, WHITE, PANEL, middle_center, t, 87, THEME_BTN_Y + THEME_BTN_H / 2);
  text(g, &f_label, WHITE, PANEL, middle_center, ledAlways ? "LIGHT: ALWAYS ON" : "LIGHT: NEW MILES", 233,
       THEME_BTN_Y + THEME_BTN_H / 2);
  text(g, &f_label, DIM, BLACK, top_center, "HOLD 10 SECONDS TO ERASE & START OVER", 160, 204);
  text(g, &f_label, DIM, BLACK, top_center, "TAP ANYWHERE ELSE TO GO BACK", 160, 222);
}

void resetHold(lgfx::LovyanGFX& g, int secondsLeft) {
  g.fillScreen(c(g, BLACK));
  header(g, "Start over?", true);
  char n[8];
  snprintf(n, sizeof n, "%d", secondsLeft);
  text(g, &f_big, RED, BLACK, top_center, n, 160, 46);
  text(g, &f_mid, WHITE, BLACK, top_center, "Keep holding to erase", 160, 140);
  text(g, &f_mid, WHITE, BLACK, top_center, "WiFi, watch link and goal.", 160, 164);
  text(g, &f_body, GREY, BLACK, top_center, "Let go to cancel.", 160, 200);
}

}  // namespace ui
