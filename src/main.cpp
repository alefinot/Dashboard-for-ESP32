#include "dashboard.h"
#include <stdarg.h>

char logBuf[LOG_BUF_SIZE];
volatile int logHead = 0;
volatile int logTail = 0;
volatile unsigned long logSequence = 0;

unsigned long g_startupTime = 0;
bool forceFullRedraw = false;
volatile bool pendingSleep = false;
volatile bool pendingReboot = false;
volatile bool otaUpdateInProgress = false;

bool pendingInvertDisplay = false;
int pendingBacklightValue = -1;
int currentBrightnessTarget = 0;

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
  // Upgrade NVS defaults on first boot with optimized firmware
  {
    Preferences pref;
    pref.begin("cfg", false);
    if (!pref.isKey("CFG_VER")) {
      SPI_BUS_SPEED = 60000000;
      TARGET_FPS = 60;
      ENABLE_DYNAMIC_CPU = false;
      pref.putInt("SPI_FREQ", SPI_BUS_SPEED);
      pref.putInt("TGT_FPS", TARGET_FPS);
      pref.putBool("DYN_CPU", ENABLE_DYNAMIC_CPU);
      pref.putInt("CFG_VER", 1);
    }
    pref.end();
  }
  recalculateDerivedParams();

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

  gpsSerial.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(100);

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

  xTaskCreatePinnedToCore(sensorTask, "SensorTaskCore0", 10240, NULL, 2, NULL,
                           0);
  xTaskCreatePinnedToCore(webServerTask, "WebTaskCore0", 8192, NULL, 1, NULL,
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

  SensorSnapshot snap;
  if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    snap = g_sensorData;
    xSemaphoreGive(g_stateMutex);
  }

  if (DISPLAY_REFRESH_MS == 0 ||
      (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS)) {
    static unsigned long lastFrameTime = 0;
    static float filteredFrameTimeMs = 16.6f;
    unsigned long frameDeltaMs = now - lastFrameTime;
    if (frameDeltaMs > 0) {
      lastFrameTime = now;
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

    if (!otaUpdateInProgress) {
      updateBigDisplay(snap);
      drawFpsOverlay();
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
      if (fillW >= 258 && !otaRebootShown) {
        otaRebootShown = true;
        delay(100);
        ESP.restart();
      }
    }
  }

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
  vTaskDelay(pdMS_TO_TICKS(1));
}
