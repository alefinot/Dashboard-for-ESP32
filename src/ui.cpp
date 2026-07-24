#include "dashboard.h"

static constexpr uint16_t GHOST_COLOR = 0x2104;

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
  static bool firstRun = true, lastSelfTestState = false;
  display.startWrite();

  bool selfTestChanged = (lastSelfTestState != isSelfTestActive);
  lastSelfTestState = isSelfTestActive;
  bool forceDraw =
      firstRun || isSelfTestActive || selfTestChanged || forceFullRedraw;
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
    display.fillRect(BIG_CENTER_X - 125, 48, 240, 74, TFT_BLACK);
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setTextColor(display.color565(150, 150, 150));
    display.getTextBounds(DASHBOARD_SIGNATURE.c_str(), 0, 0, &x1, &y1, &w, &h);
    int sigX = BIG_CENTER_X + OFFSET_BIG_SIGNATURE_X,
        sigY = BIG_CENTER_Y + OFFSET_BIG_SIGNATURE_Y;
    display.setCursor(sigX - (w / 2) - x1, sigY - y1);
    display.print(DASHBOARD_SIGNATURE);
    drawDebugBox(display, sigX - (w / 2) - 2, sigY - 2, w + 4, h + 4);
  }

  bool isGpsActive = snap.isGpsSpeedValid;
  int currentSourceState = isSelfTestActive ? 2 : (isGpsActive ? 1 : 0);
  if (currentSourceState != lastSpeedSourceState || forceDraw) {
    lastSpeedSourceState = currentSourceState;
    componentUpdated = true;
    uint16_t colorDarkGrey = display.color565(70, 70, 70), hallColor, gpsColor;
    if (isSelfTestActive) {
      hallColor = TFT_YELLOW;
      gpsColor = TFT_GREEN;
    } else if (isGpsActive) {
      hallColor = colorDarkGrey;
      gpsColor = TFT_GREEN;
    } else {
      hallColor = TFT_YELLOW;
      gpsColor = colorDarkGrey;
    }
    display.setFont(&Conthrax_SemiBold12pt7b);

    drawBadge("HALL", OFFSET_HALL_ICON_X, OFFSET_HALL_ICON_Y, hallColor);
    drawBadge("GPS", OFFSET_GPS_ICON_X, OFFSET_GPS_ICON_Y, gpsColor);
  }

  static int lastWifiState = -1;
  int currentWifiState = 0;
  if (isSelfTestActive) {
    currentWifiState = 2;
  } else if (WiFi.getMode() == WIFI_OFF) {
    currentWifiState = 0;
  } else if (WiFi.softAPgetStationNum() > 0) {
    currentWifiState = 2;
  } else {
    currentWifiState = 1;
  }

  if (currentWifiState != lastWifiState || forceDraw) {
    lastWifiState = currentWifiState;
    int wifiX = BIG_CENTER_X + OFFSET_WIFI_ICON_X;
    int wifiY = BIG_CENTER_Y + OFFSET_WIFI_ICON_Y;
    display.fillRect(wifiX - 4, wifiY, 24, 16, TFT_BLACK);
    if (currentWifiState == 1) {
      drawWifiIcon(wifiX, wifiY, TFT_WHITE);
    } else if (currentWifiState == 2) {
      drawWifiIcon(wifiX, wifiY, TFT_GREEN);
    }
    drawDebugBox(display, wifiX - 4, wifiY, 24, 16);
  }

  unsigned long now = millis();
  static unsigned long lastTimeUpdate = 0;
  if ((snap.localHour != lastHour || snap.minute != lastMin ||
       snap.day != lastDay) && (REFRESH_TIME_MS == 0 || now - lastTimeUpdate >= (unsigned long)REFRESH_TIME_MS) || forceDraw) {
    lastTimeUpdate = now;
    lastHour = snap.localHour;
    lastMin = snap.minute;
    lastDay = snap.day;
    componentUpdated = true;
    char hourStr[4] = "--", minStr[4] = "--", dayStr[4] = "--",
         monthStr[4] = "--", yearStr[4] = "--";
    if (overrideTimeDateStr) {
      strcpy(hourStr, "88");
      strcpy(minStr, "88");
      strcpy(dayStr, "88");
      strcpy(monthStr, "88");
      strcpy(yearStr, "88");
    } else {
      if (snap.timeValid) {
        snprintf(hourStr, 4, "%02d", snap.localHour);
        snprintf(minStr, 4, "%02d", snap.minute);
      }
      if (snap.dateValid) {
        snprintf(dayStr, 4, "%02d", snap.day);
        snprintf(monthStr, 4, "%02d", snap.month);
        snprintf(yearStr, 4, "%02d", snap.year);
      }
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
                       32, TFT_BLACK);
    }
    int clearTimeX = timeX - 4, clearTimeW = timeW + 8;
    if (lastTimeWidth > 0 && lastTimeStartX >= 0 && lastTimeY == timeY) {
      clearTimeX = std::min(timeX, lastTimeStartX) - 4;
      clearTimeW = std::max(timeX + timeW, lastTimeStartX + lastTimeWidth) + 4 -
                   clearTimeX;
    }
    display.fillRect(clearTimeX - 4, timeY - 22, clearTimeW + 12, 32,
                     TFT_BLACK);
    lastTimeStartX = timeX;
    lastTimeWidth = timeW;
    lastTimeY = timeY;

    if (lastDateStartX >= 0 && lastDateY >= 0 && lastDateY != dateY) {
      display.fillRect(lastDateStartX - 8, lastDateY - 22, lastDateWidth + 16,
                       32, TFT_BLACK);
    }
    int clearDateX = dateX - 4, clearDateW = dateW + 8;
    if (lastDateWidth > 0 && lastDateStartX >= 0 && lastDateY == dateY) {
      clearDateX = std::min(dateX, lastDateStartX) - 4;
      clearDateW = std::max(dateX + dateW, lastDateStartX + lastDateWidth) + 4 -
                   clearDateX;
    }
    display.fillRect(clearDateX - 4, dateY - 22, clearDateW + 12, 32,
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
    drawDebugBox(display, timeX - 2, timeY - 22, timeW + 4, 32);

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
    drawDebugBox(display, dateX - 2, dateY - 22, dateW + 4, 32);
  }

  // --- DS_DIGIT15pt7b digit metrics & cell positions (measured once) ---
  static bool ds15Measured = false;
  static int ds15_digitWidth[10] = {0}, ds15_digitXOff[10] = {0};
  static int16_t ds15_refY1 = 0;
  static uint16_t ds15_dotWidth = 0;
  static int16_t ds15_dotXOff = 0;
  static int cellR4[4] = {0}, cellR5[5] = {0};
  static int cellR4_w = 0, cellR5_w = 0;

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

    constexpr int G = 3;
    char s4[] = "88.8";
    int cumX = 0;
    for (int i = 0; i < 4; i++) {
      char cb[2] = {s4[i], 0};
      display.getTextBounds(cb, cumX, 0, &bx1, &by1, &bw, &bh);
      cellR4[i] = cumX + bw;
      cumX += bw + G;
    }
    cellR4_w = cumX - G;

    char s5[] = "88.88";
    cumX = 0;
    for (int i = 0; i < 5; i++) {
      char cb[2] = {s5[i], 0};
      display.getTextBounds(cb, cumX, 0, &bx1, &by1, &bw, &bh);
      cellR5[i] = cumX + bw;
      cumX += bw + G;
    }
    cellR5_w = cumX - G;
  }

  int currentSpeed = isSelfTestActive ? overrideSpeed : (int)snap.currentSpeed;
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
    static int spdCellR[3] = {0};
    static bool speedLayoutInit = false;
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
      char s3[] = "888";
      int cumX = 0;
      for (int i = 0; i < 3; i++) {
        char cb[2] = {s3[i], 0};
        display.getTextBounds(cb, cumX, 0, &sx1, &sy1, &tw, &th);
        spdCellR[i] = cumX + tw;
        cumX += tw + SG;
      }
      w_speed3_max = cumX - SG;
      h_speed_max = th;
    }
    char speedStr[6];
    snprintf(speedStr, sizeof(speedStr), "%d", currentSpeed);
    int len = strlen(speedStr);

    int boxLeft = applyAlign(speedNumX, w_speed3_max, ALIGN_BIG_SPEED_NUM);

    display.setFont(&DS_DIGIT50pt7b);
    display.setTextColor(TFT_WHITE);

    static char lastCellChar[3] = {0};

    if (forceDraw) {
      display.fillRect(boxLeft - 6, speedNumY - 2, w_speed3_max + 12,
                       h_speed_max + 6, TFT_BLACK);
    } else {
      for (int ci = 0; ci < 3; ci++) {
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

    display.setTextColor(GHOST_COLOR);
    {
      const char ghostDigits[] = {'1', '8', '8'};
      for (int ci = 0; ci < 3; ci++) {
        int cellRight = boxLeft + spdCellR[ci];
        int d = ghostDigits[ci] - '0';
        int cx = cellRight - 2 - digitXOff[d] - digitWidth[d];
        display.setCursor(cx, speedNumY - refY1);
        display.print(ghostDigits[ci]);
        lastCellChar[ci] = ghostDigits[ci];
      }
    }

    display.setTextColor(TFT_WHITE);
    for (int i = 0; i < len; i++) {
      int d = speedStr[i] - '0';
      int cellIdx = (3 - len) + i;
      int cellRight = boxLeft + spdCellR[cellIdx];
      int cx = cellRight - 2 - digitXOff[d] - digitWidth[d];
      display.setCursor(cx, speedNumY - refY1);
      display.print(speedStr[i]);
      lastCellChar[cellIdx] = speedStr[i];
    }
    drawDebugBox(display, boxLeft - 1, speedNumY - 1, w_speed3_max + 2, h_speed_max + 2);
  }

  double displayOdo = isSelfTestActive ? overrideOdo : snap.totalDistanceKm;
  static unsigned long lastOdoUpdate = 0;
  if ((fabs(displayOdo - lastDispOdo) >= 0.1 && (REFRESH_ODO_MS == 0 || now - lastOdoUpdate >= (unsigned long)REFRESH_ODO_MS)) || forceDraw) {
    lastOdoUpdate = now;
    lastDispOdo = displayOdo;
    componentUpdated = true;
    static int refOdoX = 0;
    static uint16_t w_odo_num_max = 0, w_odo_unit_max = 0;
    static bool odoLayoutInit = false;
    if (!odoLayoutInit) {
      odoLayoutInit = true;
      int16_t tx1, ty1;
      uint16_t tw1, th1, tw2, th2;
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("999999.9", 0, 0, &tx1, &ty1, &tw1, &th1);
      w_odo_num_max = tw1;
      display.setFont(&Conthrax_SemiBold12pt7b);
      display.getTextBounds(" KM", 0, 0, &tx1, &ty1, &tw2, &th2);
      w_odo_unit_max = tw2;
      refOdoX = BIG_CENTER_X + OFFSET_BIG_ODO_X - ((w_odo_num_max + tw2) / 2) +
                w_odo_num_max;
    }
    char odoNumStr[12];
    snprintf(odoNumStr, sizeof(odoNumStr), "%07.1f", displayOdo);
    uint16_t w_num, h_num;
    int16_t tx15, ty15;
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(odoNumStr, 0, 0, &tx15, &ty15, &w_num, &h_num);
    int16_t odoTopY = (BIG_CENTER_Y + OFFSET_BIG_ODO_Y) - h_num;

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    int targetX = refOdoX - w_num - tx15;
    int boxLeft = refOdoX - w_odo_num_max - 4;

    if (targetX > boxLeft)
      display.fillRect(boxLeft, odoTopY - 2, targetX - boxLeft, h_num + 4,
                       TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setCursor(targetX, odoTopY - ty15);
    display.print(odoNumStr);

    display.setFont(&Conthrax_SemiBold12pt7b);
    display.setCursor(refOdoX, odoTopY - ty15);
    display.print(" KM");
    drawDebugBox(display, boxLeft, odoTopY - 2, refOdoX - boxLeft + w_odo_unit_max + 4, h_num + 4);
  }

  // --- SIDEBARS: Engine Temp & Fuel ---
  static int lastFuelPct = -1;
  static int lastEngineTemp = -999;

  int currentFuel = snap.fuelPercentage;
  int currentTemp = (int)snap.engineTemperature;

  if (forceDraw) {
    lastFuelPct = -1;
    lastEngineTemp = -999;
  }

  // Left Sidebar: Engine Temp
  static unsigned long lastSideTempUpdate = 0;
  if ((currentTemp != lastEngineTemp && (REFRESH_SIDEBAR_TEMP_MS == 0 || now - lastSideTempUpdate >= (unsigned long)REFRESH_SIDEBAR_TEMP_MS)) || forceDraw) {
    lastSideTempUpdate = now;
    lastEngineTemp = currentTemp;

    int barX = SIDEBAR_LEFT_X, barY = SIDEBAR_LEFT_Y, barW = 8, barH = 200;
    uint16_t trackColor = display.color565(40, 40, 40);
    fillAARoundRect(display, barX, barY, barW, barH, 4, trackColor, TFT_BLACK,
                    TFT_BLACK);

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

    float tClamped = (float)constrain(currentTemp, TEMP_BAR_MIN, TEMP_BAR_MAX);
    float tRange = (float)(TEMP_BAR_MAX - TEMP_BAR_MIN);
    float pTemp = (tRange > 0.0f) ? ((tClamped - (float)TEMP_BAR_MIN) / tRange) : 0.0f;
    int fillH = (int)(barH * pTemp);
    if (fillH > barH)
      fillH = barH;
    if (fillH > 0) {
      if (fillH < 8)
        fillH = 8;
      fillAARoundRect(display, barX, barY + barH - fillH, barW, fillH, 4,
                      tempColor, trackColor, TFT_BLACK);
    }

    display.fillRect(barX + barW + 2, barY + barH - 18, 55, 20, TFT_BLACK);

    char tBuf[16];
    snprintf(tBuf, sizeof(tBuf), "%d", currentTemp);
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.setTextColor(tempColor);

    int numStartX = barX + barW + 6;
    display.setTextDatum(BL_DATUM);
    display.drawString(tBuf, numStartX, barY + barH);

    int textW = display.textWidth(tBuf);
    int cStartX = numStartX + textW + 1;

    drawAACircle(display, cStartX + 3, barY + barH - 10, 2, tempColor);
    display.drawString("c", cStartX + 6, barY + barH);
    drawDebugBox(display, barX - 2, barY - 2, barW + 4, barH + 4);
  }

  // Right Sidebar: Fuel
  static unsigned long lastSideFuelUpdate = 0;
  if ((currentFuel != lastFuelPct && (REFRESH_SIDEBAR_FUEL_MS == 0 || now - lastSideFuelUpdate >= (unsigned long)REFRESH_SIDEBAR_FUEL_MS)) || forceDraw) {
    lastSideFuelUpdate = now;
    lastFuelPct = currentFuel;

    int barX = SIDEBAR_RIGHT_X, barY = SIDEBAR_RIGHT_Y, barW = 8, barH = 200;
    uint16_t trackColor = display.color565(40, 40, 40);
    fillAARoundRect(display, barX, barY, barW, barH, 4, trackColor, TFT_BLACK,
                    TFT_BLACK);

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

    int fillH = (int)(barH * (currentFuel / 100.0f));
    if (fillH > barH)
      fillH = barH;
    if (fillH < 0)
      fillH = 0;
    if (fillH > 0) {
      if (fillH < 8)
        fillH = 8;
      fillAARoundRect(display, barX, barY + barH - fillH, barW, fillH, 4,
                      fuelColor, trackColor, TFT_BLACK);
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

  int displaySat = isSelfTestActive ? overrideSat : snap.satellites;
  float displayBat = isSelfTestActive ? overrideBat : snap.batteryVoltage;
  float displayTmr = isSelfTestActive ? overrideTimer : snap.accelResultTime;
  TimerState displayAccelState = isSelfTestActive ? RUNNING : snap.accelState;
  static int w_sat_max = 0, w_bat_max = 0, w_badge_max = 0, w_tmr_max = 0;
  static uint16_t h_sat_max = 0, h_bat_max = 0, h_tmr_max = 0,
                  w_bat_num_max = 0;
  static bool bottomRowLayoutInit = false;

  if (!bottomRowLayoutInit) {
    bottomRowLayoutInit = true;
    int16_t bx1, by1;
    uint16_t bw, bh;
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds("88", 0, 0, &bx1, &by1, &bw, &bh);
    w_sat_max = 16 + 6 + bw;
    h_sat_max = (bh > 15) ? bh : 15;
    display.getTextBounds("88.8", 0, 0, &bx1, &by1, &bw, &bh);
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
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds("88.88", 0, 0, &bx1, &by1, &bw, &bh);
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
                     ((22 > h_tmr_max) ? 22 : h_tmr_max) + 2, TFT_BLACK);
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

  // -- Satellite count (2 fixed digit cells) --
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

    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 2; ci++) {
      int cellRight = satX + 22 + cellR4[ci];
      int cx = cellRight - 2 - ds15_digitXOff[8] - ds15_digitWidth[8];
      display.setCursor(cx, satY);
      display.print('8');
    }
    display.setTextColor(TFT_WHITE);
    for (int i = 0; i < len; i++) {
      int d = satStr[i] - '0';
      int cellIdx = (2 - len) + i;
      int cellRight = satX + 22 + cellR4[cellIdx];
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
    display.fillRect(badgeX - 1, tmrY - 2, w_badge_max + 2, 22, TFT_BLACK);
    drawAARoundRect(display, badgeX, tmrY - 1, w_badge_max, 20, 3, timerColor);
    display.setTextColor(timerColor);
    display.setCursor(badgeX + (w_badge_max - bw1_b) / 2 - bx1_b,
                      tmrY + 2 - by1_b);
    display.print(ACCEL_BADGE_LINE1);
    display.setCursor(badgeX + (w_badge_max - bw2_b) / 2 - bx2_b,
                      tmrY + 2 + bh1_b + 2 - by2_b);
    display.print(ACCEL_BADGE_LINE2);
  }

  // -- Timer (5 fixed cells: tens, ones, dot, tenths, hundredths) --
  char tmrStr[8];
  snprintf(tmrStr, sizeof(tmrStr), "%.2f", displayTmr);
  static char lastTmrStr[8] = "";
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

    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 5; ci++) {
      int cellRight = tmrX + cellR5[ci];
      char gc = (ci == 2) ? '.' : '8';
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
      int cellIdx = (5 - len) + i;
      int cellRight = tmrX + cellR5[cellIdx];
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
    int sRight = tmrX + cellR5[4];
    int sCx = sRight - 2;
    display.setCursor(sCx, tmrY - ds15_refY1);
    display.print(" S");
    drawDebugBox(display, badgeX - 1, tmrY - 2, tmrTotalW + 2, ((22 > h_tmr_max) ? 22 : h_tmr_max) + 2);
  }

  // -- Battery voltage (4 fixed cells: tens, ones, dot, tenths) --
  char batStr[8];
  snprintf(batStr, sizeof(batStr), "%.1f", displayBat);
  static char lastBatStr[8] = "";
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
    int batClearTop = batY + ds15_refY1 - 1;
    int batClearH = h_bat_max + 2;
    display.fillRect(batX, batClearTop, w_bat_max, batClearH,
                     TFT_BLACK);
    int iconCY = batY + ds15_refY1 + (h_bat_max / 2);
    drawBatteryIcon(batX, iconCY - 9, displayBat, batColor);
    display.setFont(&DS_DIGIT15pt7b);

    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 4; ci++) {
      int cellRight = (batX + 14) + cellR4[ci];
      char gc = (ci == 2) ? '.' : '8';
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
      int cellIdx = (4 - len) + i;
      int cellRight = (batX + 14) + cellR4[cellIdx];
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
    display.print(" V");
    drawDebugBox(display, batX, batClearTop, w_bat_max, batClearH);
  }

  float displayInstKml = isSelfTestActive ? 18.8f : snap.instantKml;
  float displayAvgKml = isSelfTestActive ? 18.8f : snap.averageKml;
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

    static int instBlockWidthMax = 0;
    static uint16_t badgeW_ist = 0, badgeH_ist = 0;
    static int16_t badgeBx_ist = 0, badgeBy_ist = 0;
    static uint16_t w_kml_unit = 0, w_num_max = 0;
    static bool instLayoutInit = false;

    if (!instLayoutInit) {
      instLayoutInit = true;
      int16_t tl1, tl2;
      uint16_t th1;
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("88.8", 0, 0, &tl1, &tl2, &w_num_max, &th1);
      display.setFont(&Conthrax_SemiBold4pt7b);
      display.getTextBounds("KM/L", 0, 0, &tl1, &tl2, &w_kml_unit, &th1);
      uint16_t bw, bh;
      display.getTextBounds("IST", 0, 0, &badgeBx_ist, &badgeBy_ist, &bw, &bh);
      badgeW_ist = bw + 6;
      badgeH_ist = bh + 4;
      uint16_t w_right = (badgeW_ist > w_kml_unit) ? badgeW_ist : w_kml_unit;
      instBlockWidthMax = w_num_max + 4 + w_right;
    }

    char instStr[8];
    if (displayInstKml > 0.0f)
      snprintf(instStr, sizeof(instStr), "%.1f", displayInstKml);
    else
      snprintf(instStr, sizeof(instStr), "0.0");

    uint16_t w_right_ist = (badgeW_ist > w_kml_unit) ? badgeW_ist : w_kml_unit;
    int currentInstWidth = w_num_max + 4 + w_right_ist;
    int instNumAreaX = applyAlign(instCenterX, currentInstWidth, ALIGN_INST_KML);

    int clearInstX = instCenterX - (instBlockWidthMax / 2) - 4;
    display.fillRect(clearInstX, instY - 20, instBlockWidthMax + 8, 26,
                     TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 4; ci++) {
      int cellRight = instNumAreaX + cellR4[ci];
      char gc = (ci == 2) ? '.' : '8';
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
      int cellIdx = (4 - instLen) + i;
      int cellRight = instNumAreaX + cellR4[cellIdx];
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

    int rightColInstX = instNumAreaX + w_num_max + 4;
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

    static int avgBlockWidthMax = 0;
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
      uint16_t w_right = (badgeW_avg > w_kml_unit) ? badgeW_avg : w_kml_unit;
      avgBlockWidthMax = w_num_max + 4 + w_right;
    }

    char avgStr[8];
    if (displayAvgKml > 0.0f)
      snprintf(avgStr, sizeof(avgStr), "%.1f", displayAvgKml);
    else
      snprintf(avgStr, sizeof(avgStr), "0.0");

    uint16_t w_right_avg = (badgeW_avg > w_kml_unit) ? badgeW_avg : w_kml_unit;
    int currentAvgWidth = w_num_max + 4 + w_right_avg;
    int avgNumAreaX = applyAlign(avgCenterX, currentAvgWidth, ALIGN_AVG_KML);

    int clearAvgX = avgCenterX - (avgBlockWidthMax / 2) - 4;
    display.fillRect(clearAvgX, avgY - 20, avgBlockWidthMax + 8, 26, TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 4; ci++) {
      int cellRight = avgNumAreaX + cellR4[ci];
      char gc = (ci == 2) ? '.' : '8';
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
      int cellIdx = (4 - avgLen) + i;
      int cellRight = avgNumAreaX + cellR4[cellIdx];
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

    int rightColAvgX = avgNumAreaX + w_num_max + 4;
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
  float displayFuelLtrs = isSelfTestActive ? 5.0f : snap.fuelLiters;
  static float lastDispFuelLtrs = -1.0f;
  static unsigned long lastFuelUpdate = 0;
  if ((fabsf(displayFuelLtrs - lastDispFuelLtrs) >= 0.1f && (REFRESH_FUEL_MS == 0 || now - lastFuelUpdate >= (unsigned long)REFRESH_FUEL_MS)) || forceDraw) {
    lastFuelUpdate = now;
    lastDispFuelLtrs = displayFuelLtrs;
    componentUpdated = true;

    int fuelCenterX = BIG_CENTER_X + OFFSET_FUEL_LTRS_X;
    int fuelY = BIG_CENTER_Y + OFFSET_FUEL_LTRS_Y;

    char fuelStr[8];
    snprintf(fuelStr, sizeof(fuelStr), "%.1f", displayFuelLtrs);

    static uint16_t w_fuel_max = 0, w_fuel_unit = 0, fuelBlockWidthMax = 0;
    static bool fuelLayoutInit = false;
    if (!fuelLayoutInit) {
      fuelLayoutInit = true;
      int16_t tfx1, tfx2;
      uint16_t tfth;
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("88.8", 0, 0, &tfx1, &tfx2, &w_fuel_max, &tfth);
      display.setFont(&Conthrax_SemiBold7pt7b);
      display.getTextBounds("L", 0, 0, &tfx1, &tfx2, &w_fuel_unit, &tfth);
      fuelBlockWidthMax = w_fuel_max + 4 + w_fuel_unit;
    }

    int totalW = fuelBlockWidthMax;
    int fuelNumAreaX = applyAlign(fuelCenterX, totalW, ALIGN_FUEL_LTRS);
    int unitX = fuelNumAreaX + w_fuel_max + 4;

    int clearY = fuelY - 20;
    int clearX = fuelCenterX - (fuelBlockWidthMax / 2) - 4;
    display.fillRect(clearX, clearY, fuelBlockWidthMax + 8, 32, TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setTextColor(GHOST_COLOR);
    for (int ci = 0; ci < 4; ci++) {
      int cellRight = fuelNumAreaX + cellR4[ci];
      char gc = (ci == 2) ? '.' : '8';
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
      int cellIdx = (4 - fuelLen) + i;
      int cellRight = fuelNumAreaX + cellR4[cellIdx];
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
  if (snap.timeValid) {
    bool shouldBeNightMode = (snap.localHour >= NIGHT_MODE_START_HOUR ||
                              snap.localHour < NIGHT_MODE_END_HOUR);
    if (shouldBeNightMode != isNightModeActive || firstCheck) {
      isNightModeActive = shouldBeNightMode;
      firstCheck = false;
      int level = isNightModeActive
                      ? 75
                      : map(BACKLIGHT_BRIGHTNESS, 0, 100, 0, 255);
      analogWrite(BL_DISPLAY, level);
    }
  }
}
