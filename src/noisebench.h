// Noise bench (master plan step 8 / sensor plan D1): a measurement instrument
// for the four sensor ADC inputs. It samples the same GPIOs the sensor
// pipeline reads, so the numbers are what the sensors actually see.
#pragma once
#include <Arduino.h>

#define NOISE_BENCH_RUNS 4
#define NOISE_BENCH_CHANNELS 4
#define NOISE_BENCH_SAMPLES 4000

enum : uint8_t {
  NOISE_BENCH_NEVER = 0,
  NOISE_BENCH_RUNNING = 1,
  NOISE_BENCH_DONE = 2,
};

enum : uint8_t {
  NOISE_BENCH_START_OK = 0,
  NOISE_BENCH_START_BUSY = 1,
  NOISE_BENCH_START_FAILED = 2,
};

// Statistics for one channel in one run. "Lsb" is the raw ADC code domain
// (what the sensor conversions use); "Mv" is that code times ADC_VOLTS_FACTOR.
struct NoiseBenchChannel {
  const char *id;
  float meanMv;
  float sigmaMv;
  float ppMv;
  float meanLsb;
  float sigmaLsb;
  float ppLsb;
};

struct NoiseBenchRun {
  const char *id;
  uint32_t samples;
  float intervalMs;
  NoiseBenchChannel ch[NOISE_BENCH_CHANNELS];
};

struct NoiseBenchResult {
  uint8_t state;      // NOISE_BENCH_NEVER / RUNNING / DONE
  uint8_t completed;  // finished runs, 0..NOISE_BENCH_RUNS
  uint32_t jobMs;
  NoiseBenchRun run[NOISE_BENCH_RUNS];
};

// Polled by the other tasks while a job runs. One writer (the bench task), one
// reader per subsystem, no compound state.
extern volatile bool noiseBenchAdcHold;  // sensor task pauses its ADC reads
extern volatile bool noiseBenchTft;      // display loop force-redraws frames
extern volatile bool noiseBenchHoldBl;   // backlight auto-control suspended

// Starts a job in a transient worker task on core 0. Returns a
// NOISE_BENCH_START_* code. The worker deletes itself when the job is done.
uint8_t noiseBenchStart();

// Copies the current result (guarded). Cheap: one struct copy.
void noiseBenchSnapshot(NoiseBenchResult *out);
