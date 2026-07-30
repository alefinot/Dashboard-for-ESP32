#include "dashboard.h"
#include <vector>

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

#include "DS_DIGIT50pt7b.h"
#include "DS_DIGIT15pt7b.h"
#include "Conthrax_SemiBold12pt7b.h"
#include "Conthrax_SemiBold7pt7b.h"
#include "Conthrax_SemiBold4pt7b.h"

VFontData getVLWData120() {
  static std::vector<uint8_t> buf;
  if (buf.empty()) {
    fs::File f = LittleFS.open("/Fonts/DS-DIGIT_120px.vlw", "r");
    if (f) {
      buf.resize(f.size());
      f.read(buf.data(), buf.size());
      f.close();
    }
  }
  return { buf.empty() ? nullptr : buf.data(), buf.size() };
}

void LGFX_ST7789_4::loadVLWFont(const char *path) {
  setTextDatum(lgfx::textdatum_t::baseline_left);

  bool is120  = (path[7] == 'D' && path[16] == '1');
  bool isDs28 = (path[7] == 'D' && path[16] == '2');
  bool isCon28 = (path[7] == 'C' && path[25] == '2');
  bool isCon16 = (path[7] == 'C' && path[26] == '6');

  static int lastFont = -1;
  int cur = is120 ? 0 : isDs28 ? 1 : isCon28 ? 2 : isCon16 ? 3 : 4;
  if (cur == lastFont) return;
  lastFont = cur;

  if (is120) {
    auto vfd = getVLWData120();
    if (vfd.data) {
      if (!loadFont(vfd.data, lgfx::v1::IFont::font_type_t::ft_vlw))
        logPrintf("Font load failed (parse): %s\n", path);
    } else {
      logPrintf("Font load failed (open): %s\n", path);
    }
  } else if (isDs28) {
    setFont(&DS_DIGIT15pt7b);
  } else if (isCon28) {
    setFont(&Conthrax_SemiBold12pt7b);
  } else if (isCon16) {
    setFont(&Conthrax_SemiBold7pt7b);
  } else {
    setFont(&Conthrax_SemiBold4pt7b);
  }
}

void LGFX_ST7789_4::getTextBounds(const char *string, int16_t x, int16_t y,
                                int16_t *x1, int16_t *y1, uint16_t *w,
                                uint16_t *h) {
  *x1 = 0;
  *y1 = -_font_metrics.baseline;
  *w = textWidth(string);
  *h = fontHeight();
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
  if (alpha <= 0.02f) return bg;
  if (alpha >= 0.98f) return fg;
  // Fast integer blend: alpha as 8-bit fraction
  uint32_t a = (uint32_t)(alpha * 256.0f);
  if (a > 255) a = 255;
  uint32_t ia = 256 - a;
  uint32_t fg32 = fg, bg32 = bg;
  uint32_t r = ((bg32 & 0xF800) * ia + (fg32 & 0xF800) * a) >> 8;
  uint32_t g = ((bg32 & 0x07E0) * ia + (fg32 & 0x07E0) * a) >> 8;
  uint32_t b = ((bg32 & 0x001F) * ia + (fg32 & 0x001F) * a) >> 8;
  return (uint16_t)((r & 0xF800) | (g & 0x07E0) | (b & 0x001F));
}

uint16_t blendColorLinear(uint16_t c1, uint16_t c2, float t) {
  if (t <= 0.0f) return c1;
  if (t >= 1.0f) return c2;
  // Fast integer blend using 8-bit fraction
  uint32_t a = (uint32_t)(t * 256.0f);
  if (a > 255) a = 255;
  uint32_t ia = 256 - a;
  uint32_t c1_32 = c1, c2_32 = c2;
  uint32_t r = ((c1_32 & 0xF800) * ia + (c2_32 & 0xF800) * a) >> 8;
  uint32_t g = ((c1_32 & 0x07E0) * ia + (c2_32 & 0x07E0) * a) >> 8;
  uint32_t b = ((c1_32 & 0x001F) * ia + (c2_32 & 0x001F) * a) >> 8;
  return (uint16_t)((r & 0xF800) | (g & 0x07E0) | (b & 0x001F));
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
  if (!ENABLE_ANTIALIASING) {
    disp.drawLine((int)roundf(x0), (int)roundf(y0), (int)roundf(x1), (int)roundf(y1), color);
    return;
  }
  const float aaSh = AA_SHARPNESS;
  auto plot = [&](int x, int y, float c) {
    if (c <= 0.0f)
      return;
    if (aaSh != 1.0f) c = powf(c, aaSh);
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
  if (!ENABLE_ANTIALIASING) {
    disp.drawCircle(cx, cy, r, color);
    return;
  }
  if (r <= 0)
    return;
  const float aaSh = AA_SHARPNESS;
  int x = r, y = 0;
  auto plot8 = [&](int px, int py, float alpha) {
    if (alpha <= 0.0f)
      return;
    if (aaSh != 1.0f) alpha = powf(alpha, aaSh);
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
  const float aaSh = AA_SHARPNESS;
  int x = r, y = 0;
  int signX = (corner == 0 || corner == 1) ? 1 : -1;
  int signY = (corner == 1 || corner == 2) ? 1 : -1;
  auto plotCorner = [&](int px, int py, float alpha) {
    if (alpha <= 0.0f)
      return;
    if (aaSh != 1.0f) alpha = powf(alpha, aaSh);
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
  if (!ENABLE_ANTIALIASING) {
    disp.drawRoundRect(x, y, w, h, r, color);
    return;
  }
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
  if (!ENABLE_ANTIALIASING) {
    disp.fillRoundRect(x, y, w, h, r, color);
    return;
  }
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
  const float aaSh = AA_SHARPNESS;
  auto fillCornerSquare = [&](int cx, int cy, int signX, int signY,
                              uint16_t bgColor) {
    for (int py = 0; py <= r; py++) {
      float x_exact = sqrtf((float)(r * r - py * py));
      int px = (int)ceilf(x_exact);
      float T_val = (float)px - x_exact;
      if (T_val < 1.0f) {
        float aa = 1.0f - T_val;
        if (aaSh != 1.0f) aa = powf(aa, aaSh);
        uint16_t c1 = blendColor(color, bgColor, aa);
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
  drawAALine(display, (float)(x + 8), (float)(y + 4), (float)(x + 8), (float)(y + 8), color);
  drawAALine(display, (float)(x + 8), (float)(y + 8), (float)(x + 12), (float)(y + 8), color);
}

void drawStopwatchIcon(int x, int y, uint16_t color) {
  display.fillRect(x + 6, y, 4, 2, color);
  display.fillRect(x + 7, y - 1, 2, 1, color);
  drawAACircle(display, x + 8, y + 8, 7, color);
  drawAALine(display, (float)(x + 8), (float)(y + 8), (float)(x + 12), (float)(y + 4), color);
}

void drawLocationIcon(int x, int y, uint16_t color) {
  int cx = x + 8, cy = y + 5, r = 6;
  int px = r, py = 0;
  auto plotUpper = [&](int dx, int dy, float a) {
    if (a <= 0.0f) return;
    uint16_t c = blendColorWithBlack(color, a);
    display.drawPixel(cx + dx, cy - dy, c);
    display.drawPixel(cx - dx, cy - dy, c);
  };
  plotUpper(r, 0, 1.0f);
  plotUpper(0, r, 1.0f);
  while (px > py) {
    py++;
    float x_exact = sqrtf((float)(r * r - py * py));
    px = (int)ceilf(x_exact);
    float T_val = (float)px - x_exact;
    float a1 = powf(1.0f - T_val, AA_SHARPNESS);
    float a2 = powf(T_val, AA_SHARPNESS);
    plotUpper(px, py, a1);
    if (px > 1) plotUpper(px - 1, py, a2);
    plotUpper(py, px, a1);
    if (px > 1) plotUpper(py, px - 1, a2);
  }
  drawAALine(display, (float)(x + 14), (float)(y + 5), (float)(x + 8), (float)(y + 16), color);
  drawAALine(display, (float)(x + 8), (float)(y + 16), (float)(x + 2), (float)(y + 5), color);
  drawAACircle(display, cx, cy, 2, color);
}

void drawWifiIcon(int x, int y, uint16_t color, bool filled) {
  int cx = x + 8, cy = y + 8;
  drawAALine(display, (float)cx, (float)(cy + 5), (float)(cx - 6), (float)(cy - 4), color);
  drawAALine(display, (float)cx, (float)(cy + 5), (float)(cx + 6), (float)(cy - 4), color);
  drawAALine(display, (float)(cx - 6), (float)(cy - 4), (float)(cx - 4), (float)(cy - 5), color);
  drawAALine(display, (float)(cx - 4), (float)(cy - 5), (float)(cx - 2), (float)(cy - 5.5f), color);
  drawAALine(display, (float)(cx - 2), (float)(cy - 5.5f), (float)cx, (float)(cy - 6), color);
  drawAALine(display, (float)cx, (float)(cy - 6), (float)(cx + 2), (float)(cy - 5.5f), color);
  drawAALine(display, (float)(cx + 2), (float)(cy - 5.5f), (float)(cx + 4), (float)(cy - 5), color);
  drawAALine(display, (float)(cx + 4), (float)(cy - 5), (float)(cx + 6), (float)(cy - 4), color);
  if (filled) {
    display.fillTriangle(cx, cy + 5, cx - 6, cy - 4, cx + 6, cy - 4, color);
    display.drawFastHLine(cx - 4, cy - 5, 9, color);
    display.drawFastHLine(cx - 2, cy - 6, 5, color);
  }
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
  drawDebugBox(display, x - 3, y - 3, badgeW + 6, badgeH + 6);
  display.setTextColor(color);
  display.setCursor(x + (badgeW - w) / 2 - tx1, y + (badgeH - h) / 2 - ty1 + 1);
  display.print(text);
}

// ----------------------------------------------------------------------------
// Splash / boot
// ----------------------------------------------------------------------------
static void drawBarFill(int barX, int barY, int progress) {
  int fillW = (260 * progress) / 100;
  if (fillW > 260) fillW = 260;
  display.startWrite();
  if (fillW > 0)
    display.fillRect(barX, barY, fillW, 8, TFT_CYAN);
  display.endWrite();
}

void drawSplashBase() {
  splashCurrentProgress = 0;
  display.startWrite();
  display.fillScreen(TFT_BLACK);
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
  display.setTextColor(TFT_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("DASHBOARD++", 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 120 - y1);
  display.print("DASHBOARD++");
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(SPLASH_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 185 - y1);
  display.print(SPLASH_SIGNATURE);
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  drawAARoundRect(display, barX - 2, 160 - 2, 260 + 4, 8 + 4, 3, TFT_CYAN);
  display.endWrite();
}

void updateSplashProgress(int targetProgress) {
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  const int barY = 160;
  if (targetProgress <= splashCurrentProgress) return;

  int startProgress = splashCurrentProgress;
  int range = targetProgress - startProgress;
  unsigned long duration = range * random(18, 40);
  if (duration < 80) duration = 80;

  unsigned long animStart = millis();
  while (splashCurrentProgress < targetProgress) {
    unsigned long elapsed = millis() - animStart;
    if (elapsed >= duration) {
      splashCurrentProgress = targetProgress;
    } else {
      float t = (float)elapsed / (float)duration;
      splashCurrentProgress = startProgress + (int)(range * (1.0f - powf(1.0f - t, 3.0f)) + 0.5f);
    }
    drawBarFill(barX, barY, splashCurrentProgress);
    delay(16);
  }
}

void showGoodbyeScreen(bool isSleep) {
  preferences.begin("dashboard", false);
  preferences.putDouble("odo", totalDistanceKm);
  preferences.end();

  int fadeTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  if (fadeTarget > 255) fadeTarget = 255;
  logPrintf("goodbye fade-out: fadeTarget=%d BACKLIGHT_BRIGHTNESS=%d\n", fadeTarget, BACKLIGHT_BRIGHTNESS);
  int fadeStepCount = (fadeTarget / 8) + 1;
  for (int level = fadeTarget; level >= 0; level -= 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, 0);

  display.startWrite();
  display.fillScreen(TFT_BLACK);
  display.endWrite();

  int16_t x1, y1;
  uint16_t w, h;
  String title = isSleep ? "SEE YOU SOON" : "REBOOTING...";

  display.startWrite();
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 120 - y1);
  display.print(title);
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(REBOOT_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 185 - y1);
  display.print(REBOOT_SIGNATURE);
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  drawAARoundRect(display, barX - 2, 160 - 2, 260 + 4, 8 + 4, 3, TFT_CYAN);
  display.endWrite();

  fadeTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  if (fadeTarget > 255) fadeTarget = 255;
  fadeStepCount = (fadeTarget / 8) + 1;
  for (int level = 0; level <= fadeTarget; level += 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, fadeTarget);

  int currentProgress = 0;
  unsigned long remainingDuration = SHUTDOWN_TIME_MS;
  unsigned long animStart = millis();
  while (currentProgress < 100) {
    unsigned long elapsed = millis() - animStart;
    if (elapsed >= remainingDuration) {
      currentProgress = 100;
    } else {
      float t = (float)elapsed / (float)remainingDuration;
      currentProgress = (int)(100.0f * (1.0f - powf(1.0f - t, 3.0f)) + 0.5f);
    }
    drawBarFill(barX, 160, currentProgress);
    delay(16);
  }
  delay(50);

  fadeTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  if (fadeTarget > 255) fadeTarget = 255;
  fadeStepCount = (fadeTarget / 8) + 1;
  for (int level = fadeTarget; level >= 0; level -= 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, 0);

  if (isSleep) {
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
// OTA updating screen
// ----------------------------------------------------------------------------
volatile int otaProgressFillW = 0;
volatile int otaProgressTarget = 0;

void showUpdatingScreen() {
  otaProgressFillW = 0;
  otaProgressTarget = 0;

  int fadeTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  if (fadeTarget > 255) fadeTarget = 255;
  int fadeStepCount = (fadeTarget / 8) + 1;
  for (int level = fadeTarget; level >= 0; level -= 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, 0);

  int16_t x1, y1;
  uint16_t w, h;

  display.startWrite();
  display.fillScreen(TFT_BLACK);

  String title = "UPDATING...";
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
  display.setTextColor(TFT_WHITE);
  display.getTextBounds(title.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 120 - y1);
  display.print(title);

  display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
  display.setTextColor(TFT_WHITE);
  String msg = "DO NOT TURN OFF THE DEVICE";
  display.getTextBounds(msg.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(DISPLAY_WIDTH / 2 - (w / 2) - x1, 185 - y1);
  display.print(msg);
  int barX = DISPLAY_WIDTH / 2 - (260 / 2);
  drawAARoundRect(display, barX - 2, 160 - 2, 260 + 4, 8 + 4, 3, TFT_CYAN);
  display.endWrite();

  fadeTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  if (fadeTarget > 255) fadeTarget = 255;
  fadeStepCount = (fadeTarget / 8) + 1;
  for (int level = 0; level <= fadeTarget; level += 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, fadeTarget);
}

void updateOTAProgress(int progress, int total) {
  int targetW = (260L * progress) / total;
  if (targetW > 260) targetW = 260;
  if (targetW > otaProgressTarget)
    otaProgressTarget = targetW;
}

// ----------------------------------------------------------------------------
// Filesystem (SPIFFS) initialization
// ----------------------------------------------------------------------------
void initFilesystem() {
  if (!LittleFS.begin(false)) {
    logPrintf("LittleFS mount failed, formatting...\n");
    if (!LittleFS.begin(true)) {
      logPrintf("LittleFS format+re-mount failed!\n");
      return;
    }
  }
  logPrintf("LittleFS mounted OK\n");
}

// ----------------------------------------------------------------------------
// FPS overlay
// ----------------------------------------------------------------------------
void drawFpsOverlay() {
  static float lastDrawnFpsBig = -1.0f, lastDrawnAvgFpsBig = -1.0f;
  static bool lastStateBig = false;
  static int lastAnchorX = -1, lastAnchorY = -1;
  static int lastBoxW = 0, lastBoxH = 0;
  static unsigned long lastFpsDrawTime = 0;
  int anchorX = BIG_CENTER_X + OFFSET_BIG_FPS_X;
  int anchorY = BIG_CENTER_Y + OFFSET_BIG_FPS_Y;
  unsigned long now = millis();
  if (now - lastFpsDrawTime < 250 && lastStateBig)
    return;
  lastFpsDrawTime = now;
  if (!showFpsCounter || lastAnchorX != anchorX || lastAnchorY != anchorY) {
    if (lastStateBig && lastAnchorX >= 0) {
      display.startWrite();
      display.fillRect(lastAnchorX - (lastBoxW / 2), lastAnchorY - (lastBoxH / 2),
                       lastBoxW, lastBoxH, TFT_BLACK);
      display.endWrite();
      lastStateBig = false;
      lastDrawnFpsBig = -1.0f;
      lastDrawnAvgFpsBig = -1.0f;
    }
    lastAnchorX = anchorX;
    lastAnchorY = anchorY;
    lastBoxW = 0;
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
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_GREEN);
    int16_t tx1, ty1;
    uint16_t tw, th;
    display.getTextBounds(fpsBuf, 0, 0, &tx1, &ty1, &tw, &th);
    int boxW = tw + 3 + 4 + 2;
    int boxH = th + 6;
    if (lastBoxW > 0)
      display.fillRect(lastAnchorX - (lastBoxW / 2), lastAnchorY - (lastBoxH / 2),
                       lastBoxW, lastBoxH, TFT_BLACK);
    int boxLeft = anchorX - (boxW / 2);
    int boxTop = anchorY - (boxH / 2);
    display.fillRect(boxLeft, boxTop, boxW, boxH, TFT_BLACK);
    display.setCursor(boxLeft + 2, boxTop + boxH - 3);
    display.print(fpsBuf);
    int cx = display.getCursorX();
    int cy = boxTop + (boxH / 2) - 1;
    drawAACircle(display, cx + 1, cy, 1, TFT_GREEN);
    display.setCursor(cx + 4, boxTop + boxH - 3);
    display.print("c");
    drawDebugBox(display, boxLeft, boxTop, boxW, boxH);
    lastBoxW = boxW;
    lastBoxH = boxH;
    display.endWrite();
  }
}
