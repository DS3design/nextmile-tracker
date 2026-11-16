#include "ledfx.h"

#include <math.h>
#include <stdlib.h>

namespace ledfx {
namespace {

const float TAU = 6.2831853f;

Px rgb(uint32_t c, float k = 1) { return {((c >> 16) & 0xFF) / 255.0f * k, ((c >> 8) & 0xFF) / 255.0f * k, (c & 0xFF) / 255.0f * k}; }
Px hue(float h, float k = 1) {
  h -= floorf(h);
  auto f = [](float v) { return v < 0 ? 0 : v > 1 ? 1 : v; };
  return {f(fabsf(h * 6 - 3) - 1) * k, f(2 - fabsf(h * 6 - 2)) * k, f(2 - fabsf(h * 6 - 4)) * k};
}
float rnd() { return (rand() & 0xFFFF) / 65535.0f; }

// Trails and sparks fade from frame to frame, so each effect only adds light.
Px glow[MAX_LEDS];
uint32_t lastMs = 0;

void fadeAll(int n, float dt, float perSecond) {
  const float k = powf(perSecond, dt);
  for (int i = 0; i < n; i++) glow[i].r *= k, glow[i].g *= k, glow[i].b *= k;
}
void add(int n, float pos, Px c) {  // light at a fractional position, split between the two nearest LEDs
  int i = (int)floorf(pos);
  float f = pos - i;
  auto put = [&](int j, float w) {
    if (j < 0 || j >= n) return;
    glow[j].r += c.r * w, glow[j].g += c.g * w, glow[j].b += c.b * w;
  };
  put(i, 1 - f);
  put(i + 1, f);
}

// 1D fireworks: a rocket climbs the strip, bursts, and sparks fly both ways and fade.
struct Spark { float pos, vel, life; Px c; bool rocket; float burstAt; };
Spark sparks[24];
float nextLaunch = 0;

void fireworks(int n, float t, float dt, bool busy) {
  fadeAll(n, dt, 0.02f);
  if (t >= nextLaunch) {
    for (Spark& s : sparks)
      if (s.life <= 0) {
        s = {0, n * (0.9f + 0.5f * rnd()), 1, {1, 0.8f, 0.5f}, true, n * (0.45f + 0.45f * rnd())};
        break;
      }
    nextLaunch = t + (busy ? 0.4f + 0.6f * rnd() : 1.2f + 2.5f * rnd());
  }
  for (Spark& s : sparks) {
    if (s.life <= 0) continue;
    if (s.rocket) {
      s.pos += s.vel * dt;
      add(n, s.pos, {0.6f, 0.45f, 0.25f});
      if (s.pos < s.burstAt) continue;
      s.life = 0;  // burst: one colour per shell
      const Px c = hue(rnd());
      const int count = n < 12 ? 4 : 8;
      for (int k = 0, made = 0; k < 24 && made < count; k++) {
        Spark& p = sparks[k];
        if (p.life > 0) continue;
        p = {s.pos, (made % 2 ? 1 : -1) * n * (0.15f + 0.35f * rnd()), 1, c, false, 0};
        made++;
      }
      add(n, s.pos, {1.5f, 1.5f, 1.5f});
      continue;
    }
    s.pos += s.vel * dt;
    s.vel *= powf(0.15f, dt);
    s.life -= dt / (0.9f + 0.4f * rnd());
    if (s.life > 0) add(n, s.pos, {s.c.r * s.life, s.c.g * s.life, s.c.b * s.life});
  }
}

void comet(int n, float t, float dt, uint32_t theme) {
  fadeAll(n, dt, 0.002f);
  const float lap = 2.2f;
  float p = fmodf(t, lap * 2) / lap;  // there and back
  add(n, (p < 1 ? p : 2 - p) * (n - 1), rgb(theme));
}

void twinkle(int n, float dt, uint32_t theme) {
  fadeAll(n, dt, 0.15f);
  const float perLedPerSec = 0.6f;
  for (int i = 0; i < n; i++)
    if (rnd() < perLedPerSec * dt) {
      Px c = rnd() < 0.5f ? rgb(theme) : hue(rnd());
      glow[i].r += c.r, glow[i].g += c.g, glow[i].b += c.b;
    }
}

}  // namespace

const char* name(int e) {
  static const char* const N[] = {"Gentle pulse", "Rainbow", "Twinkle", "Comet", "Fireworks", "A different one every 10 minutes"};
  return N[e >= 0 && e < EFFECT_N ? e : 0];
}

void reset() {
  for (Px& p : glow) p = {0, 0, 0};
  for (Spark& s : sparks) s.life = 0;
  nextLaunch = 0;
  lastMs = 0;
}

void step(Px* out, int n, uint32_t ms, int effect, uint32_t theme, bool busy) {
  if (n > MAX_LEDS) n = MAX_LEDS;
  float dt = lastMs ? (ms - lastMs) / 1000.0f : 0.02f;
  if (dt > 0.2f) dt = 0.2f;
  lastMs = ms;
  const float t = ms / 1000.0f;
  if (effect == MIX) effect = (ms / 600000) % 5;  // 10 minutes each
  if (nextLaunch > t + 10) nextLaunch = 0;  // clock jumped back
  switch (effect) {
    case RAINBOW:
      for (int i = 0; i < n; i++) out[i] = hue(t / 8 + (float)i / n);
      return;
    case PULSE: {
      const Px c = rgb(theme, 0.2f + 0.8f * (0.5f - 0.5f * cosf(t * TAU / 2.5f)));
      for (int i = 0; i < n; i++) out[i] = c;
      return;
    }
    case TWINKLE: twinkle(n, dt, theme); break;
    case COMET: comet(n, t, dt, theme); break;
    default: fireworks(n, t, dt, busy); break;
  }
  for (int i = 0; i < n; i++)
    out[i] = {glow[i].r > 1 ? 1 : glow[i].r, glow[i].g > 1 ? 1 : glow[i].g, glow[i].b > 1 ? 1 : glow[i].b};
}

}  // namespace ledfx
