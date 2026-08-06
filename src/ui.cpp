#include "dashboard.h"
#include "Conthrax_SemiBold7pt7b.h"
#include "Conthrax_SemiBold4pt7b.h"

#define MAX_CELLS 16

static constexpr unsigned long STARTUP_RAMP_DURATION_MS = 3000;

// Frame-budget gate for updateBigDisplay(). A 60 FPS slot is 16.6ms, but the
// speed sprite push (~18-20ms at 60MHz SPI) plus the clock (~9ms) plus the
// odo/sidebar/middle-row redraws (~15-30ms) can land in the same frame and
// blow that budget, producing the 35-75ms stutter frames seen in the logs.
// Once a frame has already spent FRAME_DEFER_MS on the top phases, the
// throttle-gated lower-priority components are deferred wholesale to the next
// frame (their REFRESH_* throttles make a one-frame delay invisible). The
// same budget is also checked per group inside the odo/middle/compass band.
static constexpr unsigned long FRAME_DEFER_MS = 10;

// In-band budget check: true while the frame has not yet consumed the whole
// video-frame budget. Used to gate the low-priority redraw groups so that
// odo + sidebars + middle row + compass no longer stack into one 35ms frame.
#define IN_BAND_BUDGET ((millis() - tFrame0) < (unsigned long)FRAME_DEFER_MS)

// Pre-rendered speed-digit sprite (~40KB), the cached 120px VLW file buffer
// (~45KB) and the loaded 120px glyph tables (~30KB). Freed by the display task
// itself (safe point, see processOtaMemRelease) before an OTA pull check so
// the TLS handshake gets big contiguous heap blocks; rebuilt after the check
// finishes.
static LGFX_Sprite sp(&display);
static bool spValid = false;
static bool spBuildAttempted = false;
static bool vlw120Ready = false;
volatile bool otaMemReleaseRequested = false;
volatile bool otaMemReleased = false;

// One-time-per-boot metrics for the 120px speed digits, shared by the sprite
// path and the direct-panel fallback. Cached so a post-OTA rebuild never
// re-parses the VLW font on the display or re-measures every digit.
static bool spdMetricsReady = false;
static int spdCountCached = 0;
static uint16_t w_speed3_max = 0, h_speed_max = 0;
static int16_t refY1 = 0;
static int digitWidth[10] = {0}, digitXOff[10] = {0}, digitRightOff[10] = {0};
static int spdCellR[MAX_CELLS] = {0};

static void ensureSpeedMetrics() {
  if (spdMetricsReady && spdCountCached == SPEED_DIGITS) return;
  spdMetricsReady = true;
  spdCountCached = SPEED_DIGITS;
  // The 120px VLW gets parsed on the display here, once per boot (cheap when
  // the font cache survives). The sprite build below parses it a second time
  // into the sprite's own glyph tables.
  display.loadVLWFont("/Fonts/DS-DIGIT_120px.vlw");
  {
    int16_t sx1, sy1;
    uint16_t tw, th;
    char mBuf[2] = "0";
    for (int i = 0; i < 10; i++) {
      mBuf[0] = '0' + i;
      display.getTextBounds(mBuf, 0, 0, &sx1, &sy1, &tw, &th);
      digitWidth[i] = tw;
      digitXOff[i] = sx1;
    }
    display.getTextBounds("0", 0, 0, &sx1, &refY1, &tw, &th);
    int refW = digitWidth[8];
    for (int i = 0; i < 10; i++) {
      int diff = refW - digitWidth[i];
      digitRightOff[i] = (diff > 10) ? diff / 5 : 0;
    }
    constexpr int SG = -3;
    int cumX = 0;
    for (int i = 0; i < spdCountCached; i++) {
      char cb[2] = {'8', 0};
      display.getTextBounds(cb, cumX, 0, &sx1, &sy1, &tw, &th);
      spdCellR[i] = cumX + sx1 + tw;
      cumX += sx1 + tw + SG;
    }
    w_speed3_max = cumX - SG;
    h_speed_max = th;
  }
  vlw120Ready = true;
}

// Rebuilds the pre-rendered speed-digit sprite after an OTA check or a
// low-heap release dropped it. Runs from loop() at a safe point between frames
// (never inside updateBigDisplay), does a single VLW parse into the sprite
// (the digit metrics are cached), and skips the build while an OTA/TLS window
// or memory-saver mode is active. spBuildAttempted latches so a failed
// allocation is retried only after the next release, never every frame.
void ensureSpeedSprite() {
  if (!SHOW_ELEMENT_SPEED || spValid || spBuildAttempted || memSaverActive ||
      otaMemReleaseRequested || otaUpdateInProgress)
    return;
  spBuildAttempted = true;
  ensureSpeedMetrics();
  auto vfd = getVLWData120();
  if (!vfd.data) return;
  sp.setColorDepth(8);
  sp.setTextDatum(lgfx::textdatum_t::baseline_left);
  if (sp.createSprite(w_speed3_max + 12, h_speed_max + 6) &&
      sp.loadFont(vfd.data, lgfx::v1::IFont::font_type_t::ft_vlw)) {
    spValid = true;
    logPrintf("Speed sprite rebuilt (%dx%d, heap=%lu)\n", w_speed3_max + 12,
              h_speed_max + 6, (unsigned long)ESP.getFreeHeap());
  }
}

// Offscreen backbuffer for the compass tape's scrolling strip (ticks + bg).
// The strip is painted here and pushed to the panel in one burst so the
// display never shows a partially repainted tape. Freed together with the
// other big buffers during an OTA check (see processOtaMemRelease).
static LGFX_Sprite tapeSprite(&display);
static bool tapeSpriteReady = false;
static int tapeSpriteW = 0, tapeSpriteH = 0;

bool speedSpriteValid() { return spValid; }
bool tapeSpriteValid() { return tapeSpriteReady; }
bool isSpeedFallback() { return SHOW_ELEMENT_SPEED && !spValid && !otaMemReleaseRequested; }

// Called from loop() every frame, before any sprite use: frees the big UI
// buffers when an OTA check has asked for it. Runs in the display task so no
// other task can be using the sprite at the same time.
void processOtaMemRelease() {
  if (!otaMemReleaseRequested || otaMemReleased) return;
  sp.deleteSprite();
  spValid = false;
  spBuildAttempted = false;
  if (tapeSpriteReady) {
    tapeSprite.deleteSprite();
    tapeSpriteReady = false;
  }
  vlw120Ready = false;
  resetVLWFontCache();
  freeVLWData120();
  otaMemReleased = true;
}

// Same release, triggered by the web task when free heap gets dangerously low
// (the big buffers starve /api/config and the TLS stack). Once active, the
// speed sprite and tape sprite stay freed until the next reboot; the 120px
// font buffer is only re-read lazily if a speed digit actually redraws.
volatile bool memSaverRequested = false;
volatile bool memSaverActive = false;

void processMemSaverRelease() {
  if (!memSaverRequested || memSaverActive) return;
  sp.deleteSprite();
  spValid = false;
  spBuildAttempted = false;
  if (tapeSpriteReady) {
    tapeSprite.deleteSprite();
    tapeSpriteReady = false;
  }
  vlw120Ready = false;
  resetVLWFontCache();
  freeVLWData120();
  memSaverActive = true;
  logPrintf("MEM SAVER: dropped speed sprite, tape sprite and VLW120 (heap=%lu B)\n",
            (unsigned long)ESP.getFreeHeap());
}

static void measureDs15Cells(int *cells, int &totalW, int count, int decimalPos) {
  constexpr int G = 1;
  int16_t bx1, by1;
  uint16_t bw, bh;
  int cumX = 0;
  display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
  for (int i = 0; i < count; i++) {
    char cb[2] = {(i == decimalPos) ? '.' : '8', 0};
    display.getTextBounds(cb, cumX, 0, &bx1, &by1, &bw, &bh);
    cells[i] = cumX + bx1 + bw;
    cumX += bx1 + bw + G;
  }
  totalW = cumX - G;
}

// ----------------------------------------------------------------------------
// Compass tape dynamic layer (scrolling ticks)
// ----------------------------------------------------------------------------
// Fades tape content toward black at the screen edges (HUD look).
static uint16_t tapeFadeColor(uint16_t color, int x, int tapeW, int fadeW) {
  float f = 1.0f;
  if (x < fadeW) {
    f = (float)(x + 1) / (float)fadeW;
  } else if (x >= tapeW - fadeW) {
    f = (float)(tapeW - x) / (float)fadeW;
  }
  return blendColorWithBlack(color, f);
}

// Erases the strip (black fill) and paints the whole scrolling layer - ticks
// AND labels - into `d`. yBase is the strip top in `d`'s coordinate space and
// bandTopL the strip height (sprite: yBase=0, bandTopL=stripH; panel:
// yBase=labelTop, bandTopL=bandTop-labelTop). Labels use the same compiled-in
// Conthrax fonts as the panel, so sprite and direct rendering match exactly.
// Fonts must be switched through the panel's loadVLWFont shim when drawing on
// the display: the shim caches the last font (g_lastVLWFont) and skips setFont
// on a hit, so a direct setFont() desyncs the cache and later components (e.g.
// the FPS counter) keep drawing with a stale, larger font. Sprites have no
// such cache, so they can use setFont directly.
static inline void tapeSetFont(LGFX_ST7789_4 &d, const lgfx::IFont *f) {
  d.loadVLWFont(f == &Conthrax_SemiBold7pt7b ? "/Fonts/Conthrax_SemiBold_16px.vlw"
                                             : "/Fonts/Conthrax_SemiBold_10px.vlw");
}
template <typename T>
static inline void tapeSetFont(T &d, const lgfx::IFont *f) {
  d.setFont(f);
}

template <typename T>
static void renderTapeStrip(T &d, int tapeW, int compassX, int yBase, int bandTopL,
                            float refDeg, float displayHeading, float pxPerDeg,
                            uint16_t c_tickMin, uint16_t c_tickMaj, int fadeW,
                            bool headValid) {
  int labelBaselineL = bandTopL - 10;
  d.fillRect(0, yBase, tapeW, bandTopL, TFT_BLACK);
  for (int t = -40; t <= 40; t += 5) {
    if (t % 10 == 0) continue;
    int x = compassX + (int)roundf((refDeg + (float)t - displayHeading) * pxPerDeg);
    if (x < -6 || x >= tapeW + 6) continue;
    drawAALine(d, (float)x, (float)(yBase + bandTopL - 6), (float)x, (float)(yBase + bandTopL - 1),
               tapeFadeColor(c_tickMin, x, tapeW, fadeW));
  }
  for (int t = -40; t <= 40; t += 10) {
    int x = compassX + (int)roundf((refDeg + (float)t - displayHeading) * pxPerDeg);
    if (x < -16 || x >= tapeW + 16) continue;
    drawAALine(d, (float)x, (float)(yBase + bandTopL - 9), (float)x, (float)(yBase + bandTopL - 1),
               tapeFadeColor(c_tickMaj, x, tapeW, fadeW));
  }

  // Degree labels / cardinal letters (10° steps), centered on the major ticks
  if (headValid) {
    // Numbers first (small font)
    tapeSetFont(d, &Conthrax_SemiBold4pt7b);
    for (int t = -40; t <= 40; t += 10) {
      int deg = ((int)roundf(refDeg + (float)t) % 360 + 360) % 360;
      if (deg == 0 || deg == 90 || deg == 180 || deg == 270) continue;
      int x = compassX + (int)roundf((refDeg + (float)t - displayHeading) * pxPerDeg);
      if (x < -16 || x >= tapeW + 16) continue;
      char lb[4];
      snprintf(lb, sizeof(lb), "%d", deg);
      d.setTextColor(tapeFadeColor(TFT_WHITE, x, tapeW, fadeW));
      d.setCursor(x - (d.textWidth(lb) / 2), yBase + labelBaselineL);
      d.print(lb);
    }
    // Cardinal letters on top (bigger font)
    tapeSetFont(d, &Conthrax_SemiBold7pt7b);
    for (int t = -40; t <= 40; t += 10) {
      int deg = ((int)roundf(refDeg + (float)t) % 360 + 360) % 360;
      if (deg != 0 && deg != 90 && deg != 180 && deg != 270) continue;
      const char *label;
      uint16_t col;
      if (deg == 0) {
        label = "N";
        col = TFT_RED;
      } else if (deg == 90) {
        label = "E";
        col = TFT_WHITE;
      } else if (deg == 180) {
        label = "S";
        col = TFT_WHITE;
      } else {
        label = "W";
        col = TFT_WHITE;
      }
      int x = compassX + (int)roundf((refDeg + (float)t - displayHeading) * pxPerDeg);
      if (x < -16 || x >= tapeW + 16) continue;
      d.setTextColor(tapeFadeColor(col, x, tapeW, fadeW));
      d.setCursor(x - (d.textWidth(label) / 2), yBase + labelBaselineL);
      d.print(label);
    }
  } else {
    tapeSetFont(d, &Conthrax_SemiBold7pt7b);
    d.setTextColor(d.color565(70, 70, 70));
    d.setCursor(compassX - (d.textWidth("---") / 2), yBase + labelBaselineL);
    d.print("---");
  }
}

// ----------------------------------------------------------------------------
// Main dashboard renderer
// ----------------------------------------------------------------------------
void updateBigDisplay(const SensorSnapshot &snap) {
  static int lastHour = -1, lastMin = -1, lastDay = -1, lastSpeed = -1,
             lastSat = -1, lastTimeStartX = -1, lastTimeWidth = -1,
             lastTimeY = -1, lastDateStartX = -1, lastDateWidth = -1,
             lastDateY = -1, lastSpeedSourceState = -1;
  static double lastDispOdo = -1.0;
  static TimerState lastState = READY;
  static bool firstRun = true;
  static int ds15_fontH = 0;

  // Startup ramp: smoothly scale display values from 0 to actual over ~3s
  SensorSnapshot displaySnap = snap;
  unsigned long rampElapsed = millis() - g_startupTime;
  if (rampElapsed < STARTUP_RAMP_DURATION_MS) {
    float t = (float)rampElapsed / (float)STARTUP_RAMP_DURATION_MS;
    float rampFactor = t * (2.0f - t);
    displaySnap.currentSpeed *= rampFactor;
    displaySnap.fuelLiters *= rampFactor;
    displaySnap.fuelPercentage = (int)(displaySnap.fuelPercentage * rampFactor);
    displaySnap.batteryVoltage *= rampFactor;
    displaySnap.engineTemperature *= rampFactor;
    displaySnap.satellites = (int)(displaySnap.satellites * rampFactor);
    displaySnap.instantKml *= rampFactor;
    displaySnap.averageKml *= rampFactor;
    displaySnap.averageSpeed *= rampFactor;
  }
  display.startWrite();

  // Diagnostics: per-phase frame timing, logged only when the frame exceeds
  // the 60 FPS budget (33ms). Breakdown points at the guilty component.
  unsigned long tFrame0 = millis();
  unsigned long tAfterSpeed = tFrame0, tAfterOdo = tFrame0,
                tAfterCompass = tFrame0, tAfterWeather = tFrame0;
  unsigned long tClockMs = 0, tSprMs = 0;
  unsigned long tAfterSb = tFrame0, tAfterMid = tFrame0;

  bool forceDraw =
      firstRun || forceFullRedraw;
  bool layoutReset = forceFullRedraw;
  if (forceFullRedraw) {
    display.fillScreen(TFT_BLACK);
    forceFullRedraw = false;
  }
  bool componentUpdated = false;
  int16_t x1, y1, tx1, ty1;
  uint16_t w, h;

  if (firstRun || forceDraw) {
    firstRun = false;
    if (SHOW_ELEMENT_SPEED_UNIT) {
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
      display.setTextColor(TFT_WHITE);
      display.getTextBounds("KM/H", 0, 0, &x1, &y1, &w, &h);
      int unitX = BIG_CENTER_X + OFFSET_BIG_SPEED_UNIT_X,
          unitY = BIG_CENTER_Y + OFFSET_BIG_SPEED_UNIT_Y;
      display.setCursor(unitX - (w / 2) - x1, unitY - y1);
      display.print("KM/H");
      drawDebugBox(display, unitX - (w / 2) - 2, unitY - 2, w + 4, h + 4);
    }
  }
  if (forceDraw) {
    if (SHOW_ELEMENT_SIGNATURE) {
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
      display.setTextColor(display.color565(150, 150, 150));
      display.getTextBounds(DASHBOARD_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
      int sigX = BIG_CENTER_X + OFFSET_BIG_SIGNATURE_X,
          sigY = BIG_CENTER_Y + OFFSET_BIG_SIGNATURE_Y;
      display.fillRect(sigX - (w / 2) - 3, sigY - 3, w + 6, h + 6, TFT_BLACK);
      display.setCursor(sigX - (w / 2) - x1, sigY - y1);
      display.print(DASHBOARD_SIGNATURE);
      drawDebugBox(display, sigX - (w / 2) - 2, sigY - 2, w + 4, h + 4);
    }
  }

  int currentSourceState = displaySnap.speedSourceMode;
  if (ENABLE_DEMO_MODE) {
    currentSourceState = (millis() / 2000) % 3;
  }
  if (SHOW_ELEMENT_SPEED_SOURCE &&
      (currentSourceState != lastSpeedSourceState || forceDraw)) {
    lastSpeedSourceState = currentSourceState;
    componentUpdated = true;
    const char *label = "HAL";
    uint16_t color = TFT_CYAN;
    if (currentSourceState == 1) {
      label = "GPS";
      color = TFT_YELLOW;
    } else if (currentSourceState == 2) {
      label = "G+H";
      color = TFT_GREEN;
    }
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
    drawBadge(label, OFFSET_HALL_ICON_X, OFFSET_HALL_ICON_Y, color);
  }

  static int lastWifiState = -1;
  static bool lastShowPlus = false;
  static int cachedWifiState = -1;
  static bool cachedShowPlus = false;
  static unsigned long lastWifiPoll = 0;
  unsigned long wifiNow = millis();
  if (cachedWifiState < 0 || wifiNow - lastWifiPoll >= 1000) {
    lastWifiPoll = wifiNow;
    int hasClients = WiFi.softAPgetStationNum() > 0;
    bool isStation = WiFi.status() == WL_CONNECTED;
    bool isAP = WiFi.getMode() != WIFI_OFF;
    cachedWifiState = isAP ? (hasClients ? 2 : 1) : 0;
    cachedShowPlus = isStation;
  }
  int currentWifiState = (cachedWifiState < 0) ? 0 : cachedWifiState;
  bool showPlus = cachedShowPlus;

  if (SHOW_ELEMENT_WIFI &&
      (currentWifiState != lastWifiState || showPlus != lastShowPlus || forceDraw)) {
    lastWifiState = currentWifiState;
    lastShowPlus = showPlus;
    int wifiX = BIG_CENTER_X + OFFSET_WIFI_ICON_X;
    int wifiY = BIG_CENTER_Y + OFFSET_WIFI_ICON_Y;
    display.fillRect(wifiX, wifiY - 3, 21, 18, TFT_BLACK);
    if (currentWifiState == 1) {
      drawWifiIcon(wifiX, wifiY, TFT_WHITE, false);
    } else if (currentWifiState == 2) {
      drawWifiIcon(wifiX, wifiY, TFT_WHITE, true);
    }
    if (showPlus) {
      int px = wifiX + 16, py = wifiY + 1;
      drawAALine(display, (float)(px - 3), (float)py, (float)(px + 3), (float)py, TFT_WHITE);
      drawAALine(display, (float)px, (float)(py - 3), (float)px, (float)(py + 3), TFT_WHITE);
    }
    drawDebugBox(display, wifiX, wifiY - 3, 21, 18);
  }

  unsigned long now = millis();
  unsigned long tClock0 = now;
  static unsigned long lastTimeUpdate = 0;
  // Phase-offset the time-row redraw 125ms off the second boundary. The demo
  // clock ticks once per second (minute = t/1000 in sensors.cpp) and the speed
  // digits redraw every 250ms from the same absolute boot grid, so without an
  // offset BOTH redraws land in the same 16ms frame at every second boundary
  // (~9ms clock + ~18ms speed = ~30ms frame, invisible to the 33ms SLOW FRAME
  // threshold but plainly visible as a 1Hz hitch). 125ms places the 1Hz
  // redraw midway between the 250ms speed redraws.
  constexpr unsigned long TIME_REDRAW_PHASE_MS = 125;
  bool timeRowChanged = displaySnap.localHour != lastHour ||
                        displaySnap.minute != lastMin ||
                        displaySnap.day != lastDay;
  unsigned long timeRowThrottle =
      (REFRESH_TIME_MS == 0) ? TIME_REDRAW_PHASE_MS
                             : (unsigned long)REFRESH_TIME_MS + TIME_REDRAW_PHASE_MS;
  if ((SHOW_ELEMENT_TIME || SHOW_ELEMENT_DATE) &&
      ((timeRowChanged && now - lastTimeUpdate >= timeRowThrottle) || forceDraw)) {
    lastTimeUpdate = now;
    lastHour = displaySnap.localHour;
    lastMin = displaySnap.minute;
    lastDay = displaySnap.day;
    componentUpdated = true;
    char hourStr[4] = "--", minStr[4] = "--", dayStr[4] = "--",
         monthStr[4] = "--", yearStr[4] = "--";
    if (displaySnap.timeValid) {
      snprintf(hourStr, 4, "%02d", displaySnap.localHour);
      snprintf(minStr, 4, "%02d", displaySnap.minute);
    }
    if (displaySnap.dateValid) {
      snprintf(dayStr, 4, "%02d", displaySnap.day);
      snprintf(monthStr, 4, "%02d", displaySnap.month);
      snprintf(yearStr, 4, "%02d", displaySnap.year);
    }
    uint16_t w_h, h_h, w_m, h_m, w_d, h_d, w_mo, h_mo, w_y, h_y;
    static uint16_t w_sep_t = 0, h_sep_t = 0, w_sep_d = 0, h_sep_d = 0;
    static int16_t tx1_sep_t = 0, tx1_sep_d = 0;
    static int16_t ty1_sep_t = 0, ty1_sep_d = 0;
    if (w_sep_t == 0) {
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
      display.getTextBounds(":", 0, 0, &tx1_sep_t, &ty1_sep_t, &w_sep_t, &h_sep_t);
      display.getTextBounds("/", 0, 0, &tx1_sep_d, &ty1_sep_d, &w_sep_d, &h_sep_d);
    }
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.getTextBounds(hourStr, 0, 0, &tx1, &ty1, &w_h, &h_h);
    int16_t save_h_x1 = tx1;
    display.getTextBounds(minStr, 0, 0, &tx1, &ty1, &w_m, &h_m);
    display.getTextBounds(dayStr, 0, 0, &tx1, &ty1, &w_d, &h_d);
    int16_t save_d_x1 = tx1;
    display.getTextBounds(monthStr, 0, 0, &tx1, &ty1, &w_mo, &h_mo);
    display.getTextBounds(yearStr, 0, 0, &tx1, &ty1, &w_y, &h_y);
    int16_t digitTY1 = ty1;

    int timeW = 16 + 6 + w_h + w_sep_t + w_m;
    int timeX = (BIG_CENTER_X + OFFSET_BIG_TIME_X) - (timeW / 2);
    int timeY = BIG_CENTER_Y + OFFSET_BIG_TIME_Y;

    int dateW = 16 + 6 + w_d + w_sep_d + w_mo + w_sep_d + w_y;
    int dateX = (BIG_CENTER_X + OFFSET_BIG_DATE_X) - (dateW / 2);
    int dateY = BIG_CENTER_Y + OFFSET_BIG_DATE_Y;

    int timeClearOfsY = std::min(std::min((int)-18, (int)digitTY1), (int)ty1_sep_t) - 4;
    int timeClearH = std::max(std::max((int)-2, (int)digitTY1 + (int)max(h_h, h_m)), (int)ty1_sep_t + (int)h_sep_t) - std::min(std::min((int)-18, (int)digitTY1), (int)ty1_sep_t) + 8;

    int dateClearOfsY = std::min(std::min((int)-18, (int)digitTY1), (int)ty1_sep_d) - 4;
    int dateClearH = std::max(std::max((int)-2, (int)digitTY1 + (int)max(max(h_d, h_mo), h_y)), (int)ty1_sep_d + (int)h_sep_d) - std::min(std::min((int)-18, (int)digitTY1), (int)ty1_sep_d) + 8;

    if (lastTimeStartX >= 0 && lastTimeY >= 0 && lastTimeY != timeY) {
      display.fillRect(lastTimeStartX - 8, lastTimeY + timeClearOfsY, lastTimeWidth + 16,
                        timeClearH, TFT_BLACK);
    }
    int clearTimeX = timeX - 4, clearTimeW = timeW + 8;
    if (lastTimeWidth > 0 && lastTimeStartX >= 0 && lastTimeY == timeY) {
      clearTimeX = std::min(timeX, lastTimeStartX) - 4;
      clearTimeW = std::max(timeX + timeW, lastTimeStartX + lastTimeWidth) + 4 -
                   clearTimeX;
    }
    display.fillRect(clearTimeX - 4, timeY + timeClearOfsY, clearTimeW + 12, timeClearH,
                     TFT_BLACK);
    lastTimeStartX = timeX;
    lastTimeWidth = timeW;
    lastTimeY = timeY;

    if (lastDateStartX >= 0 && lastDateY >= 0 && lastDateY != dateY) {
      display.fillRect(lastDateStartX - 8, lastDateY + dateClearOfsY, lastDateWidth + 16,
                        dateClearH, TFT_BLACK);
    }
    int clearDateX = dateX - 4, clearDateW = dateW + 8;
    if (lastDateWidth > 0 && lastDateStartX >= 0 && lastDateY == dateY) {
      clearDateX = std::min(dateX, lastDateStartX) - 4;
      clearDateW = std::max(dateX + dateW, lastDateStartX + lastDateWidth) + 4 -
                   clearDateX;
    }
    display.fillRect(clearDateX - 4, dateY + dateClearOfsY, clearDateW + 12, dateClearH,
                     TFT_BLACK);
    lastDateStartX = dateX;
    lastDateWidth = dateW;
    lastDateY = dateY;

    if (SHOW_ELEMENT_TIME) {
      drawClockIcon(timeX, timeY - 18, TFT_WHITE);
      display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
      display.setTextColor(TFT_WHITE);
      display.setCursor(timeX + 22 - save_h_x1, timeY);
      display.print(hourStr);
      display.setCursor(timeX + 22 - save_h_x1 + w_h + 4 + w_sep_t + 4, timeY);
      display.print(minStr);
      drawDebugBox(display, timeX - 2, timeY + timeClearOfsY + 2, timeW + 12, timeClearH - 4);
    }

    if (SHOW_ELEMENT_DATE) {
      drawCalendarIcon(dateX, dateY - 18, TFT_WHITE);
      display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
      display.setCursor(dateX + 22 - save_d_x1, dateY);
      display.print(dayStr);
      display.setCursor(dateX + 22 - save_d_x1 + w_d + w_sep_d, dateY);
      display.print(monthStr);
      display.setCursor(dateX + 22 - save_d_x1 + w_d + w_sep_d + w_mo + w_sep_d, dateY);
      display.print(yearStr);
    }

    // Draw separators in Conthrax font (single switch)
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_28px.vlw");
    display.setTextColor(TFT_WHITE);
    if (SHOW_ELEMENT_TIME) {
      display.setCursor(timeX + 22 - save_h_x1 + w_h + 4, timeY);
      display.print(":");
    }
    if (SHOW_ELEMENT_DATE) {
      display.setCursor(dateX + 22 - save_d_x1 + w_d, dateY);
      display.print("/");
      display.setCursor(dateX + 22 - save_d_x1 + w_d + w_sep_d + w_mo, dateY);
      display.print("/");
      drawDebugBox(display, dateX - 2, dateY + dateClearOfsY + 2, dateW + 4, dateClearH - 4);
    }
  }
  tClockMs = millis() - tClock0;

  // --- DS_DIGIT15pt7b digit metrics (measured once) ---
  static bool ds15Measured = false;
  static int ds15_digitWidth[10] = {0}, ds15_digitXOff[10] = {0};
  static int16_t ds15_refY1 = 0;
  static uint16_t ds15_dotWidth = 0;
  static int16_t ds15_dotXOff = 0;

  if (!ds15Measured) {
    ds15Measured = true;
    int16_t bx1, by1;
    uint16_t bw, bh;
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    char buf[2] = "0";
    for (int i = 0; i < 10; i++) {
      buf[0] = '0' + i;
      display.getTextBounds(buf, 0, 0, &bx1, &by1, &bw, &bh);
      ds15_digitWidth[i] = bw;
      ds15_digitXOff[i] = bx1;
    }
    display.getTextBounds("0", 0, 0, &bx1, &ds15_refY1, &bw, &bh);
    display.getTextBounds(".", 0, 0, &bx1, &by1, &ds15_dotWidth, &bh);
    ds15_dotXOff = bx1;
  }

  int currentSpeed = (int)displaySnap.currentSpeed;
  static unsigned long lastSpeedUpdate = 0;
  if (SHOW_ELEMENT_SPEED && ((currentSpeed != lastSpeed &&
      (REFRESH_SPEED_MS == 0 || now - lastSpeedUpdate >= (unsigned long)REFRESH_SPEED_MS)) || forceDraw)) {
    int speedNumX = BIG_CENTER_X + OFFSET_BIG_SPEED_NUM_X,
        speedNumY = BIG_CENTER_Y + OFFSET_BIG_SPEED_NUM_Y;

    // Digit metrics are measured once per boot and cached; the sprite itself
    // is (re)built by ensureSpeedSprite() at a safe point in loop(), never
    // mid-frame, so a post-OTA rebuild can't stall this frame.
    ensureSpeedMetrics();
    int spdCount = spdCountCached;

    char speedStr[12];
    snprintf(speedStr, sizeof(speedStr), "%d", currentSpeed);
    int len = strlen(speedStr);
    int boxLeft = applyAlign(speedNumX, w_speed3_max, ALIGN_BIG_SPEED_NUM);

    // The direct-panel fallback below can skip frames (throttle); only commit
    // the new speed once a draw actually happened so a skipped change still
    // gets painted on the next redraw opportunity.
    bool drew = false;
    if (spValid) {
      unsigned long tSpr0 = millis();
      sp.fillSprite(TFT_BLACK);
      sp.setTextColor(ghost_color);
      for (int ci = 0; ci < spdCount; ci++) {
        int cx = spdCellR[ci] + 4 - digitXOff[8] - digitWidth[8];
        sp.setCursor(cx, 2 - refY1);
        sp.print('8');
      }
      sp.setTextColor(TFT_WHITE);
      for (int i = 0; i < len; i++) {
        int d = speedStr[i] - '0';
        int cellIdx = (spdCount - len) + i;
        int cx = spdCellR[cellIdx] + 4 - digitXOff[d] - digitWidth[d] + digitRightOff[d];
        sp.setCursor(cx, 2 - refY1);
        sp.print(speedStr[i]);
      }
      sp.pushSprite(&display, boxLeft - 6, speedNumY - 2);
      drew = true;
      tSprMs = millis() - tSpr0;
    } else {
      // Direct-panel fallback (no sprite: OTA check, mem-saver or allocation
      // failed). Reload the 120px VLW on the display if a mem release
      // invalidated it, and cap the redraw rate so the big per-glyph SPI cost
      // can't eat the whole frame budget at 60 FPS. During an OTA check the
      // VLW file must NOT be re-read from flash mid-frame, and the last drawn
      // digits stay on screen until the sprite is rebuilt.
      static unsigned long lastFallbackDraw = 0;
      bool canFallback =
          !otaMemReleaseRequested && (forceDraw || now - lastFallbackDraw >= 33);
      if (canFallback) {
        lastFallbackDraw = now;
        if (!vlw120Ready) {
          // In mem-saver mode the 45KB VLW120 buffer must stay freed (that is
          // the whole point of the mode); reloading it here used to re-run the
          // allocation on a fragmented heap and crash (bad_alloc -> abort).
          if (!memSaverActive) {
            display.loadVLWFont("/Fonts/DS-DIGIT_120px.vlw");
            vlw120Ready = isVLW120FontReady();
          }
        }
        if (vlw120Ready) {
          display.setTextColor(TFT_WHITE);
          for (int i = 0; i < len; i++) {
            int d = speedStr[i] - '0';
            int cellIdx = (spdCount - len) + i;
            int cellRight = boxLeft + spdCellR[cellIdx];
            int cx = cellRight - 2 - digitXOff[d] - digitWidth[d] + digitRightOff[d];
            display.setCursor(cx, speedNumY - refY1);
            display.print(speedStr[i]);
          }
          drew = true;
        }
      }
    }
    if (drew) {
      lastSpeed = currentSpeed;
      lastSpeedUpdate = now;
      componentUpdated = true;
    }
    drawDebugBox(display, boxLeft - 6, speedNumY - 2, w_speed3_max + 12, h_speed_max + 6);
  }
  tAfterSpeed = millis();

  // endWrite + slow-frame diagnostics, shared by the deferral early-return and
  // the normal end of frame.
  auto finishFrame = [&]() {
    if ((componentUpdated || forceDraw) && ENABLE_CIRCLE_TEST)
      for (int i = 0; i < 4; i++)
        drawAACircle(display, BIG_CENTER_X, BIG_CENTER_Y, 173 - i, TFT_CYAN);
    display.endWrite();
    unsigned long tFrameEnd = millis();
    if (tFrameEnd - tFrame0 > 33) {
      logPrintf("SLOW FRAME %lums: time+spd=%lu odo=%lu sb=%lu mid=%lu cmp=%lu wx+tail=%lu clk=%lu spr=%lu heap=%lu maxAlloc=%lu sp=%d fallback=%d\n",
                (unsigned long)(tFrameEnd - tFrame0),
                (unsigned long)(tAfterSpeed - tFrame0),
                (unsigned long)(tAfterOdo - tAfterSpeed),
                (unsigned long)(tAfterSb - tAfterOdo),
                (unsigned long)(tAfterMid - tAfterSb),
                (unsigned long)(tAfterCompass - tAfterMid),
                (unsigned long)(tFrameEnd - tAfterWeather),
                (unsigned long)tClockMs, (unsigned long)tSprMs,
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap(),
                (int)speedSpriteValid(), (int)isSpeedFallback());
    }
  };

  // Defer the throttle-gated lower-priority components (odo, sidebars, middle
  // row, compass, weather) when this frame's top phases (clock + speed sprite)
  // already ate the 16.6ms 60FPS budget. A deferral is never repeated two
  // frames in a row, so those components always make progress at their own
  // REFRESH_* cadence.
  static bool frameDeferredLast = false;
  bool frameDeferRest =
      !forceDraw && !frameDeferredLast &&
      (millis() - tFrame0) > FRAME_DEFER_MS;
  frameDeferredLast = frameDeferRest;
  if (frameDeferRest) {
    tAfterOdo = tAfterSb = tAfterMid = tAfterCompass = tAfterWeather = millis();
    finishFrame();
    return;
  }

  double displayOdo = displaySnap.totalDistanceKm;
  static unsigned long lastOdoUpdate = 0;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_ODO && ((fabs(displayOdo - lastDispOdo) >= 0.1 &&
      (REFRESH_ODO_MS == 0 || now - lastOdoUpdate >= (unsigned long)REFRESH_ODO_MS)) || forceDraw))) {
    lastOdoUpdate = now;
    lastDispOdo = displayOdo;
    componentUpdated = true;
    static uint16_t w_odo_unit_max = 0, h_odo_max = 0;
    static bool odoLayoutInit = false;
    if (!odoLayoutInit) {
      odoLayoutInit = true;
      int16_t tx1, ty1;
      uint16_t tw1, th1, tw2, th2;
      display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
      display.getTextBounds("999999.9", 0, 0, &tx1, &ty1, &tw1, &th1);
      h_odo_max = th1;
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
      display.getTextBounds(" KM", 0, 0, &tx1, &ty1, &tw2, &th2);
      w_odo_unit_max = tw2;
      if (th2 > h_odo_max) h_odo_max = th2;
    }
    static int odoCells[MAX_CELLS] = {0};
    static int odoCellW = 0, odoCellsCount = 0;
    if (odoCellsCount == 0) {
      odoCellsCount = ODO_INT_DIGITS + 1 + ODO_DEC_DIGITS;
      measureDs15Cells(odoCells, odoCellW, odoCellsCount, ODO_INT_DIGITS);
    }
    int odoCellX = BIG_CENTER_X + OFFSET_BIG_ODO_X - ((odoCellW + 4 + w_odo_unit_max) / 2);
    int odoUnitX = odoCellX + odoCellW + 4;

    char odoNumStr[20];
    snprintf(odoNumStr, sizeof(odoNumStr), "%.*f", ODO_DEC_DIGITS, displayOdo);
    int len = strlen(odoNumStr);

    int16_t odoY = (BIG_CENTER_Y + OFFSET_BIG_ODO_Y) - h_odo_max - ds15_refY1;
    int odoClearY = odoY + ds15_refY1 - 2;
    int odoClearW = odoCellW + 4 + w_odo_unit_max + 4;
    display.fillRect(odoCellX - 2, odoClearY, odoClearW, h_odo_max + 4, TFT_BLACK);

    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.setTextColor(ghost_color);
    for (int ci = 0; ci < odoCellsCount; ci++) {
      int cellRight = odoCellX + odoCells[ci];
      char gc = (ci == ODO_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, odoY);
      display.print(gc);
    }
    display.setTextColor(TFT_WHITE);
    for (int i = 0; i < len; i++) {
      char c = odoNumStr[i];
      int cellIdx = (odoCellsCount - len) + i;
      int cellRight = odoCellX + odoCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, odoY);
      display.print(c);
    }

    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.setCursor(odoUnitX, odoY);
    display.print(" KM");
    drawDebugBox(display, odoCellX - 2, odoClearY, odoClearW, h_odo_max + 4);
  }
  tAfterOdo = millis();

  // --- SIDEBARS: Engine Temp & Fuel ---
  static int lastFuelPct = -1;
  static int lastEngineTemp = -999;
  static float animTempFill = 0.0f;
  static float animFuelFill = 0.0f;
  static int lastTempFillH = 0;
  static int lastFuelFillH = 0;
  static constexpr unsigned long SIDEBAR_EASE_MS = 400;

  // Per-sidebar ease state
  static float easeTempFrom = 0.0f, easeTempTo = 0.0f;
  static float easeFuelFrom = 0.0f, easeFuelTo = 0.0f;
  static unsigned long easeTempStart = 0, easeFuelStart = 0;

  auto easeOutCubic = [](float t) -> float {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
  };

  int currentFuel = displaySnap.fuelPercentage;
  int currentTemp = (int)displaySnap.engineTemperature;

  float targetTempPct = 0.0f;
  {
    float tClamped = (float)constrain(currentTemp, TEMP_BAR_MIN, TEMP_BAR_MAX);
    float tRange = (float)(TEMP_BAR_MAX - TEMP_BAR_MIN);
    targetTempPct = (tRange > 0.0f) ? ((tClamped - (float)TEMP_BAR_MIN) / tRange) : 0.0f;
  }
  float targetFuelPct = constrain(currentFuel / 100.0f, 0.0f, 1.0f);

  if (forceDraw) {
    lastFuelPct = -1;
    lastEngineTemp = -999;
    animTempFill = targetTempPct;
    animFuelFill = targetFuelPct;
    easeTempFrom = targetTempPct;
    easeTempTo = targetTempPct;
    easeFuelFrom = targetFuelPct;
    easeFuelTo = targetFuelPct;
    lastTempFillH = 0;
    lastFuelFillH = 0;
  }

  // Start a new ease when the target changes
  if (fabsf(targetTempPct - easeTempTo) > 0.001f) {
    easeTempFrom = animTempFill;
    easeTempTo = targetTempPct;
    easeTempStart = now;
  }
  if (fabsf(targetFuelPct - easeFuelTo) > 0.001f) {
    easeFuelFrom = animFuelFill;
    easeFuelTo = targetFuelPct;
    easeFuelStart = now;
  }

  // Compute eased fill values (skip if already at target)
  {
    if (fabsf(animTempFill - easeTempTo) > 0.001f) {
      float elapsed = (float)(now - easeTempStart);
      float t = (elapsed >= (float)SIDEBAR_EASE_MS) ? 1.0f : (elapsed / (float)SIDEBAR_EASE_MS);
      animTempFill = easeTempFrom + (easeTempTo - easeTempFrom) * easeOutCubic(t);
      if (t >= 1.0f) animTempFill = easeTempTo;
    }
  }
  {
    if (fabsf(animFuelFill - easeFuelTo) > 0.001f) {
      float elapsed = (float)(now - easeFuelStart);
      float t = (elapsed >= (float)SIDEBAR_EASE_MS) ? 1.0f : (elapsed / (float)SIDEBAR_EASE_MS);
      animFuelFill = easeFuelFrom + (easeFuelTo - easeFuelFrom) * easeOutCubic(t);
      if (t >= 1.0f) animFuelFill = easeFuelTo;
    }
  }

  // Left Sidebar: Engine Temp
  static unsigned long lastSideTempUpdate = 0;
  bool tempMoving = (now - easeTempStart) < SIDEBAR_EASE_MS;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_SIDEBAR_TEMP &&
      (((currentTemp != lastEngineTemp || tempMoving) && (tempMoving || REFRESH_SIDEBAR_TEMP_MS == 0 || now - lastSideTempUpdate >= (unsigned long)REFRESH_SIDEBAR_TEMP_MS)) || forceDraw))) {
    lastSideTempUpdate = now;
    lastEngineTemp = currentTemp;

    int barX = SIDEBAR_LEFT_X, barY = SIDEBAR_LEFT_Y, barW = SIDEBAR_BAR_WIDTH, barH = SIDEBAR_BAR_HEIGHT;

    uint16_t tempColor;
    if (currentTemp <= TEMP_WARN_GRN) {
      tempColor = c_temp_norm;
    } else if (currentTemp <= TEMP_WARN_YEL) {
      float range = (float)(TEMP_WARN_YEL - TEMP_WARN_GRN);
      float t = (range > 0) ? (currentTemp - TEMP_WARN_GRN) / range : 1.0f;
      tempColor = blendColorLinear(c_temp_norm, c_temp_warn, t);
    } else if (currentTemp <= TEMP_WARN_RED) {
      float range = (float)(TEMP_WARN_RED - TEMP_WARN_YEL);
      float t = (range > 0) ? (currentTemp - TEMP_WARN_YEL) / range : 1.0f;
      tempColor = blendColorLinear(c_temp_warn, c_temp_crit, t);
    } else {
      tempColor = c_temp_crit;
    }

    drawAARoundRect(display, barX - 2, barY - 2, barW + 4, barH + 4, 4, tempColor);

    int fillH = (int)(barH * animTempFill);
    if (fillH > barH)
      fillH = barH;
    if (fillH < lastTempFillH) {
      display.fillRect(barX, barY + barH - lastTempFillH, barW, lastTempFillH - fillH, TFT_BLACK);
    }
    lastTempFillH = fillH;
    if (fillH > 0) {
      fillAARoundRect(display, barX, barY + barH - fillH, barW, fillH, 0,
                      tempColor, TFT_BLACK, TFT_BLACK);
    }

    char tBuf[16];
    snprintf(tBuf, sizeof(tBuf), "%d", currentTemp);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    int tempTextW = display.textWidth(tBuf);
    int tempH = display.fontHeight();
    int cTextW = display.textWidth("c");
    int totalW = tempTextW + 1 + 3 + 6 + cTextW;

    int numStartX = barX + barW + 6;
    display.fillRect(numStartX - 4, barY + barH - 2 - tempH - 4, totalW + 8,
                     tempH + 8, TFT_BLACK);

    display.setTextColor(tempColor);
    display.setTextDatum(BL_DATUM);
    display.drawString(tBuf, numStartX, barY + barH);

    int cStartX = numStartX + tempTextW + 1;

    drawAACircle(display, cStartX + 3, barY + barH - 10, 2, tempColor);
    display.drawString("c", cStartX + 6, barY + barH);
    drawDebugBox(display, barX - 2, barY - 2, barW + 4 + 6 + totalW, barH + 4);
  }

  // Right Sidebar: Fuel
  static unsigned long lastSideFuelUpdate = 0;
  bool fuelMoving = (now - easeFuelStart) < SIDEBAR_EASE_MS;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_SIDEBAR_FUEL &&
      (((currentFuel != lastFuelPct || fuelMoving) && (fuelMoving || REFRESH_SIDEBAR_FUEL_MS == 0 || now - lastSideFuelUpdate >= (unsigned long)REFRESH_SIDEBAR_FUEL_MS)) || forceDraw))) {
    lastSideFuelUpdate = now;
    lastFuelPct = currentFuel;

    int barX = SIDEBAR_RIGHT_X, barY = SIDEBAR_RIGHT_Y, barW = SIDEBAR_BAR_WIDTH, barH = SIDEBAR_BAR_HEIGHT;

    uint16_t fuelColor;
    if (currentFuel >= 100) {
      fuelColor = c_fuel_norm;
    } else if (currentFuel >= FUEL_WARN_YEL) {
      float range = (float)(100 - FUEL_WARN_YEL);
      float t = (range > 0) ? (100 - currentFuel) / range : 1.0f;
      fuelColor = blendColorLinear(c_fuel_norm, c_fuel_warn, t);
    } else if (currentFuel >= FUEL_WARN_RED) {
      float range = (float)(FUEL_WARN_YEL - FUEL_WARN_RED);
      float t = (range > 0) ? (FUEL_WARN_YEL - currentFuel) / range : 1.0f;
      fuelColor = blendColorLinear(c_fuel_warn, c_fuel_crit, t);
    } else {
      fuelColor = c_fuel_crit;
    }

    drawAARoundRect(display, barX - 2, barY - 2, barW + 4, barH + 4, 4, fuelColor);

    int fillH = (int)(barH * animFuelFill);
    if (fillH > barH)
      fillH = barH;
    if (fillH < 0)
      fillH = 0;
    if (fillH < lastFuelFillH) {
      display.fillRect(barX, barY + barH - lastFuelFillH, barW, lastFuelFillH - fillH, TFT_BLACK);
    }
    lastFuelFillH = fillH;
    if (fillH > 0) {
      fillAARoundRect(display, barX, barY + barH - fillH, barW, fillH, 0,
                      fuelColor, TFT_BLACK, TFT_BLACK);
    }

    char fBuf[10];
    snprintf(fBuf, sizeof(fBuf), "%d%%", currentFuel);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.setTextColor(fuelColor, TFT_BLACK);

    display.setTextDatum(BR_DATUM);
    display.setTextPadding(48);
    display.drawString(fBuf, barX - 5, barY + barH);
    display.setTextPadding(0);
    int fuelTxtW = display.textWidth(fBuf);
    drawDebugBox(display, barX - 5 - fuelTxtW - 2, barY - 2, barW + 9 + fuelTxtW, barH + 4);
  }

  int displaySat = displaySnap.satellites;
  float displayBat = displaySnap.batteryVoltage;
  float displayTmr = displaySnap.accelResultTime;
  TimerState displayAccelState = displaySnap.accelState;
  static int w_sat_max = 0, w_bat_max = 0, w_badge_max = 0, w_tmr_max = 0;
  static uint16_t h_sat_max = 0, h_bat_max = 0, h_tmr_max = 0,
                  h_badge_max = 0, w_bat_num_max = 0;
  static bool bottomRowLayoutInit = false;

  if (layoutReset) bottomRowLayoutInit = false;
  if (!bottomRowLayoutInit) {
    bottomRowLayoutInit = true;
    int16_t bx1, by1;
    uint16_t bw, bh;
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    int satCells = SAT_DIGITS;
    char satPat[16];
    for (int i = 0; i < satCells; i++) satPat[i] = '8';
    satPat[satCells] = 0;
    display.getTextBounds(satPat, 0, 0, &bx1, &by1, &bw, &bh);
    w_sat_max = 16 + 6 + bw;
    h_sat_max = (bh > 15) ? bh : 15;

    int batCells = BAT_INT_DIGITS + 1 + BAT_DEC_DIGITS;
    char batPat[16];
    for (int i = 0; i < batCells; i++) batPat[i] = (i == BAT_INT_DIGITS) ? '.' : '8';
    batPat[batCells] = 0;
    display.getTextBounds(batPat, 0, 0, &bx1, &by1, &bw, &bh);
    w_bat_num_max = bw;
    uint16_t w_v_unit, h_v_unit;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.getTextBounds(" V", 0, 0, &bx1, &by1, &w_v_unit, &h_v_unit);
    w_bat_max = 14 + w_bat_num_max + w_v_unit;
    h_bat_max = (bh > 18) ? bh : 18;
    if (h_v_unit > h_bat_max)
      h_bat_max = h_v_unit;
    uint16_t bw1, bh1;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.getTextBounds(ACCEL_BADGE_LINE1.c_str(), 0, 0, &bx1, &by1, &bw1,
                          &bh1);
    int iconSize = 16;
    w_badge_max = ((bw1 + 8) > iconSize) ? (bw1 + 8) : iconSize;
    h_badge_max = iconSize + 2 + bh1 + 2;
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");

    int tmrCells = TMR_INT_DIGITS + 1 + TMR_DEC_DIGITS;
    char tmrPat[16];
    for (int i = 0; i < tmrCells; i++) tmrPat[i] = (i == TMR_INT_DIGITS) ? '.' : '8';
    tmrPat[tmrCells] = 0;
    display.getTextBounds(tmrPat, 0, 0, &bx1, &by1, &bw, &bh);
    uint16_t w_s_unit, h_s_unit;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.getTextBounds(" S", 0, 0, &bx1, &by1, &w_s_unit, &h_s_unit);
    w_tmr_max = bw + w_s_unit;
    h_tmr_max = (bh > h_s_unit) ? bh : h_s_unit;
  }

  static int lastSatX = -1, lastSatY = -1;
  static int lastTmrX = -1, lastTmrY = -1;
  static int lastBatX = -1, lastBatY = -1;

  int satX = applyAlign(BIG_CENTER_X + OFFSET_BIG_SAT_X, w_sat_max, ALIGN_BIG_SAT);
  int satY = BIG_CENTER_Y + OFFSET_BIG_SAT_Y;
  int tmrTotalW = w_badge_max + 5 + w_tmr_max;
  int badgeX = applyAlign(BIG_CENTER_X + OFFSET_BIG_TMR_X, tmrTotalW, ALIGN_BIG_TMR);
  int tmrX = badgeX + w_badge_max + 5;
  int tmrY = BIG_CENTER_Y + OFFSET_BIG_TMR_Y;
  int batX = applyAlign(BIG_CENTER_X + OFFSET_BIG_BAT_X, w_bat_max, ALIGN_BIG_BAT);
  int batY = BIG_CENTER_Y + OFFSET_BIG_BAT_Y;

  bool forceDrawSat = forceDraw, forceDrawTmr = forceDraw,
       forceDrawBat = forceDraw;

  if (lastSatX >= 0 && lastSatY >= 0 &&
      (lastSatX != satX || lastSatY != satY)) {
    display.fillRect(lastSatX - 1, lastSatY - 1, w_sat_max + 2, h_sat_max + 2,
                     TFT_BLACK);
    forceDrawSat = true;
  }
  lastSatX = satX;
  lastSatY = satY;

  if (lastTmrX >= 0 && lastTmrY >= 0 &&
      (lastTmrX != badgeX || lastTmrY != tmrY)) {
    display.fillRect(lastTmrX - 1, lastTmrY - 2, tmrTotalW + 2,
                     ((h_badge_max > h_tmr_max) ? h_badge_max : h_tmr_max) + 3, TFT_BLACK);
    forceDrawTmr = true;
  }
  lastTmrX = badgeX;
  lastTmrY = tmrY;

  if (lastBatX >= 0 && lastBatY >= 0 &&
      (lastBatX != batX || lastBatY != batY)) {
    display.fillRect(lastBatX - 1, lastBatY - 1, w_bat_max + 2, h_bat_max + 3,
                     TFT_BLACK);
    forceDrawBat = true;
  }
  lastBatX = batX;
  lastBatY = batY;

  tAfterSb = millis();

  // -- Satellite count --
  static int satCells[MAX_CELLS] = {0};
  static int satCellW = 0, satCellsCount = 0;
  if (layoutReset) satCellsCount = 0;
  if (satCellsCount == 0) {
    satCellsCount = SAT_DIGITS;
    measureDs15Cells(satCells, satCellW, satCellsCount, -1);
  }
  static unsigned long lastSatUpdate = 0;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_SAT &&
      ((displaySat != lastSat && (REFRESH_SAT_MS == 0 || now - lastSatUpdate >= (unsigned long)REFRESH_SAT_MS)) || forceDrawSat))) {
    lastSatUpdate = now;
    lastSat = displaySat;
    componentUpdated = true;
    char satStr[8];
    snprintf(satStr, sizeof(satStr), "%d", displaySat);
    int len = strlen(satStr);
    int satClearTop = satY + ds15_refY1 - 2;
    int satClearH = h_sat_max + 4;
    display.fillRect(satX - 1, satClearTop, w_sat_max + 2, satClearH,
                     TFT_BLACK);
    int iconCY = satY + ds15_refY1 + (h_sat_max / 2);
    drawLocationIcon(satX, iconCY - 7, TFT_WHITE);
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");

    display.setTextColor(ghost_color);
    for (int ci = 0; ci < satCellsCount; ci++) {
      int cellRight = satX + 22 + satCells[ci];
      int cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      display.setCursor(cx, satY);
      display.print('8');
    }
    display.setTextColor(TFT_WHITE);
    for (int i = 0; i < len; i++) {
      int d = satStr[i] - '0';
      int cellIdx = (satCellsCount - len) + i;
      int cellRight = satX + 22 + satCells[cellIdx];
      int cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      display.setCursor(cx, satY);
      display.print(satStr[i]);
    }
    drawDebugBox(display, satX - 1, satClearTop, w_sat_max + 2, satClearH);
  }

  // -- Accel badge --
  static unsigned long lastBadgeUpdate = 0;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_TMR &&
      ((displayAccelState != lastState && (REFRESH_TMR_MS == 0 || now - lastBadgeUpdate >= (unsigned long)REFRESH_TMR_MS)) || forceDrawTmr))) {
    lastBadgeUpdate = now;
    lastState = displayAccelState;
    componentUpdated = true;
    uint16_t timerColor =
        (displayAccelState == RUNNING)
            ? TFT_YELLOW
            : ((displayAccelState == FINISHED) ? TFT_GREEN : TFT_WHITE);
    int16_t bx1_b, by1_b;
    uint16_t bw1_b, bh1_b;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.getTextBounds(ACCEL_BADGE_LINE1.c_str(), 0, 0, &bx1_b, &by1_b,
                          &bw1_b, &bh1_b);
    int badgeClearH = ((19 + by1_b + bh1_b) > 17) ? (19 + by1_b + bh1_b) : 17;
    display.fillRect(badgeX - 1, tmrY - 2, w_badge_max + 2, badgeClearH + 4, TFT_BLACK);
    int iconX = badgeX + (w_badge_max - 16) / 2;
    drawStopwatchIcon(iconX, tmrY + 1, timerColor);
    display.setTextColor(timerColor);
    display.setCursor(badgeX + (w_badge_max - bw1_b) / 2 - bx1_b,
                      tmrY + 19 - by1_b);
    display.print(ACCEL_BADGE_LINE1);
  }

  // -- Timer --
  static int tmrCells[MAX_CELLS] = {0};
  static int tmrCellW = 0, tmrCellsCount = 0;
  if (layoutReset) tmrCellsCount = 0;
  if (tmrCellsCount == 0) {
    tmrCellsCount = TMR_INT_DIGITS + 1 + TMR_DEC_DIGITS;
    measureDs15Cells(tmrCells, tmrCellW, tmrCellsCount, TMR_INT_DIGITS);
  }
  char tmrStr[12];
  snprintf(tmrStr, sizeof(tmrStr), "%.*f", TMR_DEC_DIGITS, displayTmr);
  static char lastTmrStr[12] = "";
  static TimerState lastStateTimer = READY;
  static unsigned long lastTmrUpdate = 0;
  bool tmrChanged = strcmp(tmrStr, lastTmrStr) != 0 || displayAccelState != lastStateTimer;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_TMR &&
      ((tmrChanged && (REFRESH_TMR_MS == 0 || now - lastTmrUpdate >= (unsigned long)REFRESH_TMR_MS)) || forceDrawTmr))) {
    lastTmrUpdate = now;
    strcpy(lastTmrStr, tmrStr);
    lastStateTimer = displayAccelState;
    componentUpdated = true;
    uint16_t timerColor =
        (displayAccelState == RUNNING)
            ? TFT_YELLOW
            : ((displayAccelState == FINISHED) ? TFT_GREEN : TFT_WHITE);
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.fillRect(tmrX - 1, tmrY - 1, w_tmr_max + 2, h_tmr_max + 2,
                     TFT_BLACK);

    display.setTextColor(ghost_color);
    for (int ci = 0; ci < tmrCellsCount; ci++) {
      int cellRight = tmrX + tmrCells[ci];
      char gc = (ci == TMR_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, tmrY - ds15_refY1);
      display.print(gc);
    }
    int len = strlen(tmrStr);
    display.setTextColor(timerColor);
    for (int i = 0; i < len; i++) {
      char c = tmrStr[i];
      int cellIdx = (tmrCellsCount - len) + i;
      int cellRight = tmrX + tmrCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, tmrY - ds15_refY1);
      display.print(c);
    }
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    int sRight = tmrX + tmrCells[tmrCellsCount - 1];
    int sCx = sRight - 2;
    display.setCursor(sCx, tmrY - ds15_refY1);
    display.print(" S");
    drawDebugBox(display, badgeX - 1, tmrY - 2, tmrTotalW + 2, ((h_badge_max > h_tmr_max) ? h_badge_max : h_tmr_max) + 3);
  }

  // -- Battery voltage --
  static int batCells[MAX_CELLS] = {0};
  static int batCellW = 0, batCellsCount = 0;
  if (layoutReset) batCellsCount = 0;
  if (batCellsCount == 0) {
    batCellsCount = BAT_INT_DIGITS + 1 + BAT_DEC_DIGITS;
    measureDs15Cells(batCells, batCellW, batCellsCount, BAT_INT_DIGITS);
  }
  char batStr[12];
  snprintf(batStr, sizeof(batStr), "%.*f", BAT_DEC_DIGITS, displayBat);
  static char lastBatStr[12] = "";
  static unsigned long lastBatUpdate = 0;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_BAT &&
      ((strcmp(batStr, lastBatStr) != 0 && (REFRESH_BAT_MS == 0 || now - lastBatUpdate >= (unsigned long)REFRESH_BAT_MS)) || forceDrawBat))) {
    lastBatUpdate = now;
    strcpy(lastBatStr, batStr);
    componentUpdated = true;
    uint16_t batColor = TFT_YELLOW;
    if (displayBat > 0.5f && displayBat < 11.8f)
      batColor = TFT_RED;
    else if (displayBat >= 13.1f)
      batColor = TFT_GREEN;
    else if (displayBat <= 0.5f)
      batColor = TFT_WHITE;
    int batClearTop = batY + ds15_refY1 - 3;
    int batClearH = h_bat_max + 6;
    display.fillRect(batX, batClearTop, w_bat_max, batClearH,
                     TFT_BLACK);
    int iconCY = batY + ds15_refY1 + (h_bat_max / 2);
    drawBatteryIcon(batX, iconCY - 9, displayBat, batColor);
    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");

    display.setTextColor(ghost_color);
    for (int ci = 0; ci < batCellsCount; ci++) {
      int cellRight = (batX + 14) + batCells[ci];
      char gc = (ci == BAT_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, batY);
      display.print(gc);
    }
    int len = strlen(batStr);
    display.setTextColor(batColor);
    for (int i = 0; i < len; i++) {
      char c = batStr[i];
      int cellIdx = (batCellsCount - len) + i;
      int cellRight = (batX + 14) + batCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, batY);
      display.print(c);
    }
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.setTextColor(batColor);
    display.print(" V");
    drawDebugBox(display, batX, batClearTop, w_bat_max, batClearH);
  }

  // --- Instant KM/L ---
  float displayInstKml = displaySnap.instantKml;
  static float lastDispInstKml = -1.0f;
  static unsigned long lastInstUpdate = 0;
  bool instChanged = fabsf(displayInstKml - lastDispInstKml) >= 0.1f;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_INST_KML &&
      ((instChanged && (REFRESH_INST_MS == 0 || now - lastInstUpdate >= (unsigned long)REFRESH_INST_MS)) || forceDraw))) {
    lastInstUpdate = now;
    lastDispInstKml = displayInstKml;
    componentUpdated = true;

    int instCenterX = BIG_CENTER_X + OFFSET_INST_KML_X;
    int instY = BIG_CENTER_Y + OFFSET_INST_KML_Y;

    static uint16_t badgeW_ist = 0, badgeH_ist = 0;
    static int16_t badgeBx_ist = 0, badgeBy_ist = 0;
    static uint16_t w_kml_unit = 0;
    static bool instLayoutInit = false;

    if (!instLayoutInit) {
      instLayoutInit = true;
      int16_t tl1, tl2;
      uint16_t th1, bw, bh;
      display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
      display.getTextBounds("88.8", 0, 0, &tl1, &tl2, &bw, &th1);
      ds15_fontH = th1;
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
      display.getTextBounds("KM/L", 0, 0, &tl1, &tl2, &w_kml_unit, &th1);
      display.getTextBounds("IST", 0, 0, &badgeBx_ist, &badgeBy_ist, &bw, &bh);
      badgeW_ist = bw + 6;
      badgeH_ist = bh + 4;
    }
    static int instCells[MAX_CELLS] = {0};
    static int instCellW = 0, instCellsCount = 0;
    if (instCellsCount == 0) {
      instCellsCount = INST_INT_DIGITS + 1 + INST_DEC_DIGITS;
      measureDs15Cells(instCells, instCellW, instCellsCount, INST_INT_DIGITS);
    }

    char instStr[12];
    if (displayInstKml > 0.0f)
      snprintf(instStr, sizeof(instStr), "%.*f", INST_DEC_DIGITS, displayInstKml);
    else
      snprintf(instStr, sizeof(instStr), "0.0");

    uint16_t w_right_ist = (badgeW_ist > w_kml_unit) ? badgeW_ist : w_kml_unit;
    int currentInstWidth = instCellW + 4 + w_right_ist;
    int instNumAreaX = applyAlign(instCenterX, currentInstWidth, ALIGN_INST_KML);

    int clearInstH = ds15_fontH + 8;
    int clearInstW = instCellW + 4 + w_right_ist;
    int clearInstX = instCenterX - (clearInstW / 2) - 4;
    display.fillRect(clearInstX, instY - clearInstH + 4, clearInstW + 8, clearInstH,
                     TFT_BLACK);

    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.setTextColor(ghost_color);
    for (int ci = 0; ci < instCellsCount; ci++) {
      int cellRight = instNumAreaX + instCells[ci];
      char gc = (ci == INST_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, instY);
      display.print(gc);
    }
    int instLen = strlen(instStr);
    display.setTextColor(TFT_CYAN);
    for (int i = 0; i < instLen; i++) {
      char c = instStr[i];
      int cellIdx = (instCellsCount - instLen) + i;
      int cellRight = instNumAreaX + instCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, instY);
      display.print(c);
    }

    int rightColInstX = instNumAreaX + instCellW + 4;
    int instBadgeX = rightColInstX + (w_right_ist - badgeW_ist) / 2;
    int instBadgeY = instY - 18;
    drawAARoundRect(display, instBadgeX, instBadgeY, badgeW_ist, badgeH_ist, 2,
                    TFT_CYAN);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_CYAN);
    display.setCursor(instBadgeX + 3 - badgeBx_ist,
                      instBadgeY - badgeBy_ist + 2);
    display.print("IST");

    int kmlInstX = rightColInstX + (w_right_ist - w_kml_unit) / 2;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_CYAN);
    display.setCursor(kmlInstX, instY - 1);
    display.print("KM/L");

    if (SHOW_ELEMENT_BOUNDS) {
      int instTop = instY + std::min((int)ds15_refY1, -18);
      int instBottom = instY + (int)ds15_refY1 + (int)ds15_fontH;
      drawDebugBox(display, instNumAreaX - 2, instTop - 2, currentInstWidth + 4,
                   instBottom - instTop + 4);
    }
  }

  // --- Average KM/L ---
  float displayAvgKml = displaySnap.averageKml;
  static float lastDispAvgKml = -1.0f;
  static unsigned long lastAvgUpdate = 0;
  bool avgChanged = fabsf(displayAvgKml - lastDispAvgKml) >= 0.1f;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_AVG_KML &&
      ((avgChanged && (REFRESH_AVG_MS == 0 || now - lastAvgUpdate >= (unsigned long)REFRESH_AVG_MS)) || forceDraw))) {
    lastAvgUpdate = now;
    lastDispAvgKml = displayAvgKml;
    componentUpdated = true;

    int avgCenterX = BIG_CENTER_X + OFFSET_AVG_KML_X;
    int avgY = BIG_CENTER_Y + OFFSET_AVG_KML_Y;

    static uint16_t badgeW_avg = 0, badgeH_avg = 0;
    static int16_t badgeBx_avg = 0, badgeBy_avg = 0;
    static uint16_t w_kml_unit_avg = 0;
    static bool avgLayoutInit = false;

    if (!avgLayoutInit) {
      avgLayoutInit = true;
      uint16_t bw, bh;
      int16_t tl1, tl2;
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
      display.getTextBounds("KM/L", 0, 0, &tl1, &tl2, &w_kml_unit_avg, &bh);
      display.getTextBounds("AVG", 0, 0, &badgeBx_avg, &badgeBy_avg, &bw, &bh);
      badgeW_avg = bw + 6;
      badgeH_avg = bh + 4;
    }
    static int avgCells[MAX_CELLS] = {0};
    static int avgCellW = 0, avgCellsCount = 0;
    if (avgCellsCount == 0) {
      avgCellsCount = AVG_INT_DIGITS + 1 + AVG_DEC_DIGITS;
      measureDs15Cells(avgCells, avgCellW, avgCellsCount, AVG_INT_DIGITS);
    }

    char avgStr[12];
    if (displayAvgKml > 0.0f)
      snprintf(avgStr, sizeof(avgStr), "%.*f", AVG_DEC_DIGITS, displayAvgKml);
    else
      snprintf(avgStr, sizeof(avgStr), "0.0");

    uint16_t w_right_avg = (badgeW_avg > w_kml_unit_avg) ? badgeW_avg : w_kml_unit_avg;
    int currentAvgWidth = avgCellW + 4 + w_right_avg;
    int avgNumAreaX = applyAlign(avgCenterX, currentAvgWidth, ALIGN_AVG_KML);

    int clearAvgH = ds15_fontH + 8;
    int clearAvgW = avgCellW + 4 + w_right_avg;
    int clearAvgX = avgCenterX - (clearAvgW / 2) - 4;
    display.fillRect(clearAvgX, avgY - clearAvgH + 4, clearAvgW + 8, clearAvgH, TFT_BLACK);

    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.setTextColor(ghost_color);
    for (int ci = 0; ci < avgCellsCount; ci++) {
      int cellRight = avgNumAreaX + avgCells[ci];
      char gc = (ci == AVG_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, avgY);
      display.print(gc);
    }
    int avgLen = strlen(avgStr);
    display.setTextColor(TFT_YELLOW);
    for (int i = 0; i < avgLen; i++) {
      char c = avgStr[i];
      int cellIdx = (avgCellsCount - avgLen) + i;
      int cellRight = avgNumAreaX + avgCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, avgY);
      display.print(c);
    }

    int rightColAvgX = avgNumAreaX + avgCellW + 4;
    int avgBadgeX = rightColAvgX + (w_right_avg - badgeW_avg) / 2;
    int avgBadgeY = avgY - 18;
    drawAARoundRect(display, avgBadgeX, avgBadgeY, badgeW_avg, badgeH_avg, 2,
                    TFT_YELLOW);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_YELLOW);
    display.setCursor(avgBadgeX + 3 - badgeBx_avg, avgBadgeY - badgeBy_avg + 2);
    display.print("AVG");

    int kmlAvgX = rightColAvgX + (w_right_avg - w_kml_unit_avg) / 2;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_YELLOW);
    display.setCursor(kmlAvgX, avgY - 1);
    display.print("KM/L");

    if (SHOW_ELEMENT_BOUNDS) {
      int avgTop = avgY + std::min((int)ds15_refY1, -18);
      int avgBottom = avgY + (int)ds15_refY1 + (int)ds15_fontH;
      drawDebugBox(display, avgNumAreaX - 2, avgTop - 2, currentAvgWidth + 4,
                   avgBottom - avgTop + 4);
    }
  }

  // --- Average Speed (3 int digits, no decimal) ---
  float displayAvgSpd = displaySnap.averageSpeed;
  static float lastDispAvgSpd = -1.0f;
  static unsigned long lastAvgSpdUpdate = 0;
  bool avgSpdChanged = fabsf(displayAvgSpd - lastDispAvgSpd) >= 1.0f;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_AVG_SPEED &&
      ((avgSpdChanged && (REFRESH_AVG_SPEED_MS == 0 || now - lastAvgSpdUpdate >= (unsigned long)REFRESH_AVG_SPEED_MS)) || forceDraw))) {
    lastAvgSpdUpdate = now;
    lastDispAvgSpd = displayAvgSpd;
    componentUpdated = true;

    int avgSpdCenterX = BIG_CENTER_X + OFFSET_AVG_SPEED_X;
    int avgSpdY = BIG_CENTER_Y + OFFSET_AVG_SPEED_Y;

    static uint16_t w_kmh_unit = 0;
    static uint16_t badgeW_avgSpd = 0, badgeH_avgSpd = 0;
    static int16_t badgeBx_avgSpd = 0, badgeBy_avgSpd = 0;
    static bool avgSpdLayoutInit = false;

    if (!avgSpdLayoutInit) {
      avgSpdLayoutInit = true;
      uint16_t bw, bh;
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
      display.getTextBounds("AVG", 0, 0, &badgeBx_avgSpd, &badgeBy_avgSpd, &bw, &bh);
      badgeW_avgSpd = bw + 6;
      badgeH_avgSpd = bh + 4;
      display.getTextBounds("KM/H", 0, 0, &badgeBx_avgSpd, &badgeBy_avgSpd, &w_kmh_unit, &bh);
    }
    static int avgSpdCells[MAX_CELLS] = {0};
    static int avgSpdCellW = 0, avgSpdCellsCount = 0;
    if (avgSpdCellsCount == 0) {
      avgSpdCellsCount = AVG_SPEED_INT_DIGITS + (AVG_SPEED_DEC_DIGITS > 0 ? 1 + AVG_SPEED_DEC_DIGITS : 0);
      measureDs15Cells(avgSpdCells, avgSpdCellW, avgSpdCellsCount, AVG_SPEED_DEC_DIGITS > 0 ? AVG_SPEED_INT_DIGITS : -1);
    }

    char avgSpdStr[12];
    if (displayAvgSpd > 0.0f) {
      if (AVG_SPEED_DEC_DIGITS > 0)
        snprintf(avgSpdStr, sizeof(avgSpdStr), "%.*f", AVG_SPEED_DEC_DIGITS, displayAvgSpd);
      else
        snprintf(avgSpdStr, sizeof(avgSpdStr), "%d", (int)displayAvgSpd);
    } else {
      snprintf(avgSpdStr, sizeof(avgSpdStr), "0");
    }

    uint16_t w_right_avgSpd = (badgeW_avgSpd > w_kmh_unit) ? badgeW_avgSpd : w_kmh_unit;
    int currentAvgSpdWidth = avgSpdCellW + 4 + w_right_avgSpd;
    int avgSpdNumAreaX = applyAlign(avgSpdCenterX, currentAvgSpdWidth, ALIGN_AVG_SPEED);

    int clearAvgSpdH = ds15_fontH + 8;
    int clearAvgSpdW = avgSpdCellW + 4 + w_right_avgSpd;
    int clearAvgSpdX = avgSpdCenterX - (clearAvgSpdW / 2) - 4;
    display.fillRect(clearAvgSpdX, avgSpdY - clearAvgSpdH + 4, clearAvgSpdW + 8, clearAvgSpdH, TFT_BLACK);

    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.setTextColor(ghost_color);
    for (int ci = 0; ci < avgSpdCellsCount; ci++) {
      int cellRight = avgSpdNumAreaX + avgSpdCells[ci];
      char gc = (AVG_SPEED_DEC_DIGITS > 0 && ci == AVG_SPEED_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, avgSpdY);
      display.print(gc);
    }
    int avgSpdLen = strlen(avgSpdStr);
    display.setTextColor(TFT_YELLOW);
    for (int i = 0; i < avgSpdLen; i++) {
      char c = avgSpdStr[i];
      int cellIdx = (avgSpdCellsCount - avgSpdLen) + i;
      int cellRight = avgSpdNumAreaX + avgSpdCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, avgSpdY);
      display.print(c);
    }

    int rightColAvgSpdX = avgSpdNumAreaX + avgSpdCellW + 4;
    int avgSpdBadgeX = rightColAvgSpdX + (w_right_avgSpd - badgeW_avgSpd) / 2;
    int avgSpdBadgeY = avgSpdY - 18;
    drawAARoundRect(display, avgSpdBadgeX, avgSpdBadgeY, badgeW_avgSpd, badgeH_avgSpd, 2, TFT_YELLOW);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_YELLOW);
    display.setCursor(avgSpdBadgeX + 3 - badgeBx_avgSpd, avgSpdBadgeY - badgeBy_avgSpd + 2);
    display.print("AVG");

    int kmhAvgX = rightColAvgSpdX + (w_right_avgSpd - w_kmh_unit) / 2;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.setTextColor(TFT_YELLOW);
    display.setCursor(kmhAvgX, avgSpdY - 1);
    display.print("KM/H");

    if (SHOW_ELEMENT_BOUNDS) {
      int avgSpdTop = avgSpdY + std::min((int)ds15_refY1, -18);
      int avgSpdBottom = avgSpdY + (int)ds15_refY1 + (int)ds15_fontH;
      drawDebugBox(display, avgSpdNumAreaX - 2, avgSpdTop - 2,
                   currentAvgSpdWidth + 4, avgSpdBottom - avgSpdTop + 4);
    }
  }

  // --- Fuel Liters (4 fixed cells: tens, ones, dot, tenths) ---
  float displayFuelLtrs = displaySnap.fuelLiters;
  static float lastDispFuelLtrs = -1.0f;
  static unsigned long lastFuelUpdate = 0;
  if (forceDraw || (IN_BAND_BUDGET && SHOW_ELEMENT_FUEL_LTRS &&
      ((fabsf(displayFuelLtrs - lastDispFuelLtrs) >= 0.1f && (REFRESH_FUEL_MS == 0 || now - lastFuelUpdate >= (unsigned long)REFRESH_FUEL_MS)) || forceDraw))) {
    lastFuelUpdate = now;
    lastDispFuelLtrs = displayFuelLtrs;
    componentUpdated = true;

    int fuelCenterX = BIG_CENTER_X + OFFSET_FUEL_LTRS_X;
    int fuelY = BIG_CENTER_Y + OFFSET_FUEL_LTRS_Y;

    static uint16_t w_fuel_unit = 0;
    static bool fuelLayoutInit = false;
    if (!fuelLayoutInit) {
      fuelLayoutInit = true;
      int16_t tfx1, tfx2;
      uint16_t tfth;
      display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
      display.getTextBounds("L", 0, 0, &tfx1, &tfx2, &w_fuel_unit, &tfth);
    }
    static int fuelCells[MAX_CELLS] = {0};
    static int fuelCellW = 0, fuelCellsCount = 0;
    if (fuelCellsCount == 0) {
      fuelCellsCount = FUEL_INT_DIGITS + 1 + FUEL_DEC_DIGITS;
      measureDs15Cells(fuelCells, fuelCellW, fuelCellsCount, FUEL_INT_DIGITS);
    }

    char fuelStr[12];
    snprintf(fuelStr, sizeof(fuelStr), "%.*f", FUEL_DEC_DIGITS, displayFuelLtrs);

    int totalW = fuelCellW + 4 + w_fuel_unit;
    int fuelNumAreaX = applyAlign(fuelCenterX, totalW, ALIGN_FUEL_LTRS);
    int unitX = fuelNumAreaX + fuelCellW + 4;

    int clearY = fuelY + std::min((int)ds15_refY1, -18) - 2;
    int clearW = fuelCellW + 4 + w_fuel_unit;
    int clearX = fuelCenterX - (clearW / 2) - 4;
    display.fillRect(clearX, clearY, clearW + 8, ds15_fontH + 12, TFT_BLACK);

    display.loadVLWFont("/Fonts/DS-DIGIT_28px.vlw");
    display.setTextColor(ghost_color);
    for (int ci = 0; ci < fuelCellsCount; ci++) {
      int cellRight = fuelNumAreaX + fuelCells[ci];
      char gc = (ci == FUEL_INT_DIGITS) ? '.' : '8';
      int cx;
      if (gc == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      }
      display.setCursor(cx, fuelY);
      display.print(gc);
    }
    int fuelLen = strlen(fuelStr);
    display.setTextColor(TFT_CYAN);
    for (int i = 0; i < fuelLen; i++) {
      char c = fuelStr[i];
      int cellIdx = (fuelCellsCount - fuelLen) + i;
      int cellRight = fuelNumAreaX + fuelCells[cellIdx];
      int cx;
      if (c == '.') {
        cx = cellRight - 2 - ds15_dotXOff - ds15_dotWidth;
      } else {
        int d = c - '0';
        cx = cellRight - 2 - ds15_digitXOff[d] - ds15_digitWidth[d];
      }
      display.setCursor(cx, fuelY);
      display.print(c);
    }
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.setCursor(unitX, fuelY - 1);
    display.setTextColor(TFT_CYAN);
    display.print("L");

    if (SHOW_ELEMENT_BOUNDS)
      drawDebugBox(display, fuelNumAreaX - 2, fuelY + (int)ds15_refY1 - 2, totalW + 4,
                   (int)ds15_fontH + 4);
  }

  tAfterMid = millis();

  // --- Compass (full-width HUD heading tape) ---
  float displayHeading = displaySnap.heading;
  static int lastTapePix = -1;
  static bool compassStaticDrawn = false;
  static bool lastHeadValid = false;
  static unsigned long lastCompassUpdate = 0;
  int currentHeadingInt = ((int)displayHeading + 360) % 360;

  int compassX = display.width() / 2;
  int compassY = BIG_CENTER_Y + OFFSET_COMPASS_Y;
  int tapeW = display.width();

  const float pxPerDeg = 6.0f;
  int tapePix = (int)roundf(displayHeading * pxPerDeg);

  // With the tape sprite active, render EVERY frame the heading moves: the
  // strip is painted in RAM and pushed in one DMA burst, so the tape rolls
  // smoothly (1-2 px/frame) instead of stepping every REFRESH_COMPASS_MS
  // (10 Hz updates made the labels visibly jump). The throttle still applies
  // to the direct-panel fallback, where per-pixel SPI writes would eat the
  // frame budget at 60 FPS.
  bool tapeMoved = tapePix != lastTapePix;
  bool tapeThrottleOk = (REFRESH_COMPASS_MS == 0 || tapeSpriteReady ||
                         (now - lastCompassUpdate >= (unsigned long)REFRESH_COMPASS_MS));
  // The tape is the one component that wants a redraw every frame. On frames
  // that are already over the band budget it drops to every-other-frame
  // (alternation) so it can't stack on top of odo/sidebars/middle-row draws.
  static bool tapeDeferredLast = false;
  bool tapeWants = (SHOW_ELEMENT_COMPASS && tapeMoved && tapeThrottleOk) || forceDraw;
  bool tapeBudgetOk = (millis() - tFrame0) < FRAME_DEFER_MS || !tapeDeferredLast;
  tapeDeferredLast = tapeWants && !tapeBudgetOk;

  // Glyph-top metrics for the tape label band, hoisted out of the draw block
  // so the "compass disabled" erase path below can reuse them.
  static int16_t labelY1 = 0;
  static int16_t cardY1 = 0;
  static bool labelMeasured = false;
  if (!labelMeasured) {
    labelMeasured = true;
    int16_t tlx, tly;
    uint16_t tlw, tlh;
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_10px.vlw");
    display.getTextBounds("0", 0, 0, &tlx, &labelY1, &tlw, &tlh);
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.getTextBounds("N", 0, 0, &tlx, &cardY1, &tlw, &tlh);
  }

  if (tapeWants && tapeBudgetOk) {
    lastCompassUpdate = now;
    lastTapePix = tapePix;
    componentUpdated = true;

    const int bandTop = compassY - 7;
    const int bandH = 10;

    // Numbers sit just above the band (cursor y = baseline)
    int labelBaseline = bandTop - 10;
    const int markerH = 8; // bottom marker triangle height
    int labelTop = std::min(labelBaseline + (int)labelY1,
                            std::min(labelBaseline + (int)cardY1, bandTop - 10)) - 2;
    int stripBottom = bandTop + bandH + markerH;

    bool headValid = compassReady || ENABLE_DEMO_MODE;
    uint16_t c_border = display.color565(90, 90, 90);
    uint16_t c_tickMin = display.color565(110, 110, 110);
    uint16_t c_tickMaj = display.color565(170, 170, 170);
    uint16_t c_marker = headValid ? TFT_RED : display.color565(70, 70, 70);

    // Edge fade: tape content blends toward black at the screen edges (HUD look)
    const int fadeW = 72;
    static uint16_t fadeBandColors[72];
    static uint16_t fadeBorderColors[72];
    static bool fadeInit = false;
    if (!fadeInit) {
      fadeInit = true;
      for (int i = 0; i < fadeW; i++) {
        float f = (float)(i + 1) / (float)fadeW;
        fadeBandColors[i] = blendColorWithBlack(display.color565(20, 20, 20), f);
        fadeBorderColors[i] = blendColorWithBlack(display.color565(90, 90, 90), f);
      }
    }

    // Static layer: the band, its edge fades and the center marker never
    // scroll, so draw them once instead of every update. Erasing the full
    // strip (erase-to-black + repaint) on every heading change was what made
    // the tape flash. Redrawn only on a full redraw or when the marker's
    // validity state flips (gray -> red).
    if (!compassStaticDrawn || forceDraw || headValid != lastHeadValid) {
      lastHeadValid = headValid;
      compassStaticDrawn = true;
      display.fillRect(0, labelTop, tapeW, stripBottom - labelTop + 2, TFT_BLACK);

      // Tape band (edge to edge, fixed reference frame), fading at the edges
      int midX0 = fadeW, midX1 = tapeW - fadeW;
      display.fillRect(midX0, bandTop, midX1 - midX0, bandH, display.color565(20, 20, 20));
      display.fillRect(midX0, bandTop, midX1 - midX0, 1, c_border);
      display.fillRect(midX0, bandTop + bandH - 1, midX1 - midX0, 1, c_border);
      for (int i = 0; i < fadeW; i += 6) {
        int w = std::min(6, fadeW - i);
        int fi = i + w / 2;
        display.fillRect(i, bandTop, w, bandH, fadeBandColors[fi]);
        display.fillRect(tapeW - i - w, bandTop, w, bandH, fadeBandColors[fi]);
        display.fillRect(i, bandTop, w, 1, fadeBorderColors[fi]);
        display.fillRect(i, bandTop + bandH - 1, w, 1, fadeBorderColors[fi]);
        display.fillRect(tapeW - i - w, bandTop, w, 1, fadeBorderColors[fi]);
        display.fillRect(tapeW - i - w, bandTop + bandH - 1, w, 1, fadeBorderColors[fi]);
      }

      // Fixed center marker: inverted triangle below the band, apex on the band
      // (kept clear of the numbers so it never covers the center heading)
      display.fillTriangle(compassX - 5, bandTop + bandH + markerH - 1,
                           compassX + 5, bandTop + bandH + markerH - 1,
                           compassX, bandTop + bandH, c_marker);
    }

    // Dynamic layer: only the label/tick region above the band scrolls. It is
    // painted into an offscreen sprite and pushed to the panel in one burst,
    // so the display never shows a partially repainted tape. Falls back to
    // drawing directly if the sprite buffer can't be allocated.
    int stripH = bandTop - labelTop;
    if (tapeSpriteReady && (tapeSpriteW != tapeW || tapeSpriteH != stripH)) {
      tapeSprite.deleteSprite();
      tapeSpriteReady = false;
    }
    if (!tapeSpriteReady && !memSaverActive && !otaMemReleaseRequested) {
      // NOTE: LGFX_Sprite::setColorDepth returns the sprite's buffer pointer
      // (nullptr until createSprite allocates one), NOT a bool. It must be
      // called as a statement - inside a bool expression the null return
      // short-circuits && and createSprite would never run.
      // 8-bit RGB332 primary (halves RAM vs 16-bit; ticks/labels are
      // grayscale so the palette loss is invisible), 4-bit as a last resort.
      tapeSprite.setColorDepth(8);
      tapeSpriteReady = tapeSprite.createSprite(tapeW, stripH);
      if (!tapeSpriteReady) {
        tapeSprite.setColorDepth(4);
        tapeSpriteReady = tapeSprite.createSprite(tapeW, stripH);
      }
      if (tapeSpriteReady)
        tapeSprite.setTextDatum(lgfx::textdatum_t::baseline_left);
      tapeSpriteW = tapeW;
      tapeSpriteH = stripH;
      logPrintf("Compass tape sprite %s (%dx%d, depth=%d, heap=%lu)\n",
                tapeSpriteReady ? "OK" : "FAILED (fallback: direct panel)",
                tapeW, stripH,
                tapeSpriteReady ? (int)tapeSprite.getColorDepth() : 0,
                (unsigned long)ESP.getFreeHeap());
    }

    // Tape scrolls with the fractional heading so ticks/numbers glide smoothly
    int nearest10 = (currentHeadingInt + 5) / 10 * 10;
    float refDeg = (float)nearest10;

    if (tapeSpriteReady) {
      renderTapeStrip(tapeSprite, tapeW, compassX, 0, stripH, refDeg,
                      displayHeading, pxPerDeg, c_tickMin, c_tickMaj, fadeW,
                      headValid);
      tapeSprite.pushSprite(0, labelTop);
    } else {
      renderTapeStrip(display, tapeW, compassX, labelTop, stripH, refDeg,
                      displayHeading, pxPerDeg, c_tickMin, c_tickMaj, fadeW,
                      headValid);
    }

    drawDebugBox(display, 0, labelTop, tapeW, stripBottom - labelTop + 2);
  }

  // When the compass element is toggled off live, erase the tape band it left
  // on screen (the draw block above never runs in that state).
  if (!SHOW_ELEMENT_COMPASS && compassStaticDrawn) {
    compassStaticDrawn = false;
    lastTapePix = -1;
    lastCompassUpdate = 0;
    int eCompassY = BIG_CENTER_Y + OFFSET_COMPASS_Y;
    int eBandTop = eCompassY - 7;
    int eLabelTop = std::min(eBandTop - 10 + (int)labelY1,
                             std::min(eBandTop - 10 + (int)cardY1, eBandTop - 10)) - 2;
    int eStripBottom = eBandTop + 10 + 8;
    display.fillRect(0, eLabelTop, display.width(), eStripBottom - eLabelTop + 2,
                     TFT_BLACK);
  }
  tAfterCompass = millis();

  // ----------------------------------------------------------------------------
  // Weather Widget Rendering
  // ----------------------------------------------------------------------------
  static int lastWx = -999;
  static int lastWy = -999;
  static bool lastWeatherShow = false;
  static float lastTemp = -999.0f;
  static int lastHum = -1;
  static int lastCode = -1;
  static String lastSunset = "";
  static String lastCity = "";

  int wx = BIG_CENTER_X + OFFSET_WEATHER_X - 240;
  int wy = BIG_CENTER_Y + OFFSET_WEATHER_Y - 14;
  bool showWeather = SHOW_ELEMENT_WEATHER;

  if ((lastWx != wx || lastWy != wy || lastWeatherShow != showWeather || forceDraw) && lastWeatherShow) {
    display.fillRect(lastWx - 2, lastWy - 2, 480 + 4, 28 + 4, TFT_BLACK);
  }

  bool weatherChanged = (g_weatherData.temperature != lastTemp ||
                         g_weatherData.humidity != lastHum ||
                         g_weatherData.weatherCode != lastCode ||
                         g_weatherData.sunsetTime != lastSunset ||
                         WEATHER_CITY != lastCity);

  if (showWeather) {
    if (weatherChanged || lastWx != wx || lastWy != wy || lastWeatherShow != showWeather || forceDraw) {
      lastTemp = g_weatherData.temperature;
      lastHum = g_weatherData.humidity;
      lastCode = g_weatherData.weatherCode;
      lastSunset = g_weatherData.sunsetTime;
      lastCity = WEATHER_CITY;
      lastWx = wx;
      lastWy = wy;
      lastWeatherShow = showWeather;
      
      void drawWeatherWidget(int x, int y, const SensorSnapshot &snap, bool forceDraw);
      drawWeatherWidget(wx, wy, displaySnap, forceDraw);
    }
  } else {
    lastWeatherShow = false;
  }
  tAfterWeather = millis();

  finishFrame();
}

void checkNightMode(const SensorSnapshot &snap) {
  static bool isNightModeActive = false, firstCheck = true;
  if (snap.timeValid && ENABLE_NIGHT_MODE) {
    bool shouldBeNightMode = (snap.localHour >= NIGHT_MODE_START_HOUR ||
                              snap.localHour < NIGHT_MODE_END_HOUR);
    if (shouldBeNightMode != isNightModeActive || firstCheck) {
      isNightModeActive = shouldBeNightMode;
      firstCheck = false;
      int level = isNightModeActive
                      ? 75
                      : (BACKLIGHT_BRIGHTNESS * 255) / 100;
      ledcWrite(BACKLIGHT_CHANNEL, level);
    }
  }
}

// ----------------------------------------------------------------------------
// Weather Widget Render Helpers & Implementation
// ----------------------------------------------------------------------------
static const char* getWindCardinal(float degrees) {
  if (degrees < 22.5f || degrees >= 337.5f) return "N";
  if (degrees < 67.5f) return "NE";
  if (degrees < 112.5f) return "E";
  if (degrees < 157.5f) return "SE";
  if (degrees < 202.5f) return "S";
  if (degrees < 247.5f) return "SW";
  if (degrees < 292.5f) return "W";
  return "NW";
}

void drawWeatherIcon(int cx, int cy, int size, int weatherCode, bool isNight) {
  int iconType = 0; // 0: Sun/Moon, 1: Cloud, 2: Rain, 3: Storm, 4: Snow, 5: Fog
  switch (weatherCode) {
    case 0: iconType = 0; break;
    case 1: case 2: case 3: iconType = 1; break;
    case 45: case 48: iconType = 5; break;
    case 51: case 53: case 55:
    case 61: case 63: case 65:
    case 80: case 81: case 82: iconType = 2; break;
    case 95: case 96: case 99: iconType = 3; break;
    default: iconType = 4; break;
  }
  
  if (iconType == 0) {
    if (isNight) {
      display.fillCircle(cx + 2, cy - 2, size * 0.4, TFT_WHITE);
      display.fillCircle(cx - 2, cy - 4, size * 0.35, TFT_BLACK);
    } else {
      display.fillCircle(cx, cy, size * 0.35, TFT_YELLOW);
      for (int angle = 0; angle < 360; angle += 45) {
        float rad = angle * DEG_TO_RAD;
        int x0 = cx + cos(rad) * (size * 0.42);
        int y0 = cy + sin(rad) * (size * 0.42);
        int x1 = cx + cos(rad) * (size * 0.58);
        int y1 = cy + sin(rad) * (size * 0.58);
        display.drawLine(x0, y0, x1, y1, TFT_YELLOW);
      }
    }
  } else if (iconType == 1) {
    uint16_t cloudColor = display.color565(200, 200, 200);
    int r_main = size * 0.28;
    int r_left = size * 0.2;
    int r_right = size * 0.18;
    display.fillCircle(cx, cy - size * 0.05, r_main, cloudColor);
    display.fillCircle(cx - size * 0.2, cy + size * 0.08, r_left, cloudColor);
    display.fillCircle(cx + size * 0.2, cy + size * 0.08, r_right, cloudColor);
    display.fillRect(cx - size * 0.2, cy + size * 0.08, size * 0.4, size * 0.12, cloudColor);
  } else if (iconType == 2) {
    uint16_t cloudColor = display.color565(120, 130, 140);
    int r_main = size * 0.24;
    int r_left = size * 0.18;
    int r_right = size * 0.16;
    int c_cy = cy - size * 0.08;
    display.fillCircle(cx, c_cy - size * 0.05, r_main, cloudColor);
    display.fillCircle(cx - size * 0.18, c_cy + size * 0.08, r_left, cloudColor);
    display.fillCircle(cx + size * 0.18, c_cy + size * 0.08, r_right, cloudColor);
    display.fillRect(cx - size * 0.18, c_cy + size * 0.08, size * 0.36, size * 0.12, cloudColor);
    
    uint16_t rainColor = display.color565(100, 180, 255);
    for (int i = -1; i <= 1; i++) {
      int rx = cx + i * (size * 0.18);
      int ry = cy + size * 0.16;
      display.drawLine(rx, ry, rx - 3, ry + 8, rainColor);
    }
  } else if (iconType == 3) {
    uint16_t cloudColor = display.color565(100, 110, 120);
    int r_main = size * 0.24;
    int r_left = size * 0.18;
    int r_right = size * 0.16;
    int c_cy = cy - size * 0.08;
    display.fillCircle(cx, c_cy - size * 0.05, r_main, cloudColor);
    display.fillCircle(cx - size * 0.18, c_cy + size * 0.08, r_left, cloudColor);
    display.fillCircle(cx + size * 0.18, c_cy + size * 0.08, r_right, cloudColor);
    display.fillRect(cx - size * 0.18, c_cy + size * 0.08, size * 0.36, size * 0.12, cloudColor);
    
    uint16_t rainColor = display.color565(100, 180, 255);
    display.drawLine(cx - size * 0.18, cy + size * 0.16, cx - size * 0.18 - 2, cy + size * 0.16 + 6, rainColor);
    display.drawLine(cx + size * 0.18, cy + size * 0.16, cx + size * 0.18 - 2, cy + size * 0.16 + 6, rainColor);

    uint16_t boltColor = TFT_YELLOW;
    int lx = cx - 2;
    int ly = cy + size * 0.08;
    display.drawLine(lx + 2, ly, lx - 4, ly + 8, boltColor);
    display.drawLine(lx - 4, ly + 8, lx + 1, ly + 8, boltColor);
    display.drawLine(lx + 1, ly + 8, lx - 5, ly + 16, boltColor);
  } else if (iconType == 4) {
    uint16_t cloudColor = display.color565(180, 190, 200);
    int r_main = size * 0.24;
    int r_left = size * 0.18;
    int r_right = size * 0.16;
    int c_cy = cy - size * 0.08;
    display.fillCircle(cx, c_cy - size * 0.05, r_main, cloudColor);
    display.fillCircle(cx - size * 0.18, c_cy + size * 0.08, r_left, cloudColor);
    display.fillCircle(cx + size * 0.18, c_cy + size * 0.08, r_right, cloudColor);
    display.fillRect(cx - size * 0.18, c_cy + size * 0.08, size * 0.36, size * 0.12, cloudColor);
    
    for (int i = -1; i <= 1; i++) {
      int sx = cx + i * (size * 0.18);
      int sy = cy + size * 0.18;
      display.fillCircle(sx, sy, 1.5, TFT_WHITE);
    }
  } else {
    uint16_t fogColor = display.color565(170, 180, 190);
    for (int i = -1; i <= 1; i++) {
      int fy = cy + i * (size * 0.16);
      int fw = size * (0.8 - abs(i) * 0.15);
      display.fillRect(cx - fw / 2, fy, fw, 2, fogColor);
    }
  }
}

void drawWeatherWidget(int wx, int wy, const SensorSnapshot &snap, bool forceDraw) {
  int w = 480;
  int h = 28;
  
  uint16_t cardBg = display.color565(15, 15, 15);
  uint16_t borderCol = display.color565(45, 45, 45);
  
  fillAARoundRect(display, wx, wy, w, h, 4, cardBg);
  drawAARoundRect(display, wx, wy, w, h, 4, borderCol);
  
  if (!g_weatherData.valid) {
    display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
    display.setTextColor(display.color565(150, 150, 150));
    display.setCursor(wx + 20, wy + 20);
    display.print("Weather Offline (Syncing with WiFi...)");
    return;
  }
  
  bool isNight = snap.timeValid && (snap.localHour >= NIGHT_MODE_START_HOUR || snap.localHour < NIGHT_MODE_END_HOUR);
  
  // Section 1: Weather Icon and City Name
  drawWeatherIcon(wx + 16, wy + 14, 16, g_weatherData.weatherCode, isNight);
  
  display.loadVLWFont("/Fonts/Conthrax_SemiBold_16px.vlw");
  display.setTextColor(TFT_WHITE);
  display.setCursor(wx + 28, wy + 20);
  
  String dispCity = WEATHER_CITY;
  if (dispCity.length() > 12) {
    dispCity = dispCity.substring(0, 11) + "..";
  }
  display.print(dispCity);
  
  // Section 2: Temperature
  int iconX1 = wx + 140, iconY1 = wy + 6;
  display.fillCircle(iconX1 + 4, iconY1 + 11, 2, TFT_RED);
  display.fillRect(iconX1 + 3, iconY1 + 3, 2, 6, TFT_RED);
  display.drawCircle(iconX1 + 4, iconY1 + 11, 3, TFT_WHITE);
  display.drawRect(iconX1 + 2, iconY1 + 2, 3, 8, TFT_WHITE);
  
  display.setTextColor(display.color565(255, 120, 120));
  display.setCursor(iconX1 + 12, wy + 20);
  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.0fC", g_weatherData.temperature);
  display.print(tempStr);
  
  // Section 3: Humidity
  int iconX2 = wx + 215, iconY2 = wy + 6;
  display.fillCircle(iconX2 + 4, iconY2 + 9, 3, display.color565(100, 180, 255));
  display.fillTriangle(iconX2 + 4, iconY2 + 2, iconX2 + 1, iconY2 + 8, iconX2 + 7, iconY2 + 8, display.color565(100, 180, 255));
  
  display.setTextColor(display.color565(150, 200, 255));
  display.setCursor(iconX2 + 12, wy + 20);
  char humStr[16];
  snprintf(humStr, sizeof(humStr), "%d%%", g_weatherData.humidity);
  display.print(humStr);
  
  // Section 4: Wind
  int iconX3 = wx + 290, iconY3 = wy + 6;
  display.fillRect(iconX3 + 1, iconY3 + 2, 1, 12, TFT_WHITE);
  display.fillRoundRect(iconX3 + 2, iconY3 + 3, 8, 4, 1, TFT_ORANGE);
  display.fillRect(iconX3 + 4, iconY3 + 3, 2, 4, TFT_WHITE);
  
  display.setTextColor(display.color565(180, 255, 180));
  display.setCursor(iconX3 + 12, wy + 20);
  char windStr[24];
  float msWind = g_weatherData.windSpeed / 3.6f;
  const char* windDir = getWindCardinal(g_weatherData.windDirection);
  snprintf(windStr, sizeof(windStr), "%.1f %s", msWind, windDir);
  display.print(windStr);
  
  // Section 5: Sunset
  int iconX4 = wx + 380, iconY4 = wy + 6;
  display.fillCircle(iconX4 + 4, iconY4 + 8, 3, TFT_YELLOW);
  display.fillRect(iconX4, iconY4 + 8, 8, 4, cardBg);
  display.drawFastHLine(iconX4 - 1, iconY4 + 8, 10, TFT_ORANGE);
  display.drawFastVLine(iconX4 + 4, iconY4 + 2, 2, TFT_RED);
  
  display.setTextColor(display.color565(255, 200, 100));
  display.setCursor(iconX4 + 12, wy + 20);
  display.print(g_weatherData.sunsetTime.length() > 0 ? g_weatherData.sunsetTime : "--:--");
}
