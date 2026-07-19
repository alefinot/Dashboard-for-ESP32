#include "dashboard.h"

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
  }

  if (snap.localHour != lastHour || snap.minute != lastMin ||
      snap.day != lastDay || forceDraw) {
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
    uint16_t w_h, h_h, w_m, h_m, w_sep_t, h_sep_t, w_d, h_d, w_mo, h_mo, w_y,
        h_y, w_sep_d, h_sep_d;
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(hourStr, 0, 0, &tx1, &ty1, &w_h, &h_h);
    int16_t save_h_x1 = tx1;
    display.getTextBounds(minStr, 0, 0, &tx1, &ty1, &w_m, &h_m);
    display.getTextBounds(dayStr, 0, 0, &tx1, &ty1, &w_d, &h_d);
    int16_t save_d_x1 = tx1;
    display.getTextBounds(monthStr, 0, 0, &tx1, &ty1, &w_mo, &h_mo);
    display.getTextBounds(yearStr, 0, 0, &tx1, &ty1, &w_y, &h_y);
    display.setFont(&Conthrax_SemiBold12pt7b);
    display.getTextBounds(":", 0, 0, &tx1, &ty1, &w_sep_t, &h_sep_t);
    display.getTextBounds("/", 0, 0, &tx1, &ty1, &w_sep_d, &h_sep_d);

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
  }

  int currentSpeed = isSelfTestActive ? overrideSpeed : (int)snap.currentSpeed;
  if (currentSpeed != lastSpeed || forceDraw) {
    lastSpeed = currentSpeed;
    componentUpdated = true;
    int speedNumX = BIG_CENTER_X + OFFSET_BIG_SPEED_NUM_X,
        speedNumY = BIG_CENTER_Y + OFFSET_BIG_SPEED_NUM_Y;
    static uint16_t w_speed3_max = 0, h_speed_max = 0;
    static bool speedLayoutInit = false;
    display.setFont(&DS_DIGIT50pt7b);
    if (!speedLayoutInit) {
      speedLayoutInit = true;
      int16_t sx1, sy1;
      display.getTextBounds("999", 0, 0, &sx1, &sy1, &w_speed3_max,
                            &h_speed_max);
    }
    char speedStr[6];
    snprintf(speedStr, sizeof(speedStr), "%d", currentSpeed);
    display.getTextBounds(speedStr, 0, 0, &x1, &y1, &w, &h);

    int targetX = speedNumX - (w / 2) - x1;
    int boxLeft = speedNumX - (w_speed3_max / 2) - 6;
    int targetRight = targetX + w;
    int boxRight = boxLeft + w_speed3_max + 12;

    display.setFont(&DS_DIGIT50pt7b);
    display.setTextColor(TFT_WHITE, TFT_BLACK);

    if (targetX > boxLeft)
      display.fillRect(boxLeft, speedNumY - 2, targetX - boxLeft,
                       h_speed_max + 6, TFT_BLACK);
    if (boxRight > targetRight)
      display.fillRect(targetRight, speedNumY - 2, boxRight - targetRight,
                       h_speed_max + 6, TFT_BLACK);

    display.setCursor(targetX, speedNumY - y1);
    display.print(speedStr);
  }

  double displayOdo = isSelfTestActive ? overrideOdo : snap.totalDistanceKm;
  if (fabs(displayOdo - lastDispOdo) >= 0.1 || forceDraw) {
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
  if (currentTemp != lastEngineTemp) {
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

    display.drawCircle(cStartX + 3, barY + barH - 10, 2, tempColor);
    display.drawString("c", cStartX + 6, barY + barH);
  }

  // Right Sidebar: Fuel
  if (currentFuel != lastFuelPct) {
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

  int satX = (BIG_CENTER_X + OFFSET_BIG_SAT_X) - (w_sat_max / 2);
  int satY = BIG_CENTER_Y + OFFSET_BIG_SAT_Y;
  int tmrTotalW = w_badge_max + 5 + w_tmr_max;
  int badgeX = (BIG_CENTER_X + OFFSET_BIG_TMR_X) - (tmrTotalW / 2);
  int tmrX = badgeX + w_badge_max + 5;
  int tmrY = BIG_CENTER_Y + OFFSET_BIG_TMR_Y;
  int batX = (BIG_CENTER_X + OFFSET_BIG_BAT_X) - (w_bat_max / 2);
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

  if (displaySat != lastSat || forceDrawSat) {
    lastSat = displaySat;
    componentUpdated = true;
    char satStr[8];
    snprintf(satStr, sizeof(satStr), "%d", displaySat);
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(satStr, 0, 0, &x1, &y1, &w, &h);
    display.fillRect(satX - 1, satY - 1, w_sat_max + 2, h_sat_max + 2,
                     TFT_BLACK);
    drawLocationIcon(satX, satY + 2, TFT_WHITE);
    display.setCursor(satX + 22 - x1, satY - y1);
    display.setTextColor(TFT_WHITE);
    display.print(satStr);
  }

  if (displayAccelState != lastState || forceDrawTmr) {
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

  char tmrStr[8];
  snprintf(tmrStr, sizeof(tmrStr), "%.2f", displayTmr);
  static char lastTmrStr[8] = "";
  static TimerState lastStateTimer = READY;
  if (strcmp(tmrStr, lastTmrStr) != 0 || displayAccelState != lastStateTimer ||
      forceDrawTmr) {
    strcpy(lastTmrStr, tmrStr);
    lastStateTimer = displayAccelState;
    componentUpdated = true;
    uint16_t timerColor =
        (displayAccelState == RUNNING)
            ? TFT_YELLOW
            : ((displayAccelState == FINISHED) ? TFT_GREEN : TFT_WHITE);
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(tmrStr, 0, 0, &x1, &y1, &w, &h);
    display.fillRect(tmrX - 1, tmrY - 1, w_tmr_max + 2, h_tmr_max + 2,
                     TFT_BLACK);
    display.setCursor(tmrX - x1, tmrY - y1);
    display.setTextColor(timerColor);
    display.print(tmrStr);
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.print(" S");
  }

  char batStr[8];
  snprintf(batStr, sizeof(batStr), "%.1f", displayBat);
  static char lastBatStr[8] = "";
  if (strcmp(batStr, lastBatStr) != 0 || forceDrawBat) {
    strcpy(lastBatStr, batStr);
    componentUpdated = true;
    uint16_t batColor = TFT_YELLOW;
    if (displayBat > 0.5f && displayBat < 11.8f)
      batColor = TFT_RED;
    else if (displayBat >= 13.1f)
      batColor = TFT_GREEN;
    else if (displayBat <= 0.5f)
      batColor = TFT_WHITE;
    display.fillRect(batX - 1, batY - 1, w_bat_max + 2, h_bat_max + 3,
                     TFT_BLACK);
    drawBatteryIcon(batX, batY + 1, displayBat, batColor);
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(batStr, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(batColor);
    display.setCursor(batX + 14 - x1, batY - y1);
    display.print(batStr);
    display.setFont(&Conthrax_SemiBold7pt7b);
    display.print(" V");
  }

  float displayInstKml = isSelfTestActive ? 18.8f : snap.instantKml;
  float displayAvgKml = isSelfTestActive ? 18.8f : snap.averageKml;
  static float lastDispInstKml = -1.0f;
  static float lastDispAvgKml = -1.0f;

  if (fabsf(displayInstKml - lastDispInstKml) >= 0.1f ||
      fabsf(displayAvgKml - lastDispAvgKml) >= 0.1f || forceDraw) {
    lastDispInstKml = displayInstKml;
    lastDispAvgKml = displayAvgKml;
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
      int16_t tx1, ty1;
      uint16_t th1;
      display.setFont(&DS_DIGIT15pt7b);
      display.getTextBounds("00.0", 0, 0, &tx1, &ty1, &w_num_max, &th1);
      display.setFont(&Conthrax_SemiBold4pt7b);
      display.getTextBounds("KM/L", 0, 0, &tx1, &ty1, &w_kml_unit, &th1);
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

    int16_t tx15, ty15;
    uint16_t w_num, h_num;
    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(instStr, 0, 0, &tx15, &ty15, &w_num, &h_num);

    uint16_t w_right_ist = (badgeW_ist > w_kml_unit) ? badgeW_ist : w_kml_unit;
    int currentInstWidth = w_num + 4 + w_right_ist;
    int startInstX = instCenterX - (currentInstWidth / 2);

    int clearInstX = instCenterX - (instBlockWidthMax / 2) - 4;
    display.fillRect(clearInstX, instY - 20, instBlockWidthMax + 8, 26,
                     TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setCursor(startInstX - tx15, instY);
    display.setTextColor(TFT_CYAN);
    display.print(instStr);

    int rightColInstX = startInstX + w_num + 4;
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
      drawDebugBox(display, startInstX - 2, instY - 20, currentInstWidth + 4,
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

    display.setFont(&DS_DIGIT15pt7b);
    display.getTextBounds(avgStr, 0, 0, &tx15, &ty15, &w_num, &h_num);

    uint16_t w_right_avg = (badgeW_avg > w_kml_unit) ? badgeW_avg : w_kml_unit;
    int currentAvgWidth = w_num + 4 + w_right_avg;
    int startAvgX = avgCenterX - (currentAvgWidth / 2);

    int clearAvgX = avgCenterX - (avgBlockWidthMax / 2) - 4;
    display.fillRect(clearAvgX, avgY - 20, avgBlockWidthMax + 8, 26, TFT_BLACK);

    display.setFont(&DS_DIGIT15pt7b);
    display.setCursor(startAvgX - tx15, avgY);
    display.setTextColor(TFT_YELLOW);
    display.print(avgStr);

    int rightColAvgX = startAvgX + w_num + 4;
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
      drawDebugBox(display, startAvgX - 2, avgY - 20, currentAvgWidth + 4, 24);
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


