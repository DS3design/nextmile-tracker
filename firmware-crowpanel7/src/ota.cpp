#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "certs_gen.h"
#include "ota_pubkey.h"

#ifndef FW_VERSION
#define FW_VERSION 1
#endif
#ifndef FW_ENV
#define FW_ENV "cyd_ili9341"
#endif

namespace {

const char* MANIFEST = "https://ds3design.github.io/nextmile-tracker/fw/manifest.json";
const int MAX_TRIES = 3;
Preferences prefs;

bool fromHex(const char* s, uint8_t* out, size_t n) {
  if (strlen(s) != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    char b[3] = {s[2 * i], s[2 * i + 1], 0};
    out[i] = strtoul(b, nullptr, 16);
  }
  return true;
}

bool verifySig(const uint8_t* hash, const char* sigB64) {
  uint8_t sig[160];
  size_t n = 0;
  if (mbedtls_base64_decode(sig, sizeof sig, &n, (const uint8_t*)sigB64, strlen(sigB64)) != 0) return false;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)OTA_PUBKEY_PEM, strlen(OTA_PUBKEY_PEM) + 1) == 0 &&
            mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, n) == 0;
  mbedtls_pk_free(&pk);
  return ok;
}

}  // namespace

namespace ota {

int version() { return FW_VERSION; }
bool enabled() { return strlen(OTA_PUBKEY_PEM) > 0; }

// The stock Arduino bootloader can't roll back by itself, so count boots of a fresh update and
// switch back to the previous slot if it never reaches markHealthy().
void begin() {
  prefs.begin("ota", false);
  if (!prefs.getBool("pending", false)) return;
  int tries = prefs.getInt("tries", 0) + 1;
  prefs.putInt("tries", tries);
  if (tries <= MAX_TRIES) return;
  prefs.putBool("pending", false);
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other && esp_ota_set_boot_partition(other) == ESP_OK) {
    prefs.putInt("bad", FW_VERSION);
    ESP.restart();
  }
}

void markHealthy() {
  if (prefs.getBool("pending", false)) {
    prefs.putBool("pending", false);
    prefs.putInt("tries", 0);
  }
}

String check(Progress progress) {
  if (!enabled()) return "Updates not set up";
  WiFiClientSecure client;
  client.setCACert(ROOT_CAS);
  client.setTimeout(20);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(20000);
  if (!http.begin(client, MANIFEST)) return "No connection";
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "Update server unreachable (" + String(code) + ")";
  }
  JsonDocument m;
  DeserializationError e = deserializeJson(m, http.getStream());
  http.end();
  if (e) return "Bad update info";
  int ver = m["version"] | 0;
  JsonObject b = m["builds"][FW_ENV];
  if (ver <= FW_VERSION || b.isNull()) return "Up to date (v" + String(FW_VERSION) + ")";
  if (ver == prefs.getInt("bad", 0)) return "v" + String(ver) + " failed before, skipped";
  String url = b["url"] | "";
  size_t size = b["size"] | 0;
  uint8_t want[32];
  if (!url.length() || !size || !fromHex(b["sha256"] | "", want, 32)) return "Bad update info";
  String sig = b["sig"] | "";

  if (!http.begin(client, url)) return "No connection";
  code = http.GET();
  if (code != 200 || (size_t)http.getSize() != size) {
    http.end();
    return "Download failed (" + String(code) + ")";
  }
  if (!Update.begin(size, U_FLASH)) {
    http.end();
    return "Not enough room for update";
  }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  WiFiClient* s = http.getStreamPtr();
  static uint8_t buf[2048];
  size_t got = 0;
  int lastPct = -1;
  uint32_t idle = millis();
  while (got < size && millis() - idle < 20000) {
    size_t n = s->available();
    if (!n) {
      delay(2);
      continue;
    }
    n = s->readBytes(buf, min(n, sizeof buf));
    mbedtls_sha256_update_ret(&sha, buf, n);
    if (Update.write(buf, n) != n) break;
    got += n;
    idle = millis();
    int pct = got * 100 / size;
    if (progress && pct != lastPct) progress(lastPct = pct);
  }
  http.end();
  uint8_t hash[32];
  mbedtls_sha256_finish_ret(&sha, hash);
  mbedtls_sha256_free(&sha);
  if (got != size) {
    Update.abort();
    return "Download interrupted";
  }
  if (memcmp(hash, want, 32) != 0 || !verifySig(hash, sig.c_str())) {
    Update.abort();
    return "Update rejected: bad signature";
  }
  if (!Update.end(true)) return "Install failed";
  prefs.putBool("pending", true);
  prefs.putInt("tries", 0);
  delay(300);
  ESP.restart();
  return "Restarting";
}

}  // namespace ota
