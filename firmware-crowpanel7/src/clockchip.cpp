#include "clockchip.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "activities.h"
#include "board.h"

namespace clockchip {
namespace {
const uint8_t ADDR = 0x51;
bool have = false;
uint8_t bcd(int v) { return (v / 10) << 4 | v % 10; }
int dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
}  // namespace

bool begin() {
  uint8_t r[7];
  have = board::i2cRead(ADDR, 0x02, r, 7);
  Serial.printf("clock chip %s\n", have ? "found" : "not found");
  if (!have || (r[0] & 0x80)) return false;  // VL bit: the chip lost power, its time is meaningless
  const int y = 2000 + dec(r[6]), mo = dec(r[5] & 0x1F), d = dec(r[3] & 0x3F);
  const int h = dec(r[2] & 0x3F), mi = dec(r[1] & 0x7F), s = dec(r[0] & 0x7F);
  if (y < 2024 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  timeval tv = {(time_t)(acts::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s), 0};
  settimeofday(&tv, nullptr);
  return true;
}

void save() {
  if (!have) return;
  time_t t = time(nullptr);
  tm u;
  gmtime_r(&t, &u);
  uint8_t w[8] = {0x02, bcd(u.tm_sec), bcd(u.tm_min), bcd(u.tm_hour), bcd(u.tm_mday), (uint8_t)u.tm_wday,
                  bcd(u.tm_mon + 1), bcd(u.tm_year % 100)};
  board::i2cWrite(ADDR, w, sizeof w);
}

bool found() { return have; }

}  // namespace clockchip
