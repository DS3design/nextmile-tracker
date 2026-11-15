#include "extras.h"

#include <ArduinoJson.h>
#include <math.h>

#include <Adafruit_NeoPixel.h>

#include "app.h"
#include "board.h"
#include "net.h"
#include "ui.h"

namespace {

// CrowPanel 7": TSL2561 light sensor in the Grove I2C socket, a short 5V addressable LED strip on board::ledPin().
// The reading is "darkness" so the thresholds read like the CYD's LDR: higher = darker.
const int STRIP_LEDS = 10;
const float STRIP_MAX = 0.35f;  // keeps 10 LEDs well inside what USB power can supply
// Untested thresholds; tune on the real board using the reading shown on the settings page.
const int DIM_ON = 950, DIM_OFF = 900, DARK_ON = 995, DARK_OFF = 985;
Adafruit_NeoPixel strip(STRIP_LEDS, -1, NEO_GRB + NEO_KHZ800);
uint8_t tslAddr = 0;
const uint32_t DARK_OFF_AFTER_MS = 2UL * 60 * 1000, WAKE_MS = 30UL * 1000;
const uint32_t WX_EVERY_MS = 30UL * 60 * 1000, WX_RETRY_MS = 10UL * 60 * 1000;
const uint32_t MILESTONE_MS = 60UL * 1000;

float lightAvg = 0;
bool lightInit = false, dim = false, dark = false;
uint32_t lastLight = 0, darkSince = 0, wakeUntil = 0;

uint32_t newRunAt = 0, milestoneAt = 0;
bool newRunOn = false, milestoneOn = false, ahead = false, setupMode = false;
uint32_t lastAccent = 0, previewUntil = 0;

uint32_t hsv(float h) {  // h 0..1, full saturation and value
  float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
  auto c = [](float v) { return (uint32_t)(255 * (v < 0 ? 0 : v > 1 ? 1 : v)); };
  return c(r) << 16 | c(g) << 8 | c(b);
}
uint32_t lastLed = 0;

extras::Weather wx;
uint32_t wxAt = 0;
bool wxTried = false, wantGeo = false;
String wxStatus;

void ledWrite(uint32_t rgb, float level) {
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  // Gamma so mid values stay dim, and green trimmed because this LED's green die is much brighter;
  // otherwise purples and pinks wash out to white.
  auto ch = [&](int v, float gain) { return (int)(255 * powf(v / 255.0f, 2.2f) * gain * level + 0.5f); };
  int r = ch((rgb >> 16) & 0xFF, 1.0f), g = ch((rgb >> 8) & 0xFF, 0.5f), b = ch(rgb & 0xFF, 1.0f);
  if (board::ledPin() < 0) return;
  r *= STRIP_MAX, g *= STRIP_MAX, b *= STRIP_MAX;
  for (int i = 0; i < STRIP_LEDS; i++) strip.setPixelColor(i, r, min(255, g * 2), b);  // strip green isn't trimmed
  strip.show();
}

// TSL2561 visible+IR channel, mapped so 0 = bright daylight and 1000 = pitch dark. -1 = no sensor.
int tslDarkness() {
  if (!tslAddr) return -1;
  uint8_t d[2];
  if (!board::i2cRead(tslAddr, 0xAC, d, 2)) return -1;
  int ch0 = d[0] | d[1] << 8;  // 402 ms, 1x gain: roughly 0 in the dark to 65535 in sunlight
  float v = 1000 - 250 * log10f(1 + ch0);
  return v < 0 ? 0 : (int)v;
}

void readLight() {
  uint32_t now = millis();
  if (now - lastLight < 250) return;
  lastLight = now;
  int raw = tslDarkness();
  if (raw < 0) return;  // no sensor plugged in: never dim
  lightAvg = lightInit ? lightAvg * 0.9f + raw * 0.1f : raw;
  lightInit = true;
  if (!dim && lightAvg > DIM_ON) dim = true;
  if (dim && lightAvg < DIM_OFF) dim = false;
  bool wasDark = dark;
  if (!dark && lightAvg > DARK_ON) dark = true;
  if (dark && lightAvg < DARK_OFF) dark = false;
  if (dark && !wasDark) darkSince = now;
}

void ledLoop() {
  uint32_t now = millis();
  if (now - lastLed < 20) return;
  lastLed = now;
  const uint8_t mode = app::cfg.led;  // 0 off, 1 glow after new miles, 2 always on
  if (milestoneOn && now - milestoneAt > MILESTONE_MS) milestoneOn = false;
  if (newRunOn && now - newRunAt > extras::NEWRUN_MS) newRunOn = false;
  const uint32_t col = ui::accent();
  if (col != lastAccent) {
    if (lastAccent) previewUntil = now + 5000;  // show the new theme's colour
    lastAccent = col;
  }
  if ((int32_t)(previewUntil - now) > 0) return ledWrite(col, 1.0f);
  if (setupMode) return ledWrite(hsv((now % 12000) / 12000.0f), 1.0f);
  if (mode == 0) return ledWrite(0, 0);
  if (milestoneOn) return ledWrite(col, (now / 150) % 2 ? 1.0f : 0.05f);
  if (newRunOn && mode == 2) return ledWrite(hsv((now % 12000) / 12000.0f), 1.0f);  // always-on lights celebrate with the rainbow
  if (newRunOn) return ledWrite(col, 0.2f + 0.8f * (0.5f - 0.5f * cosf(now * 2 * PI / 2500.0f)));
  if (mode == 2) return ledWrite(col, ahead ? 1.0f : 0.6f);
  ledWrite(0, 0);
}

bool geocode() {
  String q = app::cfg.place;
  q.trim();
  if (q.length() < 2) {
    app::cfg.lat = app::cfg.lon = 0;
    app::cfg.placeName = "";
    return true;
  }
  JsonDocument doc, filter;
  JsonObject f = filter["results"][0].to<JsonObject>();
  f["name"] = f["admin1"] = f["country_code"] = f["latitude"] = f["longitude"] = true;
  net::Req r;
  r.url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" + net::urlEncode(q);
  bool digits = q.length() == 5 && strspn(q.c_str(), "0123456789") == 5;
  if (digits) r.url += "&countryCode=US";
  r.filter = &filter;
  int code = net::call(r, doc);
  if (code != 200) {
    wxStatus = "Couldn't look up that place (" + String(code) + ")";
    return false;
  }
  JsonObject res = doc["results"][0];
  if (res.isNull()) {
    wxStatus = "Place not found: " + q;
    app::cfg.lat = app::cfg.lon = 0;
    app::cfg.placeName = "";
    app::saveSettings();
    return true;
  }
  app::cfg.lat = res["latitude"] | 0.0f;
  app::cfg.lon = res["longitude"] | 0.0f;
  String n = res["name"] | "";
  const char* a = res["admin1"] | "";
  if (*a) n += ", " + String(a);
  app::cfg.placeName = n;
  app::saveSettings();
  return true;
}

bool fetchWeather() {
  JsonDocument doc, filter;
  filter["current"]["temperature_2m"] = filter["current"]["weather_code"] = filter["current"]["wind_speed_10m"] = true;
  filter["daily"]["temperature_2m_max"] = filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  net::Req r;
  char url[320];
  snprintf(url, sizeof url,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,wind_speed_10m"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=1",
           app::cfg.lat, app::cfg.lon);
  r.url = url;
  r.filter = &filter;
  int code = net::call(r, doc);
  if (code != 200 || doc["current"].isNull()) {
    wxStatus = "Weather unavailable (" + String(code) + ")";
    return false;
  }
  wx.temp = lroundf(doc["current"]["temperature_2m"] | 0.0f);
  wx.code = doc["current"]["weather_code"] | 0;
  wx.wind = lroundf(doc["current"]["wind_speed_10m"] | 0.0f);
  wx.hi = lroundf(doc["daily"]["temperature_2m_max"][0] | 0.0f);
  wx.lo = lroundf(doc["daily"]["temperature_2m_min"][0] | 0.0f);
  wx.rain = doc["daily"]["precipitation_probability_max"][0] | 0;
  wx.valid = true;
  wxStatus = "";
  return true;
}

void weatherLoop(bool netUp) {
  if (!netUp) return;
  uint32_t now = millis();
  if (wantGeo) {
    wantGeo = false;
    wx.valid = false;
    wxTried = false;
    if (!geocode()) wantGeo = true, wxTried = true, wxAt = now;
  }
  if (app::cfg.lat == 0 && app::cfg.lon == 0) {
    wx.valid = false;
    return;
  }
  uint32_t every = wx.valid ? WX_EVERY_MS : WX_RETRY_MS;
  if (wxTried && now - wxAt < every) return;
  wxTried = true;
  wxAt = now;
  fetchWeather();
  app::refresh();
}

}  // namespace

namespace extras {

void begin() {
  for (uint8_t a : {0x39, 0x29, 0x49}) {  // TSL2561 address depends on its ADDR pin
    uint8_t on[2] = {0x80, 0x03}, timing[2] = {0x81, 0x02};
    if (board::i2cWrite(a, on, 2) && board::i2cWrite(a, timing, 2)) {
      tslAddr = a;
      break;
    }
  }
  Serial.printf("light sensor %s\n", tslAddr ? "found" : "not found");
  if (board::ledPin() >= 0) {
    strip.setPin(board::ledPin());
    strip.begin();
  }
  ledWrite(0, 0);
}

void loop(bool netUp) {
  readLight();
  ledLoop();
  weatherLoop(netUp);
}

bool screenOff() {
  uint32_t now = millis();
  return app::cfg.autoDim && dark && now - darkSince > DARK_OFF_AFTER_MS && (int32_t)(wakeUntil - now) <= 0;
}

uint8_t backlight() {
  const uint8_t b = app::cfg.bright;
  if (!app::cfg.autoDim) return b;
  if (screenOff()) return 0;
  if (dim || dark) return max(8, b / 6);
  return b;
}

void wake() { wakeUntil = millis() + WAKE_MS; }
int lightRaw() { return (int)lightAvg; }

void newRun(uint32_t ageMs) {
  newRunOn = true;
  newRunAt = millis() - ageMs;
}

bool celebrating() { return newRunOn && millis() - newRunAt < NEWRUN_MS; }

void milestone() {
  milestoneOn = true;
  milestoneAt = millis();
  newRun();
}

void dismiss() { milestoneOn = false; }
void setAhead(bool a) { ahead = a; }
void setSetup(bool on) { setupMode = on; }

const Weather& weather() { return wx; }
void locationChanged() { wantGeo = true; }
String weatherStatus() { return wxStatus; }

const char* describe(int c) {
  if (c == 0 || c == 1) return "CLEAR";
  if (c == 2) return "PT CLOUDY";
  if (c == 3) return "CLOUDY";
  if (c == 45 || c == 48) return "FOG";
  if (c >= 51 && c <= 57) return "DRIZZLE";
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "RAIN";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "SNOW";
  if (c >= 95) return "STORMS";
  return "";
}

}  // namespace extras
