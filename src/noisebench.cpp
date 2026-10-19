#include "noisebench.h"

#include "dashboard.h"

volatile bool noiseBenchAdcHold = false;
volatile bool noiseBenchTft = false;
volatile bool noiseBenchHoldBl = false;

// The four analog sensor inputs, in report order. All are ADC1 channels, the
// only ones usable while the WiFi stack is active.
static const uint8_t NB_PINS[NOISE_BENCH_CHANNELS] = {
    BATTERY_SENSE_PIN, TEMP_SENSE_PIN, LIGHT_SENSOR_PIN, FUEL_TOUCH_PIN};
static const char *const NB_CH_IDS[NOISE_BENCH_CHANNELS] = {"bat", "cool", "ldr",
                                                            "fuel"};
// Normal-running conditions only (user decision 2026-10-19): no synthetic
// aggressors, just what the board does by itself.
static const char *const NB_RUN_IDS[NOISE_BENCH_RUNS] = {"idle", "tft", "bl10",
                                                         "bl90"};

// Shared result. Both accessors run on core 0 (web task and bench worker), so
// the portMUX critical section is enough and stays short.
static portMUX_TYPE nbMux = portMUX_INITIALIZER_UNLOCKED;
static NoiseBenchResult nbResult;

// One run: 4000 ticks at 1 kHz, all four channels read inside every tick so
// they share the same time window. The achieved interval is reported, never
// assumed.
static void sampleRun(NoiseBenchRun &out) {
  double sum[NOISE_BENCH_CHANNELS] = {0};
  double sumSq[NOISE_BENCH_CHANNELS] = {0};
  uint16_t mn[NOISE_BENCH_CHANNELS] = {4095, 4095, 4095, 4095};
  uint16_t mx[NOISE_BENCH_CHANNELS] = {0, 0, 0, 0};

  TickType_t lastWake = xTaskGetTickCount();
  unsigned long t0 = micros();
  for (uint32_t i = 0; i < NOISE_BENCH_SAMPLES; i++) {
    for (uint8_t c = 0; c < NOISE_BENCH_CHANNELS; c++) {
      int v = analogRead(NB_PINS[c]);
      sum[c] += (double)v;
      sumSq[c] += (double)v * (double)v;
      if (v < (int)mn[c]) mn[c] = (uint16_t)v;
      if (v > (int)mx[c]) mx[c] = (uint16_t)v;
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
  }
  unsigned long dtUs = micros() - t0;

  out.samples = NOISE_BENCH_SAMPLES;
  out.intervalMs =
      (float)dtUs / 1000.0f / (float)NOISE_BENCH_SAMPLES;
  for (uint8_t c = 0; c < NOISE_BENCH_CHANNELS; c++) {
    double mean = sum[c] / (double)NOISE_BENCH_SAMPLES;
    double var = sumSq[c] / (double)NOISE_BENCH_SAMPLES - mean * mean;
    if (var < 0.0) var = 0.0;  // round-off only
    float sigma = sqrtf((float)var);
    NoiseBenchChannel &s = out.ch[c];
    s.id = NB_CH_IDS[c];
    s.meanLsb = (float)mean;
    s.sigmaLsb = sigma;
    s.ppLsb = (float)(mx[c] - mn[c]);
    s.meanMv = s.meanLsb * ADC_VOLTS_FACTOR;
    s.sigmaMv = s.sigmaLsb * ADC_VOLTS_FACTOR;
    s.ppMv = s.ppLsb * ADC_VOLTS_FACTOR;
  }
}

static void publishRun(uint8_t index, const NoiseBenchRun &run) {
  portENTER_CRITICAL(&nbMux);
  nbResult.run[index] = run;
  nbResult.completed = index + 1;
  portEXIT_CRITICAL(&nbMux);
  for (uint8_t c = 0; c < NOISE_BENCH_CHANNELS; c++) {
    const NoiseBenchChannel &s = run.ch[c];
    logPrintf(
        "NB %s %s: mean=%.1f lsb (%.1f mV) sigma=%.2f lsb (%.2f mV) "
        "pp=%.0f lsb (%.1f mV)\n",
        run.id, s.id, s.meanLsb, s.meanMv, s.sigmaLsb, s.sigmaMv, s.ppLsb,
        s.ppMv);
  }
}

static void noiseBenchTask(void *pv) {
  (void)pv;
  const unsigned long jobStart = millis();
  const int prevDuty = currentBrightnessTarget;
  NoiseBenchRun job[NOISE_BENCH_RUNS];

  // Reserve the ADC for the whole job: the sensor task skips its four analog
  // reads while this is set. The settle delay lets an in-flight read finish.
  noiseBenchAdcHold = true;
  vTaskDelay(pdMS_TO_TICKS(200));
  logPrintf("NB: sampling bat,cool,ldr,fuel - sensor ADC reads paused\n");

  for (uint8_t r = 0; r < NOISE_BENCH_RUNS; r++) {
    job[r].id = NB_RUN_IDS[r];
    if (r == 1) noiseBenchTft = true;  // full-frame repaint every display tick
    if (r == 2) {
      noiseBenchHoldBl = true;  // stop auto-brightness / night mode writes
      applyBacklight(10);
    }
    if (r == 3) applyBacklight(90);
    vTaskDelay(pdMS_TO_TICKS(150));  // let the condition settle
    sampleRun(job[r]);
    if (r == 1) {
      noiseBenchTft = false;
      forceFullRedraw = true;  // repaint once at the normal cadence
    }
    publishRun(r, job[r]);
  }

  noiseBenchAdcHold = false;
  noiseBenchTft = false;
  ledcWrite(BL_DISPLAY, constrain(prevDuty, 0, 255));
  currentBrightnessTarget = prevDuty;
  noiseBenchHoldBl = false;
  forceFullRedraw = true;

  portENTER_CRITICAL(&nbMux);
  nbResult.state = NOISE_BENCH_DONE;
  nbResult.jobMs = millis() - jobStart;
  portEXIT_CRITICAL(&nbMux);
  logPrintf("NB DONE: %lu ms, sensor reads and backlight restored\n",
            (unsigned long)(millis() - jobStart));
  vTaskDelete(NULL);
}

uint8_t noiseBenchStart() {
  uint8_t prevState;
  portENTER_CRITICAL(&nbMux);
  if (nbResult.state == NOISE_BENCH_RUNNING) {
    portEXIT_CRITICAL(&nbMux);
    return NOISE_BENCH_START_BUSY;
  }
  prevState = nbResult.state;
  nbResult.state = NOISE_BENCH_RUNNING;
  nbResult.completed = 0;
  nbResult.jobMs = 0;
  portEXIT_CRITICAL(&nbMux);

  logPrintf("NB START: runs=%d samples=%d\n", NOISE_BENCH_RUNS,
            NOISE_BENCH_SAMPLES);
  if (xTaskCreatePinnedToCore(noiseBenchTask, "NoiseBench", 4096, NULL, 1, NULL,
                              0) != pdPASS) {
    portENTER_CRITICAL(&nbMux);
    nbResult.state = prevState;
    portEXIT_CRITICAL(&nbMux);
    logPrintf("NB: no room for the worker task (free heap %lu)\n",
              (unsigned long)ESP.getFreeHeap());
    return NOISE_BENCH_START_FAILED;
  }
  return NOISE_BENCH_START_OK;
}

void noiseBenchSnapshot(NoiseBenchResult *out) {
  portENTER_CRITICAL(&nbMux);
  *out = nbResult;
  portEXIT_CRITICAL(&nbMux);
}
