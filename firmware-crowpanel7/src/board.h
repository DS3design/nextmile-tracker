#pragma once
// Elecrow CrowPanel Advance 7.0" (ESP32-S3, 800x480 RGB panel, GT911 touch).
// Elecrow changed the wiring between board versions, so the version is detected at startup:
//   V1.x (V1.3-V1.5): touch + Grove I2C on GPIO15/16, backlight set over I2C by an STC8 chip at 0x30.
//   V3.0: touch + Grove I2C on GPIO19/20 (touch reset through a PCA9557 at 0x18), backlight PWM on GPIO2.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

namespace board {
enum Version { UNKNOWN, V1, V3 };
Version detect();             // call once, before lcd().init()
Version version();
const char* versionName();
lgfx::LGFX_Device& lcd();
int i2cPort();                // the touch I2C bus, also wired to the Grove socket
bool i2cWrite(uint8_t addr, const uint8_t* d, size_t n);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* d, size_t n);
int ledPin();                 // data pin for the addressable LED strip, -1 = none
}  // namespace board
