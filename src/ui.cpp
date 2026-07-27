#include "dashboard.h"

#define MAX_CELLS 16

static constexpr unsigned long STARTUP_RAMP_DURATION_MS = 3000;

static void measureDs15Cells(int *cells, int &totalW, int count, int decimalPos) {
  constexpr int G = 3;
  int16_t bx1, by1;
  uint16_t bw, bh;
  int cumX = 0;
  display.setFont(&DS_DIGIT15pt7b);
  for (int i = 0; i < count; i++) {
    char cb[2] = {(i == decimalPos) ? '.' : '8', 0};
    display.getTextBounds(cb, cumX, 0, &bx1, &by1, &bw, &bh);
    cells[i] = cumX + bw;
    cumX += bw + G;
  }
  totalW = cumX - G;
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
  }
  display.startWrite();

  bool forceDraw =
      firstRun || forceFullRedraw;
  if (forceFullRedraw) {
    display.fillScreen(TFT_BLACK);
    forceFullRedraw = false;
  }
  bool componentUpdated = false;
  int16_t x1, y1, tx1, ty1;
  uint16_t w, h;

  if (firstRun || forceDraw) {
    firstRun = false;
    display.setFont(&Conthrax_SemiBold12pt7b);
    display.setTextColor(TFT_WHITE);
    display.getTextBounds("KM/H", 0, 0, &x1, &y1, &w, &h);
    int unitX = BIG_CENTER_X + OFFSET_BIG_SPEED_UNIT_X,
        unitY = BIG_CENTER_Y + OFFSET_BIG_SPEED_UNIT_Y;
    display.setCursor(unitX - (w / 2) - x1, unitY - y1);
    display.print("KM/H");
    drawDebugBox(display, unitX - (w / 2) - 2, unitY - 2, w + 4, h + 4);
  }
  if (forceDraw) {
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setTextColor(display.color565(150, 150, 150));
    display.getTextBounds(DASHBOARD_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
    int sigX = BIG_CENTER_X + OFFSET_BIG_SIGNATURE_X,
        sigY = BIG_CENTER_Y + OFFSET_BIG_SIGNATURE_Y;
    display.fillRect(sigX - (w / 2) - 6, sigY - 6, w + 12, h + 12, TFT_BLACK);
    display.setCursor(sigX - (w / 2) - x1, sigY - y1);
    display.print(DASHBOARD_SIGNATURE);
    drawDebugBox(display, sigX - (w / 2) - 2, sigY - 2, w + 4, h + 4);
  }

  int currentSourceState = displaySnap.speedSourceMode;
  if (ENABLE_DEMO_MODE) {
    currentSourceState = (millis() / 2000) % 3;
  }
  if (currentSourceState != lastSpeedSourceState || forceDraw) {
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
    display.setFont(&Conthrax_SemiBold12pt7b);
    drawBadge(label, OFFSET_HALL_ICON_X, OFFSET_HALL_ICON_Y, color);
  }

  static int lastWifiState = -1;
  static bool lastShowPlus = false;
  int hasClients = WiFi.softAPgetStationNum() > 0;
  bool isStation = WiFi.status() == WL_CONNECTED;
  bool isAP = WiFi.getMode() != WIFI_OFF;
  int currentWifiState = 0;
  if (isAP) {
    currentWifiState = hasClients ? 2 : 1;
  }
  bool showPlus = isStation;

  if (currentWifiState != lastWifiState || showPlus != lastShowPlus || forceDraw) {
    lastWifiState = currentWifiState;
    lastShowPlus = showPlus;
    int wifiX = BIG_CENTER_X + OFFSET_WIFI_ICON_X;
    int wifiY = BIG_CENTER_Y + OFFSET_WIFI_ICON_Y;
    display.fillRect(wifiX - 4, wifiY - 3, 24, 19, TFT_BLACK);
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
    drawDebugBox(display, wifiX - 4, wifiY, 24, 16);
  }

  unsigned long now = millis();
  static unsigned long lastTimeUpdate = 0;
  if ((displaySnap.localHour != lastHour || displaySnap.minute != lastMin ||
       displaySnap.day != lastDay) && (REFRESH_TIME_MS == 0 || now - lastTimeUpdate >= (unsigned long)REFRESH_TIME_MS) || forceDraw) {
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
    if (w_sep_t == 0) {
      display.setFont(&Conthrax_SemiBold12pt7b);
      display.getTextBounds(":", 0, 0, &tx1_sep_t, &ty1, &w_sep_t, &h_sep_t);
      display.getTextBounds("/", 0, 0, &tx1_sep_d, &ty1, &w_sep_d, &h_sep_d);
    }
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(hourStr, 0, 0, &tx1, &ty1, &w_h, &h_h);
    static int timeBlockH = 0, dateBlockH = 0;
    if (timeBlockH == 0) {
      timeBlockH = max((int)h_h, 16) + 8;
      display.getTextBounds(dayStr, 0, 0, &tx1, &ty1, &w_d, &h_d);
      dateBlockH = max((int)h_d, 16) + 8;
    }
    int16_t save_h_x1 = tx1;
    display.getTextBounds(minStr, 0, 0, &tx1, &ty1, &w_m, &h_m);
    display.getTextBounds(dayStr, 0, 0, &tx1, &ty1, &w_d, &h_d);
    int16_t save_d_x1 = tx1;
    display.getTextBounds(monthStr, 0, 0, &tx1, &ty1, &w_mo, &h_mo);
    display.getTextBounds(yearStr, 0, 0, &tx1, &ty1, &w_y, &h_y);

    int timeW = 16 + 6 + w_h + w_sep_t + w_m;
    int timeX = (BIG_CENTER_X + OFFSET_BIG_TIME_X) - (timeW / 2);
    int timeY = BIG_CENTER_Y + OFFSET_BIG_TIME_Y;

    int dateW = 16 + 6 + w_d + w_sep_d + w_mo + w_sep_d + w_y;
    int dateX = (BIG_CENTER_X + OFFSET_BIG_DATE_X) - (dateW / 2);
    int dateY = BIG_CENTER_Y + OFFSET_BIG_DATE_Y;

    if (lastTimeStartX >= 0 && lastTimeY >= 0 && lastTimeY != timeY) {
      display.fillRect(lastTimeStartX - 8, lastTimeY - 22, lastTimeWidth + 16,
                        timeBlockH, TFT_BLACK);
    }
    int clearTimeX = timeX - 4, clearTimeW = timeW + 8;
    if (lastTimeWidth > 0 && lastTimeStartX >= 0 && lastTimeY == timeY) {
      clearTimeX = std::min(timeX, lastTimeStartX) - 4;
      clearTimeW = std::max(timeX + timeW, lastTimeStartX + lastTimeWidth) + 4 -
                   clearTimeX;
    }
    display.fillRect(clearTimeX - 4, timeY - 22, clearTimeW + 12, timeBlockH,
                     TFT_BLACK);
    lastTimeStartX = timeX;
    lastTimeWidth = timeW;
    lastTimeY = timeY;

    if (lastDateStartX >= 0 && lastDateY >= 0 && lastDateY != dateY) {
      display.fillRect(lastDateStartX - 8, lastDateY - 22, lastDateWidth + 16,
                        dateBlockH, TFT_BLACK);
    }
    int clearDateX = dateX - 4, clearDateW = dateW + 8;
    if (lastDateWidth > 0 && lastDateStartX >= 0 && lastDateY == dateY) {
      clearDateX = std::min(dateX, lastDateStartX) - 4;
      clearDateW = std::max(dateX + dateW, lastDateStartX + lastDateWidth) + 4 -
                   clearDateX;
    }
    display.fillRect(clearDateX - 4, dateY - 22, clearDateW + 12, dateBlockH,
                     TFT_BLACK);
    lastDateStartX = dateX;
    lastDateWidth = dateW;
    lastDateY = dateY;

    drawClockIcon(timeX, timeY - 18, TFT_WHITE);
    display.setTextColor(TFT_WHITE);
    display.setFont(&DS_DIGIT15pt7b);
    display.setCursor(timeX + 22 - save_h_x1, timeY);
    display.print(hourStr);
    display.setFont(&Conthrax_SemiBold12pt7b);
    display.print(":");
    display.setFont(&DS_DIGIT15pt7b);
    display.print(minStr);
    drawDebugBox(display, timeX - 2, timeY - 22, timeW + 4, timeBlockH);

    drawCalendarIcon(dateX, dateY - 18, TFT_WHITE);
    display.setFont(&DS_DIGIT15pt7b);
    display.setCursor(dateX + 22 - save_d_x1, dateY);
    display.print(dayStr);
    display.setFont(&Conthrax_SemiBold12pt7b);
    display.print("/");
    display.setFont(&DS_DIGIT15pt7b);
    display.print(monthStr);
    display.setFont(&Conthrax_SemiBold12pt7b);
    display.print("/");
    display.setFont(&DS_DIGIT15pt7b);
    display.print(yearStr);
    drawDebugBox(display, dateX - 2, dateY - 22, dateW + 4, dateBlockH);
  }

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
    display.setFont(&DS_DIGIT15pt7b);
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
  if ((currentSpeed != lastSpeed && (REFRESH_SPEED_MS == 0 || now - lastSpeedUpdate >= (unsigned long)REFRESH_SPEED_MS)) || forceDraw) {
    lastSpeed = currentSpeed;
    lastSpeedUpdate = now;
    componentUpdated = true;
    int speedNumX = BIG_CENTER_X + OFFSET_BIG_SPEED_NUM_X,
        speedNumY = BIG_CENTER_Y + OFFSET_BIG_SPEED_NUM_Y;
    static uint16_t w_speed3_max = 0, h_speed_max = 0;
    static int16_t refY1 = 0;
    static int digitWidth[10] = {0}, digitXOff[10] = {0};
    static int spdCellR[MAX_CELLS] = {0};
    static bool speedLayoutInit = false;
    int spdCount = SPEED_DIGITS;
    display.setFont(&DS_DIGIT50pt7b);
    if (!speedLayoutInit) {
      speedLayoutInit = true;
      int16_t sx1, sy1;
      uint16_t tw, th;
      char buf[2] = "0";
      for (int i = 0; i < 10; i++) {
        buf[0] = '0' + i;
        display.getTextBounds(buf, 0, 0, &sx1, &sy1, &tw, &th);
        digitWidth[i] = tw;
        digitXOff[i] = sx1;
      }
      display.getTextBounds("0", 0, 0, &sx1, &refY1, &tw, &th);
      constexpr int SG = 4;
      int cumX = 0;
      for (int i = 0; i < spdCount; i++) {
        char cb[2] = {'8', 0};
        display.getTextBounds(cb, cumX, 0, &sx1, &sy1, &tw, &th);
        spdCellR[i] = cumX + tw;
        cumX += tw + SG;
      }
      w_speed3_max = cumX - SG;
      h_speed_max = th;
    }
    char speedStr[12];
    snprintf(speedStr, sizeof(speedStr), "%d", currentSpeed);
    int len = strlen(speedStr);

    int boxLeft = applyAlign(speedNumX, w_speed3_max, ALIGN_BIG_SPEED_NUM);

    display.setFont(&DS_DIGIT50pt7b);
    display.setTextColor(TFT_WHITE);

    static char lastCellChar[MAX_CELLS] = {0};

    if (forceDraw) {
      display.fillRect(boxLeft - 6, speedNumY - 2, w_speed3_max + 12,
                       h_speed_max + 6, TFT_BLACK);
    } else {
      for (int ci = 0; ci < spdCount; ci++) {
        if (lastCellChar[ci] != 0) {
          int cellRight = boxLeft + spdCellR[ci];
          int d = lastCellChar[ci] - '0';
          int cx = cellRight - 2 - digitXOff[d] - digitWidth[d];
          display.setTextColor(TFT_BLACK);
          display.setCursor(cx, speedNumY - refY1);
          display.print(lastCellChar[ci]);
        }
      }
    }

    display.setTextColor(ghost_color);
    for (int ci = 0; ci < spdCount; ci++) {
      int cellRight = boxLeft + spdCellR[ci];
      int d = 8;
      int cx = cellRight - 2 - digitXOff[d] - digitWidth[d];
      display.setCursor(cx, speedNumY - refY1);
      display.print('8');
      lastCellChar[ci] = '8';
    }

    display.setTextColor(TFT_WHITE);
    for (int i = 0; i < len; i++) {
      int d = speedStr[i] - '0';
      int cellIdx = (spdCount - len) + i;
      int cellRight = boxLeft + spdCellR[cellIdx];
      int cx = cellRight - 2 - digitXOff[d] - digitWidth[d];
      display.setCursor(cx, speedNumY - refY1);
      display.print(speedStr[i]);
      lastCellChar[cellIdx] = speedStr[i];
    }
    drawDebugBox(display, boxLeft - 1, speedNumY - 1, w_speed3_max + 2, h_speed_max + 2);
  }

  double displayOdo = displaySnap.totalDistanceKm;
  static unsigned long lastOdoUpdate = 0;
  if ((fabs(displayOdo - lastDispOdo) >= 0.1 && (REFRESH_ODO_MS == 0 || now - lastOdoUpdate >= (unsigned long)REFRESH_ODO_MS)) || forceDraw) {
    lastOdoUpdate = now;
    lastDispOdo = displayOdo;
    componentUpdated = true;
    static uint16_t w_odo_unit_max = 0, h_odo_max = 0;
    static bool odoLayoutInit = false;
    if (!odoLayoutInit) {
      odoLayoutInit = true;
      int16_t tx1, ty1;
      uint16_t tw1, th1, tw2, th2;
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("999999.9", 0, 0, &tx1, &ty1, &tw1, &th1);
      h_odo_max = th1;
      display.setFont(&Conthrax_SemiBold7pt7b);
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

    display.setFont(&DS_DIGIT15pt7b);
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

    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setCursor(odoUnitX, odoY);
    display.print(" KM");
    drawDebugBox(display, odoCellX - 2, odoClearY, odoClearW, h_odo_max + 4);
  }

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

  // Compute eased fill values
  {
    float elapsed = (float)(now - easeTempStart);
    float t = constrain(elapsed / (float)SIDEBAR_EASE_MS, 0.0f, 1.0f);
    animTempFill = easeTempFrom + (easeTempTo - easeTempFrom) * easeOutCubic(t);
  }
  {
    float elapsed = (float)(now - easeFuelStart);
    float t = constrain(elapsed / (float)SIDEBAR_EASE_MS, 0.0f, 1.0f);
    animFuelFill = easeFuelFrom + (easeFuelTo - easeFuelFrom) * easeOutCubic(t);
  }

  // Left Sidebar: Engine Temp
  static unsigned long lastSideTempUpdate = 0;
  bool tempMoving = (now - easeTempStart) < SIDEBAR_EASE_MS;
  if (((currentTemp != lastEngineTemp || tempMoving) && (tempMoving || REFRESH_SIDEBAR_TEMP_MS == 0 || now - lastSideTempUpdate >= (unsigned long)REFRESH_SIDEBAR_TEMP_MS)) || forceDraw) {
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
    display.setFont(&Conthrax_SemiBold7pt7b);
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
    drawDebugBox(display, barX - 2, barY - 2, barW + 4, barH + 4);
  }

  // Right Sidebar: Fuel
  static unsigned long lastSideFuelUpdate = 0;
  bool fuelMoving = (now - easeFuelStart) < SIDEBAR_EASE_MS;
  if (((currentFuel != lastFuelPct || fuelMoving) && (fuelMoving || REFRESH_SIDEBAR_FUEL_MS == 0 || now - lastSideFuelUpdate >= (unsigned long)REFRESH_SIDEBAR_FUEL_MS)) || forceDraw) {
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
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setTextColor(fuelColor, TFT_BLACK);

    display.setTextDatum(BR_DATUM);
    display.setTextPadding(48);
    display.drawString(fBuf, barX - 5, barY + barH);
    display.setTextPadding(0);
    drawDebugBox(display, barX - 2, barY - 2, barW + 4, barH + 4);
  }

  int displaySat = displaySnap.satellites;
  float displayBat = displaySnap.batteryVoltage;
  float displayTmr = displaySnap.accelResultTime;
  TimerState displayAccelState = displaySnap.accelState;
  static int w_sat_max = 0, w_bat_max = 0, w_badge_max = 0, w_tmr_max = 0;
  static uint16_t h_sat_max = 0, h_bat_max = 0, h_tmr_max = 0,
                  h_badge_max = 0, w_bat_num_max = 0;
  static bool bottomRowLayoutInit = false;

  if (!bottomRowLayoutInit) {
    bottomRowLayoutInit = true;
    int16_t bx1, by1;
    uint16_t bw, bh;
    display.setFont(&DS_DIGIT15pt7b);
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
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.getTextBounds(" V", 0, 0, &bx1, &by1, &w_v_unit, &h_v_unit);
    w_bat_max = 14 + w_bat_num_max + w_v_unit;
    h_bat_max = (bh > 18) ? bh : 18;
    if (h_v_unit > h_bat_max)
      h_bat_max = h_v_unit;
    uint16_t bw1, bh1, bw2, bh2;
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.getTextBounds(ACCEL_BADGE_LINE1.c_str(), 0, 0, &bx1, &by1, &bw1,
                          &bh1);
    display.getTextBounds(ACCEL_BADGE_LINE2.c_str(), 0, 0, &bx1, &by1, &bw2,
                          &bh2);
    w_badge_max = ((bw1 > bw2) ? bw1 : bw2) + 8;
    h_badge_max = bh1 + 2 + bh2 + 4 + 2;
    display.setFont(&DS_DIGIT15pt7b);

    int tmrCells = TMR_INT_DIGITS + 1 + TMR_DEC_DIGITS;
    char tmrPat[16];
    for (int i = 0; i < tmrCells; i++) tmrPat[i] = (i == TMR_INT_DIGITS) ? '.' : '8';
    tmrPat[tmrCells] = 0;
    display.getTextBounds(tmrPat, 0, 0, &bx1, &by1, &bw, &bh);
    uint16_t w_s_unit, h_s_unit;
    display.setFont(&Conthrax_SemiBold7pt7b);
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

  // -- Satellite count --
  static int satCells[MAX_CELLS] = {0};
  static int satCellW = 0, satCellsCount = 0;
  if (satCellsCount == 0) {
    satCellsCount = SAT_DIGITS;
    measureDs15Cells(satCells, satCellW, satCellsCount, -1);
  }
  static unsigned long lastSatUpdate = 0;
  if ((displaySat != lastSat && (REFRESH_SAT_MS == 0 || now - lastSatUpdate >= (unsigned long)REFRESH_SAT_MS)) || forceDrawSat) {
    lastSatUpdate = now;
    lastSat = displaySat;
    componentUpdated = true;
    char satStr[8];
    snprintf(satStr, sizeof(satStr), "%d", displaySat);
    int len = strlen(satStr);
    int satClearTop = satY + ds15_refY1 - 2;
    int satClearH = (-ds15_refY1) + h_sat_max + 4;
    display.fillRect(satX - 1, satClearTop, w_sat_max + 2, satClearH,
                     TFT_BLACK);
    int iconCY = satY + ds15_refY1 + (h_sat_max / 2);
    drawLocationIcon(satX, iconCY - 7, TFT_WHITE);
    display.setFont(&DS_DIGIT15pt7b);

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
  if ((displayAccelState != lastState && (REFRESH_TMR_MS == 0 || now - lastBadgeUpdate >= (unsigned long)REFRESH_TMR_MS)) || forceDrawTmr) {
    lastBadgeUpdate = now;
    lastState = displayAccelState;
    componentUpdated = true;
    uint16_t timerColor =
        (displayAccelState == RUNNING)
            ? TFT_YELLOW
            : ((displayAccelState == FINISHED) ? TFT_GREEN : TFT_WHITE);
    int16_t bx1_b, by1_b, bx2_b, by2_b;
    uint16_t bw1_b, bh1_b, bw2_b, bh2_b;
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.getTextBounds(ACCEL_BADGE_LINE1.c_str(), 0, 0, &bx1_b, &by1_b,
                          &bw1_b, &bh1_b);
    display.getTextBounds(ACCEL_BADGE_LINE2.c_str(), 0, 0, &bx2_b, &by2_b,
                          &bw2_b, &bh2_b);
    int badgeH = bh1_b + 2 + bh2_b + 4;
    display.fillRect(badgeX - 1, tmrY - 2, w_badge_max + 2, badgeH + 2, TFT_BLACK);
    drawAARoundRect(display, badgeX, tmrY - 1, w_badge_max, badgeH, 3, timerColor);
    display.setTextColor(timerColor);
    display.setCursor(badgeX + (w_badge_max - bw1_b) / 2 - bx1_b,
                      tmrY + 2 - by1_b);
    display.print(ACCEL_BADGE_LINE1);
    display.setCursor(badgeX + (w_badge_max - bw2_b) / 2 - bx2_b,
                      tmrY + 2 + bh1_b + 2 - by2_b);
    display.print(ACCEL_BADGE_LINE2);
  }

  // -- Timer --
  static int tmrCells[MAX_CELLS] = {0};
  static int tmrCellW = 0, tmrCellsCount = 0;
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
  if ((tmrChanged && (REFRESH_TMR_MS == 0 || now - lastTmrUpdate >= (unsigned long)REFRESH_TMR_MS)) || forceDrawTmr) {
    lastTmrUpdate = now;
    strcpy(lastTmrStr, tmrStr);
    lastStateTimer = displayAccelState;
    componentUpdated = true;
    uint16_t timerColor =
        (displayAccelState == RUNNING)
            ? TFT_YELLOW
            : ((displayAccelState == FINISHED) ? TFT_GREEN : TFT_WHITE);
    display.setFont(&DS_DIGIT15pt7b);
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
    display.setFont(&Conthrax_SemiBold7pt7b);
    int sRight = tmrX + tmrCells[tmrCellsCount - 1];
    int sCx = sRight - 2;
    display.setCursor(sCx, tmrY - ds15_refY1);
    display.print(" S");
    drawDebugBox(display, badgeX - 1, tmrY - 2, tmrTotalW + 2, ((h_badge_max > h_tmr_max) ? h_badge_max : h_tmr_max) + 3);
  }

  // -- Battery voltage --
  static int batCells[MAX_CELLS] = {0};
  static int batCellW = 0, batCellsCount = 0;
  if (batCellsCount == 0) {
    batCellsCount = BAT_INT_DIGITS + 1 + BAT_DEC_DIGITS;
    measureDs15Cells(batCells, batCellW, batCellsCount, BAT_INT_DIGITS);
  }
  char batStr[12];
  snprintf(batStr, sizeof(batStr), "%.*f", BAT_DEC_DIGITS, displayBat);
  static char lastBatStr[12] = "";
  static unsigned long lastBatUpdate = 0;
  if ((strcmp(batStr, lastBatStr) != 0 && (REFRESH_BAT_MS == 0 || now - lastBatUpdate >= (unsigned long)REFRESH_BAT_MS)) || forceDrawBat) {
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
    display.setFont(&DS_DIGIT15pt7b);

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
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setTextColor(batColor);
    display.print(" V");
    drawDebugBox(display, batX, batClearTop, w_bat_max, batClearH);
  }

  float displayInstKml = displaySnap.instantKml;
  float displayAvgKml = displaySnap.averageKml;
  static float lastDispInstKml = -1.0f;
  static float lastDispAvgKml = -1.0f;

  bool instChanged = fabsf(displayInstKml - lastDispInstKml) >= 0.1f;
  bool avgChanged = fabsf(displayAvgKml - lastDispAvgKml) >= 0.1f;
  static unsigned long lastInstAvgUpdate = 0;
  unsigned long instAvgMs = (REFRESH_INST_MS == 0 || REFRESH_AVG_MS == 0)
                                ? 0UL
                                : (unsigned long)min(REFRESH_INST_MS, REFRESH_AVG_MS);
  if ((instChanged || avgChanged) && (instAvgMs == 0 || now - lastInstAvgUpdate >= instAvgMs) || forceDraw) {
    lastInstAvgUpdate = now;
    if (instChanged) lastDispInstKml = displayInstKml;
    if (avgChanged) lastDispAvgKml = displayAvgKml;
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
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("88.8", 0, 0, &tl1, &tl2, &bw, &th1);
      ds15_fontH = th1;
      display.setFont(&Conthrax_SemiBold4pt7b);
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

    display.setFont(&DS_DIGIT15pt7b);
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
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.setTextColor(TFT_CYAN);
    display.setCursor(instBadgeX + 3 - badgeBx_ist,
                      instBadgeY - badgeBy_ist + 2);
    display.print("IST");

    int kmlInstX = rightColInstX + (w_right_ist - w_kml_unit) / 2;
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.setTextColor(TFT_CYAN);
    display.setCursor(kmlInstX, instY - 1);
    display.print("KM/L");

    if (SHOW_ELEMENT_BOUNDS)
      drawDebugBox(display, instNumAreaX - 2, instY - 20, currentInstWidth + 4,
                   24);

    int avgCenterX = BIG_CENTER_X + OFFSET_AVG_KML_X;
    int avgY = BIG_CENTER_Y + OFFSET_AVG_KML_Y;

    static uint16_t badgeW_avg = 0, badgeH_avg = 0;
    static int16_t badgeBx_avg = 0, badgeBy_avg = 0;
    static bool avgLayoutInit = false;

    if (!avgLayoutInit) {
      avgLayoutInit = true;
      uint16_t bw, bh;
      display.setFont(&Conthrax_SemiBold4pt7b);
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

    uint16_t w_right_avg = (badgeW_avg > w_kml_unit) ? badgeW_avg : w_kml_unit;
    int currentAvgWidth = avgCellW + 4 + w_right_avg;
    int avgNumAreaX = applyAlign(avgCenterX, currentAvgWidth, ALIGN_AVG_KML);

    int clearAvgH = ds15_fontH + 8;
    int clearAvgW = avgCellW + 4 + w_right_avg;
    int clearAvgX = avgCenterX - (clearAvgW / 2) - 4;
    display.fillRect(clearAvgX, avgY - clearAvgH + 4, clearAvgW + 8, clearAvgH, TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
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
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.setTextColor(TFT_YELLOW);
    display.setCursor(avgBadgeX + 3 - badgeBx_avg, avgBadgeY - badgeBy_avg + 2);
    display.print("AVG");

    int kmlAvgX = rightColAvgX + (w_right_avg - w_kml_unit) / 2;
    display.setFont(&Conthrax_SemiBold4pt7b);
    display.setTextColor(TFT_YELLOW);
    display.setCursor(kmlAvgX, avgY - 1);
    display.print("KM/L");

    if (SHOW_ELEMENT_BOUNDS)
      drawDebugBox(display, avgNumAreaX - 2, avgY - 20, currentAvgWidth + 4, 24);
  }

  // --- Fuel Liters (4 fixed cells: tens, ones, dot, tenths) ---
  float displayFuelLtrs = displaySnap.fuelLiters;
  static float lastDispFuelLtrs = -1.0f;
  static unsigned long lastFuelUpdate = 0;
  if ((fabsf(displayFuelLtrs - lastDispFuelLtrs) >= 0.1f && (REFRESH_FUEL_MS == 0 || now - lastFuelUpdate >= (unsigned long)REFRESH_FUEL_MS)) || forceDraw) {
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
      display.setFont(&Conthrax_SemiBold7pt7b);
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

    int clearY = fuelY - 20;
    int clearW = fuelCellW + 4 + w_fuel_unit;
    int clearX = fuelCenterX - (clearW / 2) - 4;
    display.fillRect(clearX, clearY, clearW + 8, ds15_fontH + 12, TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
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
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setCursor(unitX, fuelY - 1);
    display.setTextColor(TFT_CYAN);
    display.print("L");

    if (SHOW_ELEMENT_BOUNDS)
      drawDebugBox(display, fuelCenterX - totalW - 2, clearY, totalW + 4, 32);
  }

  if ((componentUpdated || forceDraw) && ENABLE_CIRCLE_TEST)
    for (int i = 0; i < 4; i++)
      drawAACircle(display, BIG_CENTER_X, BIG_CENTER_Y, 173 - i, TFT_CYAN);
  display.endWrite();
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
