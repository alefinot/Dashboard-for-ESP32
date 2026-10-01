#include "dashboard.h"
#include "bootinfo.h"
#include <stdarg.h>

char logBuf[LOG_BUF_SIZE];
volatile int logHead = 0;
volatile int logTail = 0;
volatile unsigned long logSequence = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

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

// The backlight LEDC channel is 8-bit (ledcAttach(BL_DISPLAY, 1000, 8)) while
// every caller works in percent. Before this, percent->duty was open-coded in
// four places and nothing limited it: a BACKLIGHT_BRIGHTNESS outside 0..100
// (an old NVS write, or a POST that bypassed validation) reached ledcWrite
// directly - a negative value wrapped to a huge duty and left the panel dark at
// boot with no explanation (issue #20). One conversion, one writer.
int backlightDuty(int percent) {
  if (percent < 0 || percent > 100) {
    logPrintf("Backlight: %d%% outside [0..100], clamped\n", percent);
    percent = constrain(percent, 0, 100);
  }
  return (percent * 255) / 100;
}

// The only place that drives the backlight channel directly. Also keeps
// currentBrightnessTarget in step, so the sleep/wake and splash fades ramp to
// the level the panel is actually at. Callers that fade gradually (splash,
// auto-brightness) set the target through backlightDuty() and write themselves.
void applyBacklight(int percent) {
  int duty = backlightDuty(percent);
  currentBrightnessTarget = duty;
  ledcWrite(BL_DISPLAY, duty);
}

// Config-save handoff (issue #10): the web task parses and writes the config,
// the display loop applies the panel-bus and CPU-frequency changes at a frame
// gap, and no frame is painted while the parameter group is being rewritten.
volatile bool pendingApplyBusConfig = false;
volatile bool pendingCpuReeval = false;
volatile bool configSaveInProgress = false;
volatile unsigned long configSaveStartMs = 0;

// Web-task watchdog state: disarmed when the device shows a fast-reboot loop
// so a watchdog can't brick the device by restarting it forever.
static bool watchdogDisabled = false;

// CPU busy probes: one task per core. Each probe increments its counter once
// per 1 ms tick slot in which its core ran nothing higher-priority, so
// (1 - ticks/elapsedMs) is the fraction of time the core was busy. (ccount
// cannot measure this: it ticks at the fixed XTAL rate regardless of load.)
static volatile uint32_t cpuProbeTicks[2] = {0, 0};

static void cpuProbeTask(void *pv) {
  int core = (int)(intptr_t)pv;
  for (;;) {
    cpuProbeTicks[core]++;
    vTaskDelay(1);
  }
}

void logPrintf(const char *fmt, ...) {
  char tmp[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);
  if (len > 0) {
    // Shared by every task (main/web/gps/sensor) plus the /api/serial
    // reader - guard the ring so two writers can't tear a line and the
    // reader never sees a half-written window. Serial print stays OUTSIDE
    // the critical section (UART TX is slow; keep the lock window tiny).
    portENTER_CRITICAL(&logMux);
    for (int i = 0; i < len && i < 256; i++) {
      logBuf[logHead] = tmp[i];
      logHead = (logHead + 1) % LOG_BUF_SIZE;
      if (logHead == logTail)
        logTail = (logTail + 1) % LOG_BUF_SIZE;
    }
    logSequence++;
    portEXIT_CRITICAL(&logMux);
    Serial.print(tmp);
  }
}

// Task handles, kept for one purpose only: /api/perf reports the FreeRTOS stack
// high-water mark of each task (issue #27). Nothing else uses them.
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t gpsTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;

// Issue #25: xTaskCreatePinnedToCore returns errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
// when the heap is tight, and that result used to be thrown away. A task that never
// started was invisible - you only noticed later as a frozen heartbeat, no Web UI or
// dead sensors. Now every start is checked, retried once on half the requested stack
// (a reduced stack is usually enough and beats a missing subsystem), and recorded in
// failedTasksMask so the boot log names exactly what is missing.
static uint8_t failedTasksMask = 0; // bit0 sensor, bit1 gps, bit2 web, bits3-4 probes

static bool startTask(const char *name, TaskFunction_t fn, uint32_t stackBytes,
                      UBaseType_t prio, BaseType_t core, void *param,
                      TaskHandle_t *outHandle, uint8_t failBit) {
  if (xTaskCreatePinnedToCore(fn, name, stackBytes, param, prio, outHandle,
                             core) == pdPASS)
    return true;
  logPrintf("Task %s: no room for a %lu B stack (free heap %lu) - retrying at %lu B\n",
            name, (unsigned long)stackBytes, (unsigned long)ESP.getFreeHeap(),
            (unsigned long)(stackBytes / 2));
  if (xTaskCreatePinnedToCore(fn, name, stackBytes / 2, param, prio, outHandle,
                             core) == pdPASS) {
    logPrintf("Task %s started on the reduced stack\n", name);
    return true;
  }
  failedTasksMask |= (1u << failBit);
  logPrintf("Task %s FAILED to start (free heap %lu) - that subsystem is offline this session\n",
            name, (unsigned long)ESP.getFreeHeap());
  return false;
}

// setCpuFrequencyMhz() returns false when the switch is refused (unsupported
// value, PLL/APB conflict). The config layer already snaps MANUAL_CPU_FREQ to
// 80/160/240 (issue #21), but the call can still fail - and logging the new
// frequency without checking the result is how "it says 240MHz but runs at 80"
// bug reports start (issue #34).
static bool applyCpuFreq(int mhz, const char *why) {
  if (setCpuFrequencyMhz(mhz)) return true;
  logPrintf("CPU: refused to switch to %dMHz (%s) - still running at %dMHz\n",
            mhz, why, (int)getCpuFrequencyMhz());
  return false;
}

void setup() {
  applyCpuFreq(240, "boot");
  Serial.setTxBufferSize(256);
  // arduino-esp32 3.x moved the UART1 console default to GPIO26/27 - pin it
  // explicitly to GPIO1/3 (the device's console wiring) to keep 2.x behavior.
  Serial.setPins(1, 3);
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
    NvsSession session("cfg", false);
    int cfgVer = -1;
    if (session.opened()) cfgVer = session.nvs.getInt("CFG_VER", -1);
    // If NVS could not be opened, seeding would overwrite a configured unit
    // with factory defaults - the opposite of a recovery. Skip instead.
    if (!session.opened())
      logPrintf("Config: NVS unreadable at boot - factory seeding skipped\n");
    else if (cfgVer < 5)
      seedNVSWithFactoryDefaults();
  }
  recalculateDerivedParams();
  logStoredWifiProfiles();

  // Recovery + watchdog guard:
  // - Serial "RESET" within the first 2s after boot: factory reset (NVS wipe).
  // - Hold BOOT (GPIO0) for 8s within the first 30s after boot: factory reset.
  // - Track fast reboot loops (RTC memory survives ESP.restart) so the
  //   web-task watchdog can never brick the device by rebooting it forever.
  // Boot/reboot forensics (Phase 0): reset reason, last-reboot tag, and the
  // fast-reboot-storm latch now live in bootinfo.cpp (bootinfo.h). A storm
  // still disables the web watchdog (as before) AND is exposed to the rest of
  // the firmware so clean auto-reboots can be *broken*, not just delayed.
  pinMode(0, INPUT_PULLUP);
  bootinfo_init();
  if (bootinfo_storm_active()) {
    watchdogDisabled = true;
    logPrintf("Warning: %u fast reboots in 2 min, auto-reboots suppressed for this boot\n",
              bootinfo_boot_count());
  }
  {
    unsigned long resetDeadline = millis() + 2000;
    // Rolling window, not an accumulating capture (issue #39): the boot banner,
    // log echo and any line-noise can pour well over 256 characters into UART0
    // inside this 2 s window, and a capture buffer that hits its limit stops
    // accepting input - so a later "RESET" could never match. Only the most
    // recent characters can answer the question "did RESET just arrive?", and
    // 32 bytes costs nothing.
    char bootWindow[32];
    int bootWindowLen = 0;
    bootWindow[0] = 0;
    while (millis() < resetDeadline) {
      while (Serial.available()) {
        char c = (char)Serial.read();
        if (bootWindowLen < (int)sizeof(bootWindow) - 1) {
          bootWindow[bootWindowLen++] = c;
        } else {
          memmove(bootWindow, bootWindow + 1, (size_t)bootWindowLen);
          bootWindow[bootWindowLen - 1] = c;
        }
        bootWindow[bootWindowLen] = 0;
        if (strstr(bootWindow, "RESET")) {
          logPrintf("Serial factory reset command received\n");
          factoryResetConfig();
          logPrintf("Factory reset done, rebooting\n");
          delay(100);
          bootinfo_tag_reboot("factory-reset");
          ESP.restart();
        }
      }
      delay(10);
    }
  }

  display.applyBusConfig();

  if (!ENABLE_DYNAMIC_CPU) {
    applyCpuFreq(MANUAL_CPU_FREQ, "manual mode");
  }

  logPrintf("Starting Dashboard++\n");

  g_stateMutex = xSemaphoreCreateMutex();
  if (g_stateMutex == NULL) {
    logPrintf("*** FATAL: g_stateMutex allocation failed ***\n");
    ESP.restart();
  }
  prefsMux = xSemaphoreCreateMutex();
  if (prefsMux == NULL) {
    logPrintf("*** FATAL: prefsMux allocation failed ***\n");
    ESP.restart();
  }
  gpsFixMux = xSemaphoreCreateMutex();
  if (gpsFixMux == NULL) {
    logPrintf("*** FATAL: gpsFixMux allocation failed ***\n");
    ESP.restart();
  }
  pinMode(CS_DISPLAY, OUTPUT);
  digitalWrite(CS_DISPLAY, HIGH);
  // arduino-esp32 3.x: ledcSetup/ledcAttachPin are gone - ledcAttach merges them
  // (first arg is the pin, returns bool; the channel is allocated internally).
  // Pin BL_DISPLAY = GPIO12.
  bool _ledcOk = ledcAttach(BL_DISPLAY, 1000, 8);
  logPrintf("ledcAttach pin=%d ok=%d\n", BL_DISPLAY, (int)_ledcOk);
  if (!_ledcOk) logPrintf("*** LEDC SETUP FAILED ***\n");
  ledcWrite(BL_DISPLAY, 0);
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
  // CHANGE, not FALLING (issue #30): the pulse width is measured from the
  // fall/rise pair instead of being sampled after a busy-wait inside the ISR.
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), hallSensorISR,
                  CHANGE);
  initFuelSensor();
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  // Physical trip-reset button to GND (issue #17); the internal pull-up keeps
  // the line high when nothing is pressed.
  pinMode(TRIP_RESET_PIN, INPUT_PULLUP);

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
      currentBrightnessTarget = backlightDuty(pct);
    } else {
      currentBrightnessTarget = backlightDuty(BACKLIGHT_BRIGHTNESS);
    }
  } else {
    currentBrightnessTarget = backlightDuty(BACKLIGHT_BRIGHTNESS);
  }
  // currentBrightnessTarget is always 0..255 now, so a 0 setting is simply a
  // dark panel: the fade loop below still runs its single step instead of
  // looking like a dead display.

  drawSplashBase();
  logPrintf("drawSplashBase done\n");
  logPrintf("splash fade: currentBrightnessTarget=%d\n", currentBrightnessTarget);
  int fadeStepCount = (currentBrightnessTarget / 8) + 1;
  for (int level = 0; level <= currentBrightnessTarget; level += 8) {
    ledcWrite(BL_DISPLAY, level);
    logPrintf("  fade step: level=%d\n", level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BL_DISPLAY, currentBrightnessTarget);
  logPrintf("  fade final: value=%d\n", currentBrightnessTarget);
  updateSplashProgress(20);

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GNSS_UART2_RX_PIN, GNSS_UART2_TX_PIN);
  gpsSerial.setTimeout(20);
  delay(100);
  configureGNSS();

  updateSplashProgress(40);

  if (prefsMux)
    xSemaphoreTake(prefsMux, portMAX_DELAY);
  preferences.begin("dashboard", false);
  double odoBoot = preferences.getDouble("odo", 0.0);
  preferences.end();
  odoSet(odoBoot);
  odoMarkSaved(odoBoot);
  if (prefsMux)
    xSemaphoreGive(prefsMux);

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
    ledcWrite(BL_DISPLAY, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BL_DISPLAY, 0);
  display.fillScreen(TFT_BLACK);

  SensorSnapshot emptySnap;
  updateBigDisplay(emptySnap);
  fadeStepCount = (currentBrightnessTarget / 8) + 1;
  for (int level = 0; level <= currentBrightnessTarget; level += 8) {
    ledcWrite(BL_DISPLAY, level);
    delay(FADE_DURATION_MS / fadeStepCount);
  }
  ledcWrite(BL_DISPLAY, currentBrightnessTarget);
  logPrintf("post-fade confirm: ledcWrite(%d, %d)\n", BL_DISPLAY, currentBrightnessTarget);
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
  startTask("SensorTaskCore1", sensorTask, 4096, 2, 1, NULL, &sensorTaskHandle, 0);
  startTask("GpsTaskCore0", gpsTask, 4096, 2, 0, NULL, &gpsTaskHandle, 1);
  startTask("WebTaskCore0", webServerTask, 6144, 1, 0, NULL, &webTaskHandle, 2);
  startTask("CPUProbe0", cpuProbeTask, 2048, 1, 0, (void *)0, NULL, 3);
  startTask("CPUProbe1", cpuProbeTask, 2048, 1, 1, (void *)1, NULL, 4);
  if (failedTasksMask)
    logPrintf("Boot finished with failed tasks (mask 0x%02X) - check the lines above\n",
              failedTasksMask);

  logPrintf("Setup done\n");
}

// Panel-bus reconfiguration and CPU-frequency switching requested by a config
// save. These used to run inside the POST /api/config handler on core 0 while
// this core was drawing: _bus_instance.config() on a live bus during a
// LovyanGFX transaction (corrupted frames at best), and an APB/clock change
// under an in-flight SPI transfer. Both are applied here, at the frame gap,
// before anything is drawn (issue #10).
static void processConfigApply() {
  if (pendingApplyBusConfig) {
    pendingApplyBusConfig = false;
    display.applyBusConfig();
    forceFullRedraw = true;
  }
  if (pendingCpuReeval) {
    pendingCpuReeval = false;
    uint32_t freq = ENABLE_DYNAMIC_CPU ? 240 : MANUAL_CPU_FREQ;
    if (getCpuFrequencyMhz() != freq) {
      if (applyCpuFreq((int)freq, "config change"))
        logPrintf("CPU: %dMHz (config change)\n", (int)freq);
    }
  }
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
    applyBacklight(pendingBacklightValue);
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
        autoBrightTarget = backlightDuty(pct);
        if (autoBrightPwmF < 0.0f) autoBrightPwmF = (float)autoBrightTarget;
      }
    }
    if (autoBrightTarget >= 0) {
      currentBrightnessTarget = autoBrightTarget;
      float alpha = 1.0f - expf(-(float)DISPLAY_REFRESH_MS / (float)AUTO_BRIGHT_FADE_MS);
      autoBrightPwmF += ((float)autoBrightTarget - autoBrightPwmF) * alpha;
      if (abs(autoBrightTarget - (int)autoBrightPwmF) <= 1)
        autoBrightPwmF = (float)autoBrightTarget;
      // autoBrightPwmF is a smoothed *duty* (not a percent), so it cannot go
      // through applyBacklight(); the constrain keeps it inside the 8-bit range
      // the channel was configured with (issue #20).
      ledcWrite(BL_DISPLAY, constrain((int)autoBrightPwmF, 0, 255));
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
    logPrintf("HB: up=%lus fps=%.1f freq=%uMHz tgtFps=%d heap=%lu min=%lu maxAlloc=%lu sp=%d fallback=%d otaReq=%d memAct=%d wifi=%d rssi=%d sta=%d/%d/%u apClients=%u temp=%.1f maxFrame=%lums over24=%lu sMaxGap=%lums\n",
              millis() / 1000UL, (double)currentMeasuredFps,
              (unsigned)getCpuFrequencyMhz(), TARGET_FPS,
              (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap(), (int)speedSpriteValid(),
              (int)isSpeedFallback(),
              (int)otaMemReleaseRequested, (int)memSaverActive,
              (int)WiFi.status(), (int)WiFi.RSSI(),
              (int)staDbgPhase, staDbgNetIdx, (unsigned)staDbgReason,
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

  // Bus / CPU-frequency changes requested by the config-save handler, applied at
  // the safe point between frames (issue #10).
  processConfigApply();

  // Rebuilds the speed sprite dropped by either release above. Runs here, at a
  // safe point between frames, so the VLW parse + allocation never stalls a
  // draw frame (previously it ran mid-frame on the next speed change).
  ensureSpeedSprite();

  // A config save rewrites ~90 parameters as a group (layout geometry, offsets,
  // digit counts). Painting while that write is in flight can capture a
  // half-old/half-new set, so the screen keeps the previous frame until the
  // writer is done. Bounded, and non-blocking, so a writer that never clears
  // the flag cannot freeze the display.
  if (configSaveInProgress && (millis() - configSaveStartMs) < 2000) return;

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

  // DISPLAY_REFRESH_MS can never be 0: TARGET_FPS is band-checked to 5..120
  // (issue #36), so the old "unlimited" special case was dead code.
  if (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
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
    lastDisplayUpdate += DISPLAY_REFRESH_MS;
    if (now - lastDisplayUpdate > DISPLAY_REFRESH_MS)
      lastDisplayUpdate = now; else {
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
        bootinfo_tag_reboot("ota");
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
    // Snapshot reads only: the TinyGPS++ objects are owned by gpsTask on core 0
    // and their accessors are single-consumer (issue #9).
    GpsFixSnapshot fix;
    gpsSnapshotCopy(fix);
    double valLat = fix.lat, valLon = fix.lon;
    float gpsSpeed = fix.speedValid ? fix.speedKmh : 0.0f;
    float hdop = fix.hdopValid ? fix.hdop : 0.0f;
    float altitude = fix.altValid ? fix.altitudeM : 0.0f;

    logPrintf("[RAW] hallInt=%.1fms hallCnt=%lu fuelADC=%d fuelFlt=%.1f "
              "lightADC=%d batADC=%d tempADC=%d\n",
              hallIntUs / 1000.0f, hallCnt,
              rawFuelADC, filteredReading, rawLightADC,
              rawBatteryADC, rawTempADC);

    logPrintf("[VAL] spd=%.1fkmh src=%s bat=%.1fV engT=%.1fC fuel=%.1fL(%d%%) "
              "sat=%d hdop=%.1f alt=%.0fm lat=%.6f lon=%.6f gpsSpd=%.1f "
              "odo=%.1fkm trip=%.2fkm avg=%.1fkmh avgKml=%.1f "
              "instKml=%.1f accel=%.2fs\n",
              snap.currentSpeed, speedSrc, snap.batteryVoltage,
              snap.engineTemperature, snap.fuelLiters, snap.fuelPercentage,
              snap.satellites, hdop, altitude,
              valLat, valLon, gpsSpeed,
              snap.totalDistanceKm, tripDistanceKm,
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
  // CPU busy: probe tasks (one per core) tick once per 1 ms slot in which
  // their core ran nothing higher-priority, so busy = 1 - ticks/elapsed.
  static uint32_t lastProbeTicks[2] = {0, 0};
  static unsigned long lastCpuProbeCheck = 0;
  if (now - lastCpuProbeCheck >= 1000) {
    if (lastCpuProbeCheck == 0) {
      lastProbeTicks[0] = cpuProbeTicks[0];
      lastProbeTicks[1] = cpuProbeTicks[1];
      lastCpuProbeCheck = now;
    } else {
      unsigned long win = now - lastCpuProbeCheck;
      float busy = 1.0f -
                   ((float)((cpuProbeTicks[0] - lastProbeTicks[0]) +
                             (cpuProbeTicks[1] - lastProbeTicks[1])) / (float)(win * 2UL));
      cpuUsagePct = constrain(busy * 100.0f, 0.0f, 100.0f);
      lastProbeTicks[0] = cpuProbeTicks[0];
      lastProbeTicks[1] = cpuProbeTicks[1];
      lastCpuProbeCheck = now;
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
      if (applyCpuFreq(targetFreq, "dynamic scaling"))
        logPrintf("CPU: %dMHz (%.1f FPS, %.1fC)\n", targetFreq, currentAverageFps, cpuTemp);
    }
  }
  // BOOT-hold factory reset (recovery when a forgotten config PIN locks the
  // web UI): hold BOOT for 4 s, let go, hold again for 4 s, all inside the
  // first 30 s after boot.
  //
  // The release in the middle is the whole point, not decoration. GPIO0 is
  // shared with the USB-serial adapter - DTR drives it low through the
  // auto-reset transistor - so a host that keeps DTR asserted holds GPIO0 low
  // indefinitely. The old single 8 s hold therefore wiped the configuration on
  // every boot with nobody touching the board (observed on this bench twice:
  // any serial session that left DTR asserted erased the config 8 s after
  // power-on, which is indistinguishable from lost saves unless the log is
  // read). A stuck-low line can still pass stage 1, but it can never produce
  // the release stage 2 requires.
  static int bootHoldStage = 0;  // 1 first hold, 2 armed/waiting release, 3 gap, 4 second hold
  static unsigned long bootHoldStart = 0;
  static unsigned long bootHoldDeadline = 0;
  if (millis() < 30000) {
    bool bootLow = (digitalRead(0) == LOW);
    switch (bootHoldStage) {
      case 0:
        if (bootLow) {
          bootHoldStage = 1;
          bootHoldStart = millis();
        }
        break;
      case 1:
        if (!bootLow) {
          bootHoldStage = 0;
        } else if (millis() - bootHoldStart > 4000) {
          bootHoldStage = 2;
          bootHoldDeadline = millis() + 6000;
          logPrintf("BOOT held 4s: release BOOT and hold again for 4s to factory reset\n");
        }
        break;
      case 2:  // armed, but the required release has not happened
        if (!bootLow) {
          bootHoldStage = 3;
        } else if (millis() > bootHoldDeadline) {
          bootHoldStage = 0;
          logPrintf("Factory reset aborted: BOOT was never released - a host holding GPIO0 low cannot wipe the config\n");
        }
        break;
      case 3:  // released, waiting for the second press
        if (bootLow) {
          bootHoldStage = 4;
          bootHoldStart = millis();
        } else if (millis() > bootHoldDeadline) {
          bootHoldStage = 0;
          logPrintf("Factory reset aborted: BOOT was not held again within 6s\n");
        }
        break;
      case 4:
        if (!bootLow) {
          bootHoldStage = 0;
          logPrintf("Factory reset aborted: second BOOT hold ended before 4s\n");
        } else if (millis() - bootHoldStart > 4000) {
          logPrintf("BOOT held 4s, released, held 4s: factory reset\n");
          factoryResetConfig();
          logPrintf("Factory reset done, rebooting\n");
          delay(100);
          bootinfo_tag_reboot("factory-reset");
          ESP.restart();
        }
        break;
    }
  }

  // Web-task heartbeat watchdog: if the web server task stops advancing its
  // counter, the config page would be unreachable forever. Reboot to recover.
  // Disarmed only for the duration of a fast-reboot storm: the permanent
  // disarming let a later web wedge stay dead forever, so re-arm 3 minutes
  // after boot.
  // The 45s budget must exceed the web task's 30s self-heal (which restarts
  // the listen socket when handleClient() blocks on a slow AP client): a
  // weak-RSSI phone receiving the multi-KB config page can legitimately
  // stall handleClient() for 15-30s, and rebooting on that makes the AP
  // appear unstable. A genuinely wedged web task is still caught, just
  // after the self-heal window.
  if (watchdogDisabled && millis() > 180000) {
    watchdogDisabled = false;
    logPrintf("Web watchdog re-armed\n");
  }
  // Seed from the live counter (not 0) so the first check is a real
  // "did it advance since arming?" test, not a 0==0 comparison.
  static unsigned long lastWebLoop = (unsigned long)webLoopCount;
  static unsigned long webWatchdogDue = 0;
  if (webWatchdogDue == 0) webWatchdogDue = millis() + 60000;
  if (now >= webWatchdogDue) {
    if (webLoopCount != lastWebLoop) {
      lastWebLoop = webLoopCount;
      webWatchdogDue = now + 45000;
    } else if (!watchdogDisabled) {
      logPrintf("Web task stalled (heartbeat stopped), rebooting\n");
      delay(100);
      bootinfo_tag_reboot("web-watchdog");
      ESP.restart();
    }
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}
