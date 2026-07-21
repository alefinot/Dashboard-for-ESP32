#include "dashboard.h"
#include <stdarg.h>

char logBuf[LOG_BUF_SIZE];
volatile int logHead = 0;
volatile int logTail = 0;
volatile unsigned long logSequence = 0;

bool forceFullRedraw = false;
volatile bool pendingSleep = false;
volatile bool pendingReboot = false;

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
  }
}

void setup() {
  setCpuFrequencyMhz(240);
  Serial.setTxBufferSize(256);
  Serial.begin(115200);
  delay(50);

  processConfig(0);
  recalculateDerivedParams();

  // Apply SPI bus speed (loaded from NVS above) to the display bus before init.
  // The global display constructor already captured the default; this re-applies
  // the user-configured SPI_BUS_SPEED so the web UI setting actually takes effect.
  display.applyBusConfig();

  if (!ENABLE_DYNAMIC_CPU) {
    setCpuFrequencyMhz(MANUAL_CPU_FREQ);
  }

  logPrintf("Starting Dashboard++\n");

  g_stateMutex = xSemaphoreCreateMutex();
  pinMode(CS_DISPLAY, OUTPUT);
  digitalWrite(CS_DISPLAY, HIGH);
  pinMode(BL_DISPLAY, OUTPUT);
  digitalWrite(BL_DISPLAY, LOW);
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

  logPrintf("display.init() start\n");
  bool initOk = display.init();
  logPrintf("display.init() done ok=%d\n", (int)initOk);
  display.setRotation(DISPLAY_ROTATION);
  logPrintf("setRotation done\n");
  drawSplashBase();
  logPrintf("drawSplashBase done\n");
  analogWrite(BL_DISPLAY, 255);
  updateSplashProgress(20);

  gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(100);

  // Clear any corrupted saved config from the NEO-M8N's flash and restore
  // factory defaults (all constellations, 9600 baud, continuous tracking,
  // NMEA output).  No UBX tuning is applied afterwards — the factory config
  // works reliably with TinyGPSPlus.
  {
    // UBX builder helper (local, used only during this one-shot reset).
    auto sendUBX = [](uint8_t cls, uint8_t id, const uint8_t *p, uint16_t n) {
      uint8_t buf[96];
      buf[0] = 0xB5; buf[1] = 0x62; buf[2] = cls; buf[3] = id;
      buf[4] = n & 0xFF; buf[5] = (n >> 8) & 0xFF;
      memcpy(&buf[6], p, n);
      uint8_t ckA = 0, ckB = 0;
      for (uint16_t i = 2; i < 6 + n; i++) { ckA += buf[i]; ckB += ckA; }
      gpsSerial.write(buf, 6 + n);
      gpsSerial.write(ckA); gpsSerial.write(ckB);
      gpsSerial.flush();
    };

    // Baud recovery — try to reach the module at every common rate and tell
    // it to switch to 9600 (handles stale baud saves from prior sessions).
    static const uint32_t rates[] = {9600, 38400, 57600, 115200};
    for (uint8_t i = 0; i < 4; i++) {
      gpsSerial.begin(rates[i], SERIAL_8N1, RXD2, TXD2);
      delay(50);
      uint8_t prt[20] = {0};
      prt[0] = 1;            // UART1
      prt[4] = 0xC0; prt[5] = 0x08;  // mode = 8N1
      prt[8] = 0x80; prt[9] = 0x25;  // baud = 9600
      prt[12] = 0x03;                 // inProtoMask = NMEA + UBX
      prt[14] = 0x03;                 // outProtoMask = NMEA + UBX
      sendUBX(0x06, 0x00, prt, 20);
      delay(50);
    }
    gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);
    delay(100);

    // CFG-CFG (0x06 0x09): clear all config sections from BBR/flash.
    // 12-byte payload: clearMask=0xFFFFFFFF, saveMask=0, loadMask=0.
    logPrintf("GPS: Factory reset (clear all saved config)...\n");
    uint8_t cfg[12];
    memset(cfg, 0xFF, 4);      // clearMask = all bits
    memset(cfg + 4, 0, 8);     // saveMask=0, loadMask=0
    sendUBX(0x06, 0x09, cfg, 12);
    delay(100);

    // CFG-RST (0x06 0x04): cold reset with all nav data cleared.
    // navBbrMask=0xFFFF, resetMode=0x02 (cold start).
    uint8_t rst[4] = {0xFF, 0xFF, 0x02, 0x00};
    sendUBX(0x06, 0x04, rst, 4);
    logPrintf("GPS: Cold reset issued, waiting 3 s...\n");
    delay(3000);
    logPrintf("GPS: Ready (factory defaults, all constellations)\n");
  }

  updateSplashProgress(40);

  preferences.begin("dashboard", false);
  if (!preferences.isKey("odo"))
    preferences.putDouble("odo", INITIAL_ODOMETER_KM);
  totalDistanceKm = preferences.getDouble("odo", INITIAL_ODOMETER_KM);
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
  for (int b = 255; b >= 0; b -= 15) {
    analogWrite(BL_DISPLAY, b);
    delay(2);
  }
  analogWrite(BL_DISPLAY, 0);
  display.fillScreen(TFT_BLACK);

  if (ENABLE_SLEEP_AFTER_REBOOT) {
    logPrintf("Sleep test, deep sleep\n");
    ENABLE_SLEEP_AFTER_REBOOT = false;
    {
      Preferences pref;
      pref.begin("cfg", false);
      pref.putBool("SLP_RBT", false);
      pref.end();
    }
    showGoodbyeScreen(true);
  }

  runDisplaySelfTest();

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

  unsigned long now = millis();
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

    SensorSnapshot snap;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      snap = g_sensorData;
      xSemaphoreGive(g_stateMutex);
    }

    updateBigDisplay(snap);
    drawFpsOverlay();
    checkNightMode(snap);
  }

  static unsigned long lastTelemetryUpdate = 0;
  if (now - lastTelemetryUpdate >= TELEMETRY_REFRESH_MS) {
    lastTelemetryUpdate = now;
    SensorSnapshot snap;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      snap = g_sensorData;
      xSemaphoreGive(g_stateMutex);
    }
    // Always log to ring buffer (read by web UI when WiFi is on)
    logPrintf("%s|%.2fL(%d%%)|%.1fV|%.1fC|%dkmh|%dsat|%.1fkm|%.2fs|%.1ffps\n",
              snap.isGpsSpeedValid ? "GPS" : "HALL",
              snap.fuelLiters, snap.fuelPercentage, snap.batteryVoltage,
              snap.engineTemperature, (int)snap.currentSpeed, snap.satellites,
              snap.totalDistanceKm, snap.accelResultTime, currentMeasuredFps);
  }
  static unsigned long lastCpuScaleCheck = 0;
  if (now - lastCpuScaleCheck >= 1000) {
    lastCpuScaleCheck = now;
    uint32_t targetFreq;
    if (ENABLE_DYNAMIC_CPU) {
      if (currentAverageFps < (TARGET_FPS * 0.5f))
        targetFreq = 240;
      else if (currentAverageFps < (TARGET_FPS * 0.8f))
        targetFreq = 160;
      else
        targetFreq = 80;
      if (WiFi.getMode() != WIFI_OFF && targetFreq < 240)
        targetFreq = 240;
    } else {
      targetFreq = MANUAL_CPU_FREQ;
    }
    if (getCpuFrequencyMhz() != targetFreq) {
      setCpuFrequencyMhz(targetFreq);
      logPrintf("CPU: %dMHz (%.1f FPS)\n", targetFreq, currentAverageFps);
    }
  }
  vTaskDelay(pdMS_TO_TICKS(1));
}
