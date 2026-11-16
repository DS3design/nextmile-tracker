// Next Mile mile tracker: shows a runner's watch miles for the current year on an Elecrow CrowPanel Advance 7".
#include <Arduino.h>
#include <ESPmDNS.h>
#include <JPEGDEC.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include <algorithm>

#include "app.h"
#include "board.h"
#include "clockchip.h"
#include "ledfx.h"
#include <esp_sntp.h>
#include "activities.h"
#include "extras.h"
#include "net.h"
#include "ota.h"
#include "source.h"
#include "ui.h"
#include "web.h"

namespace {

enum class Screen { None, WifiList, WifiPass, WifiPhone, Joining, Connecting, Clock, WifiRetry, Link, Main, Together, Night, Info, NotConfigured };

const uint32_t RECENT_EVERY_MS = 30UL * 60 * 1000;
const uint32_t FULL_EVERY_MS = 24UL * 60 * 60 * 1000;
const uint32_t INFO_TIMEOUT_MS = 60UL * 1000;
const uint32_t OTA_FIRST_MS = 3UL * 60 * 1000, OTA_EVERY_MS = 24UL * 60 * 60 * 1000, HEALTHY_MS = 2UL * 60 * 1000;
const uint32_t HOLD_SHOW_MS = 2000, HOLD_RESET_MS = 10000, TAP_MAX_MS = 1500;

#define lcd (board::lcd())  // the board version is only known once board::detect() runs
WiFiManager wm;
Preferences prefs;
JPEGDEC jpeg;

Screen shown = Screen::None;
bool overlayInfo = false;
uint32_t infoSince = 0;
bool dirty = true;

bool wifiFailed = false, wifiTrying = false;
// Touchscreen WiFi setup.
enum class SetupView { List, Pass, Phone };
SetupView setupView = SetupView::List;
std::vector<ui::WifiNet> nets;
int netFirst = 0, kbLayer = 0;
bool scanning = false, joining = false;
String pickSsid, pickPass, listErr, passErr;
uint32_t joinAt = 0;
bool portalActive = false, netUp = false, everConnected = false, wifiSaved = false;
uint32_t wifiTryAt = 0;
bool wantPortal = false, wantReset = false;
uint32_t bootMs = 0;
bool syncing = false, loadingShown = false;

// Sync state for each runner (two-runner mode). The provider and run cache hold one runner at a time,
// so each runner's totals are kept here for drawing.
struct Runner {
  bool wantFull = true, wantRecent = false, needRelink = false, syncedOnce = false, bootSync = true, reloadAll = false;
  uint32_t lastFull = 0, lastRecent = 0, lastSyncAt = 0, backoffUntil = 0;
  src::Result lastResult = src::OK;
  bool linked = false;
  src::Kind kind = src::NONE;
  String athlete;
  acts::Totals tot;
  float todayM = 0, weekM = 0, monthM = 0;
  int32_t statDay = 0;  // local day number those were counted on
};
Runner run[2];
bool together = false;  // two-runner mode: showing the combined miles

const char* key(const char* k, int i) {  // per-runner prefs key: runner 2 gets a "1" on the end
  static char b[16];
  snprintf(b, sizeof b, "%s%s", k, i ? "1" : "");
  return b;
}

// Local wall-clock seconds (what activity start times are stored in).
int32_t localNow() {
  time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  return acts::daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday) * 86400 + l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
}

// Copy what's loaded for the current runner, so both can be drawn without switching.
void snap() {
  Runner& r = run[src::current()];
  if (time(nullptr) > 1700000000) {
    const int32_t day = localNow() / 86400;
    time_t t = time(nullptr);
    tm l;
    localtime_r(&t, &l);
    r.todayM = acts::since(day * 86400);
    r.weekM = acts::since((day - (day + 3) % 7) * 86400);  // since Monday; day 0 was a Thursday
    r.monthM = acts::since(acts::daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, 1) * 86400);
    r.statDay = day;
  }
  r.tot = acts::totals();
  r.linked = src::linked();
  r.kind = src::kind();
  r.athlete = src::athleteName();
}

// Two runners on screen: the mode is on and both have linked.
bool duoShown() { return app::cfg.duo && run[0].linked && run[1].linked; }
bool anyData() { return run[0].linked && !run[0].needRelink; }

bool pressed = false;
uint32_t pressStart = 0, lastTouch = 0;
int holdShown = -1;

int jpegYOffset = 0, jpegXOffset = 0;
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
  lcd.pushImage(p->x + jpegXOffset, p->y + jpegYOffset, p->iWidth, p->iHeight, (lgfx::swap565_t*)p->pPixels);
  return 1;
}

// Draws the JPEG centred on the screen.
bool drawJpeg(uint8_t* buf, size_t len) {
  if (!jpeg.openRAM(buf, len, jpegDraw)) return false;
  jpegXOffset = (lcd.width() - jpeg.getWidth()) / 2;
  jpegYOffset = (lcd.height() - jpeg.getHeight()) / 2;
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

// intro.bin: u32 frame count, u32 fps, u32 sizes[count], then the JPEG frames (800x450, letterboxed).
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
    drawJpeg(buf, sizes[i]);
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
    drawJpeg(buf, n);
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
  c.duo = prefs.getBool("duo", false);
  c.name2 = prefs.getString("name1", "");
  c.goalMi2 = prefs.getFloat("goal1", 0);
  c.kinds2 = prefs.getUChar("kinds1", 1);
  c.sharedGoal = prefs.getFloat("shared", 0);
  c.nightClock = prefs.getBool("nightclk", true);
  c.ledFx = prefs.getUChar("ledfx", ledfx::MIX);
  c.ledCount = prefs.getUChar("ledn", 10);
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
    "addEventListener('submit',function(e){var b=e.target.querySelector('button');if(b&&/\\/wifi(\\?|$)/.test(e.target.action))"
    "b.textContent='Looking for WiFi... (about 10 seconds)'});"
    "addEventListener('click',function(e){if(e.target.getAttribute&&e.target.getAttribute('data-ssid')!==null)"
    "setTimeout(function(){var p=document.getElementById('p');if(!p.disabled)p.focus()},50)});</script>";

void startScan() {
  scanning = true;
  nets.clear();
  netFirst = 0;
  if (setupView == SetupView::List) shown = Screen::None;
}

// Scans once "Looking for WiFi..." is on screen. Blocking: the async scan never finished alongside the setup WiFi.
void pollScan() {
  if (!scanning || (setupView == SetupView::List && shown != Screen::WifiList)) return;
  WiFi.scanDelete();
  const int n = WiFi.scanNetworks(false, false, false, 300);
  Serial.printf("wifi scan returned %d\n", n);
  scanning = false;
  for (int i = 0; i < n; i++) {
    String id = WiFi.SSID(i);
    if (!id.length()) continue;
    auto it = std::find_if(nets.begin(), nets.end(), [&](const ui::WifiNet& w) { return id == w.ssid; });
    if (it != nets.end()) {
      it->rssi = max(it->rssi, (int)WiFi.RSSI(i));
      continue;
    }
    ui::WifiNet w;
    strlcpy(w.ssid, id.c_str(), sizeof w.ssid);
    w.rssi = WiFi.RSSI(i);
    w.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    nets.push_back(w);
  }
  std::sort(nets.begin(), nets.end(), [](const ui::WifiNet& a, const ui::WifiNet& b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
  Serial.printf("wifi scan: %d found\n", (int)nets.size());
  if (setupView == SetupView::List) shown = Screen::None;
}

void startPortal() {
  wantPortal = false;
  wifiTrying = false;
  web::stop();
  MDNS.end();
  netUp = false;
  overlayInfo = false;
  shown = Screen::None;
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
    lcd.startWrite();
    ui::message(lcd, "Connecting...", "Joining your home WiFi.", "This takes about 20 seconds.");
    lcd.endWrite();
    shown = Screen::None;
  });
  wm.setHostname("nextmile");
  // A public-range address: some Android phones (Samsung) won't pop up a sign-in page for a private one.
  wm.setAPStaticIPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));
  wm.setDebugOutput(true, WM_DEBUG_DEV);
  static std::vector<const char*> menu = {"wifi"};
  wm.setMenu(menu);
  wm.startConfigPortal(AP_NAME);
  portalActive = true;
  startScan();
}

void joinWifi(const String& ssid, const String& pass) {
  Serial.printf("joining %s\n", ssid.c_str());
  pickSsid = ssid;
  if (portalActive) {
    wm.stopConfigPortal();
    portalActive = false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  joining = true;
  joinAt = millis();
}

void checkJoin() {
  const wl_status_t st = WiFi.status();
  const uint32_t since = millis() - joinAt;
  if (st == WL_CONNECTED) {
    joining = false;
    wifiSaved = true;
    pickPass = "";
    return;
  }
  const bool bad = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) && since > 6000;
  if (!bad && since < 25000) return;
  joining = false;
  WiFi.disconnect(false, true);  // don't keep a password that didn't work
  wifiSaved = false;
  Serial.printf("join failed (status %d)\n", (int)st);
  startPortal();
  if (st == WL_NO_SSID_AVAIL) {
    listErr = "Couldn't reach " + pickSsid + ". Try again.";
  } else {
    passErr = "Wrong password. Try again.";
    setupView = SetupView::Pass;
  }
}

// Lights the key under the finger while it's down, like a phone keyboard; the key types when the finger lifts.
int keyCode = ui::KB_NONE, keyX = 0, keyY = 0;
void trackKey() {
  const int code = ui::wifiKeyHit(touchX, touchY, kbLayer);
  if (code == keyCode) return;
  lcd.startWrite();
  if (keyCode != ui::KB_NONE) ui::wifiKeyFlash(lcd, keyX, keyY, kbLayer, false);
  if (code != ui::KB_NONE) ui::wifiKeyFlash(lcd, touchX, touchY, kbLayer, true);
  lcd.endWrite();
  keyCode = code;
  keyX = touchX;
  keyY = touchY;
}

void setupTap() {
  if (setupView == SetupView::List) {
    const int hit = ui::wifiListHit(touchX, touchY, nets.size(), netFirst);
    if (hit >= 0) {
      const ui::WifiNet& w = nets[hit];
      listErr = "";
      if (w.open) return joinWifi(w.ssid, "");
      if (pickSsid != w.ssid) pickPass = "";
      pickSsid = w.ssid;
      passErr = "";
      kbLayer = 0;
      setupView = SetupView::Pass;
    } else if (hit == ui::LIST_AGAIN) {
      listErr = "";
      startScan();
    } else if (hit == ui::LIST_MORE) {
      netFirst += ui::NET_ROWS;
      if (netFirst >= (int)nets.size()) netFirst = 0;
      shown = Screen::None;
    } else if (hit == ui::LIST_PHONE) {
      setupView = SetupView::Phone;
    }
    return;
  }
  if (setupView == SetupView::Phone) {
    if (touchY >= 372) setupView = SetupView::List;
    return;
  }
  const int k = ui::wifiKeyHit(touchX, touchY, kbLayer);
  keyCode = ui::KB_NONE;
  if (k == ui::KB_NONE) return;
  if (k == ui::KB_BACK) {
    setupView = SetupView::List;
    return;
  }
  if (k == ui::KB_OK) {
    if (pickPass.length()) return joinWifi(pickSsid, pickPass);
    lcd.startWrite();
    ui::wifiKeyFlash(lcd, touchX, touchY, kbLayer, false);
    lcd.endWrite();
    return;
  }
  const int was = kbLayer;
  if (k == ui::KB_SHIFT) kbLayer = kbLayer < 2 ? kbLayer ^ 1 : kbLayer == 2 ? 3 : 2;
  else if (k == ui::KB_SYM) kbLayer = kbLayer < 2 ? 2 : 0;
  else if (k == ui::KB_DEL) pickPass.remove(pickPass.length() ? pickPass.length() - 1 : 0);
  else if (pickPass.length() < 63) {
    pickPass += (char)k;
    if (kbLayer == 1) kbLayer = 0;  // shift is for one letter, like a phone keyboard
  }
  lcd.startWrite();
  if (kbLayer != was) ui::wifiKeyboard(lcd, kbLayer);
  else ui::wifiKeyFlash(lcd, touchX, touchY, kbLayer, false);
  ui::wifiPasswordField(lcd, pickPass.c_str());
  lcd.endWrite();
}

void factoryReset() {
  ui::message(lcd, "Erasing...", "Removing WiFi, watch link and settings.", nullptr);
  for (int i = 1; i >= 0; i--) {
    src::use(i);
    src::unlink();
  }
  prefs.clear();
  LittleFS.remove("/photo.jpg");
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

// Photo behind the miles: /photo.jpg (800x480, sized by the phone page) decoded once into PSRAM.
uint16_t* photo = nullptr;
int photoDraw(JPEGDRAW* p) {
  for (int y = 0; y < p->iHeight && p->y + y < 480; y++)
    for (int x = 0; x < p->iWidth && p->x + x < 800; x++) photo[(p->y + y) * 800 + p->x + x] = p->pPixels[y * p->iWidth + x];
  return 1;
}
void loadPhoto() {
  ui::setBackdrop(nullptr);
  File f = LittleFS.open("/photo.jpg", "r");
  if (!f) return;
  const size_t n = f.size();
  uint8_t* buf = (uint8_t*)ps_malloc(n);
  if (!photo) photo = (uint16_t*)ps_malloc(800 * 480 * 2);
  bool ok = buf && photo && f.read(buf, n) == n && jpeg.openRAM(buf, n, photoDraw);
  if (ok) {
    memset(photo, 0, 800 * 480 * 2);
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpeg.decode(0, 0, 0);
    jpeg.close();
  }
  Serial.printf("photo %s\n", ok ? "loaded" : "failed");
  if (ok) ui::setBackdrop(photo);
  free(buf);
  f.close();
}

void onNetUp() {
  netUp = true;
  wifiSaved = true;
  everConnected = true;
  sntp_set_time_sync_notification_cb([](timeval*) { clockchip::save(); });  // keep the clock chip right
  configTzTime(app::cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.nist.gov");
  MDNS.begin("nextmile");
  MDNS.addService("http", "tcp", 80);
  web::begin();
  dirty = true;
}

// LED celebrations: a new run, every 100 miles, reaching the goal, passing last year's total.
void checkEvents(int i, int prevRuns, float prevMi) {
  const acts::Totals& t = acts::totals();
  if (t.runs <= prevRuns) return;
  const float mi = t.meters / 1609.344f;
  UiData d = app::uiData(i);
  bool big = floorf(mi / 100) > floorf(prevMi / 100) || (d.goal_mi > 0 && prevMi < d.goal_mi && mi >= d.goal_mi) ||
             (d.last_year_mi > 0 && prevMi <= d.last_year_mi && mi > d.last_year_mi);
  Serial.printf("new miles: %d -> %d activities%s\n", prevRuns, (int)t.runs, big ? " (milestone)" : "");
  if (big) extras::milestone();
  else extras::newRun();
  prefs.putUInt("partyAt", (uint32_t)time(nullptr));  // so a restart doesn't cut the celebration short
}

// Light confetti over the main screen while new miles are being celebrated.
void confetti() {
  static bool on = false;
  const bool want = (shown == Screen::Main || shown == Screen::Together) && extras::celebrating() && blNow > 0;
  if (!want && !on) return;
  lcd.startWrite();
  if (want) ui::confettiStep(lcd, millis());
  else ui::confettiClear(lcd);
  lcd.endWrite();
  on = want;
}

void updatePace() {
  static uint32_t last = 0;
  if (last && millis() - last < 60000) return;
  last = millis();
  if (!timeValid()) return;
  time_t now = time(nullptr);
  static bool resumed = false;
  if (!resumed) {
    resumed = true;
    const uint32_t at = prefs.getUInt("partyAt", 0), age = (uint32_t)now - at;
    if (at && age < extras::NEWRUN_MS / 1000) extras::newRun(age * 1000);
  }
  struct tm tmv;
  localtime_r(&now, &tmv);
  int y = tmv.tm_year + 1900;
  int days = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  float frac = (tmv.tm_yday + (tmv.tm_hour * 60 + tmv.tm_min) / 1440.0f) / days;
  bool ahead = false;  // the light glows while anyone on screen is ahead of goal pace
  for (int i = 0; i < app::runners(); i++) {
    UiData d = app::uiData(i);
    ahead = ahead || (run[i].linked && d.goal_mi > 0 && d.ytd_m / 1609.344f >= d.goal_mi * frac);
  }
  extras::setAhead(ahead);
}


void drawLoading() {
  ui::message(lcd, "Loading your miles...", "Adding up this year so far.", "This can take a minute.");
  shown = Screen::None;
  loadingShown = true;
}

void maybeSync(int i) {
  Runner& R = run[i];
  if (!R.linked || R.needRelink || !netUp || !timeValid()) return;
  uint32_t now = millis();
  if ((int32_t)(R.backoffUntil - now) > 0) return;
  const int y = localYear();
  const acts::Totals& t = R.tot;
  const int prevYear = t.year, prevRuns = t.runs;
  const float prevMi = t.meters / 1609.344f;
  const bool hadData = R.syncedOnce || prevYear == y;  // this year's cache survives a power-off, so new miles found at power-up count
  if (R.bootSync) {  // a full re-read of the year at every power-up is slow, so skip it if one ran today
    R.bootSync = false;
    uint32_t at = prefs.getUInt(key("fullAt", i), 0);
    if (R.wantFull && t.year == y && at && (uint32_t)time(nullptr) - at < FULL_EVERY_MS / 1000) {
      R.wantFull = false;
      R.wantRecent = true;
      R.lastFull = now;
    }
  }
  bool full = R.wantFull || t.year != y || now - R.lastFull > FULL_EVERY_MS;
  bool recent = R.wantRecent || now - R.lastRecent > RECENT_EVERY_MS;
  bool lastYear = t.lastYear != y - 1;
  if (!full && !recent && !lastYear) return;
  src::use(i);

  const bool reread = R.reloadAll;  // also redo last year, which is otherwise read only once
  if ((t.year == 0 || reread) && shown != Screen::Info) drawLoading();
  R.reloadAll = false;
  src::Result r = src::OK;
  syncing = true;
  if (full || recent) r = src::sync(y, full, app::kindsOf(i));
  if (r == src::OK && (reread || acts::totals().lastYear != y - 1)) r = src::syncLastYear(y - 1, app::kindsOf(i));
  syncing = loadingShown = false;

  R.lastResult = r;
  if (r == src::OK) {
    if (full) {
      R.lastFull = now;
      prefs.putUInt(key("fullAt", i), (uint32_t)time(nullptr));
    }
    R.lastRecent = now;
    R.lastSyncAt = now;
    R.syncedOnce = true;
    R.wantFull = R.wantRecent = false;
    snap();
    if (hadData && prevYear == y) checkEvents(i, prevRuns, prevMi);
  } else if (r == src::AUTH) {
    R.needRelink = true;
  } else {
    R.backoffUntil = now + (r == src::RATE ? 60UL : 5UL) * 60 * 1000;
  }
  dirty = true;
}

void maybeSync() {
  for (int i = 0; i < app::runners(); i++) maybeSync(i);
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
  if (joining) return Screen::Joining;
  if (portalActive)
    return setupView == SetupView::Pass ? Screen::WifiPass : setupView == SetupView::Phone ? Screen::WifiPhone : Screen::WifiList;
  bool showData = anyData();
  if (showData && extras::night() && timeValid()) return Screen::Night;
  if (showData && overlayInfo && netUp) return Screen::Info;
  if (showData) return together && duoShown() ? Screen::Together : Screen::Main;
  if (!netUp) return (everConnected || millis() - bootMs > 30000) ? Screen::WifiRetry : Screen::Connecting;
  if (!timeValid()) return Screen::Clock;
  return Screen::Link;
}

void draw(Screen s) {
  String ip = app::ip();
  lcd.startWrite();
  ui::confettiReset();
  switch (s) {
    case Screen::Main:
      if (duoShown()) ui::duo(lcd, app::uiData(0), app::uiData(1));
      else ui::main(lcd, app::uiData(0));
      break;
    case Screen::Together: ui::together(lcd, app::uiData(0), app::uiData(1), app::cfg.sharedGoal); break;
    case Screen::Night: {
      UiData r[2] = {app::uiData(0), app::uiData(1)};
      time_t t = time(nullptr);
      tm l;
      localtime_r(&t, &l);
      char hm[8], date[40];
      snprintf(hm, sizeof hm, "%d:%02d", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min);
      strftime(date, sizeof date, "%A, %B %e", &l);
      for (char* p = date; *p; p++) *p = toupper(*p);
      ui::night(lcd, r, duoShown() ? 2 : 1, hm, l.tm_hour < 12 ? "AM" : "PM", date);
      break;
    }
    case Screen::Info: ui::info(lcd, ("http://" + ip + "/").c_str(), ip.c_str(), WiFi.SSID().c_str(), app::cfg.led == 2); break;
    case Screen::Link: ui::linkStrava(lcd, ("http://" + ip + "/link").c_str(), ip.c_str(), run[0].needRelink); break;
    case Screen::WifiRetry: ui::wifiRetry(lcd, WiFi.SSID().c_str()); break;
    case Screen::Connecting: ui::message(lcd, "Connecting...", "Joining your WiFi.", nullptr); break;
    case Screen::Clock: ui::message(lcd, "Connected!", "Setting the clock...", nullptr); break;
    case Screen::WifiList: ui::wifiList(lcd, nets.data(), nets.size(), netFirst, scanning, listErr.c_str()); break;
    case Screen::WifiPass: ui::wifiPassword(lcd, pickSsid.c_str(), pickPass.c_str(), kbLayer, passErr.c_str()); break;
    case Screen::WifiPhone: ui::wifiPhone(lcd, AP_NAME, wifiFailed); break;
    case Screen::Joining: ui::message(lcd, "Connecting...", ("Joining " + pickSsid + ".").c_str(), "This takes about 20 seconds."); break;
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
      if (!netUp && millis() - bootMs > 60000) {
        wantPortal = true;  // no WiFi for a while: let them pick a new network
      } else if (duoShown()) {  // two runners: tap to the combined miles, then to settings
        together = true;
        infoSince = millis();
      } else if (netUp) {
        overlayInfo = true;
        infoSince = millis();
      }
      break;
    case Screen::Together:
      together = false;
      if (netUp) {
        overlayInfo = true;
        infoSince = millis();
      }
      break;
    case Screen::Info:
      if (touchY >= ui::THEME_BTN_Y - 10 && touchY <= ui::THEME_BTN_Y + ui::THEME_BTN_H + 6) {
        if (touchX < 400) {
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
    case Screen::WifiList:
    case Screen::WifiPass:
    case Screen::WifiPhone: setupTap(); return;
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
      swallowTap = extras::screenOff() || extras::night();  // first touch in a dark room only wakes the screen
      extras::wake();
      extras::dismiss();
      applyBacklight();
    }
    if (shown == Screen::WifiPass && !swallowTap) trackKey();
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
  if (pressed && now - lastTouch > 80) {  // touch can drop out briefly mid-press; wait before calling it a release
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
  prefs.putBool("duo", cfg.duo);
  prefs.putString("name1", cfg.name2);
  prefs.putFloat("goal1", cfg.goalMi2);
  prefs.putUChar("kinds1", cfg.kinds2);
  prefs.putFloat("shared", cfg.sharedGoal);
  prefs.putBool("nightclk", cfg.nightClock);
  prefs.putUChar("ledfx", cfg.ledFx);
  prefs.putUChar("ledn", cfg.ledCount);
  applyBacklight();
  dirty = true;
}

void applyTz() {
  setenv("TZ", cfg.tz.c_str(), 1);
  tzset();
  for (Runner& r : run) r.wantFull = true, r.bootSync = false;
}

void requestSync(bool full) {
  Runner& r = run[src::current()];
  if (full) r.wantFull = r.reloadAll = true, r.bootSync = false;
  for (Runner& x : run) x.wantRecent = true, x.backoffUntil = 0;
  dirty = true;
}

void refresh() { dirty = true; }
void requestPortal() { wantPortal = true; }
void requestReset() { wantReset = true; }

void onLinked() {
  Runner& r = run[src::current()];
  r.bootSync = r.needRelink = false;
  r.wantFull = true;
  r.backoffUntil = 0;
  r.syncedOnce = false;
  snap();
  overlayInfo = false;
  dirty = true;
}

void onPushed() {
  Runner& r = run[src::current()];
  r.lastSyncAt = millis();
  r.syncedOnce = true;
  r.lastResult = src::OK;
  r.needRelink = false;
  snap();
  dirty = true;
}

float carryInMi() { return prefs.getFloat(key("carryMi", src::current()), 0); }

void setCarryInMi(float mi) {
  if (mi < 0 || mi > 20000) return;
  prefs.putFloat(key("carryMi", src::current()), mi);
  if (timeValid()) acts::setCarryIn(localYear(), mi * 1609.344f);
  dirty = true;
}

String ip() { return WiFi.localIP().toString(); }

void requestUpdate() { wantOta = true; }
void photoChanged() {
  loadPhoto();
  dirty = true;
}
String updateStatus() { return otaStatus; }

uint32_t lastSyncAgoS() {
  const Runner& r = run[src::current()];
  return r.syncedOnce ? (millis() - r.lastSyncAt) / 1000 : UINT32_MAX;
}

int runners() { return cfg.duo ? 2 : 1; }
String& nameOf(int i) { return i ? cfg.name2 : cfg.name; }
float& goalOf(int i) { return i ? cfg.goalMi2 : cfg.goalMi; }
uint8_t& kindsOf(int i) { return i ? cfg.kinds2 : cfg.kinds; }

UiData uiData() { return uiData(src::current()); }

UiData uiData(int i) {
  const Runner& R = run[i];
  const acts::Totals& t = R.tot;
  UiData d;
  String n = nameOf(i).length() ? nameOf(i) : R.athlete;
  strlcpy(d.athlete, n.c_str(), sizeof d.athlete);
  d.year = timeValid() ? localYear() : (t.year ? t.year : 2026);
  bool current = t.year == d.year;
  d.ytd_m = current ? t.meters : 0;
  d.ytd_runs = current ? t.runs : 0;
  if (current) d.last = t.last;
  d.kinds = kindsOf(i);
  d.last_year = d.year - 1;
  d.last_year_mi = t.lastYear == d.year - 1 ? t.lastYearMeters / 1609.344f : 0;
  if (goalOf(i) > 0) {
    d.goal_mi = goalOf(i);
    d.goal_auto = false;
  } else {
    // "Beat last year": the goal is exactly last year's miles.
    d.goal_mi = d.last_year_mi;
    d.goal_auto = true;
  }
  const extras::Weather& w = extras::weather();
  d.wx_valid = w.valid;
  d.wx_temp = w.temp;
  d.wx_rain = w.rain;
  d.wx_desc = extras::describe(w.code);
  const int32_t day = timeValid() ? localNow() / 86400 : R.statDay;
  if (day == R.statDay) {
    d.today_m = R.todayM, d.week_m = R.weekM, d.month_m = R.monthM;
  } else {  // a new day since this runner was last counted: start from zero where the period rolled over
    const bool newWeek = (day + 3) % 7 < (R.statDay + 3) % 7 || day - R.statDay >= 7;
    time_t tn = time(nullptr);
    tm l;
    localtime_r(&tn, &l);
    d.week_m = newWeek ? 0 : R.weekM;
    d.month_m = acts::daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, 1) > R.statDay ? 0 : R.monthM;
  }
  if (w.valid && timeValid()) {
    time_t tn = time(nullptr);
    for (int k = 0; k < w.ndays && k < 3; k++) {
      DayFc& f = d.fc[k];
      f.valid = true;
      f.hi = w.days[k].hi, f.lo = w.days[k].lo, f.rain = w.days[k].rain;
      f.desc = extras::describe(w.days[k].code);
      time_t td = tn + k * 86400;
      tm l;
      localtime_r(&td, &l);
      if (k == 0) strcpy(f.name, "TODAY");
      else strftime(f.name, sizeof f.name, "%a", &l);
      for (char* p = f.name; *p; p++) *p = toupper(*p);
    }
  }
  d.wifi_ok = netUp;
  d.sync_error = R.lastResult != src::OK || R.needRelink;
  d.source = R.kind == src::GOOGLE ? "FITBIT" : R.kind == src::APPLE ? "APPLE" : R.kind == src::POLAR ? "POLAR" : R.kind == src::GARMIN ? "GARMIN" : "STRAVA";
  return d;
}

}  // namespace app

void setup() {
  Serial.begin(115200);
  bootMs = millis();
  board::detect();
  Serial.printf("CrowPanel %s\n", board::versionName());
  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  lcd.fillRect(0, 0, 2, 1, TFT_RED);  // confetti needs to read the screen back
  Serial.printf("readback %04x (want f800)\n", (unsigned)lcd.readPixel(1, 0));
  lcd.fillRect(0, 0, 2, 1, TFT_BLACK);
  ui::init();

  ota::begin();
  net::setPump(syncPump);
  prefs.begin("app", false);
  loadSettings();
  extras::begin();
  clockchip::begin();  // time from the clock chip, so the miles and new-year reset don't wait for WiFi
  setenv("TZ", app::cfg.tz.c_str(), 1);
  tzset();
  if (!LittleFS.begin(false)) Serial.println("LittleFS mount failed; skipping intro");
  loadPhoto();
  src::begin();
  snap();
  src::use(1);
  snap();
  src::use(0);

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

  pollScan();
  if (joining) checkJoin();

  if (portalActive) {
    wm.process();
    if (!wm.getConfigPortalActive()) portalActive = false;
    if (wifiTrying) {  // a password was submitted on the phone and the join attempt just finished
      wifiTrying = false;
      if (portalActive && WiFi.status() != WL_CONNECTED) {
        wifiFailed = true;
        shown = Screen::None;
      }
    }
    if (WiFi.status() == WL_CONNECTED && !portalActive) dirty = true;
  }

  // Routers often refuse the first join right after a restart, and the ESP32 doesn't retry that on its own.
  if (!portalActive && wifiSaved && !joining) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) wifiTryAt = millis();  // after a drop, give the ESP32's own reconnect 20 s first
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
  static uint32_t snapAt = 0;
  if (millis() - snapAt > 500) {  // the settings page may have just linked or changed the current runner
    snapAt = millis();
    snap();
  }
  maybeSync();
  extras::setSetup(shown != Screen::Main && shown != Screen::Together && shown != Screen::Night && shown != Screen::Info &&
                   shown != Screen::None);
  extras::loop(netUp && !portalActive);
  updatePace();
  applyBacklight();
  maybeOta();

  static int shownMin = -1;  // night clock: redraw when the minute changes
  if (shown == Screen::Night && time(nullptr) / 60 != shownMin) shownMin = time(nullptr) / 60, dirty = true;

  if ((overlayInfo || together) && millis() - infoSince > INFO_TIMEOUT_MS) {
    overlayInfo = together = false;
    dirty = true;
  }

  if (Serial.available() && Serial.read() == 'C') extras::newRun();  // over USB: try out the new-miles celebration
  static uint32_t dbgAt = 0;
  if (millis() - dbgAt > 2000) {
    dbgAt = millis();
    Serial.printf("ldr=%d bl=%d pressed=%d screen=%d wifi=%d\n", extras::lightRaw(), blNow, pressed, (int)shown, (int)WiFi.status());
  }
  handleTouch();
  if (holdShown < 0) {
    Screen want = baseScreen();
    if (want != shown || (dirty && want != Screen::WifiList && want != Screen::WifiPass && want != Screen::WifiPhone)) draw(want);
    confetti();
  }
  delay(10);
}
