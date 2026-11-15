#include "board.h"

#include <Wire.h>
#include <driver/i2c.h>

namespace {

// V1.x backlight: one byte to the STC8 at 0x30. 0 = brightest, 244 = dimmest, 245 = off.
class Light_STC8 : public lgfx::ILight {
 public:
  bool init(uint8_t b) override { setBrightness(b); return true; }
  void setBrightness(uint8_t b) override {
    uint8_t v = b == 0 ? 245 : 244 - (uint32_t)(b - 1) * 244 / 254;
    board::i2cWrite(0x30, &v, 1);
  }
};

void rgbBus(lgfx::Bus_RGB::config_t& c) {
  c.hsync_polarity = 0;
  c.vsync_polarity = 0;
}

class LGFX_V1 : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  Light_STC8 _light;
  lgfx::Touch_GT911 _touch;

 public:
  LGFX_V1() {
    auto p = _panel.config();
    p.memory_width = p.panel_width = 800;
    p.memory_height = p.panel_height = 480;
    _panel.config(p);
    auto pd = _panel.config_detail();
    pd.use_psram = 1;
    _panel.config_detail(pd);
    auto c = _bus.config();
    c.panel = &_panel;
    const int8_t d[16] = {21, 47, 48, 45, 38, 9, 10, 11, 12, 13, 14, 7, 17, 18, 3, 46};  // B0-4 G0-5 R0-4
    for (int i = 0; i < 16; i++) (&c.pin_d0)[i] = d[i];
    c.pin_henable = 42;
    c.pin_vsync = 41;
    c.pin_hsync = 40;
    c.pin_pclk = 39;
    c.freq_write = 21000000;
    rgbBus(c);
    c.hsync_front_porch = 8;
    c.hsync_pulse_width = 4;
    c.hsync_back_porch = 8;
    c.vsync_front_porch = 8;
    c.vsync_pulse_width = 4;
    c.vsync_back_porch = 8;
    c.pclk_idle_high = 1;
    _bus.config(c);
    _panel.setBus(&_bus);
    _panel.setLight(&_light);
    auto t = _touch.config();
    t.x_min = 0, t.x_max = 799, t.y_min = 0, t.y_max = 479;
    t.pin_int = -1, t.pin_rst = -1;
    t.bus_shared = false;
    t.i2c_port = I2C_NUM_0;
    t.pin_sda = 15, t.pin_scl = 16;
    t.freq = 400000;
    t.i2c_addr = 0x5D;
    _touch.config(t);
    _panel.setTouch(&_touch);
    setPanel(&_panel);
  }
};

class LGFX_V3 : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;
  lgfx::Touch_GT911 _touch;

 public:
  LGFX_V3() {
    auto p = _panel.config();
    p.memory_width = p.panel_width = 800;
    p.memory_height = p.panel_height = 480;
    _panel.config(p);
    auto pd = _panel.config_detail();
    pd.use_psram = 1;
    _panel.config_detail(pd);
    auto c = _bus.config();
    c.panel = &_panel;
    const int8_t d[16] = {15, 7, 6, 5, 4, 9, 46, 3, 8, 16, 1, 14, 21, 47, 48, 45};
    for (int i = 0; i < 16; i++) (&c.pin_d0)[i] = d[i];
    c.pin_henable = 41;
    c.pin_vsync = 40;
    c.pin_hsync = 39;
    c.pin_pclk = 0;
    c.freq_write = 12000000;  // V3.0 runs the panel at 12 MHz
    rgbBus(c);
    c.hsync_front_porch = 40;
    c.hsync_pulse_width = 48;
    c.hsync_back_porch = 40;
    c.vsync_front_porch = 1;
    c.vsync_pulse_width = 31;
    c.vsync_back_porch = 13;
    c.pclk_active_neg = 1;
    c.de_idle_high = 0;
    c.pclk_idle_high = 0;
    _bus.config(c);
    _panel.setBus(&_bus);
    auto l = _light.config();
    l.pin_bl = 2;
    l.freq = 12000;
    l.pwm_channel = 7;
    _light.config(l);
    _panel.setLight(&_light);
    auto t = _touch.config();
    t.x_min = 0, t.x_max = 799, t.y_min = 0, t.y_max = 479;
    t.pin_int = -1, t.pin_rst = -1;
    t.bus_shared = false;
    t.i2c_port = I2C_NUM_1;
    t.pin_sda = 19, t.pin_scl = 20;
    t.freq = 400000;
    t.i2c_addr = 0x5D;
    _touch.config(t);
    _panel.setTouch(&_touch);
    setPanel(&_panel);
  }
};

board::Version ver = board::UNKNOWN;
lgfx::LGFX_Device* dev = nullptr;

bool probe(int sda, int scl, uint8_t addr) {
  Wire.begin(sda, scl, 100000);
  Wire.beginTransmission(addr);
  bool ok = Wire.endTransmission() == 0;
  Wire.end();
  return ok;
}

void pca(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

}  // namespace

namespace board {

Version detect() {
  if (dev) return ver;
  // V1.x: hold GT911 INT (GPIO1) low through power-up so it answers at 0x5D.
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);
  delay(120);
  pinMode(1, INPUT);
  if (probe(15, 16, 0x30)) {
    ver = V1;
    dev = new LGFX_V1();
  } else {
    ver = probe(19, 20, 0x18) ? V3 : UNKNOWN;
    if (ver == V3) {
      // Reset the GT911 through the PCA9557. IO2-IO7 stay high: they power other parts of the board.
      Wire.begin(19, 20);
      pca(0x03, 0x00);
      pca(0x01, 0xFC);
      delay(20);
      pca(0x01, 0xFD);
      delay(100);
      pca(0x03, 0x02);
      Wire.end();
    }
    dev = new LGFX_V3();  // best guess when nothing answered
  }
  return ver;
}

Version version() { return ver; }
const char* versionName() { return ver == V1 ? "V1.x" : ver == V3 ? "V3.0" : "unknown"; }
lgfx::LGFX_Device& lcd() { return *dev; }
int i2cPort() { return ver == V1 ? I2C_NUM_0 : I2C_NUM_1; }

bool i2cWrite(uint8_t addr, const uint8_t* d, size_t n) {
  return lgfx::i2c::transactionWrite(i2cPort(), addr, d, n, 100000).has_value();
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* d, size_t n) {
  return lgfx::i2c::transactionWriteRead(i2cPort(), addr, &reg, 1, d, n, 100000).has_value();
}

// V1.x: GPIO20 is free when the board's function switch is set away from the microphone.
// V3.0: not confirmed yet; set once the board is here.
int ledPin() { return ver == V1 ? 20 : -1; }

}  // namespace board
