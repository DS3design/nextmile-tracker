#pragma once
// Signed over-the-air updates from GitHub Pages. Unsigned or tampered images are never installed.
#include <Arduino.h>

namespace ota {
using Progress = void (*)(int percent);
void begin();             // call early in setup(): handles the "did the new version boot?" rollback guard
void markHealthy();       // call once the new version has run fine for a while
bool enabled();           // public key compiled in
// Checks the manifest; installs and restarts if a newer signed build exists. Returns a short status.
String check(Progress p);
int version();
}  // namespace ota
