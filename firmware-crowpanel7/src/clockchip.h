#pragma once
// The board's battery-backed clock chip (BM8563 / PCF8563-compatible, I2C 0x51). Keeps UTC.
namespace clockchip {
bool begin();  // found and holding a valid time: sets the system clock from it
void save();   // write the system clock (call after an internet time sync)
bool found();
}
