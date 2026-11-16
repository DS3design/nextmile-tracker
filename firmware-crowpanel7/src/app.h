#pragma once
// State shared between the main loop and the settings web server.
#include <Arduino.h>
#include "ui.h"

#define AP_NAME "NextMile-Setup"
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
  bool duo = false;       // two-runner mode: a second person with their own link, name, goal and what counts
  String name2;
  float goalMi2 = 0;
  uint8_t kinds2 = 1;
  float sharedGoal = 0;   // two-runner mode: miles to reach together; 0 = both goals added up
  bool nightClock = true; // dark room: big dim clock instead of turning the screen off
  uint8_t ledFx = 5;      // ledfx::Effect after new miles' opening fireworks
  uint8_t ledCount = 10;  // LEDs on the strip
};

namespace app {
extern Settings cfg;
void saveSettings();
void applyTz();
void requestSync(bool full, int runner = -1);
void refresh();  // redraw the current screen
void requestPortal();
void requestReset();
void onLinked();
void onPushed();
void requestUpdate();
String updateStatus();
float carryInMi();
void setCarryInMi(float mi);
void photoChanged();  // /photo.jpg was uploaded or removed
UiData uiData();  // the runner the settings page is on
UiData uiData(int runner);
int runners();  // 1, or 2 in two-runner mode
String& nameOf(int runner);
float& goalOf(int runner);
uint8_t& kindsOf(int runner);
String ip();
uint32_t lastSyncAgoS();  // UINT32_MAX = never
}
