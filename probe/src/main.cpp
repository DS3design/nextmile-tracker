// Display-chip probe for the CYD. Tries both panels, prints PROBE results, halts.
#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>

template <typename PanelT>
class LGFXT : public lgfx::LGFX_Device {
  PanelT _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFXT() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 55000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

template <typename PanelT>
static bool tryPanel(const char* name) {
  LGFXT<PanelT> lcd;
  lcd.init();
  lcd.setRotation(0);
  lcd.fillRect(0, 0, 4, 2, 0xF800);  // red pixels
  uint32_t got = lcd.readPixel(1, 0) & 0xFFFF;
  bool ok = got == 0xF800 || got == 0x00F8;  // some clones byte-swap reads
  Serial.printf("PROBE %s readback=%04x ok=%d\n", name, (unsigned)got, ok ? 1 : 0);
  lcd.fillScreen(0);
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  bool ili = tryPanel<lgfx::Panel_ILI9341>("ILI9341");
  bool st  = tryPanel<lgfx::Panel_ST7789>("ST7789");
  if (ili && !st) Serial.println("RESULT=ILI9341");
  else if (st && !ili) Serial.println("RESULT=ST7789");
  else Serial.printf("RESULT=UNKNOWN ili=%d st=%d\n", ili ? 1 : 0, st ? 1 : 0);
}

void loop() { delay(1000); }
