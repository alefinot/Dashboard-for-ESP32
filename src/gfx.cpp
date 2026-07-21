#include "dashboard.h"

// ----------------------------------------------------------------------------
// Text bounds helper (custom glyph metrics for the loaded GFXfonts)
// ----------------------------------------------------------------------------
inline void calculateTextBounds(const GFXfont *font, const char *str, int16_t x,
                                int16_t y, int16_t *x1, int16_t *y1,
                                uint16_t *w, uint16_t *h) {
  if (!font || !str || !*str) {
    *x1 = 0;
    *y1 = 0;
    *w = 0;
    *h = 0;
    return;
  }

  int16_t minx = 32767, miny = 32767, maxx = -32768, maxy = -32768;
  int16_t cursor_x = x, cursor_y = y;
  const uint8_t first = font->first;
  const uint8_t last = font->last;

  char c;
  while ((c = *str++)) {
    if (c == '\n') {
      cursor_x = x;
      cursor_y += (int16_t)font->yAdvance;
    } else if (c != '\r') {
      uint8_t uc = (uint8_t)c;
      if (uc >= first && uc <= last) {
        const GFXglyph *glyph = &(font->glyph[uc - first]);
        const uint8_t gw = glyph->width;
        const uint8_t gh = glyph->height;

        if (gw && gh) {
          int16_t gx1 = cursor_x + glyph->xOffset;
          int16_t gy1 = cursor_y + glyph->yOffset;
          int16_t gx2 = gx1 + gw - 1;
          int16_t gy2 = gy1 + gh - 1;

          if (gx1 < minx)
            minx = gx1;
          if (gy1 < miny)
            miny = gy1;
          if (gx2 > maxx)
            maxx = gx2;
          if (gy2 > maxy)
            maxy = gy2;
        }
        cursor_x += glyph->xAdvance;
      }
    }
  }

  if (maxx >= minx) {
    *x1 = minx - x;
    *w = maxx - minx + 1;
  } else {
    *x1 = 0;
    *w = 0;
  }
  if (maxy >= miny) {
    *y1 = miny - y;
    *h = maxy - miny + 1;
  } else {
    *y1 = 0;
    *h = 0;
  }
}

// ----------------------------------------------------------------------------
// Display device (ILI9488 4-inch TFT)
// ----------------------------------------------------------------------------
LGFX_ST7789_4::LGFX_ST7789_4() {
  {
    auto cfg = _bus_instance.config();
    cfg.spi_host = SPI3_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = SPI_BUS_SPEED;
    cfg.freq_read = SPI_BUS_SPEED * 8 / 5;
    cfg.pin_sclk = 18;
    cfg.pin_mosi = 23;
    cfg.pin_miso = -1;
    cfg.pin_dc = SPI_DC;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    _bus_instance.config(cfg);
    _panel_instance.setBus(&_bus_instance);
  }
  {
    auto cfg = _panel_instance.config();
    cfg.pin_cs = CS_DISPLAY;
    cfg.pin_rst = SPI_RST;
    cfg.pin_busy = -1;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.readable = false;
    cfg.invert = DISPLAY_INVERT_COLORS;
    cfg.rgb_order = false;
    cfg.bus_shared = true;
    _panel_instance.config(cfg);
  }
  setPanel(&_panel_instance);
}

void LGFX_ST7789_4::applyBusConfig() {
  auto cfg = _bus_instance.config();
  cfg.freq_write = SPI_BUS_SPEED;
  cfg.freq_read = SPI_BUS_SPEED * 8 / 5;
  _bus_instance.config(cfg);
}

void LGFX_ST7789_4::setFont(const GFXfont *f) {
  _currentGfxFont = f;
  lgfx::LGFX_Device::setFont(f);
  lgfx::LGFX_Device::setTextDatum(lgfx::textdatum_t::baseline_left);
}

void LGFX_ST7789_4::getTextBounds(const char *string, int16_t x, int16_t y,
                                int16_t *x1, int16_t *y1, uint16_t *w,
                                uint16_t *h) {
  if (_currentGfxFont) {
    calculateTextBounds(_currentGfxFont, string, x, y, x1, y1, w, h);
  } else {
    *x1 = 0;
    *y1 = 0;
    *w = textWidth(string);
    *h = fontHeight();
  }
}

void LGFX_ST7789_4::getTextBounds(const String &str, int16_t x, int16_t y,
                                int16_t *x1, int16_t *y1, uint16_t *w,
                                uint16_t *h) {
  getTextBounds(str.c_str(), x, y, x1, y1, w, h);
}

LGFX_ST7789_4 display;

// ----------------------------------------------------------------------------
// Color blending helpers
// ----------------------------------------------------------------------------
uint16_t blendColor(uint16_t fg, uint16_t bg, float alpha) {
  if (alpha <= 0.02f)
    return bg;
  if (alpha >= 0.98f)
    return fg;

  alpha = sqrtf(alpha);

  uint8_t fg_r = (fg >> 11) & 0x1F;
  uint8_t fg_g = (fg >> 5) & 0x3F;
  uint8_t fg_b = fg & 0x1F;

  uint8_t bg_r = (bg >> 11) & 0x1F;
  uint8_t bg_g = (bg >> 5) & 0x3F;
  uint8_t bg_b = bg & 0x1F;

  uint8_t r = (uint8_t)(bg_r + (fg_r - bg_r) * alpha);
  uint8_t g = (uint8_t)(bg_g + (fg_g - bg_g) * alpha);
  uint8_t b = (uint8_t)(bg_b + (fg_b - bg_b) * alpha);
  return (r << 11) | (g << 5) | b;
}

uint16_t blendColorLinear(uint16_t c1, uint16_t c2, float t) {
  if (t <= 0.0f)
    return c1;
  if (t >= 1.0f)
    return c2;
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
  uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
  uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
  return (r << 11) | (g << 5) | b;
}

uint16_t blendColorWithBlack(uint16_t color, float alpha) {
  return blendColor(color, 0x0000, alpha);
}

// ----------------------------------------------------------------------------
// Anti-aliased drawing primitives (templates)
// ----------------------------------------------------------------------------
template <typename T>
void drawAALine(T &disp, float x0, float y0, float x1, float y1,
                uint16_t color) {
  auto plot = [&](int x, int y, float c) {
    if (c <= 0.0f)
      return;
    uint16_t aaColor = blendColorWithBlack(color, c);
    disp.drawPixel(x, y, aaColor);
  };
  bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
  }
  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }
  float dx = x1 - x0;
  float dy = y1 - y0;
  float gradient = (dx == 0.0f) ? 1.0f : dy / dx;
  int xend = (int)roundf(x0);
  float yend = y0 + gradient * (xend - x0);
  float xgap = 1.0f - (x0 + 0.5f - floorf(x0 + 0.5f));
  int xpxl1 = xend;
  int ypxl1 = (int)floorf(yend);
  if (steep) {
    plot(ypxl1, xpxl1, (1.0f - (yend - floorf(yend))) * xgap);
    plot(ypxl1 + 1, xpxl1, (yend - floorf(yend)) * xgap);
  } else {
    plot(xpxl1, ypxl1, (1.0f - (yend - floorf(yend))) * xgap);
    plot(xpxl1, ypxl1 + 1, (yend - floorf(yend)) * xgap);
  }
  float intery = yend + gradient;
  int xend2 = (int)roundf(x1);
  float yend2 = y1 + gradient * (xend2 - x1);
  float xgap2 = x1 + 0.5f - floorf(x1 + 0.5f);
  int xpxl2 = xend2;
  int ypxl2 = (int)floorf(yend2);
  if (steep) {
    plot(ypxl2, xpxl2, (1.0f - (yend2 - floorf(yend2))) * xgap2);
    plot(ypxl2 + 1, xpxl2, (yend2 - floorf(yend2)) * xgap2);
  } else {
    plot(xpxl2, ypxl2, (1.0f - (yend2 - floorf(yend2))) * xgap2);
    plot(xpxl2, ypxl2 + 1, (yend2 - floorf(yend2)) * xgap2);
  }
  if (steep) {
    for (int x = xpxl1 + 1; x <= xpxl2 - 1; x++) {
      plot((int)floorf(intery), x, 1.0f - (intery - floorf(intery)));
      plot((int)floorf(intery) + 1, x, intery - floorf(intery));
      intery += gradient;
    }
  } else {
    for (int x = xpxl1 + 1; x <= xpxl2 - 1; x++) {
      plot(x, (int)floorf(intery), 1.0f - (intery - floorf(intery)));
      plot(x, (int)floorf(intery) + 1, intery - floorf(intery));
      intery += gradient;
    }
  }
}

template <typename T>
void drawAACircle(T &disp, int cx, int cy, int r, uint16_t color) {
  if (r <= 0)
    return;
  int x = r, y = 0;
  auto plot8 = [&](int px, int py, float alpha) {
    if (alpha <= 0.0f)
      return;
    uint16_t c = blendColorWithBlack(color, alpha);
    disp.drawPixel(cx + px, cy + py, c);
    disp.drawPixel(cx - px, cy + py, c);
    disp.drawPixel(cx + px, cy - py, c);
    disp.drawPixel(cx - px, cy - py, c);
    disp.drawPixel(cx + py, cy + px, c);
    disp.drawPixel(cx - py, cy + px, c);
    disp.drawPixel(cx + py, cy - px, c);
    disp.drawPixel(cx - py, cy - px, c);
  };
  plot8(r, 0, 1.0f);
  float T_val = 0.0f;
  while (x > y) {
    y++;
    float x_exact = sqrtf((float)(r * r - y * y));
    x = (int)ceilf(x_exact);
    T_val = (float)x - x_exact;
    plot8(x, y, 1.0f - T_val);
    plot8(x - 1, y, T_val);
  }
}

template <typename T>
void drawAACornerArc(T &disp, int cx, int cy, int r, uint8_t corner,
                     uint16_t color) {
  if (r <= 0)
    return;
  int x = r, y = 0;
  int signX = (corner == 0 || corner == 1) ? 1 : -1;
  int signY = (corner == 1 || corner == 2) ? 1 : -1;
  auto plotCorner = [&](int px, int py, float alpha) {
    if (alpha <= 0.0f)
      return;
    uint16_t c = blendColorWithBlack(color, alpha);
    disp.drawPixel(cx + signX * px, cy + signY * py, c);
    if (px != py)
      disp.drawPixel(cx + signX * py, cy + signY * px, c);
  };
  float T_val = 0.0f;
  while (x >= y) {
    float x_exact = sqrtf((float)(r * r - y * y));
    x = (int)ceilf(x_exact);
    T_val = (float)x - x_exact;
    plotCorner(x, y, 1.0f - T_val);
    plotCorner(x - 1, y, T_val);
    y++;
  }
}

template <typename T>
void drawAARoundRect(T &disp, int x, int y, int w, int h, int r,
                     uint16_t color) {
  if (w <= 0 || h <= 0)
    return;
  if (r <= 0) {
    drawAALine(disp, x, y, x + w - 1, y, color);
    drawAALine(disp, x + w - 1, y, x + w - 1, y + h - 1, color);
    drawAALine(disp, x + w - 1, y + h - 1, x, y + h - 1, color);
    drawAALine(disp, x, y + h - 1, x, y, color);
    return;
  }
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  disp.drawFastHLine(x + r, y, w - 2 * r, color);
  disp.drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  disp.drawFastVLine(x, y + r, h - 2 * r, color);
  disp.drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  drawAACornerArc(disp, x + w - 1 - r, y + r, r, 0, color);
  drawAACornerArc(disp, x + w - 1 - r, y + h - 1 - r, r, 1, color);
  drawAACornerArc(disp, x + r, y + h - 1 - r, r, 2, color);
  drawAACornerArc(disp, x + r, y + r, r, 3, color);
}

template <typename T>
void fillAARoundRect(T &disp, int x, int y, int w, int h, int r, uint16_t color,
                     uint16_t bg_top, uint16_t bg_bottom) {
  if (w <= 0 || h <= 0)
    return;
  if (r <= 0) {
    disp.fillRect(x, y, w, h, color);
    return;
  }
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;
  disp.fillRect(x + r, y, w - 2 * r, h, color);
  disp.fillRect(x, y + r, r, h - 2 * r, color);
  disp.fillRect(x + w - r, y + r, r, h - 2 * r, color);
  auto fillCornerSquare = [&](int cx, int cy, int signX, int signY,
                              uint16_t bgColor) {
    for (int py = 0; py <= r; py++) {
      float x_exact = sqrtf((float)(r * r - py * py));
      int px = (int)ceilf(x_exact);
      float T_val = (float)px - x_exact;
      if (T_val < 1.0f) {
        uint16_t c1 = blendColor(color, bgColor, 1.0f - T_val);
        disp.drawPixel(cx + signX * px, cy + signY * py, c1);
      }
      if (px - 1 >= 0) {
        if (signX == 1)
          disp.drawFastHLine(cx, cy + signY * py, px, color);
        else
          disp.drawFastHLine(cx - px + 1, cy + signY * py, px, color);
      }
    }
  };
  fillCornerSquare(x + w - 1 - r, y + r, 1, -1, bg_top);
  fillCornerSquare(x + w - 1 - r, y + h - 1 - r, 1, 1, bg_bottom);
  fillCornerSquare(x + r, y + h - 1 - r, -1, 1, bg_bottom);
  fillCornerSquare(x + r, y + r, -1, -1, bg_top);
}

// Explicit instantiations so the templates are available across translation
// units that include the header declarations.
template void drawAALine(LGFX_ST7789_4 &, float, float, float, float, uint16_t);
template void drawAACircle(LGFX_ST7789_4 &, int, int, int, uint16_t);
template void drawAACornerArc(LGFX_ST7789_4 &, int, int, int, uint8_t, uint16_t);
template void drawAARoundRect(LGFX_ST7789_4 &, int, int, int, int, int, uint16_t);
template void fillAARoundRect(LGFX_ST7789_4 &, int, int, int, int, int, uint16_t,
                              uint16_t, uint16_t);

// ----------------------------------------------------------------------------
// Icons
// ----------------------------------------------------------------------------
void drawBatteryIcon(int x, int y, float voltage, uint16_t color) {
  constexpr int iconW = 10, iconH = 16, nippleW = 4, nippleH = 2, innerX = 2,
                innerY = nippleH + 2, innerW = iconW - 4, innerH = iconH - 4;
  display.fillRect(x, y, iconW, iconH + nippleH, TFT_BLACK);
  display.fillRect(x + (iconW - nippleW) / 2, y, nippleW, nippleH, color);
  display.drawRect(x, y + nippleH, iconW, iconH, color);
  float pct = 0.0f;
  if (voltage > 0.5f)
    pct = constrain((voltage - 10.5f) / 4.0f, 0.0f, 1.0f);
  int fillH = (int)(pct * innerH);
  if (fillH > 0) {
    int fillY = (y + innerY) + (innerH - fillH);
    display.fillRect(x + innerX, fillY, innerW, fillH, color);
  }
}

int getDayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3)
    y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int getEuropeanOffset(int year, int month, int day, int hour) {
  if (month < 3 || month > 10)
    return 1;
  if (month > 3 && month < 10)
    return 2;
  int lastSunday = 31 - getDayOfWeek(year, month, 31);
  if (month == 3) {
    if (day > lastSunday)
      return 2;
    if (day < lastSunday)
      return 1;
    return (hour >= 1) ? 2 : 1;
  }
  if (month == 10) {
    if (day > lastSunday)
      return 1;
    if (day < lastSunday)
      return 2;
    return (hour >= 1) ? 1 : 2;
  }
  return 1;
}

void drawCalendarIcon(int x, int y, uint16_t color) {
  display.drawRect(x, y + 3, 16, 13, color);
  display.fillRect(x, y + 3, 16, 3, color);
  display.fillRect(x + 3, y, 2, 4, color);
  display.fillRect(x + 11, y, 2, 4, color);
}

void drawClockIcon(int x, int y, uint16_t color) {
  drawAACircle(display, x + 8, y + 8, 8, color);
  display.drawFastVLine(x + 8, y + 4, 5, color);
  display.drawFastHLine(x + 8, y + 8, 5, color);
}

void drawLocationIcon(int x, int y, uint16_t color) {
  display.fillCircle(x + 8, y + 5, 5, color);
  display.fillTriangle(x + 3, y + 5, x + 13, y + 5, x + 8, y + 15, color);
  display.fillCircle(x + 8, y + 5, 2, TFT_BLACK);
}

void drawWifiIcon(int x, int y, uint16_t color) {
  int cx = x + 8, cy = y + 12;
  display.fillCircle(cx, cy, 9, color);
  display.fillCircle(cx, cy, 7, TFT_BLACK);
  display.fillCircle(cx, cy, 6, color);
  display.fillCircle(cx, cy, 4, TFT_BLACK);
  display.fillCircle(cx, cy, 3, color);
  display.fillRect(cx - 10, cy, 21, 12, TFT_BLACK);
  display.fillTriangle(cx, cy, cx - 17, cy, cx - 17, cy - 10, TFT_BLACK);
  display.fillTriangle(cx, cy, cx + 17, cy, cx + 17, cy - 10, TFT_BLACK);
}

void drawWheelIcon(int x, int y, uint16_t color) {
  drawAACircle(display, x + 8, y + 8, 7, color);
  drawAACircle(display, x + 8, y + 8, 2, color);
  display.drawFastHLine(x + 3, y + 8, 3, color);
  display.drawFastHLine(x + 10, y + 8, 3, color);
  display.drawFastVLine(x + 8, y + 3, 3, color);
  display.drawFastVLine(x + 8, y + 10, 3, color);
  display.drawPixel(x + 5, y + 5, color);
  display.drawPixel(x + 11, y + 5, color);
  display.drawPixel(x + 5, y + 11, color);
  display.drawPixel(x + 11, y + 11, color);
}

void drawBadge(const char *text, int offsetX, int offsetY, uint16_t color) {
  int16_t tx1, ty1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &tx1, &ty1, &w, &h);
  int badgeW = w + 16, badgeH = h + 8;
  int x = (BIG_CENTER_X + offsetX) - (badgeW / 2);
  int y = (BIG_CENTER_Y + offsetY) - (badgeH / 2);
  display.fillRect(x - 3, y - 3, badgeW + 6, badgeH + 6, TFT_BLACK);
  drawAARoundRect(display, x, y, badgeW, badgeH, 4, color);
  display.setTextColor(color);
  display.setCursor(x + (badgeW - w) / 2 - tx1, y + (badgeH - h) / 2 - ty1 + 1);
  display.print(text);
}

// ----------------------------------------------------------------------------
// Splash / boot
// ----------------------------------------------------------------------------
void drawSplashBase() {
  splashCurrentProgress = 0;
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  display.setFont(&Conthrax_SemiBold12pt7b);
  display.setTextColor(TFT_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("DASHBOARD++", 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 120 - y1);
  display.print("DASHBOARD++");
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  drawAARoundRect(display, barX - 2, 160 - 2, 260 + 4, 8 + 4, 3, TFT_CYAN);
  display.setFont(&Conthrax_SemiBold7pt7b);
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(SPLASH_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 185 - y1);
  display.print(SPLASH_SIGNATURE);
  display.endWrite();
}

void updateSplashProgress(int targetProgress) {
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  while (splashCurrentProgress < targetProgress) {
    int step = random(1, 6);
    if (splashCurrentProgress + step > targetProgress)
      step = targetProgress - splashCurrentProgress;
    splashCurrentProgress += step;

    int fillW = (260 * splashCurrentProgress) / 100;
    if (fillW > 0) {
      display.startWrite();
      display.fillRect(barX, 160, fillW, 8, TFT_CYAN);
      display.endWrite();
    }

    int delayTime = step * (BOOT_TIME_MS / 100);
    delayTime += random(-delayTime / 4, delayTime / 4);
    if (delayTime < 0)
      delayTime = 0;
    delay(delayTime);
  }
}

void runDisplaySelfTest() {
  isSelfTestActive = true;
  overrideTimeDateStr = true;
  overrideSpeed = 188;
  overrideOdo = 888888.8;
  overrideSat = 88;
  overrideBat = 18.8f;
  overrideTimer = 18.88f;
  SensorSnapshot dummySnap;
  updateBigDisplay(dummySnap);
  for (int b = 0; b <= 255; b += 25) {
    analogWrite(BL_DISPLAY, b);
    delay(2);
  }
  analogWrite(BL_DISPLAY, 255);
  delay(250);
  isSelfTestActive = false;
  overrideTimeDateStr = false;
  updateBigDisplay(dummySnap);
}

void showGoodbyeScreen(bool isSleep) {
  preferences.begin("dashboard", false);
  preferences.putDouble("odo", totalDistanceKm);
  preferences.end();

  display.startWrite();
  display.fillScreen(TFT_BLACK);
  display.endWrite();

  int16_t x1, y1;
  uint16_t w, h;
  String title = isSleep ? "SEE YOU SOON" : "REBOOTING...";

  analogWrite(BL_DISPLAY, 30);

  display.startWrite();
  display.setFont(&Conthrax_SemiBold12pt7b);
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 120 - y1);
  display.print(title);

  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  drawAARoundRect(display, barX - 2, 160 - 2, 260 + 4, 8 + 4, 3, TFT_CYAN);

  display.setFont(&Conthrax_SemiBold7pt7b);
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(REBOOT_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 185 - y1);
  display.print(REBOOT_SIGNATURE);
  display.endWrite();

  int currentProgress = 0;
  while (currentProgress < 100) {
    int step = random(1, 8);
    if (currentProgress + step > 100)
      step = 100 - currentProgress;
    currentProgress += step;

    int fillW = (260 * currentProgress) / 100;
    if (fillW > 0) {
      display.startWrite();
      display.fillRect(barX, 160, fillW, 8, TFT_CYAN);
      display.endWrite();
    }

    int delayTime = step * (SHUTDOWN_TIME_MS / 100);
    delayTime += random(-delayTime / 4, delayTime / 4);
    if (delayTime < 0)
      delayTime = 0;
    delay(delayTime);
  }
  delay(50);

  if (isSleep) {
    analogWrite(BL_DISPLAY, 0);
    pinMode(SPI_RST, OUTPUT);
    digitalWrite(SPI_RST, LOW);
    pinMode(CS_DISPLAY, OUTPUT);
    digitalWrite(CS_DISPLAY, HIGH);
    pinMode(SPI_DC, OUTPUT);
    digitalWrite(SPI_DC, LOW);
    delay(10);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    rtc_gpio_init((gpio_num_t)BL_DISPLAY);
    rtc_gpio_set_direction((gpio_num_t)BL_DISPLAY, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)BL_DISPLAY, 0);
    rtc_gpio_init((gpio_num_t)SPI_RST);
    rtc_gpio_set_direction((gpio_num_t)SPI_RST, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)SPI_RST, 0);
    rtc_gpio_init((gpio_num_t)CS_DISPLAY);
    rtc_gpio_set_direction((gpio_num_t)CS_DISPLAY, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)CS_DISPLAY, 1);
    rtc_gpio_init((gpio_num_t)SPI_DC);
    rtc_gpio_set_direction((gpio_num_t)SPI_DC, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level((gpio_num_t)SPI_DC, 0);
    rtc_gpio_hold_en((gpio_num_t)BL_DISPLAY);
    rtc_gpio_hold_en((gpio_num_t)SPI_RST);
    rtc_gpio_hold_en((gpio_num_t)CS_DISPLAY);
    rtc_gpio_hold_en((gpio_num_t)SPI_DC);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_SENSE_PIN, 1);
    esp_deep_sleep_start();
  } else {
    ESP.restart();
  }
}

// ----------------------------------------------------------------------------
// FPS overlay
// ----------------------------------------------------------------------------
void drawFpsOverlay() {
  static float lastDrawnFpsBig = -1.0f, lastDrawnAvgFpsBig = -1.0f;
  static bool lastStateBig = false;
  static int lastBigX = -1, lastBigY = -1;
  static unsigned long lastFpsDrawTime = 0;
  constexpr int BOX_WIDTH = 145;
  unsigned long now = millis();
  if (now - lastFpsDrawTime < 250 && lastStateBig)
    return;
  lastFpsDrawTime = now;
  if (!showFpsCounter || lastBigX != OFFSET_BIG_FPS_X ||
      lastBigY != OFFSET_BIG_FPS_Y) {
    if (lastStateBig && lastBigX >= 0) {
      display.startWrite();
      display.fillRect(lastBigX, lastBigY, BOX_WIDTH, 12, TFT_BLACK);
      display.endWrite();
      lastStateBig = false;
      lastDrawnFpsBig = -1.0f;
      lastDrawnAvgFpsBig = -1.0f;
    }
    lastBigX = OFFSET_BIG_FPS_X;
    lastBigY = OFFSET_BIG_FPS_Y;
  }
  if (!showFpsCounter)
    return;
  char fpsBuf[48];
  snprintf(fpsBuf, sizeof(fpsBuf), "%.1f | AVG %.1f | %.1f", currentMeasuredFps,
           currentAverageFps, temperatureRead());
  if (fabsf(currentMeasuredFps - lastDrawnFpsBig) >= 0.3f ||
      fabsf(currentAverageFps - lastDrawnAvgFpsBig) >= 0.3f || !lastStateBig) {
    lastDrawnFpsBig = currentMeasuredFps;
    lastDrawnAvgFpsBig = currentAverageFps;
    lastStateBig = true;
    display.startWrite();
    display.fillRect(OFFSET_BIG_FPS_X, OFFSET_BIG_FPS_Y, BOX_WIDTH, 12,
                     TFT_BLACK);
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.setTextColor(TFT_GREEN);
    display.setCursor(OFFSET_BIG_FPS_X + 2, OFFSET_BIG_FPS_Y + 9);
    display.print(fpsBuf);
    int cx = display.getCursorX();
    int cy = OFFSET_BIG_FPS_Y + 4;
    display.drawCircle(cx + 1, cy, 1, TFT_GREEN);
    display.setCursor(cx + 4, OFFSET_BIG_FPS_Y + 9);
    display.print("c");
    display.endWrite();
  }
}
