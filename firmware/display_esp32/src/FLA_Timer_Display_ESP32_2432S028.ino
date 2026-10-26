// ESP32-2432S028 Touch Diagnostic
// Upload this first.
// Touching the screen draws a red dot and prints coordinates.

#include <LovyanGFX.hpp>

class LGFX_CYD : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX_CYD() {
    auto b = _bus.config();
    b.spi_host = SPI2_HOST;
    b.pin_sclk = 14;
    b.pin_mosi = 13;
    b.pin_miso = 12;
    b.pin_dc   = 2;
    _bus.config(b);
    _panel.setBus(&_bus);

    auto p = _panel.config();
    p.pin_cs = 15;
    _panel.config(p);

    auto l = _light.config();
    l.pin_bl = 21;
    _light.config(l);
    _panel.setLight(&_light);

    auto t = _touch.config();
    t.x_min = 200;
    t.x_max = 3900;
    t.y_min = 200;
    t.y_max = 3900;
    t.pin_int = 36;
    t.offset_rotation = 0;
    t.spi_host = SPI3_HOST;
    t.freq = 1000000;
    t.pin_sclk = 25;
    t.pin_mosi = 32;
    t.pin_miso = 39;
    t.pin_cs = 33;
    t.bus_shared = true;
    _touch.config(t);
    _panel.setTouch(&_touch);

    setPanel(&_panel);
  }
};

LGFX_CYD lcd;

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.setRotation(1);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("Touch Diagnostic",10,10);
}

void loop() {
  uint16_t x,y;

  if (lcd.getTouch(&x,&y)) {
    Serial.printf("TOUCH x=%u y=%u\n", x, y);
    lcd.fillCircle(x, y, 5, TFT_RED);
  }

  delay(10);
}
