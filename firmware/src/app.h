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
  uint8_t kinds = 1;      // what counts: 1 runs, 2 walks and hikes, 3 both
  uint8_t theme = 0;      // ui colour theme index
  bool autoDim = true;    // dim with the light sensor, off in a dark room
  uint8_t led = 1;        // 0 off, 1 new runs + milestones, 2 also glow when ahead of goal pace
  String place;           // weather location as typed (ZIP or city)
  String placeName;       // resolved name, e.g. "Orangevale, California"
  float lat = 0, lon = 0;
};

namespace app {
extern Settings cfg;
void saveSettings();
void applyTz();
void requestSync(bool full);
void refresh();  // redraw the current screen
void requestPortal();
void requestReset();
void onLinked();
void onPushed();
void requestUpdate();
String updateStatus();
float carryInMi();
void setCarryInMi(float mi);
UiData uiData();
String ip();
uint32_t lastSyncAgoS();  // UINT32_MAX = never
}
