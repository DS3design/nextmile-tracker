#include <string>
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>

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

lgfx::PointerWrapper w_big, w_half, w_title, w_mid, w_label, w_body;
lgfx::VLWfont f_big, f_half, f_title, f_mid, f_label, f_body;
bool fontsReady = false;
const uint16_t* backdrop = nullptr;

void load(lgfx::VLWfont& f, lgfx::PointerWrapper& w, const uint8_t* data, size_t len) {
  w.set(data, len);
  f.loadFont(&w);
}

void text(lgfx::LovyanGFX& g, const lgfx::IFont* f, uint32_t fg, uint32_t bg, textdatum_t datum,
          const char* s, int x, int y) {
  g.setFont(f);
  (void)bg;
  // Transparent: a font's background box is taller than its letters and would cover nearby bars and panels.
  g.setTextColor(g.color888(fg >> 16, (fg >> 8) & 0xFF, fg & 0xFF));
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
      g.fillRect(x0 + x * 2, y0 + y * 2, 2, 2, g.color888(r, gg, b));  // drawn at 2x
    }
  }
}

constexpr int W = 800, H = 480, M = 30;  // screen and side margin

const char* okTxt = "CONNECT";
const char* hintTxt = "Type your WiFi password";
constexpr int IW = ICON_W * 2, IH = ICON_H * 2;

// Title across the top of the area x0..x0+w, with the Next Mile icon and an accent line under it.
void header(lgfx::LovyanGFX& g, const char* title, bool wifiOk, const char* right = nullptr, int x0 = 0, int w = W,
            bool icon = true) {
  char t[48];
  upper(t, sizeof t, title);
  const int iw = icon ? IW + 20 : 0;
  int room = w - 2 * M - iw;
  int rx = x0 + w - M - iw;
  g.setFont(&f_label);
  if (!wifiOk) {
    room -= g.textWidth("NO WIFI") + 20;
    text(g, &f_label, RED, BLACK, top_right, "NO WIFI", rx, 22);
  } else if (right && *right) {
    room -= g.textWidth(right) + 30;
    text(g, &f_label, GREY, BLACK, top_right, right, rx, 22);
  }
  fit(g, &f_title, t, room);
  text(g, &f_title, WHITE, BLACK, top_left, t, x0 + M, 8);
  if (icon) drawIcon(g, x0 + w - M - IW, 14, BLACK);
  g.fillRect(x0 + M, 76, w - 2 * M, 4, c(g, RED));
}

void qr(lgfx::LovyanGFX& g, const char* s, int x, int y, int size) {
  g.fillRoundRect(x - 12, y - 12, size + 24, size + 24, 12, c(g, 0xFFFFFF));  // QR stays dark-on-light in every theme
  g.qrcode(s, x, y, size, 2, false);
}

void lines(lgfx::LovyanGFX& g, const lgfx::IFont* f, uint32_t fg, int x, int y, int lh,
           const char* const* ls, int n) {
  for (int i = 0; i < n; i++) text(g, f, fg, BLACK, top_left, ls[i], x, y + i * lh);
}

// Word-wraps s to width w; returns the y just below the last line.
int wrap(lgfx::LovyanGFX& g, const lgfx::IFont* f, uint32_t fg, int x, int y, int w, int lh, const char* s) {
  g.setFont(f);
  std::string line, word;
  auto flush = [&]() {
    if (line.length()) text(g, f, fg, BLACK, top_left, line.c_str(), x, y), y += lh;
    line = "";
  };
  for (const char* p = s;; p++) {
    if (*p == ' ' || *p == '\n' || *p == 0) {
      std::string t = line.length() ? line + " " + word : word;
      if (line.length() && g.textWidth(t.c_str()) > w) flush(), t = word;
      line = t;
      word = "";
      if (*p == '\n' || *p == 0) flush();
      if (*p == 0) break;
    } else {
      word += *p;
    }
  }
  return y;
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
  load(f_half, w_half, font_half_vlw, sizeof font_half_vlw);
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

const char* noun(uint8_t kinds, bool plural) {
  if (kinds == 2) return plural ? "WALKS" : "WALK";
  if (kinds == 3) return plural ? "ACTIVITIES" : "ACTIVITY";
  return plural ? "RUNS" : "RUN";
}

namespace {

void goalLine(lgfx::LovyanGFX& g, const UiData& d, int x, int y, int w, int bh) {
  const float miles = d.ytd_m / 1609.344f;
  const int by = y + 40;
  g.fillRoundRect(x, by, w, bh, bh / 2, c(g, TRACK));
  if (d.goal_mi <= 0) {
    char t[64] = "TAP, THEN SCAN THE CODE TO SET A YEARLY GOAL";
    fit(g, &f_label, t, w);
    text(g, &f_label, GREY, BLACK, top_left, t, x, y);
    return;
  }
  float pct = miles / d.goal_mi;
  int fw = (int)(w * (pct > 1 ? 1 : pct));
  if (fw > 0) g.fillRoundRect(x, by, fw < bh ? bh : fw, bh, bh / 2, c(g, RED));
  char gm[20], left[64], right[16];
  formatMiles(gm, sizeof gm, d.goal_mi, d.goal_auto ? 1 : 0);
  const bool narrow = w < 500;
  if (narrow) {
    char over[20];
    formatMiles(over, sizeof over, miles - d.goal_mi, 1);
    if (pct >= 1 && d.goal_auto) snprintf(left, sizeof left, "BEAT %d BY %s!", d.last_year, over);
    else if (pct >= 1) snprintf(left, sizeof left, "GOAL %s  -  DONE!", gm);
    else if (d.goal_auto) snprintf(left, sizeof left, "BEAT %d: %s MI", d.last_year, gm);
    else snprintf(left, sizeof left, "GOAL %s MI", gm);
  } else if (pct >= 1 && d.goal_auto) {
    char over[20];
    formatMiles(over, sizeof over, miles - d.goal_mi, 1);
    snprintf(left, sizeof left, "YOU BEAT %d BY %s MI!", d.last_year, over);
  } else if (pct >= 1) {
    snprintf(left, sizeof left, "GOAL %s MI  -  REACHED!", gm);
  } else if (d.goal_auto) {
    snprintf(left, sizeof left, "GOAL %s MI  -  BEAT %d", gm, d.last_year);
  } else {
    snprintf(left, sizeof left, "GOAL %s MI", gm);
  }
  snprintf(right, sizeof right, "%d%%", (int)(pct * 100 + 0.5f));
  g.setFont(&f_label);
  fit(g, &f_label, left, w - g.textWidth(right) - 24);
  text(g, &f_label, WHITE, BLACK, top_left, left, x, y);
  text(g, &f_label, pct >= 1 ? RED : WHITE, BLACK, top_right, right, x + w, y);
}

// Latest run. Wide: one row of distance, pace and time. Narrow (half screen): distance and pace, then time and date.
void lastPanel(lgfx::LovyanGFX& g, const UiData& d, int px, int py, int pw, int ph) {
  const bool narrow = ph >= 110;
  g.fillRoundRect(px, py, pw, ph, 12, c(g, PANEL));
  g.setClipRect(px, py, pw, ph);
  char ln[64];
  if (!d.last.valid) {
    snprintf(ln, sizeof ln, "LAST %s", noun(d.kinds, false));
    text(g, &f_label, GREY, PANEL, top_left, ln, px + 20, py + 10);
    const char* none = d.kinds == 2 ? "No walks yet this year." : d.kinds == 3 ? "Nothing yet this year." : "No runs yet this year.";
    text(g, &f_mid, WHITE, PANEL, top_left, none, px + 20, py + 46);
    g.clearClipRect();
    return;
  }
  char date[24] = "", dateU[24];
  time_t t = (time_t)d.last.start_local;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(date, sizeof date, "%a %b %d", &tmv);
  upper(dateU, sizeof dateU, date);
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
  snprintf(ln, sizeof ln, "LAST %s  -  %s", noun(d.kinds, false), d.last.name);
  char nameU[64];
  upper(nameU, sizeof nameU, ln);
  if (narrow) {
    fit(g, &f_label, nameU, pw - 40);
    text(g, &f_label, GREY, PANEL, top_left, nameU, px + 20, py + 10);
    text(g, &f_mid, WHITE, PANEL, top_left, dist, px + 20, py + 44);
    text(g, &f_mid, WHITE, PANEL, top_right, pace, px + pw - 20, py + 44);
    text(g, &f_label, GREY, PANEL, top_left, dur, px + 20, py + 88);
    text(g, &f_label, GREY, PANEL, top_right, dateU, px + pw - 20, py + 88);
  } else {
    g.setFont(&f_label);
    const int dw = g.textWidth(dateU);
    fit(g, &f_label, nameU, pw - 40 - dw - 30);
    text(g, &f_label, GREY, PANEL, top_left, nameU, px + 20, py + 10);
    text(g, &f_label, GREY, PANEL, top_right, dateU, px + pw - 20, py + 10);
    text(g, &f_mid, WHITE, PANEL, top_left, dist, px + 20, py + 44);
    text(g, &f_mid, WHITE, PANEL, top_center, pace, px + pw / 2, py + 44);
    text(g, &f_mid, WHITE, PANEL, top_right, dur, px + pw - 20, py + 44);
  }
  g.clearClipRect();
}

void weatherText(char* wx, size_t n, const UiData& d) {
  *wx = 0;
  if (!d.wx_valid) return;
  if (d.wx_rain >= 30) snprintf(wx, n, "%d\u00b0  %d%% RAIN", d.wx_temp, d.wx_rain);
  else snprintf(wx, n, "%d\u00b0  %s", d.wx_temp, d.wx_desc);
}

// Theme background, or the photo blended most of the way into it so the numbers stay readable.
void bg(lgfx::LovyanGFX& g) {
  if (!backdrop) return (void)g.fillScreen(c(g, BLACK));
  const float A = 0.32f;
  const int br = BLACK >> 16, bgc = (BLACK >> 8) & 0xFF, bb = BLACK & 0xFF;
  static uint16_t row[W];
  for (int y = 0; y < H; y++) {
    const uint16_t* p = backdrop + y * W;
    for (int x = 0; x < W; x++) {
      const int r = ((p[x] >> 11) & 31) * 255 / 31, gr = ((p[x] >> 5) & 63) * 255 / 63, b = (p[x] & 31) * 255 / 31;
      const int R = br + (int)((r - br) * A), G = bgc + (int)((gr - bgc) * A), B = bb + (int)((b - bb) * A);
      row[x] = (R >> 3) << 11 | (G >> 2) << 5 | B >> 3;
    }
    g.pushImage(0, y, W, 1, (const lgfx::rgb565_t*)row);
  }
}

void drop(lgfx::LovyanGFX& g, int x, int y, uint32_t col) {  // raindrop, centred on x, y
  g.fillCircle(x, y + 3, 6, c(g, col));
  g.fillTriangle(x - 6, y + 2, x + 6, y + 2, x, y - 9, c(g, col));
}

// Three-day running forecast. Tall: day, high/low and rain chance stacked. Flat: one line per day.
void forecast(lgfx::LovyanGFX& g, const UiData& d, int px, int py, int pw, int ph) {
  g.fillRoundRect(px, py, pw, ph, 12, c(g, PANEL));
  const bool flat = ph < 80;
  const int cw = pw / 3;
  for (int i = 0; i < 3; i++) {
    const DayFc& f = d.fc[i];
    if (!f.valid) continue;
    const int cx = px + cw * i + cw / 2;
    char t[16], r[8];
    snprintf(t, sizeof t, "%d/%d\u00b0", f.hi, f.lo);
    snprintf(r, sizeof r, "%d%%", f.rain);
    const uint32_t rc = f.rain >= 30 ? RED : GREY;
    if (flat) {
      char line[40];
      snprintf(line, sizeof line, "%s  %s", f.name, t);
      g.setFont(&f_label);
      const int lw = g.textWidth(line), rw = g.textWidth(r), tw = lw + 18 + 16 + rw;
      const int x = cx - tw / 2, y = py + ph / 2;
      text(g, &f_label, WHITE, PANEL, middle_left, line, x, y);
      drop(g, x + lw + 18 + 6, y, rc);
      text(g, &f_label, rc, PANEL, middle_left, r, x + lw + 34, y);
      continue;
    }
    text(g, &f_label, GREY, PANEL, top_center, f.name, cx, py + 6);
    text(g, &f_label, WHITE, PANEL, top_center, t, cx, py + 32);
    g.setFont(&f_label);
    const int rw = g.textWidth(r);
    drop(g, cx - rw / 2 - 6, py + 72, rc);
    text(g, &f_label, rc, PANEL, middle_left, r, cx - rw / 2 + 6, py + 72);
  }
}

// One runner in the area x0..x0+w: the full screen, or one half of the split screen.
void runner(lgfx::LovyanGFX& g, const UiData& d, int x0, int w, const char* fallback, bool icon, const char* right) {
  const bool half = w < W;
  const float miles = d.ytd_m / 1609.344f;
  char big[24];
  formatMiles(big, sizeof big, miles, 1);
  text(g, half ? &f_half : &f_big, WHITE, BLACK, top_center, big, x0 + w / 2, half ? 56 : 34);
  // header after the number: the big glyph boxes reach up into the header band
  header(g, d.athlete[0] ? d.athlete : fallback, d.wifi_ok, right, x0, w, icon);
  char lbl[64];
  snprintf(lbl, sizeof lbl, "MILES IN %d  -  %u %s", d.year, d.ytd_runs, noun(d.kinds, d.ytd_runs != 1));
  text(g, &f_label, GREY, BLACK, top_center, lbl, x0 + w / 2, half ? 214 : 252);
  goalLine(g, d, x0 + M, half ? 262 : 296, w - 2 * M, half ? 22 : 28);
  if (half) {
    lastPanel(g, d, x0 + M, 352, w - 2 * M, 120);
  } else if (d.fc[0].valid) {
    lastPanel(g, d, x0 + M, 384, 424, 88);
    forecast(g, d, x0 + M + 440, 384, w - 2 * M - 440, 88);
  } else {
    lastPanel(g, d, x0 + M, 384, w - 2 * M, 88);
  }
}

}  // namespace

void setBackdrop(const uint16_t* px) { backdrop = px; }

void main(lgfx::LovyanGFX& g, const UiData& d) {
  bg(g);
  char wx[40];
  weatherText(wx, sizeof wx, d);
  runner(g, d, 0, W, "Next Mile Tracker", true, wx);
  if (d.sync_error) g.fillCircle(W - 8, H - 8, 5, c(g, RED));
}

void duo(lgfx::LovyanGFX& g, const UiData& a, const UiData& b) {
  bg(g);
  runner(g, a, 0, W / 2, "Runner 1", false, nullptr);
  runner(g, b, W / 2, W / 2, "Runner 2", false, nullptr);
  g.fillRect(W / 2 - 1, 20, 2, H - 40, c(g, TRACK));
  if (a.sync_error) g.fillCircle(8, H - 8, 5, c(g, RED));
  if (b.sync_error) g.fillCircle(W - 8, H - 8, 5, c(g, RED));
}

void together(lgfx::LovyanGFX& g, const UiData& a, const UiData& b, float sharedGoal) {
  bg(g);
  const float ma = a.ytd_m / 1609.344f, mb = b.ytd_m / 1609.344f, sum = ma + mb;
  char big[24];
  formatMiles(big, sizeof big, sum, 1);
  text(g, &f_half, WHITE, BLACK, top_center, big, W / 2, 56);
  char wx[40];
  weatherText(wx, sizeof wx, a);
  header(g, "Together", a.wifi_ok && b.wifi_ok, wx);
  char lbl[64];
  snprintf(lbl, sizeof lbl, "MILES TOGETHER IN %d  -  %u %s", a.year, a.ytd_runs + b.ytd_runs,
           a.kinds == b.kinds ? noun(a.kinds, true) : "ACTIVITIES");
  text(g, &f_label, GREY, BLACK, top_center, lbl, W / 2, 206);
  // One bar split into each person's share, against the shared goal (or both goals added up).
  const float goal = sharedGoal > 0 ? sharedGoal : a.goal_mi + b.goal_mi;
  const int bx = M, by = 272, bw = W - 2 * M, bh = 24;
  g.fillRoundRect(bx, by, bw, bh, bh / 2, c(g, TRACK));
  const float scale = goal > 0 ? (sum > goal ? sum : goal) : (sum > 0 ? sum : 1);
  const int wa = (int)(bw * ma / scale), wb = (int)(bw * mb / scale);
  if (wa + wb > 0) g.fillRoundRect(bx, by, std::max(bh, wa + wb), bh, bh / 2, c(g, GREY));
  if (wa > 0) g.fillRoundRect(bx, by, std::max(bh, wa), bh, bh / 2, c(g, RED));
  char t[64], gm[20];
  if (goal > 0) {
    formatMiles(gm, sizeof gm, goal, sharedGoal > 0 ? 0 : 1);
    const char* what = sharedGoal > 0 ? "SHARED GOAL" : "BOTH GOALS ADDED UP";
    snprintf(t, sizeof t, sum >= goal ? "%s: %s MI  -  DONE!" : "%s: %s MI", what, gm);
    text(g, &f_label, WHITE, BLACK, top_left, t, bx, 240);
    snprintf(t, sizeof t, "%d%%", (int)(sum / goal * 100 + 0.5f));
    text(g, &f_label, sum >= goal ? RED : WHITE, BLACK, top_right, t, bx + bw, 240);
  }
  // Legend in the bar's colours: who did what.
  const UiData* who[2] = {&a, &b};
  char nm[2][40], first[2][24];
  for (int i = 0; i < 2; i++) {
    upper(nm[i], sizeof nm[i], who[i]->athlete[0] ? who[i]->athlete : i ? "Runner 2" : "Runner 1");
    strncpy(first[i], nm[i], sizeof first[i] - 1);
    first[i][sizeof first[i] - 1] = 0;
    char* sp = strchr(first[i], ' ');
    if (sp && sp != first[i]) *sp = 0;
    char num[24], item[64];
    formatMiles(num, sizeof num, (i ? mb : ma), 1);
    snprintf(item, sizeof item, "%s  %s MI", nm[i], num);
    fit(g, &f_label, item, W / 2 - M - 40);
    const int x0 = i ? W / 2 + 10 : M;
    g.fillRoundRect(x0, 304, 18, 18, 4, c(g, i ? GREY : RED));
    text(g, &f_label, WHITE, BLACK, middle_left, item, x0 + 28, 313);
  }
  // Race: who's ahead this week, this month and this year.
  const bool fc = a.fc[0].valid;
  const int ry = fc ? 330 : 340, rh = fc ? 78 : 132;
  g.fillRoundRect(M, ry, W - 2 * M, rh, 12, c(g, PANEL));
  const char* const P[3] = {"THIS WEEK", "THIS MONTH", "THIS YEAR"};
  const float va[3] = {a.week_m, a.month_m, a.ytd_m}, vb[3] = {b.week_m, b.month_m, b.ytd_m};
  const int cw = (W - 2 * M) / 3;
  for (int k = 0; k < 3; k++) {
    const int cx = M + cw * k + cw / 2;
    text(g, &f_label, GREY, PANEL, top_center, P[k], cx, ry + (fc ? 4 : 24));
    const float d = (va[k] - vb[k]) / 1609.344f;
    char line[48], n[16];
    int lead = d > 0 ? 0 : 1;
    if (va[k] <= 0 && vb[k] <= 0) snprintf(line, sizeof line, "-");
    else if (fabsf(d) < 0.05f) snprintf(line, sizeof line, "TIED");
    else {
      formatMiles(n, sizeof n, fabsf(d), 1);
      snprintf(line, sizeof line, "%s +%s", first[lead], n);
    }
    fit(g, &f_mid, line, cw - 50);
    const int ly = ry + (fc ? 56 : 86);
    const bool sq = line[0] != '-' && strcmp(line, "TIED") != 0;
    g.setFont(&f_mid);
    const int lw = g.textWidth(line) + (sq ? 26 : 0);
    if (sq) g.fillRoundRect(cx - lw / 2, ly - 8, 16, 16, 4, c(g, lead ? GREY : RED));
    text(g, &f_mid, WHITE, PANEL, middle_left, line, cx - lw / 2 + (sq ? 26 : 0), ly);
  }
  if (fc) forecast(g, a, M, 416, W - 2 * M, 56);
  if (a.sync_error) g.fillCircle(8, H - 8, 5, c(g, RED));
  if (b.sync_error) g.fillCircle(W - 8, H - 8, 5, c(g, RED));
}

void night(lgfx::LovyanGFX& g, const UiData* r, int n, const char* hhmm, const char* ampm, const char* date) {
  // Always dark, whatever the theme, so it doesn't light up the room.
  const uint32_t K = 0x000000, T = 0x8A8A8A, S = 0x505050;
  g.fillScreen(c(g, K));
  g.setFont(&f_big);
  const int tw = g.textWidth(hhmm);
  g.setFont(&f_title);
  const int aw = g.textWidth(ampm);
  const int x = W / 2 - (tw + 14 + aw) / 2;
  text(g, &f_big, T, K, top_left, hhmm, x, 40);
  text(g, &f_title, S, K, top_left, ampm, x + tw + 14, 70);
  text(g, &f_label, S, K, top_center, date, W / 2, 262);
  char line[96], a[16], b[16];
  if (n < 2) {
    formatMiles(a, sizeof a, r[0].today_m / 1609.344f, 1);
    snprintf(line, sizeof line, "%s MI TODAY", a);
  } else {
    char na[24], nb[24];
    upper(na, sizeof na, r[0].athlete[0] ? r[0].athlete : "Runner 1");
    upper(nb, sizeof nb, r[1].athlete[0] ? r[1].athlete : "Runner 2");
    formatMiles(a, sizeof a, r[0].today_m / 1609.344f, 1);
    formatMiles(b, sizeof b, r[1].today_m / 1609.344f, 1);
    snprintf(line, sizeof line, "%s %s  -  %s %s", na, a, nb, b);
    text(g, &f_label, S, K, top_center, "MILES TODAY", W / 2, 304);
  }
  fit(g, &f_title, line, W - 2 * M);
  text(g, &f_title, T, K, top_center, line, W / 2, n < 2 ? 318 : 336);
  const DayFc& f = r[0].fc[1];
  if (f.valid) {
    snprintf(line, sizeof line, "TOMORROW  %d/%d\u00b0  %d%% RAIN  %s", f.hi, f.lo, f.rain, f.desc);
    text(g, &f_label, S, K, top_center, line, W / 2, 410);
  }
}

namespace {
const int LIST_Y = 124, ROW_H = 82, BTN_Y = 384, BTN_H = 88;
// Phone-style keyboard sized for a finger: 4 rows of 94 px under a 100 px top bar.
const int KB_Y = 100, KEY_H = 94;
const char* const KB_ROWS[4][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "-/:;()$&@\"", ".,?!'"},
    {"[]{}#%^*+=", "_\\|~<>`", ".,?!'"},
};
struct Key { int x, y, w, h, code; };

int layout(int layer, Key* out) {
  int n = 0;
  for (int r = 0; r < 3; r++) {
    const char* row = KB_ROWS[layer][r];
    const int len = strlen(row), y = KB_Y + r * KEY_H;
    const int w = r < 2 ? 80 : 560 / len, x0 = r < 2 ? (800 - w * len) / 2 : 120;
    for (int i = 0; i < len; i++) out[n++] = {x0 + i * w, y, w, KEY_H, row[i]};
  }
  const int y2 = KB_Y + 2 * KEY_H, y3 = KB_Y + 3 * KEY_H;
  out[n++] = {0, y2, 120, KEY_H, ui::KB_SHIFT};
  out[n++] = {680, y2, 120, KEY_H, ui::KB_DEL};
  out[n++] = {0, y3, 180, KEY_H, ui::KB_SYM};
  out[n++] = {180, y3, 300, KEY_H, ' '};
  out[n++] = {480, y3, 320, KEY_H, ui::KB_OK};
  return n;
}

void button(lgfx::LovyanGFX& g, int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg) {
  g.fillRoundRect(x, y, w, h, 12, c(g, bg));
  text(g, &f_label, fg, bg, middle_center, label, x + w / 2, y + h / 2);
}

bool keyAt(int x, int y, int layer, Key& k) {
  Key ks[48];
  const int n = layout(layer, ks);
  for (int i = 0; i < n; i++)
    if (x >= ks[i].x && x < ks[i].x + ks[i].w && y >= ks[i].y && y < ks[i].y + ks[i].h) return k = ks[i], true;
  return false;
}

void drawKey(lgfx::LovyanGFX& g, const Key& k, int layer, bool down) {
  const char* label;
  char ch[2] = {(char)k.code, 0};
  switch (k.code) {
    case ui::KB_SHIFT: label = layer == 0 ? "ABC" : layer == 1 ? "abc" : layer == 2 ? "#+=" : "123"; break;
    case ui::KB_SYM: label = layer < 2 ? "123" : "abc"; break;
    case ui::KB_DEL: label = "DEL"; break;
    case ui::KB_OK: label = okTxt; break;
    case ' ': label = "SPACE"; break;
    default: label = ch;
  }
  bool special = k.code < 0 || k.code == ' ';
  uint32_t bg = down ? WHITE : k.code == ui::KB_OK ? RED : special ? TRACK : PANEL;
  uint32_t fg = down ? BLACK : (k.code == ui::KB_OK ? 0xFFFFFF : WHITE);
  g.fillRoundRect(k.x + 3, k.y + 3, k.w - 6, k.h - 6, 10, c(g, bg));
  text(g, special ? &f_label : &f_mid, fg, bg, middle_center, label, k.x + k.w / 2, k.y + k.h / 2);
}
}  // namespace

void wifiList(lgfx::LovyanGFX& g, const WifiNet* nets, int n, int first, bool scanning, const char* error) {
  g.fillScreen(c(g, BLACK));
  header(g, "Welcome!", true);
  char e[64];
  upper(e, sizeof e, error && *error ? error : "Step 1 of 2  -  tap your home WiFi");
  fit(g, &f_label, e, 740);
  text(g, &f_label, RED, BLACK, top_left, e, 30, 86);
  if (scanning) {
    text(g, &f_mid, WHITE, BLACK, top_center, "Looking for WiFi...", 400, 200);
    text(g, &f_body, GREY, BLACK, top_center, "This takes a few seconds.", 400, 260);
  } else if (n == 0) {
    text(g, &f_mid, WHITE, BLACK, top_center, "No WiFi found.", 400, 200);
    text(g, &f_body, GREY, BLACK, top_center, "Tap Look again.", 400, 260);
  }
  for (int i = 0; !scanning && i < NET_ROWS && first + i < n; i++) {
    const WifiNet& w = nets[first + i];
    int y = LIST_Y + i * ROW_H;
    g.fillRoundRect(30, y, 740, ROW_H - 8, 12, c(g, PANEL));
    char name[40];
    strncpy(name, w.ssid, sizeof name - 1);
    name[sizeof name - 1] = 0;
    fit(g, &f_mid, name, 620);
    text(g, &f_mid, WHITE, PANEL, middle_left, name, 54, y + (ROW_H - 8) / 2);
    int bars = w.rssi > -60 ? 3 : w.rssi > -72 ? 2 : 1;
    for (int b = 0; b < 3; b++) g.fillRect(700 + b * 16, y + 54 - b * 12 - 14, 11, b * 12 + 14, c(g, b < bars ? WHITE : DIM));
  }
  button(g, 30, BTN_Y, 230, BTN_H, "LOOK AGAIN", TRACK, WHITE);
  if (!scanning && n > NET_ROWS) button(g, 285, BTN_Y, 230, BTN_H, first + NET_ROWS < n ? "MORE WIFI" : "BACK TO TOP", TRACK, WHITE);
  button(g, 540, BTN_Y, 230, BTN_H, "USE MY PHONE", TRACK, WHITE);
}

int wifiListHit(int x, int y, int n, int first) {
  if (y >= BTN_Y - 4) {
    if (x < 272) return LIST_AGAIN;
    if (x < 527) return n > NET_ROWS ? LIST_MORE : LIST_NONE;
    return LIST_PHONE;
  }
  if (y < LIST_Y - 4) return LIST_NONE;
  int i = (y - LIST_Y) / ROW_H;
  return i < NET_ROWS && first + i < n ? first + i : LIST_NONE;
}

void wifiPasswordField(lgfx::LovyanGFX& g, const char* pass) {
  g.fillRoundRect(156, 40, 636, 54, 10, c(g, PANEL));
  g.setFont(&f_mid);
  if (!*pass) {
    text(g, &f_body, DIM, PANEL, middle_left, hintTxt, 176, 67);
    g.fillRect(170, 47, 3, 40, c(g, RED));
    return;
  }
  const char* p = pass;
  while (*p && g.textWidth(p) > 590) p++;  // show the end while typing a long one
  text(g, &f_mid, WHITE, PANEL, middle_left, p, 176, 67);
  g.fillRect(180 + g.textWidth(p), 47, 3, 40, c(g, RED));
}

void wifiKeyboard(lgfx::LovyanGFX& g, int layer) {
  g.fillRect(0, KB_Y, 800, 480 - KB_Y, c(g, BLACK));
  Key ks[48];
  const int n = layout(layer, ks);
  for (int i = 0; i < n; i++) drawKey(g, ks[i], layer, false);
}

void okLabel(const char* l) { okTxt = l; }
void fieldHint(const char* h) { hintTxt = h; }

void textEntry(lgfx::LovyanGFX& g, const char* title, const char* value, int layer) {
  g.fillScreen(c(g, BLACK));
  button(g, 4, 4, 140, 88, "< BACK", TRACK, WHITE);
  char t[64];
  upper(t, sizeof t, title);
  fit(g, &f_label, t, 630);
  text(g, &f_label, GREY, BLACK, top_left, t, 160, 4);
  wifiPasswordField(g, value);
  wifiKeyboard(g, layer);
}

void wifiPassword(lgfx::LovyanGFX& g, const char* ssid, const char* pass, int layer, const char* error) {
  okLabel("CONNECT");
  fieldHint("Type your WiFi password");
  g.fillScreen(c(g, BLACK));
  button(g, 4, 4, 140, 88, "< BACK", TRACK, WHITE);
  char t[64];
  if (error && *error) {
    upper(t, sizeof t, error);
  } else {
    snprintf(t, sizeof t, "PASSWORD FOR %s", ssid);
  }
  fit(g, &f_label, t, 630);
  text(g, &f_label, error && *error ? RED : GREY, BLACK, top_left, t, 160, 4);
  wifiPasswordField(g, pass);
  wifiKeyboard(g, layer);
}

int wifiKeyHit(int x, int y, int layer) {
  if (y < KB_Y) return x < 150 ? KB_BACK : KB_NONE;
  Key k;
  return keyAt(x, y, layer, k) ? k.code : KB_NONE;
}

void wifiKeyFlash(lgfx::LovyanGFX& g, int x, int y, int layer, bool down) {
  Key k;
  if (keyAt(x, y, layer, k)) drawKey(g, k, layer, down);
}

void wifiPhone(lgfx::LovyanGFX& g, const char* apName, bool failed) {
  g.fillScreen(c(g, BLACK));
  header(g, "Use your phone", true);
  text(g, &f_label, RED, BLACK, top_left, failed ? "THAT DIDN'T CONNECT. TRY AGAIN." : "STEP 1 OF 2  -  YOUR HOME WIFI", 30, 86);
  char wifiQr[64];
  snprintf(wifiQr, sizeof wifiQr, "WIFI:T:nopass;S:%s;;", apName);
  qr(g, wifiQr, 50, 140, 208);
  int y = wrap(g, &f_body, WHITE, 310, 140, 460, 36, "Open your phone's camera, point it at this code, and tap Join.");
  wrap(g, &f_body, GREY, 310, y + 16, 460, 36, "A page opens to pick your home WiFi.");
  button(g, 30, BTN_Y, 740, BTN_H, "BACK TO THE WIFI LIST", TRACK, WHITE);
}

void wifiRetry(lgfx::LovyanGFX& g, const char* ssid) {
  g.fillScreen(c(g, BLACK));
  header(g, "WiFi not found", false);
  text(g, &f_mid, WHITE, BLACK, top_center, "Looking for:", 400, 120);
  char s[48];
  snprintf(s, sizeof s, "\"%s\"", ssid);
  text(g, &f_title, WHITE, BLACK, top_center, s, 400, 170);
  text(g, &f_body, GREY, BLACK, top_center, "It keeps trying on its own.", 400, 256);
  g.fillRect(30, 330, 740, 2, c(g, GREY));
  text(g, &f_body, WHITE, BLACK, top_center, "New router or new home?", 400, 356);
  text(g, &f_body, WHITE, BLACK, top_center, "Tap here to set up WiFi again.", 400, 400);
}

void linkStrava(lgfx::LovyanGFX& g, const char* url, const char* ip, bool relink) {
  g.fillScreen(c(g, BLACK));
  header(g, relink ? "Reconnect your watch" : "WiFi connected!", true);
  text(g, &f_label, RED, BLACK, top_left, relink ? "YOUR WATCH APP NEEDS TO RECONNECT" : "STEP 2 OF 2  -  YOUR WATCH APP", 30, 86);
  qr(g, url, 50, 150, 208);
  int y = wrap(g, &f_body, WHITE, 310, 140, 460, 36, "Point your phone's camera at this code and pick your watch.");
  wrap(g, &f_body, GREY, 310, y + 16, 460, 36, "Your phone needs to be on your home WiFi.");
  g.fillRect(30, 396, 740, 2, c(g, GREY));
  char u[48];
  snprintf(u, sizeof u, "No camera? Go to %s", ip);
  text(g, &f_body, WHITE, BLACK, top_center, u, 400, 420);
}

void message(lgfx::LovyanGFX& g, const char* title, const char* line1, const char* line2) {
  g.fillScreen(c(g, BLACK));
  header(g, "Next Mile Tracker", true);
  text(g, &f_title, WHITE, BLACK, top_center, title, 400, 180);
  if (line1) text(g, &f_body, GREY, BLACK, top_center, line1, 400, 262);
  if (line2) text(g, &f_body, GREY, BLACK, top_center, line2, 400, 304);
}

void resetHold(lgfx::LovyanGFX& g, int secondsLeft) {
  g.fillScreen(c(g, BLACK));
  header(g, "Start over?", true);
  char n[8];
  snprintf(n, sizeof n, "%d", secondsLeft);
  text(g, &f_big, RED, BLACK, top_center, n, 400, 84);
  text(g, &f_mid, WHITE, BLACK, top_center, "Keep holding to erase", 400, 300);
  text(g, &f_mid, WHITE, BLACK, top_center, "WiFi, watch links and goals.", 400, 346);
  text(g, &f_body, GREY, BLACK, top_center, "Let go to cancel.", 400, 414);
}

namespace {
struct Flake {
  float x, y, vy, sway, phase;
  uint32_t rgb;  // 0 = theme accent
  int16_t dx, dy, w, h;
  bool drawn;
  lgfx::rgb565_t under[100];
};
constexpr int FLAKES = 48;
const uint32_t FLAKE_RGB[] = {0xFFC83D, 0xFF5FA2, 0x3DD6FF, 0x6BE36B, 0xFFFFFF, 0};
const uint8_t FLAKE_SZ[4][2] = {{10, 6}, {6, 10}, {4, 10}, {6, 10}};  // cycling sizes make each flake look like it tumbles
Flake flakes[FLAKES];
bool flakesInit = false;
uint32_t flakesAt = 0;

float frand(float a, float b) { return a + (b - a) * (rand() % 1000) / 1000.0f; }

// ---- On-screen settings ------------------------------------------------------
const int TAB_X = M, TAB_W = 150, TAB_Y0 = 100, TAB_H = 66, TAB_GAP = 10;
const int SROW_X = 200, SROW_W = W - SROW_X - M, SROW_Y0 = 100, SROW_H = 72, SROW_GAP = 8;
const int CTL_Y = SROW_H / 2 - 26, CTL_W = 52, CTL_H = 52, VAL_W = 108;
const int CTL_X = SROW_X + SROW_W - (2 * CTL_W + VAL_W) - 16;

void sTab(lgfx::LovyanGFX& g, int i, const char* name, bool on) {
  char t[24];
  upper(t, sizeof t, name);
  const int y = TAB_Y0 + i * (TAB_H + TAB_GAP);
  g.fillRoundRect(TAB_X, y, TAB_W, TAB_H, 12, c(g, on ? PANEL : BLACK));
  if (on) g.drawRoundRect(TAB_X, y, TAB_W, TAB_H, 12, c(g, RED));
  text(g, &f_label, on ? WHITE : DIM, on ? PANEL : BLACK, middle_center, t, TAB_X + TAB_W / 2, y + TAB_H / 2);
}



void sRowDraw(lgfx::LovyanGFX& g, int i, const SetRow& r) {
  const int y = SROW_Y0 + i * (SROW_H + SROW_GAP);
  if (r.kind == ROW_TITLE) {
    char t[32];
    upper(t, sizeof t, r.label);
    text(g, &f_label, RED, BLACK, middle_left, t, SROW_X, y + SROW_H / 2);
    return;
  }
  if (r.kind == ROW_LINE) {
    char t[80];
    strncpy(t, r.label, sizeof t - 1);
    t[sizeof t - 1] = 0;
    fit(g, &f_body, t, SROW_W - 40);
    text(g, &f_body, DIM, BLACK, middle_center, t, SROW_X + SROW_W / 2, y + SROW_H / 2);
    return;
  }
  const uint32_t pbg = r.kind == ROW_ACTION && r.danger ? 0x380808 : PANEL;
  g.fillRoundRect(SROW_X, y, SROW_W, SROW_H, 12, c(g, pbg));
  char lbl[48];
  strncpy(lbl, r.label, sizeof lbl - 1);
  lbl[sizeof lbl - 1] = 0;
  if (r.kind == ROW_ACTION) {
    upper(lbl, sizeof lbl, lbl);
    text(g, &f_label, r.danger ? 0xFF8080 : WHITE, pbg, middle_center, lbl, SROW_X + SROW_W / 2, y + SROW_H / 2);
    return;
  }
  if (*r.hint) {
    fit(g, &f_label, lbl, SROW_W - 200);
    text(g, &f_label, WHITE, pbg, middle_left, lbl, SROW_X + 16, y + SROW_H / 2 - 14);
    char h[70];
    strncpy(h, r.hint, sizeof h - 1);
    h[sizeof h - 1] = 0;
    fit(g, &f_body, h, SROW_W - 200);
    text(g, &f_body, DIM, pbg, middle_left, h, SROW_X + 16, y + SROW_H / 2 + 18);
  } else {
    fit(g, &f_label, lbl, SROW_W - 200);
    text(g, &f_label, WHITE, pbg, middle_left, lbl, SROW_X + 16, y + SROW_H / 2);
  }
  const int cy = y + CTL_Y;
  switch (r.kind) {
    case ROW_TOGGLE: {
      const uint32_t bg = r.on ? RED : TRACK, fg = r.on ? 0xFFFFFF : WHITE;
      g.fillRoundRect(SROW_X + SROW_W - 110 - 16, cy + 10, 110, 32, 16, c(g, bg));
      g.fillCircle(SROW_X + SROW_W - 110 - 16 + (r.on ? 94 : 16), cy + 26, 12, c(g, fg));
      break;
    }
    case ROW_CHOICE:
    case ROW_STEP:
      button(g, CTL_X, cy, CTL_W, CTL_H, r.kind == ROW_CHOICE ? "<" : "-", TRACK, WHITE);
      button(g, CTL_X + CTL_W + VAL_W, cy, CTL_W, CTL_H, r.kind == ROW_CHOICE ? ">" : "+", TRACK, WHITE);
      { char v[24]; strncpy(v, r.value, sizeof v - 1); v[sizeof v - 1] = 0; fit(g, &f_label, v, VAL_W - 8);
        text(g, &f_label, WHITE, pbg, middle_center, v, CTL_X + CTL_W + VAL_W / 2, cy + CTL_H / 2); }
      break;
    case ROW_TEXT: {
      char v[32];
      strncpy(v, *r.value ? r.value : "-", sizeof v - 1);
      v[sizeof v - 1] = 0;
      fit(g, &f_label, v, CTL_X - SROW_X - 40);
      text(g, &f_label, WHITE, pbg, middle_right, v, SROW_X + SROW_W - 16, y + SROW_H / 2);
      break;
    }
    default: break;
  }
}




void spawn(Flake& f, int w, float y) {
  f.x = frand(12, w - 22);
  f.y = y;
  f.vy = frand(18, 40);
  f.sway = frand(4, 14);
  f.phase = frand(0, 6.28f);
  f.rgb = FLAKE_RGB[rand() % (sizeof FLAKE_RGB / sizeof FLAKE_RGB[0])];
}
}  // namespace

int settingsTabAt(int x, int y) {
  if (x < TAB_X || x > TAB_X + TAB_W || y < TAB_Y0) return -1;
  const int i = (y - TAB_Y0) / (TAB_H + TAB_GAP);
  return (y - TAB_Y0 - i * (TAB_H + TAB_GAP)) <= TAB_H ? i : -1;
}

int settingsHit(int x, int y, int n) {
  if (y < SROW_Y0 || x < SROW_X || x > SROW_X + SROW_W) return -1;
  const int i = (y - SROW_Y0) / (SROW_H + SROW_GAP);
  if (i >= n || y - SROW_Y0 - i * (SROW_H + SROW_GAP) > SROW_H) return -1;
  if (x >= CTL_X && x <= CTL_X + CTL_W) return i * 2;          // minus / left
  if (x >= CTL_X + CTL_W + VAL_W) return i * 2 + 1;            // plus / right
  return i * 2;
}

void settings(lgfx::LovyanGFX& g, const char* const* tabs, int nTabs, int tab, const SetRow* rows, int n,
              const char* status) {
  g.fillScreen(c(g, BLACK));
  header(g, "Settings", true);
  button(g, W - M - 110, 12, 110, 60, "DONE", RED, 0xFFFFFF);
  for (int i = 0; i < nTabs; i++) sTab(g, i, tabs[i], i == tab);
  for (int i = 0; i < n; i++) sRowDraw(g, i, rows[i]);
  if (status && *status) text(g, &f_body, DIM, BLACK, bottom_center, status, W / 2, H - 8);
}

void scan(lgfx::LovyanGFX& g, const char* title, const char* url, const char* line1, const char* line2,
          const char* btn) {
  g.fillScreen(c(g, BLACK));
  header(g, title, true);
  qr(g, url, 50, 130, 208);
  int y = wrap(g, &f_body, WHITE, 310, 140, 460, 36, line1);
  if (line2) wrap(g, &f_body, GREY, 310, y + 16, 460, 36, line2);
  char u[64];
  const char* p = url + (strncmp(url, "http://", 7) ? 0 : 7);
  snprintf(u, sizeof u, "No camera? Go to %s", p);
  text(g, &f_body, GREY, BLACK, top_center, u, 400, 380);
  if (btn) button(g, 270, 410, 260, 60, btn, PANEL, WHITE);
  else text(g, &f_label, DIM, BLACK, top_center, "TAP ANYWHERE TO GO BACK", 400, 430);
}

int scanHit(int x, int y) {
  return (x >= 270 && x <= 530 && y >= 410 && y <= 470) ? 1 : 0;
}

void confettiReset() {
  for (auto& f : flakes) f.drawn = false;
}

void confettiClear(lgfx::LovyanGFX& g) {
  for (int i = FLAKES - 1; i >= 0; i--) {  // reverse order, so overlapping flakes restore cleanly
    Flake& f = flakes[i];
    if (f.drawn) g.pushImage(f.dx, f.dy, f.w, f.h, f.under);
    f.drawn = false;
  }
}

void confettiStep(lgfx::LovyanGFX& g, uint32_t ms) {
  const int W = g.width(), H = g.height();
  if (!flakesInit) {
    srand(ms);
    for (auto& f : flakes) spawn(f, W, frand(0, H - 4)), f.drawn = false;
    flakesInit = true;
    flakesAt = ms;
  }
  float dt = (ms - flakesAt) / 1000.0f;
  if (dt < 0.04f) return;
  if (dt > 0.5f) dt = 0.5f;
  flakesAt = ms;
  int nx[FLAKES], ny[FLAKES], nw[FLAKES], nh[FLAKES];
  bool moved = false;
  for (int i = 0; i < FLAKES; i++) {
    Flake& f = flakes[i];
    f.y += f.vy * dt;
    if (f.y > H) spawn(f, W, -frand(4, 60));
    const uint8_t* sz = FLAKE_SZ[(ms / 240 + (int)(f.phase * 10)) % 4];
    nx[i] = (int)(f.x + f.sway * sinf(f.phase + ms / 800.0f));
    ny[i] = (int)f.y;
    nw[i] = sz[0];
    nh[i] = sz[1];
    const bool show = ny[i] >= 0 && ny[i] + nh[i] <= H && nx[i] >= 0 && nx[i] + nw[i] <= W;
    if (!show) nw[i] = 0;
    if (show != f.drawn || (show && (nx[i] != f.dx || ny[i] != f.dy || nw[i] != f.w || nh[i] != f.h))) moved = true;
  }
  if (!moved) return;
  confettiClear(g);
  const uint32_t acc = accent();
  for (int i = 0; i < FLAKES; i++) {
    Flake& f = flakes[i];
    if (!nw[i]) continue;
    f.dx = nx[i], f.dy = ny[i], f.w = nw[i], f.h = nh[i];
    g.readRect(f.dx, f.dy, f.w, f.h, f.under);
    g.fillRect(f.dx, f.dy, f.w, f.h, c(g, f.rgb ? f.rgb : acc));
    f.drawn = true;
  }
}

}  // namespace ui
