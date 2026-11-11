#pragma once
// The watch brand / service the tracker reads runs from. One is linked at a time.
#include <Arduino.h>

namespace src {

enum Kind : uint8_t { NONE = 0, STRAVA, GARMIN, POLAR, GOOGLE, APPLE, COROS, INTERVALS, WHOOP, OURA, WITHINGS, WAHOO };
enum Result { OK, NET, AUTH, RATE, SCOPE, CONFIG };

void begin();
Kind kind();
const char* label(Kind k);       // "Garmin"
bool available(Kind k);          // compiled-in keys present (OAuth sources)
bool linked();
bool hasHistory();               // can read runs from before linking (false for Polar/Apple)
String athleteName();

// OAuth sources: URL to send the phone to. state carries the tracker IP back via the GitHub link page.
String authorizeUrl(Kind k, const String& state);
Result exchange(Kind k, const String& code, const String& scope);
// Garmin: personal API key pasted on the setup page.
Result linkGarmin(const String& apiKey);
// intervals.icu: API key from Settings > Developer Settings.
Result linkIntervals(const String& apiKey);
// Apple: the iPhone Shortcut posts a JSON list of workouts. Token guards the endpoint.
String appleToken();
Result applePush(const String& token, const String& json);
void linkApple();

Result sync(int year, bool full, uint8_t kinds);
Result syncLastYear(int year, uint8_t kinds);
void unlink();
const char* describe(Result r);

}  // namespace src
