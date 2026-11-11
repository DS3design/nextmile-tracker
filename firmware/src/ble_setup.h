#pragma once
#include <Arduino.h>

// WiFi setup over Bluetooth, used by Android phones from the setup web page (docs/setup/).
namespace blesetup {
bool begin();          // false if Bluetooth memory was already handed back this boot
void loop();
void end();            // stops Bluetooth for good (until restart) and frees its memory
void releaseUnused();  // call at boot when setup isn't needed, to free Bluetooth's memory
bool takeCreds(String& ssid, String& pass);
void setState(const char* s);
}  // namespace blesetup
