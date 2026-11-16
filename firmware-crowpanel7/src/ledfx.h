#pragma once
// LED strip effects, drawn into an RGB buffer. Plain C++ so the desktop simulator can preview them.
#include <stdint.h>

namespace ledfx {

enum Effect : uint8_t { PULSE = 0, RAINBOW, TWINKLE, COMET, FIREWORKS, MIX, EFFECT_N };
const char* name(int e);

constexpr int MAX_LEDS = 60;
struct Px { float r, g, b; };

// One frame at time `ms` for `n` LEDs. `theme` is 0xRRGGBB. `busy` speeds fireworks up (milestones).
void step(Px* out, int n, uint32_t ms, int effect, uint32_t theme, bool busy = false);
void reset();  // forget sparks and trails, e.g. when the effect changes

}  // namespace ledfx
