#include "dashboard.h"
#include <stdarg.h>

char logBuf[LOG_BUF_SIZE];
volatile int logHead = 0;
volatile int logTail = 0;
volatile unsigned long logSequence = 0;

unsigned long g_startupTime = 0;
bool forceFullRedraw = false;

// Rolling worst inter-frame gap (ms), reset by the heartbeat every 10s. >18ms
// on a 60fps dashboard is a visible frame-drop; this catches the sub-33ms
// jitter that the SLOW FRAME diagnostic (>33ms threshold) misses.
volatile unsigned long g_diagMaxFrameMs = 0;
volatile unsigned long g_diagOver24Ms = 0;
volatile unsigned long g_diagMaxSensorGapMs = 0;
volatile bool pendingSleep = false;
volatile bool pendingReboot = false;
volatile bool otaUpdateInProgress = false;
volatile bool pendingOtaScreen = false;

bool pendingInvertDisplay = false;
int pendingBacklightValue = -1;
int currentBrightnessTarget = 0;

// Web-task watchdog state: disarmed when the device shows a fast-reboot loop
// so a watchdog can't brick the device by restarting it forever.
static bool watchdogDisabled = false;

void logPrintf(const char *fmt, ...) {
  char tmp[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);
  if (len > 0) {
    for (int i = 0; i < len && i < 256; i++) {
      logBuf[logHead] = tmp[i];
      logHead = (logHead + 1) % LOG_BUF_SIZE;
      if (logHead == logTail)
        logTail = (logTail + 1) % LOG_BUF_SIZE;
    }
    logSequence++;
    Serial.print(tmp);
  }
}

void setup() {
  setCpuFrequencyMhz(240);
  Serial.setTxBufferSize(256);
  Serial.begin(115200);
  delay(50);

  processConfig(0);
  // Factory-default seeding (supersedes the CFG_VER 1..4 migrations): any
  // unit whose config has not yet been seeded (CFG_VER < 5, including a
  // brand-new NVS) adopts the dashboard_backup.json values — first boot
  // behaves like a factory reset, and a previously configured unit gets the
  // reference backup applied once (CFG_VER -> 5). The historical v2/v3/v4
  // one-shot fixes (80 MHz SPI, dynamic CPU off, 60 FPS) are all covered by
  // the factory values, so the old migration chain is no longer needed.
  {
    Preferences pref;
    pref.begin("cfg", false);
    int cfgVer = pref.getInt("CFG_VER", -1);
    pref.end();
    if (cfgVer < 5)
      seedNVSWithFactoryDefaults();
  }
  recalculateDerivedParams();

  // Recovery + watchdog guard:
  // - Serial "RESET" within the first 2s after boot: factory reset (NVS wipe).
  // - Hold BOOT (GPIO0) for 8s within the first 30s after boot: factory reset.
  // - Track fast reboot loops (RTC memory survives ESP.restart) so the
  //   web-task watchdog can never brick the device by rebooting it forever.
  RTC_NOINIT_ATTR static uint32_t bootCount;
  RTC_NOINIT_ATTR static uint32_t bootStampSec;
  pinMode(0, INPUT_PULLUP);
  {
    uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    bootCount++;
    if (bootStampSec == 0 || bootCount > 10 || nowSec - bootStampSec > 120) {
      bootCount = 1;
    } else if (bootCount >= 4) {
      watchdogDisabled = true;
      logPrintf("Warning: %u fast reboots in 2 min, web watchdog disabled\n",
                bootCount);
    }
    bootStampSec = nowSec;
  }
  {
    unsigned long resetDeadline = millis() + 2000;
    char bootInput[256];
    int bootInputLen = 0;
    bootInput[0] = 0;
    while (millis() < resetDeadline) {
      while (Serial.available() && bootInputLen < (int)sizeof(bootInput) - 1) {
        bootInput[bootInputLen++] = (char)Serial.read();
        bootInput[bootInputLen] = 0;
        if (strstr(bootInput, "RESET")) {
          logPrintf("Serial factory reset command received\n");
          factoryResetConfig();
          logPrintf("Factory reset done, rebooting\n");
          delay(100);
          ESP.restart();
        }
      }
      delay(10);
    }
  }

  display.applyBusConfig();

  if (!ENABLE_DYNAMIC_CPU) {
    setCpuFrequencyMhz(MANUAL_CPU_FREQ);
  }

  logPrintf("Starting Dashboard++\n");

  g_stateMutex = xSemaphoreCreateMutex();
  pinMode(CS_DISPLAY, OUTPUT);
  digitalWrite(CS_DISPLAY, HIGH);
  uint32_t _lc = ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
  logPrintf("ledcSetup ch=%d freq=%u\n", BACKLIGHT_CHANNEL, _lc);
  if (_lc == 0) logPrintf("*** LEDC SETUP FAILED ***\n");
  ledcAttachPin(BL_DISPLAY, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, 0);
  pinMode(SPI_RST, OUTPUT);
  digitalWrite(SPI_RST, LOW);
  delay(10);
  digitalWrite(SPI_RST, HIGH);
  delay(20);

  pinMode(POWER_SENSE_PIN, INPUT);
  pinMode(BATTERY_SENSE_PIN, INPUT);
  pinMode(TEMP_SENSE_PIN, INPUT);
  analogSetAttenuation(ADC_11db);
  pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), hallSensorISR,
                  FALLING);
  pinMode(FUEL_TOUCH_PIN, INPUT);
  pinMode(LIGHT_SENSOR_PIN, INPUT);

  logPrintf("display.init() start\n");
  bool initOk = display.init();
  logPrintf("display.init() done ok=%d\n", (int)initOk);
  display.setRotation(DISPLAY_ROTATION);
  logPrintf("setRotation done\n");

  initFilesystem();

  for (int i = 0; i < 5; i++) {
    processLightSensor();
    delay(5);
  }
  if (ENABLE_AUTO_BRIGHTNESS) {
    int dVal = LIGHT_SENSOR_DARK_VAL;
    int bVal = LIGHT_SENSOR_BRIGHT_VAL;
    if (bVal != dVal) {
      float t = (filteredAmbientValue - (float)dVal) / (float)(bVal - dVal);
      t = constrain(t, 0.0f, 1.0f);
      int pct = AUTO_BRIGHT_DARK + (int)((AUTO_BRIGHT_LIGHT - AUTO_BRIGHT_DARK) * t);
      pct = constrain(pct, 0, 100);
      currentBrightnessTarget = (pct * 255) / 100;
    } else {
      currentBrightnessTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
    }
  } else {
    currentBrightnessTarget = (BACKLIGHT_BRIGHTNESS * 255) / 100;
  }
  if (currentBrightnessTarget > 255) currentBrightnessTarget = 255;

  drawSplashBase();
  logPrintf("drawSplashBase done\n");
  logPrintf("splash fade: currentBrightnessTarget=%d\n", currentBrightnessTarget);
  int fadeStepCount = (currentBrightnessTarget / 8) + 1;
  for (int level = 0; level <= currentBrightnessTarget; level += 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    logPrintf("  fade step: level=%d\n", level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, currentBrightnessTarget);
  logPrintf("  fade final: value=%d\n", currentBrightnessTarget);
  updateSplashProgress(20);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  gpsSerial.setTimeout(20);
  delay(100);
  configureGNSS();

  Wire.begin(COMPASS_SDA, COMPASS_SCL);
  initCompass();

  updateSplashProgress(40);

  preferences.begin("dashboard", false);
  totalDistanceKm = preferences.getDouble("odo", 0.0);
  lastSavedOdo = totalDistanceKm;

  updateSplashProgress(60);
  for (int i = 0; i < 10; i++) {
    processFuelSensor();
    processBatterySensor();
    processTemperatureSensor();
    delay(10);
    updateSplashProgress(60 + (i * 3));
  }
  updateSplashProgress(100);
  delay(50);

  fadeStepCount = (currentBrightnessTarget / 8) + 1;
  for (int level = currentBrightnessTarget; level >= 0; level -= 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, 0);
  display.fillScreen(TFT_BLACK);

  SensorSnapshot emptySnap;
  updateBigDisplay(emptySnap);
  fadeStepCount = (currentBrightnessTarget / 8) + 1;
  for (int level = 0; level <= currentBrightnessTarget; level += 8) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BACKLIGHT_CHANNEL, currentBrightnessTarget);
  logPrintf("post-fade confirm: ledcWrite(%d, %d)\n", BACKLIGHT_CHANNEL, currentBrightnessTarget);
  g_startupTime = millis();

// GPS is quarantined from the sensors: the UBX module streams ~1.1KB/s, so the
// one-byte-at-a-time drain parks ~1s per 1024 bytes and CANNOT get ahead of
// the ring (v13: parse 9us/byte but ~940us/byte wall). On the old single
// sensor task that froze every real-mode value and (on core 1) the display.
// gpsTask (core 0, prio 2) sits BELOW the httpd(5)/TCP/IP(18)/WiFi(23) stack
// so the network always wins and the drain only ever lags GPS (1Hz-tolerant).
// sensorTask (core 1, prio 2 > loopTask prio 1) owns only the short I2C/ADC
// reads + snapshot: a few ms per 20ms tick, preempting the display briefly
// then sleeping, so rendering keeps its ~62.5fps while values stay live.
  xTaskCreatePinnedToCore(sensorTask, "SensorTaskCore1", 4096, NULL, 2, NULL,
                           1);
  xTaskCreatePinnedToCore(gpsTask, "GpsTaskCore0", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(webServerTask, "WebTaskCore0", 6144, NULL, 1, NULL,
                           0);

  logPrintf("Setup done\n");
}

void loop() {
  if (pendingSleep)
    showGoodbyeScreen(true);
  if (pendingReboot)
    showGoodbyeScreen(false);

  if (pendingInvertDisplay) {
    display.invertDisplay(DISPLAY_INVERT_COLORS);
    pendingInvertDisplay = false;
  }
  if (pendingBacklightValue >= 0) {
    currentBrightnessTarget = (pendingBacklightValue * 255) / 100;
    ledcWrite(BACKLIGHT_CHANNEL, currentBrightnessTarget);
    pendingBacklightValue = -1;
  }

  if (ENABLE_AUTO_BRIGHTNESS) {
    static int autoBrightTarget = -1;
    static float autoBrightPwmF = -1.0f;
    static unsigned long lastAutoBrightMs = 0;
    unsigned long abNow = millis();
    if (abNow - lastAutoBrightMs >= 500) {
      lastAutoBrightMs = abNow;
      int dVal = LIGHT_SENSOR_DARK_VAL;
      int bVal = LIGHT_SENSOR_BRIGHT_VAL;
      if (bVal != dVal) {
        float t = (filteredAmbientValue - (float)dVal) / (float)(bVal - dVal);
        t = constrain(t, 0.0f, 1.0f);
        int pct = AUTO_BRIGHT_DARK + (int)((AUTO_BRIGHT_LIGHT - AUTO_BRIGHT_DARK) * t);
        pct = constrain(pct, 0, 100);
        autoBrightTarget = (pct * 255) / 100;
        if (autoBrightPwmF < 0.0f) autoBrightPwmF = (float)autoBrightTarget;
      }
    }
    if (autoBrightTarget >= 0) {
      currentBrightnessTarget = autoBrightTarget;
      float alpha = 1.0f - expf(-(float)DISPLAY_REFRESH_MS / (float)AUTO_BRIGHT_FADE_MS);
      autoBrightPwmF += ((float)autoBrightTarget - autoBrightPwmF) * alpha;
      if (abs(autoBrightTarget - (int)autoBrightPwmF) <= 1)
        autoBrightPwmF = (float)autoBrightTarget;
      ledcWrite(BACKLIGHT_CHANNEL, (int)autoBrightPwmF);
    }
  }

  unsigned long now = millis();

  // Diagnostic build markers: verifies the new firmware is running and gives a
  // rolling baseline (boot banner once, heartbeat every 10s).
  static bool diagBannerLogged = false;
  if (!diagBannerLogged) {
    diagBannerLogged = true;
    logPrintf("DIAG BUILD v15: freq=%uMHz fpsTarget=%u heap=%lu\n",
              getCpuFrequencyMhz(), TARGET_FPS, (unsigned long)ESP.getFreeHeap());
  }
  static unsigned long lastDiagHeartbeat = 0;
  if (now - lastDiagHeartbeat >= 10000) {
    lastDiagHeartbeat = now;
    logPrintf("HB: up=%lus fps=%.1f freq=%uMHz tgtFps=%d heap=%lu min=%lu maxAlloc=%lu sp=%d tape=%d fallback=%d otaReq=%d memAct=%d wifi=%d rssi=%d apClients=%u temp=%.1f maxFrame=%lums over24=%lu sMaxGap=%lums\n",
              millis() / 1000UL, (double)currentMeasuredFps,
              (unsigned)getCpuFrequencyMhz(), TARGET_FPS,
              (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap(), (int)speedSpriteValid(),
              (int)tapeSpriteValid(), (int)isSpeedFallback(),
              (int)otaMemReleaseRequested, (int)memSaverActive,
              (int)WiFi.status(), (int)WiFi.RSSI(),
              (unsigned)WiFi.softAPgetStationNum(), (double)temperatureRead(),
              (unsigned long)g_diagMaxFrameMs, (unsigned long)g_diagOver24Ms,
              (unsigned long)g_diagMaxSensorGapMs);
    g_diagMaxFrameMs = 0;
    g_diagOver24Ms = 0;
    g_diagMaxSensorGapMs = 0;
  }

  // Diagnostic: log OTA/mem-saver state transitions and any loop stall >50ms.
  // Logs only on change/anomaly so steady-state operation stays silent.
  static bool tOtaReq = false, tOtaRel = false, tMemReq = false, tMemAct = false,
              tOtaUp = false;
  if (otaMemReleaseRequested != tOtaReq || otaMemReleased != tOtaRel ||
      memSaverRequested != tMemReq || memSaverActive != tMemAct ||
      otaUpdateInProgress != tOtaUp) {
    tOtaReq = otaMemReleaseRequested;
    tOtaRel = otaMemReleased;
    tMemReq = memSaverRequested;
    tMemAct = memSaverActive;
    tOtaUp = otaUpdateInProgress;
    logPrintf("STATE: otaReq=%d otaRel=%d memReq=%d memAct=%d otaUp=%d heap=%lu\n",
              (int)tOtaReq, (int)tOtaRel, (int)tMemReq, (int)tMemAct, (int)tOtaUp,
              (unsigned long)ESP.getFreeHeap());
  }
  static unsigned long lastLoopEntryMs = 0;
  if (lastLoopEntryMs != 0) {
    unsigned long loopGap = now - lastLoopEntryMs;
    if (loopGap > 50)
      logPrintf("STALL: loop gap %lums heap=%lu otaReq=%d memAct=%d webCount=%lu rssi=%d\n",
                (unsigned long)loopGap, (unsigned long)ESP.getFreeHeap(),
                (int)otaMemReleaseRequested, (int)memSaverActive,
                (unsigned long)webLoopCount, (int)WiFi.RSSI());
  }
  lastLoopEntryMs = now;

  // Frees the speed sprite when an OTA check is pending (safe point: no sprite
  // is in use between frames). Must run before any TLS work starts.
  processOtaMemRelease();

  // Same release triggered by the web task when free heap gets critically low,
  // so the big UI buffers never starve the /api/config handler or TLS stack.
  processMemSaverRelease();

  // Rebuilds the speed sprite dropped by either release above. Runs here, at a
  // safe point between frames, so the VLW parse + allocation never stalls a
  // draw frame (previously it ran mid-frame on the next speed change).
  ensureSpeedSprite();

  SensorSnapshot snap;
  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    snap = g_sensorData;
    xSemaphoreGive(g_stateMutex);
  }
  // Refresh the clock/date from the system clock directly (the sensor task
  // keeps it synced from GPS/NTP; in demo mode the simulated GPS time is
  // never applied, so this is always the real clock). If the sensor task is
  // stalled by core-0 network work the on-screen clock keeps ticking.
  {
    int hh, mm, dd, mo, yy;
    if (systemTimeToLocal(hh, mm, dd, mo, yy)) {
      snap.localHour = hh;
      snap.minute = mm;
      snap.day = dd;
      snap.month = mo;
      snap.year = yy;
      snap.timeValid = true;
      snap.dateValid = true;
    } else if (ENABLE_DEMO_MODE) {
      // Demo without a synced clock: fall back to a simulated wall clock so
      // the demo still shows time/date.
      unsigned long s = 10UL * 3600UL + millis() / 1000UL;
      snap.localHour = (s / 3600) % 24;
      snap.minute = (s / 60) % 60;
      snap.day = 16;
      snap.month = 7;
      snap.year = 26;
      snap.timeValid = true;
      snap.dateValid = true;
    }
  }
  // Sensor-task heartbeat: the loop never stalls (HB proves it), so a stalled
  // sensor task is invisible without a dedicated tick watch. Track the max
  // gap between sensor ticks; printed in the HB as sMaxGap=.
  static unsigned long lastSensorTickSeen = 0;
  if (g_sensorLastTickMs != lastSensorTickSeen) {
    if (lastSensorTickSeen != 0) {
      unsigned long gap = g_sensorLastTickMs - lastSensorTickSeen;
      if (gap > g_diagMaxSensorGapMs) g_diagMaxSensorGapMs = gap;
    }
    lastSensorTickSeen = g_sensorLastTickMs;
  }

  if (DISPLAY_REFRESH_MS == 0 ||
      (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS)) {
    unsigned long frameStartMs = millis();
    static unsigned long lastFrameTime = 0;
    static float filteredFrameTimeMs = 16.6f;
    unsigned long frameDeltaMs = now - lastFrameTime;
    if (frameDeltaMs > 0) {
      lastFrameTime = now;
      if (frameDeltaMs > g_diagMaxFrameMs) g_diagMaxFrameMs = frameDeltaMs;
      if (frameDeltaMs > 24) g_diagOver24Ms++;
      filteredFrameTimeMs =
          (filteredFrameTimeMs * 0.95f) + ((float)frameDeltaMs * 0.05f);
      if (filteredFrameTimeMs > 0.0f)
        currentMeasuredFps = 1000.0f / filteredFrameTimeMs;
    }
    static unsigned long lastFpsAvgCalcTime = 0;
    if (now - lastFpsAvgCalcTime >= 250) {
      lastFpsAvgCalcTime = now;
      fpsHistory[fpsHistoryIndex] = currentMeasuredFps;
      fpsHistoryIndex = (fpsHistoryIndex + 1) % FPS_AVG_SAMPLES;
      if (fpsHistoryCount < FPS_AVG_SAMPLES)
        fpsHistoryCount++;
      float fpsSum = 0.0f;
      for (uint8_t i = 0; i < fpsHistoryCount; i++)
        fpsSum += fpsHistory[i];
      currentAverageFps = fpsSum / (float)fpsHistoryCount;
    }
    if (DISPLAY_REFRESH_MS > 0) {
      lastDisplayUpdate += DISPLAY_REFRESH_MS;
      if (now - lastDisplayUpdate > DISPLAY_REFRESH_MS)
        lastDisplayUpdate = now;
    } else {
      lastDisplayUpdate = now;
    }

    if (pendingOtaScreen) {
      pendingOtaScreen = false;
      showUpdatingScreen();
    }

    if (!otaUpdateInProgress) {
      // An OTA pull *check* must not freeze the dashboard: the big sprites are
      // dropped and rebuilt around the check (processOtaMemRelease /
      // ensureSpeedSprite) and the components fall back to direct-panel
      // drawing for its duration, so the screen keeps animating.
      updateBigDisplay(snap);
      drawFpsOverlay();
      drawGpsDebugOverlay();
      checkNightMode(snap);
    } else {
      static unsigned long lastOtaAdvance = 0;
      static bool otaRebootShown = false;
      static float otaSmoothedW = 0.0f;
      unsigned long now = millis();
      if (otaProgressFillW < otaProgressTarget && now - lastOtaAdvance >= 40) {
        lastOtaAdvance = now;
        int remaining = otaProgressTarget - otaProgressFillW;
        int step;
        if (remaining > 50)
          step = random(10, 25);
        else if (remaining > 20)
          step = random(8, 18);
        else
          step = random(5, 12);
        if (otaProgressFillW + step > otaProgressTarget)
          step = otaProgressTarget - otaProgressFillW;
        otaProgressFillW += step;
      }
      float diff = (float)otaProgressFillW - otaSmoothedW;
      otaSmoothedW += diff * 0.08f;
      if (fabsf(diff) < 1.0f) otaSmoothedW = (float)otaProgressFillW;
      int fillW = (int)(otaSmoothedW + 0.5f);
      if (fillW > 260) fillW = 260;
      if (fillW > 0) {
        int barX = DISPLAY_WIDTH / 2 - (260 / 2);
        display.startWrite();
        display.fillRect(barX, 160, fillW, 8, TFT_CYAN);
        display.endWrite();
      }
      if (otaUpdateSuccess && fillW >= 258 && !otaRebootShown) {
        otaRebootShown = true;
        delay(100);
        ESP.restart();
      }
    }
    unsigned long frameMs = millis() - frameStartMs;
    if (frameMs > 100)
      logPrintf("FRAME STALL: draw block %lums heap=%lu otaReq=%d memAct=%d\n",
                (unsigned long)frameMs, (unsigned long)ESP.getFreeHeap(),
                (int)otaMemReleaseRequested, (int)memSaverActive);
  }

  // NOTE: periodic [RAW]/[VAL]/[ESP] telemetry temporarily disabled for a
  // clean serial monitor during GNSS debugging. Re-enable by flipping to #if 1.
#if 0
  static unsigned long lastTelemetryUpdate = 0;
  if (now - lastTelemetryUpdate >= TELEMETRY_REFRESH_MS) {
    lastTelemetryUpdate = now;

    unsigned long hallIntUs, hallCnt;
    portENTER_CRITICAL(&hallMux);
    hallIntUs = hallPulseIntervalUs;
    hallCnt = hallPulseCount;
    portEXIT_CRITICAL(&hallMux);

    const char *speedSrc =
        (snap.speedSourceMode == 1) ? "GPS" :
        (snap.speedSourceMode == 2) ? "G+H" : "HALL";
    float gpsSpeed = gps.speed.isValid() ? gps.speed.kmph() : 0.0f;
    float hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0.0f;
    float altitude = gps.altitude.isValid() ? gps.altitude.meters() : 0.0f;

    logPrintf("[RAW] hallInt=%.1fms hallCnt=%lu fuelADC=%d fuelFlt=%.1f "
              "lightADC=%d batADC=%d tempADC=%d "
              "magX=%d magY=%d magZ=%d\n",
              hallIntUs / 1000.0f, hallCnt,
              rawFuelADC, filteredReading, rawLightADC,
              rawBatteryADC, rawTempADC,
              compassRawX, compassRawY, compassRawZ);

    logPrintf("[VAL] spd=%.1fkmh src=%s bat=%.1fV engT=%.1fC fuel=%.1fL(%d%%) "
              "sat=%d hdop=%.1f alt=%.0fm lat=%.6f lon=%.6f gpsSpd=%.1f "
              "head=%.0f odo=%.1fkm trip=%.2fkm avg=%.1fkmh avgKml=%.1f "
              "instKml=%.1f accel=%.2fs\n",
              snap.currentSpeed, speedSrc, snap.batteryVoltage,
              snap.engineTemperature, snap.fuelLiters, snap.fuelPercentage,
              snap.satellites, hdop, altitude,
              gps.location.lat(), gps.location.lng(), gpsSpeed,
              snap.heading, snap.totalDistanceKm, tripDistanceKm,
              snap.averageSpeed, snap.averageKml, snap.instantKml,
              snap.accelResultTime);

    logPrintf("[ESP] cpu=%uMHz apb=%uMHz xtal=%uMHz usage=%.1f%% "
              "dieTemp=%.1fC heap=%luB minHeap=%luB maxAlloc=%luB "
              "psram=%luB up=%lus fps=%.1f avgFps=%.1f chip=%s rev=%d\n",
              getCpuFrequencyMhz(), getApbFrequency() / 1000000UL, getXtalFrequencyMhz(),
              cpuUsagePct, temperatureRead(),
              ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
              ESP.getFreePsram(), millis() / 1000UL,
              currentMeasuredFps, currentAverageFps,
              ESP.getChipModel(), ESP.getChipRevision());
  }
#endif // telemetry disabled
  static unsigned long lastCpuScaleCheck = 0;
    static uint32_t lastCpuCycleCount = 0;
    static unsigned long lastCpuUsageCheck = 0;
    if (now - lastCpuUsageCheck >= 1000) {
      if (lastCpuUsageCheck == 0) {
        lastCpuCycleCount = 0;
        lastCpuUsageCheck = now;
      } else {
        uint32_t endCount;
        __asm__ volatile("rsr %0, ccount" : "=a"(endCount));
        uint32_t cycles = endCount - lastCpuCycleCount;
        // ccount ticks at APB_CLK (80 MHz on ESP32), normalize to 240 MHz baseline
        cpuUsagePct = (float)cycles / (240.0f * 1000000.0f) * 100.0f;
        lastCpuCycleCount = endCount;
        lastCpuUsageCheck = now;
      }
    }
  if (now - lastCpuScaleCheck >= 1000) {
    lastCpuScaleCheck = now;
    uint32_t targetFreq;
    float cpuTemp = temperatureRead();
    if (ENABLE_DYNAMIC_CPU) {
      // Hysteresis-based scaling to prevent frequency oscillation.
      // Each state uses different up/down thresholds.
      static int lastCpuFreq = 240;
      if (now < 5000) {
        targetFreq = 240;
      } else if (lastCpuFreq == 240) {
        if (currentAverageFps > (TARGET_FPS * 0.85f))
          targetFreq = 160;
        else
          targetFreq = 240;
      } else if (lastCpuFreq == 160) {
        if (currentAverageFps < (TARGET_FPS * 0.65f))
          targetFreq = 240;
        else if (currentAverageFps > (TARGET_FPS * 0.95f))
          targetFreq = 80;
        else
          targetFreq = 160;
      } else {
        if (currentAverageFps < (TARGET_FPS * 0.45f))
          targetFreq = 240;
        else if (currentAverageFps < (TARGET_FPS * 0.70f))
          targetFreq = 160;
        else
          targetFreq = 80;
      }
      lastCpuFreq = targetFreq;

      // Thermal throttling: cap frequency based on die temperature
      if (ENABLE_CPU_THROTTLE) {
        if (cpuTemp >= CPU_THROTTLE_TEMP_CRIT)
          targetFreq = 80;
        else if (cpuTemp >= CPU_THROTTLE_TEMP_WARN && targetFreq > 160)
          targetFreq = 160;
      }
    } else {
      targetFreq = MANUAL_CPU_FREQ;
    }
    if (getCpuFrequencyMhz() != targetFreq) {
      setCpuFrequencyMhz(targetFreq);
      logPrintf("CPU: %dMHz (%.1f FPS, %.1fC)\n", targetFreq, currentAverageFps, cpuTemp);
    }
  }
  // BOOT-hold factory reset: hold GPIO0 (BOOT) for 8s within the first 30s
  // after boot to wipe the config (recovery when a forgotten config PIN
  // locks the webui, or any other config corruption).
  static unsigned long bootHoldStart = 0;
  if (millis() < 30000) {
    if (digitalRead(0) == LOW) {
      if (bootHoldStart == 0) bootHoldStart = millis();
      if (bootHoldStart && millis() - bootHoldStart > 8000) {
        logPrintf("BOOT held 8s: factory reset\n");
        factoryResetConfig();
        logPrintf("Factory reset done, rebooting\n");
        delay(100);
        ESP.restart();
      }
    } else {
      bootHoldStart = 0;
    }
  }

  // Web-task heartbeat watchdog: if the web server task stops advancing its
  // counter, the config page would be unreachable forever. Reboot to recover.
  // Disarmed only for the duration of a fast-reboot storm: the permanent
  // disarming let a later web wedge stay dead forever, so re-arm 3 minutes
  // after boot.
  if (watchdogDisabled && millis() > 180000) {
    watchdogDisabled = false;
    logPrintf("Web watchdog re-armed\n");
  }
  static unsigned long lastWebLoop = 0;
  static unsigned long webWatchdogDue = 0;
  if (webWatchdogDue == 0) webWatchdogDue = millis() + 60000;
  if (now >= webWatchdogDue) {
    if (webLoopCount != lastWebLoop) {
      lastWebLoop = webLoopCount;
      webWatchdogDue = now + 15000;
    } else if (!watchdogDisabled) {
      logPrintf("Web task stalled (heartbeat stopped), rebooting\n");
      delay(100);
      ESP.restart();
    }
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}
