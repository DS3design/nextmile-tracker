#include "ble_setup.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_bt.h>

#include <algorithm>
#include <vector>

namespace blesetup {
namespace {
// Keep in sync with docs/setup/index.html.
const char* SVC = "6e4d0001-7e1a-4c1b-9a3e-6e6578744d31";
const char* NETS = "6e4d0002-7e1a-4c1b-9a3e-6e6578744d31";
const char* STATE = "6e4d0003-7e1a-4c1b-9a3e-6e6578744d31";
const char* CMD = "6e4d0004-7e1a-4c1b-9a3e-6e6578744d31";

bool released = false, running = false, scanning = false, wantScan = false, hasCreds = false;
NimBLECharacteristic *cNets = nullptr, *cState = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
char inbox[200];
volatile bool hasInbox = false;
String credS, credP;

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    portENTER_CRITICAL(&mux);
    size_t n = std::min(v.size(), sizeof inbox - 1);
    memcpy(inbox, v.data(), n);
    inbox[n] = 0;
    hasInbox = true;
    portEXIT_CRITICAL(&mux);
  }
};

void publishNets(int n) {
  struct Net { String s; int rssi; bool open; };
  std::vector<Net> nets;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    auto it = std::find_if(nets.begin(), nets.end(), [&](const Net& x) { return x.s == s; });
    if (it == nets.end()) nets.push_back({s, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
    else it->rssi = std::max(it->rssi, (int)WiFi.RSSI(i));
  }
  std::sort(nets.begin(), nets.end(), [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  String out = "[]";
  for (auto& x : nets) {
    JsonArray e = a.add<JsonArray>();
    e.add(x.s);
    e.add(x.rssi);
    e.add(x.open ? 1 : 0);
    String t;
    serializeJson(d, t);
    if (t.length() > 500) break;
    out = t;
  }
  cNets->setValue(out.c_str());
}
}  // namespace

bool begin() {
  if (released) return false;
  if (running) return true;
  NimBLEDevice::init("NextMile Tracker");
  NimBLEDevice::setMTU(247);
  NimBLEServer* srv = NimBLEDevice::createServer();
  NimBLEService* svc = srv->createService(SVC);
  cNets = svc->createCharacteristic(NETS, NIMBLE_PROPERTY::READ, 512);
  cState = svc->createCharacteristic(STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* cmd = svc->createCharacteristic(CMD, NIMBLE_PROPERTY::WRITE, 200);
  cmd->setCallbacks(new CmdCb);
  cNets->setValue("[]");
  cState->setValue("ready");
  svc->start();
  NimBLEDevice::getAdvertising()->start();
  running = true;
  Serial.println("ble setup on");
  return true;
}

void setState(const char* s) {
  if (!running) return;
  Serial.printf("ble state %s\n", s);
  cState->setValue(s);
  cState->notify();
}

void loop() {
  if (!running) return;
  char buf[sizeof inbox];
  bool got = false;
  portENTER_CRITICAL(&mux);
  if (hasInbox) {
    memcpy(buf, inbox, sizeof buf);
    hasInbox = false;
    got = true;
  }
  portEXIT_CRITICAL(&mux);
  if (got) {
    if (!strcmp(buf, "scan")) {
      wantScan = true;
    } else {
      JsonDocument d;
      if (!deserializeJson(d, buf)) {
        credS = d["s"] | "";
        credP = d["p"] | "";
        hasCreds = credS.length() > 0;
      }
    }
  }
  if (wantScan && !scanning) {
    wantScan = false;
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true) != WIFI_SCAN_FAILED) {
      scanning = true;
      setState("scanning");
    }
  }
  if (scanning) {
    const int n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      scanning = false;
      publishNets(n);
      WiFi.scanDelete();
      setState("nets");
    }
  }
}

bool takeCreds(String& ssid, String& pass) {
  if (!hasCreds) return false;
  hasCreds = false;
  ssid = credS;
  pass = credP;
  credP = "";
  return true;
}

void end() {
  if (!running) return;
  running = false;
  NimBLEDevice::deinit(true);
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  released = true;
  Serial.println("ble setup off");
}

void releaseUnused() {
  if (running || released) return;
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  released = true;
}
}  // namespace blesetup
