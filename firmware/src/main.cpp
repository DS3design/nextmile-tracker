// Next Mile mile tracker: shows a runner's watch miles for the current year on an ESP32 CYD.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "app.h"
#include "board.h"
#include "activities.h"
#include "extras.h"
#include "net.h"
#include "ota.h"
#include "source.h"
#include "ui.h"
#include "web.h"

namespace {

enum class Screen { None, Portal, WifiHelp, Connecting, Clock, WifiRetry, Link, Main, Info, NotConfigured };

const uint32_t RECENT_EVERY_MS = 30UL * 60 * 1000;
const uint32_t FULL_EVERY_MS = 24UL * 60 * 60 * 1000;
const uint32_t INFO_TIMEOUT_MS = 60UL * 1000;
const uint32_t OTA_FIRST_MS = 3UL * 60 * 1000, OTA_EVERY_MS = 24UL * 60 * 60 * 1000, HEALTHY_MS = 2UL * 60 * 1000;
const uint32_t HOLD_SHOW_MS = 2000, HOLD_RESET_MS = 10000, TAP_MAX_MS = 1500;

LGFX lcd;
WiFiManager wm;
Preferences prefs;
JPEGDEC jpeg;

Screen shown = Screen::None;
bool overlayInfo = false;
uint32_t infoSince = 0;
bool dirty = true;

bool wifiHelpOn = false, wifiFailed = false, wifiTrying = false;
bool portalActive = false, netUp = false, everConnected = false, wifiSaved = false;
uint32_t wifiTryAt = 0;
bool wantFull = true, wantRecent = false, wantPortal = false, wantReset = false, needRelink = false;
uint32_t lastFull = 0, lastRecent = 0, lastSyncAt = 0, backoffUntil = 0, bootMs = 0;
bool syncedOnce = false, syncing = false, loadingShown = false, bootSync = true;
src::Result lastResult = src::OK;

bool pressed = false;
uint32_t pressStart = 0, lastTouch = 0;
int holdShown = -1;

int jpegYOffset = 0;
int blNow = -1;
bool swallowTap = false;

void applyBacklight() {
  int b = extras::backlight();
  if (b != blNow) lcd.setBrightness(blNow = b);
}

bool timeValid() { return time(nullptr) > 1700000000; }

int localYear() {
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  return tmv.tm_year + 1900;
}

int jpegDraw(JPEGDRAW* p) {
  lcd.pushImage(p->x, p->y + jpegYOffset, p->iWidth, p->iHeight, (lgfx::swap565_t*)p->pPixels);
  return 1;
}

bool drawJpeg(uint8_t* buf, size_t len, int yOffset) {
  jpegYOffset = yOffset;
  if (!jpeg.openRAM(buf, len, jpegDraw)) return false;
  jpeg.setPixelType(RGB565_BIG_ENDIAN);
  lcd.startWrite();
  jpeg.decode(0, 0, 0);
  lcd.endWrite();
  jpeg.close();
  return true;
}

int32_t touchX = 0, touchY = 0;

bool touched() {
  int32_t x, y;
  if (!lcd.getTouch(&x, &y)) return false;
#if defined(PANEL_ST7789)
  y = lcd.height() - 1 - y;  // 2-USB boards: touch Y runs bottom-to-top
#endif
  touchX = x;
  touchY = y;
  return true;
}

void fade(int from, int to, int ms) {
  const int steps = 20;
  for (int i = 1; i <= steps; i++) {
    lcd.setBrightness(from + (to - from) * i / steps);
    delay(ms / steps);
  }
}

// intro.bin: u32 frame count, u32 fps, u32 sizes[count], then the JPEG frames (320x180, letterboxed).
bool playIntro() {
  File f = LittleFS.open("/intro.bin", "r");
  if (!f) return false;
  uint32_t hdr[2];
  if (f.read((uint8_t*)hdr, 8) != 8 || hdr[0] == 0 || hdr[0] > 2000 || hdr[1] == 0) return false;
  std::vector<uint32_t> sizes(hdr[0]);
  f.read((uint8_t*)sizes.data(), hdr[0] * 4);
  uint32_t maxSize = 0;
  for (uint32_t s : sizes) maxSize = max(maxSize, s);
  uint8_t* buf = (uint8_t*)malloc(maxSize);
  if (!buf) return false;
  const uint32_t frameMs = 1000 / hdr[1];
  lcd.fillScreen(TFT_BLACK);
  lcd.setBrightness(app::cfg.bright);
  uint32_t t0 = millis();
  bool skipped = false;
  for (uint32_t i = 0; i < hdr[0] && !skipped; i++) {
    if (f.read(buf, sizes[i]) != sizes[i]) break;
    drawJpeg(buf, sizes[i], (240 - 180) / 2);
    int32_t wait = (int32_t)(t0 + (i + 1) * frameMs - millis());
    if (wait > 0) delay(wait);
    if (i > 10 && touched()) skipped = true;
  }
  free(buf);
  f.close();
  fade(app::cfg.bright, 0, 250);
  return true;
}

void splash() {
  File f = LittleFS.open("/nextmile.jpg", "r");
  if (!f) return;
  size_t n = f.size();
  uint8_t* buf = (uint8_t*)malloc(n);
  if (buf && f.read(buf, n) == n) {
    lcd.fillScreen(TFT_BLACK);
    drawJpeg(buf, n, 0);
    fade(0, app::cfg.bright, 300);
    for (int i = 0; i < 20 && !touched(); i++) delay(100);
    fade(app::cfg.bright, 0, 250);
  }
  free(buf);
  f.close();
}

void loadSettings() {
  Settings& c = app::cfg;
  c.name = prefs.getString("name", "");
  c.tz = prefs.getString("tz", DEFAULT_TZ);
  c.tzManual = prefs.getBool("tzm", false);
  c.goalMi = prefs.getFloat("goal", 0);
  c.bright = prefs.getUChar("bright", 200);
  c.kinds = prefs.getUChar("kinds", prefs.getBool("walks", false) ? 3 : 1);
  c.theme = prefs.getUChar("theme", 0);
  c.autoDim = prefs.getBool("adim", true);
  c.led = prefs.getUChar("led", 1);
  c.place = prefs.getString("place", "");
  c.placeName = prefs.getString("placeN", "");
  c.lat = prefs.getFloat("lat", 0);
  c.lon = prefs.getFloat("lon", 0);
  ui::setTheme(c.theme);
}

// Styling and small helpers for the WiFi setup pages on the phone.
const char PORTAL_HEAD[] PROGMEM =
    "<style>body{font-size:17px}.wrap{max-width:440px;width:92%}h1{font-size:1.6rem;margin:.6em 0 .3em}"
    ".big{font-size:1.15rem}.hint{color:#aaa;font-size:1rem}"
    "button,input[type=submit]{background:#e4002b;border-radius:10px;line-height:3rem;font-size:1.2rem}"
    "button.ghost{background:transparent;border:2px solid #888;color:inherit;margin-top:8px}"
    "input{font-size:1.1rem;padding:12px}label[for=s],label[for=p]{display:block;margin-top:14px;font-weight:bold}"
    ".wrap>div>a{font-size:1.2rem;display:inline-block;padding:8px 0}</style>"
    "<script>addEventListener('DOMContentLoaded',function(){var w=document.querySelector('.wrap');"
    "if(!w||w.querySelector('h1'))return;var h=document.createElement('h1');h.textContent=document.title;"
    "w.insertBefore(h,w.firstChild);if(location.pathname=='/wifi'){var p=document.createElement('p');"
    "p.className='big';p.textContent='Tap your home WiFi name below, then type its password.';h.after(p)}});"
    "addEventListener('click',function(e){if(e.target.getAttribute&&e.target.getAttribute('data-ssid')!==null)"
    "setTimeout(function(){var p=document.getElementById('p');if(!p.disabled)p.focus()},50)});</script>";

void startPortal() {
  wantPortal = false;
  wifiHelpOn = wifiFailed = wifiTrying = false;
  web::stop();
  MDNS.end();
  netUp = false;
  overlayInfo = false;
  ui::wifiSetup(lcd, AP_NAME);
  shown = Screen::Portal;
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  wm.setCaptivePortalEnable(true);
  wm.setDarkMode(true);
  wm.setShowInfoErase(false);
  wm.setShowInfoUpdate(false);
  wm.setTitle("Set up your tracker");
  wm.setCustomHeadElement(PORTAL_HEAD);
  wm.setPreSaveConfigCallback([] {
    wifiTrying = true;
    wifiHelpOn = false;
    lcd.startWrite();
    ui::message(lcd, "Connecting...", "Joining your home WiFi.", "This takes about 20 seconds.");
    lcd.endWrite();
    shown = Screen::None;
  });
  wm.setHostname("nextmile");
  static std::vector<const char*> menu = {"wifi"};
  wm.setMenu(menu);
  wm.startConfigPortal(AP_NAME);
  portalActive = true;
}

void factoryReset() {
  ui::message(lcd, "Erasing...", "Removing WiFi, watch link and settings.", nullptr);
  src::unlink();
  prefs.clear();
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

void onNetUp() {
  netUp = true;
  wifiSaved = true;
  everConnected = true;
  configTzTime(app::cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.nist.gov");
  MDNS.begin("nextmile");
  MDNS.addService("http", "tcp", 80);
  web::begin();
  dirty = true;
}

// LED celebrations: a new run, every 100 miles, reaching the goal, passing last year's total.
void checkEvents(int prevRuns, float prevMi) {
  const acts::Totals& t = acts::totals();
  if (t.runs <= prevRuns) return;
  const float mi = t.meters / 1609.344f;
  UiData d = app::uiData();
  bool big = floorf(mi / 100) > floorf(prevMi / 100) || (d.goal_mi > 0 && prevMi < d.goal_mi && mi >= d.goal_mi) ||
             (d.last_year_mi > 0 && prevMi <= d.last_year_mi && mi > d.last_year_mi);
  if (big) extras::milestone();
  else extras::newRun();
}

void updatePace() {
  static uint32_t last = 0;
  if (last && millis() - last < 60000) return;
  last = millis();
  if (!timeValid()) return;
  UiData d = app::uiData();
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  int y = tmv.tm_year + 1900;
  int days = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  float frac = (tmv.tm_yday + (tmv.tm_hour * 60 + tmv.tm_min) / 1440.0f) / days;
  extras::setAhead(d.goal_mi > 0 && d.ytd_m / 1609.344f >= d.goal_mi * frac);
}

bool reloadAll = false;  // settings changed what counts, so the year is re-read from scratch

void drawLoading() {
  ui::message(lcd, "Loading your miles...", "Adding up this year so far.", "This can take a minute.");
  shown = Screen::None;
  loadingShown = true;
}

void maybeSync() {
  if (!src::linked() || needRelink || !netUp || !timeValid()) return;
  uint32_t now = millis();
  if ((int32_t)(backoffUntil - now) > 0) return;
  const int y = localYear();
  const acts::Totals& t = acts::totals();
  const int prevYear = t.year, prevRuns = t.runs;
  const float prevMi = t.meters / 1609.344f;
  const bool hadData = syncedOnce;
  if (bootSync) {  // a full re-read of the year at every power-up is slow, so skip it if one ran today
    bootSync = false;
    uint32_t at = prefs.getUInt("fullAt", 0);
    if (wantFull && t.year == y && at && (uint32_t)time(nullptr) - at < FULL_EVERY_MS / 1000) {
      wantFull = false;
      wantRecent = true;
      lastFull = now;
    }
  }
  bool full = wantFull || t.year != y || now - lastFull > FULL_EVERY_MS;
  bool recent = wantRecent || now - lastRecent > RECENT_EVERY_MS;
  bool lastYear = t.lastYear != y - 1;
  if (!full && !recent && !lastYear) return;

  const bool reread = reloadAll;  // also redo last year, which is otherwise read only once
  if ((t.year == 0 || reread) && shown != Screen::Info) drawLoading();
  reloadAll = false;
  src::Result r = src::OK;
  syncing = true;
  if (full || recent) r = src::sync(y, full, app::cfg.kinds);
  if (r == src::OK && (reread || acts::totals().lastYear != y - 1)) r = src::syncLastYear(y - 1, app::cfg.kinds);
  syncing = loadingShown = false;

  lastResult = r;
  if (r == src::OK) {
    if (full) {
      lastFull = now;
      prefs.putUInt("fullAt", (uint32_t)time(nullptr));
    }
    lastRecent = now;
    lastSyncAt = now;
    syncedOnce = true;
    wantFull = wantRecent = false;
    if (hadData && prevYear == y) checkEvents(prevRuns, prevMi);
  } else if (r == src::AUTH) {
    needRelink = true;
  } else {
    backoffUntil = now + (r == src::RATE ? 60UL : 5UL) * 60 * 1000;
  }
  dirty = true;
}

uint32_t otaLast = 0;
bool otaChecked = false, wantOta = false;
String otaStatus;

void otaProgress(int pct) {
  char b[32];
  snprintf(b, sizeof b, "Downloading... %d%%", pct);
  ui::message(lcd, "Updating", b, "Keep the tracker plugged in.");
}

void maybeOta() {
  uint32_t now = millis();
  if (now - bootMs > HEALTHY_MS) ota::markHealthy();
  if (!netUp || portalActive || !ota::enabled()) return;
  bool due = wantOta || (!otaChecked && now - bootMs > OTA_FIRST_MS) || (otaChecked && now - otaLast > OTA_EVERY_MS);
  if (!due || shown == Screen::Info) return;
  wantOta = false;
  otaChecked = true;
  otaLast = now;
  lcd.startWrite();
  otaStatus = ota::check(otaProgress);  // restarts on success
  lcd.endWrite();
  dirty = true;
}

Screen baseScreen() {
  if (portalActive) return wifiHelpOn ? Screen::WifiHelp : Screen::Portal;
  bool showData = src::linked() && !needRelink;
  if (showData && overlayInfo && netUp) return Screen::Info;
  if (showData) return Screen::Main;
  if (!netUp) return (everConnected || millis() - bootMs > 30000) ? Screen::WifiRetry : Screen::Connecting;
  if (!timeValid()) return Screen::Clock;
  return Screen::Link;
}

void draw(Screen s) {
  String ip = app::ip();
  lcd.startWrite();
  switch (s) {
    case Screen::Main: ui::main(lcd, app::uiData()); break;
    case Screen::Info: ui::info(lcd, ("http://" + ip + "/").c_str(), ip.c_str(), WiFi.SSID().c_str(), app::cfg.led == 2); break;
    case Screen::Link: ui::linkStrava(lcd, ("http://" + ip + "/link").c_str(), ip.c_str(), needRelink); break;
    case Screen::WifiRetry: ui::wifiRetry(lcd, WiFi.SSID().c_str()); break;
    case Screen::Connecting: ui::message(lcd, "Connecting...", "Joining your WiFi.", nullptr); break;
    case Screen::Clock: ui::message(lcd, "Connected!", "Setting the clock...", nullptr); break;
    case Screen::Portal: ui::wifiSetup(lcd, AP_NAME, wifiFailed); break;
    case Screen::WifiHelp: ui::wifiHelp(lcd, AP_NAME); break;
    case Screen::NotConfigured:
      ui::message(lcd, "Almost ready", "This tracker is missing its Strava keys.", "Contact DS3design to finish setup.");
      break;
    case Screen::None: break;
  }
  lcd.endWrite();
  shown = s;
  dirty = false;
}

void onTap() {
  switch (shown) {
    case Screen::Main:
      if (netUp) {
        overlayInfo = true;
        infoSince = millis();
      } else if (millis() - bootMs > 60000) {
        wantPortal = true;  // no WiFi for a while: let them pick a new network
      }
      break;
    case Screen::Info:
      if (touchY >= ui::THEME_BTN_Y - 10 && touchY <= ui::THEME_BTN_Y + ui::THEME_BTN_H + 6) {
        if (touchX < 160) {
          app::cfg.theme = (app::cfg.theme + 1) % ui::themeCount();
          ui::setTheme(app::cfg.theme);
        } else {
          app::cfg.led = app::cfg.led == 2 ? 1 : 2;
        }
        app::saveSettings();
        infoSince = millis();
      } else {
        overlayInfo = false;
      }
      break;
    case Screen::WifiRetry: wantPortal = true; break;
    case Screen::Portal: wifiHelpOn = true; break;
    case Screen::WifiHelp: wifiHelpOn = false; break;
    default: break;
  }
  dirty = true;
}

void handleTouch() {
  uint32_t now = millis();
  if (touched()) {
    lastTouch = now;
    if (!pressed) {
      pressed = true;
      pressStart = now;
      lgfx::touch_point_t raw;
      lcd.getTouchRaw(&raw, 1);
      Serial.printf("touch down x=%d y=%d raw=%d,%d\n", (int)touchX, (int)touchY, raw.x, raw.y);
      swallowTap = extras::screenOff();  // first touch in a dark room only lights the screen
      extras::wake();
      extras::dismiss();
      applyBacklight();
    }
    uint32_t held = now - pressStart;
    if (held >= HOLD_RESET_MS) factoryReset();
    if (held >= HOLD_SHOW_MS) {
      int left = (HOLD_RESET_MS - held + 999) / 1000;
      if (left != holdShown) {
        holdShown = left;
        ui::resetHold(lcd, left);
      }
    }
    return;
  }
  if (pressed && now - lastTouch > 80) {  // XPT2046 drops out briefly mid-press; wait before calling it a release
    pressed = false;
    uint32_t held = lastTouch - pressStart;
    Serial.printf("touch up held=%u ms\n", (unsigned)held);
    if (holdShown >= 0) {
      holdShown = -1;
      shown = Screen::None;  // force a redraw, even of the WiFi setup screen
      dirty = true;
    } else if (held < TAP_MAX_MS && !swallowTap) {
      onTap();
    }
  }
}

// Runs between web requests during a sync, so touch, the light and the screen keep working.
void syncPump() {
  if (!syncing) return;
  static int prevHold = -1;
  static uint32_t logAt = 0;
  if (millis() - logAt > 2000) {
    logAt = millis();
    Serial.printf("sync pump screen=%d\n", (int)shown);
  }
  handleTouch();
  extras::loop(false);
  applyBacklight();
  const int was = prevHold;
  prevHold = holdShown;
  if (holdShown >= 0) return;
  if (loadingShown) {
    if (was >= 0) drawLoading();
    return;
  }
  Screen want = baseScreen();
  if (want != shown || dirty) draw(want);
}

}  // namespace

namespace app {

Settings cfg;

void saveSettings() {
  prefs.putString("name", cfg.name);
  prefs.putString("tz", cfg.tz);
  prefs.putBool("tzm", cfg.tzManual);
  prefs.putFloat("goal", cfg.goalMi);
  prefs.putUChar("bright", cfg.bright);
  prefs.putUChar("kinds", cfg.kinds);
  prefs.putUChar("theme", cfg.theme);
  prefs.putBool("adim", cfg.autoDim);
  prefs.putUChar("led", cfg.led);
  prefs.putString("place", cfg.place);
  prefs.putString("placeN", cfg.placeName);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  applyBacklight();
  dirty = true;
}

void applyTz() {
  setenv("TZ", cfg.tz.c_str(), 1);
  tzset();
  wantFull = true;
  bootSync = false;
}

void requestSync(bool full) {
  if (full) wantFull = reloadAll = true, bootSync = false;
  else wantRecent = true;
  backoffUntil = 0;
  dirty = true;
}

void refresh() { dirty = true; }
void requestPortal() { wantPortal = true; }
void requestReset() { wantReset = true; }

void onLinked() {
  bootSync = false;
  needRelink = false;
  wantFull = true;
  backoffUntil = 0;
  overlayInfo = false;
  dirty = true;
}

void onPushed() {
  lastSyncAt = millis();
  syncedOnce = true;
  lastResult = src::OK;
  needRelink = false;
  dirty = true;
}

float carryInMi() { return prefs.getFloat("carryMi", 0); }

void setCarryInMi(float mi) {
  if (mi < 0 || mi > 20000) return;
  prefs.putFloat("carryMi", mi);
  if (timeValid()) acts::setCarryIn(localYear(), mi * 1609.344f);
  dirty = true;
}

String ip() { return WiFi.localIP().toString(); }

void requestUpdate() { wantOta = true; }
String updateStatus() { return otaStatus; }

uint32_t lastSyncAgoS() { return syncedOnce ? (millis() - lastSyncAt) / 1000 : UINT32_MAX; }

UiData uiData() {
  const acts::Totals& t = acts::totals();
  UiData d;
  String n = cfg.name.length() ? cfg.name : src::athleteName();
  strlcpy(d.athlete, n.c_str(), sizeof d.athlete);
  d.year = timeValid() ? localYear() : (t.year ? t.year : 2026);
  bool current = t.year == d.year;
  d.ytd_m = current ? t.meters : 0;
  d.ytd_runs = current ? t.runs : 0;
  if (current) d.last = t.last;
  d.kinds = cfg.kinds;
  d.last_year = d.year - 1;
  d.last_year_mi = t.lastYear == d.year - 1 ? t.lastYearMeters / 1609.344f : 0;
  if (cfg.goalMi > 0) {
    d.goal_mi = cfg.goalMi;
    d.goal_auto = false;
  } else {
    // "Beat last year": last year's miles plus ~10%, rounded up to a tidy number.
    d.goal_mi = d.last_year_mi > 0 ? ceilf(d.last_year_mi * 1.1f / 10.0f) * 10.0f : 0;
    d.goal_auto = true;
  }
  const extras::Weather& w = extras::weather();
  d.wx_valid = w.valid;
  d.wx_temp = w.temp;
  d.wx_rain = w.rain;
  d.wx_desc = extras::describe(w.code);
  d.wifi_ok = netUp;
  d.sync_error = lastResult != src::OK;
  d.source = src::kind() == src::GOOGLE ? "FITBIT" : src::kind() == src::APPLE ? "APPLE" : src::kind() == src::POLAR ? "POLAR" : src::kind() == src::GARMIN ? "GARMIN" : "STRAVA";
  return d;
}

}  // namespace app

void setup() {
  Serial.begin(115200);
  bootMs = millis();
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  ui::init();

  ota::begin();
  net::setPump(syncPump);
  prefs.begin("app", false);
  loadSettings();
  extras::begin();
  setenv("TZ", app::cfg.tz.c_str(), 1);
  tzset();
  if (!LittleFS.begin(false)) Serial.println("LittleFS mount failed; skipping intro");
  src::begin();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("nextmile");
  WiFi.setAutoReconnect(true);
  const bool haveWifi = wm.getWiFiIsSaved();
  wifiSaved = haveWifi;
  if (haveWifi) WiFi.begin();  // connects in the background while the intro plays
  wifiTryAt = millis();

  playIntro();
  splash();

  if (!haveWifi) wantPortal = true;
  draw(baseScreen());
  applyBacklight();
}

void loop() {
  if (wantReset) factoryReset();
  if (wantPortal) startPortal();

  if (portalActive) {
    wm.process();
    if (!wm.getConfigPortalActive()) portalActive = false;
    if (wifiTrying) {  // a password was submitted on the phone and the join attempt just finished
      wifiTrying = false;
      if (portalActive && WiFi.status() != WL_CONNECTED) wifiFailed = true;
    }
    if (WiFi.status() == WL_CONNECTED && !portalActive) dirty = true;
  }

  // Routers often refuse the first join right after a restart, and the ESP32 doesn't retry that on its own.
  if (!portalActive && wifiSaved) {
    const wl_status_t st = WiFi.status();
    const uint32_t since = millis() - wifiTryAt;
    if (st != WL_CONNECTED && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ? since > 4000 : since > 20000)) {
      Serial.printf("wifi retry (status %d)\n", (int)st);
      WiFi.disconnect();
      WiFi.begin();
      wifiTryAt = millis();
    }
  }

  const bool up = WiFi.status() == WL_CONNECTED;
  if (!portalActive) {
    if (up && !netUp) onNetUp();
    if (!up && netUp) {
      netUp = false;
      dirty = true;
    }
  }

  web::loop();
  maybeSync();
  extras::setSetup(shown != Screen::Main && shown != Screen::Info && shown != Screen::None);
  extras::loop(netUp && !portalActive);
  updatePace();
  applyBacklight();
  maybeOta();

  if (overlayInfo && millis() - infoSince > INFO_TIMEOUT_MS) {
    overlayInfo = false;
    dirty = true;
  }

  static uint32_t dbgAt = 0;
  if (millis() - dbgAt > 2000) {
    dbgAt = millis();
    Serial.printf("ldr=%d bl=%d pressed=%d screen=%d wifi=%d\n", extras::lightRaw(), blNow, pressed, (int)shown, (int)WiFi.status());
  }
  handleTouch();
  if (holdShown < 0) {
    Screen want = baseScreen();
    if (want != shown || (dirty && want != Screen::Portal && want != Screen::WifiHelp)) draw(want);
  }
  delay(10);
}
