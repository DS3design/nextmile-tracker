#pragma once
// State shared between the main loop and the settings web server.
#include <Arduino.h>
#include "ui.h"

#define AP_NAME "NextMile-Tracker"
#define DEFAULT_TZ "PST8PDT,M3.2.0,M11.1.0"

struct Settings {
  String name;          // display name override; empty = Strava name
  String tz = DEFAULT_TZ;
  bool tzManual = false;  // picked on the settings page, so linking won't overwrite it
  float goalMi = 0;       // 0 = beat last year
  uint8_t bright = 200;
  bool walks = false;     // count walks and hikes too
};

namespace app {
extern Settings cfg;
void saveSettings();
void applyTz();
void requestSync(bool full);
void requestPortal();
void requestReset();
void onLinked();
UiData uiData();
String ip();
uint32_t lastSyncAgoS();  // UINT32_MAX = never
}
