#include "dashboard.h"
#include <driver/touch_pad.h>

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

static void feedGpsLine(char *line); // defined in the UBX parser section below

// ----------------------------------------------------------------------------
// Demo mode: simulate the RAW sensors instead of faking final values.
// The hardware reads below are swapped for synthetic raw values (ADC counts,
// hall pulses, magnetometer axes and a synthetic GPS NMEA stream), so the
// ENTIRE real processing pipeline runs unchanged on simulated data: EMA
// filters, fuel touch-table interpolation, NTC->temperature math, hall
// speed, GPS parse/fusion (HAL/GPS/G+H), odometer, fuel consumption and the
// acceleration timer. The only simulated outputs are the raw readings
// themselves.
// ----------------------------------------------------------------------------

// Steady 20°/s heading rotation, shared by the compass and the GPS course so
// the two always agree.
static float demoSimHeadingDeg(unsigned long t) {
  return fmodf((float)t * 0.02f, 360.0f);
}

// Scripted drive cycle - the single source of truth for hall + GPS + all
// speed consumers. 0-8s: accelerate 0 -> ~55 km/h (crosses ACCEL_TARGET_SPEED
// well before the ACCEL_MAX_TIME cap, so the accel timer finishes with a real
// time). 8-60s: cruise climbing to ~95 km/h with ripple. 60-85s: brake to
// standstill. 85-90s: idle stop (accel timer re-arms, tank refuels). Then the
// loop restarts.
static float demoSimSpeedKmph(unsigned long t) {
  float phase = fmodf((float)t, 90000.0f);
  float v = 0.0f;
  if (phase < 8000.0f) {
    v = 55.0f * (phase / 8000.0f);
  } else if (phase < 60000.0f) {
    float p = phase - 8000.0f;
    v = 55.0f + 40.0f * (p / 52000.0f) + 5.0f * sinf(p / 4500.0f) +
        2.0f * sinf(p / 800.0f);
  } else if (phase < 85000.0f) {
    v = 95.0f * (1.0f - (phase - 60000.0f) / 25000.0f);
  }
  return (v < 0.0f) ? 0.0f : v;
}

// Inverse of the fuel touch-table: map a desired fuel level (liters) back to
// the raw ADC reading processFuelSensor() would have to see to compute it.
static int demoAdcForFuelLiters(float liters) {
  int n = FUEL_TOUCH_POINTS;
  if (n < 2) return touchTable[0];
  float l = constrain(liters, 0.0f, (float)(n - 1));
  int i = (int)l;
  if (i >= n - 1) i = n - 2;
  float frac = l - (float)i;
  float reading = (float)touchTable[i] +
                  frac * (float)(touchTable[i + 1] - touchTable[i]);
  return (int)(reading + 0.5f);
}

// Inverse of the NTC divider math: the raw ADC count that processTemperature-
// Sensor() would convert into the given degrees C. Kept clear of the
// 100/4000 "sensor fault" clamp.
static int demoAdcForTempC(float c) {
  // The model (before the NTC_TEMP_OFFSET bias) must yield c - NTC_TEMP_OFFSET,
  // so that processTemperatureSensor() shows exactly c.
  float cModel = c - NTC_TEMP_OFFSET;
  float tKelvin = cModel + 273.15f;
  float r = NTC_R25 * expf(NTC_BETA * (1.0f / tKelvin - NTC_INV_ROOM_KELVIN));
  float vOut = 3.3f * r / (r + NTC_R_BALANCE);
  int adc = (int)(vOut / ADC_VOLTS_FACTOR);
  return constrain(adc, 150, 3900);
}

// Inverse of the battery divider formula (processBatterySensor).
static int demoAdcForBatteryVoltage(float v) {
  int adc = (int)((((v - BATTERY_OFFSET) / (ADC_VOLTS_FACTOR * BATTERY_SCALE)) + 0.5f));
  return constrain(adc, 0, 4095);
}

// Simulated distance accumulated by the REAL odometer math (hall pulses and
// GPS movement). Kept separate from totalDistanceKm so demo mileage is
// display-only and can never reach NVS (see updateGPSOdometer).
static double demoOdoKm = 0.0;

// History of the last accepted hall pulse intervals (ring, fixed capacity).
// A single spurious pulse on the hall pin (EMI, bounce, passing magnet)
// used to land in hallPulseIntervalUs and briefly report several hundred
// km/h: with the default 1650 mm tire one 30 ms noise blip is 198 km/h.
// getHallSpeed() takes the median of the last HALL_MEDIAN_SAMPLES entries
// (WebUI-tunable; 1 = raw single interval, zero lag) so a corrupt blip can
// no longer move the reading unless a majority is corrupt. The real ISR
// and the demo simulator below both append through this ring.
static const unsigned int HALL_MEDIAN_MAX = 31; // ring capacity (compile-time)
static volatile unsigned long
    hallIntervalHist[HALL_MEDIAN_MAX] = {0};
static volatile unsigned int hallHistWriteIdx = 0;

// Injects synthetic hall pulses on the sensor task tick: interval is derived
// from the simulated speed (so getHallSpeed() reports it) and the pulse count
// accumulates real distance through updateGPSOdometer's hall path.
void simulateRawSensors() {
  unsigned long now = millis();
  static unsigned long lastSimTickMs = 0;
  static float pulseFraction = 0.0f;
  if (lastSimTickMs == 0) lastSimTickMs = now;
  float v = demoSimSpeedKmph(now);
  if (v > 0.5f) {
    unsigned long intervalUs = (unsigned long)(WHEEL_SPEED_FACTOR / v);
    float dtH = (float)(now - lastSimTickMs) / 3600000.0f;
    pulseFraction += (v * dtH) / (float)WHEEL_DIST_PER_PULSE_KM;
    int pulses = (int)pulseFraction;
    if (pulses > 0) pulseFraction -= (float)pulses;
    portENTER_CRITICAL(&hallMux);
    lastHallPulseTimeUs = micros();
    hallPulseIntervalUs = intervalUs;
    hallIntervalHist[hallHistWriteIdx] = intervalUs;
    hallHistWriteIdx = (hallHistWriteIdx + 1u) % HALL_MEDIAN_MAX;
    if (pulses > 0) hallPulseCount += (unsigned long)pulses;
    portEXIT_CRITICAL(&hallMux);
  }
  lastSimTickMs = now;
}

// Synthesizes one GPRMC + GPGGA pair per second and feeds them through
// gps.encode() so TinyGPSPlus parses genuine sentences: satellites, speed,
// location, date/time and the fusion logic all run for real. The date/time
// comes from the real system clock when one is set (never applied back - see
// gpsTask), else from a simulated wall clock.
static void demoGpsSentence() {
  static double demoLat = 48.1372, demoLon = 11.5755;
  unsigned long t = millis();
  float v = demoSimSpeedKmph(t);
  // Small independent wobble vs. the hall speed so the fusion occasionally
  // lands in the G+H band (GPS_MIN_DEV_KMH < delta < MAX_SPEED_DELTA_KMH).
  float gpsV = v + 2.0f * sinf((float)t / 20000.0f);
  if (gpsV < 0.0f) gpsV = 0.0f;
  float course = demoSimHeadingDeg(t);
  int sats = 6 + (int)(4.0f * (0.5f + 0.5f * sinf((float)t / 12000.0f)));
  bool hasFix = v > 0.5f;

  if (hasFix) {
    double dKm = v / 3600.0;
    double rad = course * (M_PI / 180.0f);
    demoLat += dKm * cos(rad) / 111.32;
    demoLon += dKm * sin(rad) / (111.32 * cos(demoLat * M_PI / 180.0f));
  }

  int yr, mo, da, hr, mi, sc;
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec > 1000000000) {
      struct tm *g = gmtime(&tv.tv_sec);
      yr = g->tm_year % 100; mo = g->tm_mon + 1; da = g->tm_mday;
      hr = g->tm_hour; mi = g->tm_min; sc = g->tm_sec;
    } else {
      unsigned long s = 10UL * 3600UL + t / 1000UL;
      yr = 26; mo = 7; da = 16;
      hr = (s / 3600) % 24; mi = (s / 60) % 60; sc = s % 60;
    }
  }

  char alat[20], alon[20];
  double aLat = fabs(demoLat);
  int dLat = (int)aLat;
  int mLat = (int)(((aLat - dLat) * 60.0) * 10000.0 + 0.5);
  if (mLat >= 600000) { mLat -= 600000; dLat++; }
  sprintf(alat, "%02d%02d.%04d", dLat, mLat / 10000, mLat % 10000);
  double aLon = fabs(demoLon);
  int dLon = (int)aLon;
  int mLon = (int)(((aLon - dLon) * 60.0) * 10000.0 + 0.5);
  if (mLon >= 600000) { mLon -= 600000; dLon++; }
  sprintf(alon, "%03d%02d.%04d", dLon, mLon / 10000, mLon % 10000);

  char line[130];
  float knots = gpsV / 1.852f;
  sprintf(line, "$GPRMC,%02d%02d%02d.00,%c,%s,%c,%s,%c,%.2f,%.1f,%02d%02d%02d*",
          hr, mi, sc, hasFix ? 'A' : 'V', alat, demoLat < 0 ? 'S' : 'N',
          alon, demoLon < 0 ? 'W' : 'E', knots, course, da, mo, yr);
  feedGpsLine(line);

  sprintf(line, "$GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,%d,%02d,%.1f,%.1f,M,0.0,M,,*",
          hr, mi, sc, alat, demoLat < 0 ? 'S' : 'N', alon, demoLon < 0 ? 'W' : 'E',
          hasFix ? 1 : 0, sats, 1.2f, 150.0f);
  feedGpsLine(line);
}

// ----------------------------------------------------------------------------
// Compass driver (I2C) - auto-detects the chip actually fitted:
//   QMC5883P @ 0x2C  (newest revision, used by BZGNSS P25 Pro: CHIPID 0x80,
//                     data at 0x01-0x06 LSB-first, mode 0x0A, config 0x0B)
//   QMC5883L @ 0x0D  (standard)
//   VCM5883L @ 0x0C  (older BZGNSS units)
//   HMC5883L @ 0x1E  (oldest modules, MSB-first data at 0x03)
// ----------------------------------------------------------------------------
#define QMC5883P_ADDR   0x2C
#define QMC5883P_CHIPID 0x00
#define QMC5883P_DATA   0x01
#define QMC5883P_STATUS 0x09
#define QMC5883P_MODE   0x0A
#define QMC5883P_CONFIG 0x0B

#define QMC5883L_ADDR   0x0D
#define VCM5883L_ADDR   0x0C
#define HMC5883L_ADDR   0x1E

#define QMC5883L_X_LSB  0x00
#define QMC5883L_CTRL1  0x0B
#define HMC5883L_CFGA   0x00
#define HMC5883L_CFGB   0x01
#define HMC5883L_MODE   0x02
#define HMC5883L_X_MSB  0x03

enum CompassChip { COMPASS_NONE = 0, COMPASS_QMC = 1, COMPASS_HMC = 2,
                   COMPASS_P = 3 };

static CompassChip compassChip = COMPASS_NONE;
static uint8_t compassAddr = 0;
bool compassReady = false;

static bool compassWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool compassRead6(uint8_t addr, uint8_t startReg, bool msbFirst,
                         int16_t &x, int16_t &y, int16_t &z) {
  Wire.beginTransmission(addr);
  Wire.write(startReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)addr, 6) < 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (msbFirst) {
    x = (int16_t)(b[0] << 8 | b[1]);
    y = (int16_t)(b[2] << 8 | b[3]);
    z = (int16_t)(b[4] << 8 | b[5]);
  } else {
    x = (int16_t)(b[1] << 8 | b[0]);
    y = (int16_t)(b[3] << 8 | b[2]);
    z = (int16_t)(b[5] << 8 | b[4]);
  }
  return true;
}

static void compassDumpRegs(uint8_t addr) {
  for (int off = 0; off < 0x40; off += 16) {
    Wire.beginTransmission(addr);
    Wire.write(off);
    if (Wire.endTransmission() != 0) break;
    Wire.requestFrom((int)addr, 16);
    uint8_t buf[16];
    uint8_t n = 0;
    while (Wire.available() && n < 16) buf[n++] = Wire.read();
    if (n == 0) break;
    char line[128];
    int p = snprintf(line, sizeof(line), "Compass: regs[0x%02X]=", off);
    for (uint8_t i = 0; i < n && p < (int)sizeof(line) - 4; i++)
      p += snprintf(line + p, sizeof(line) - p, "%02X ", buf[i]);
    logPrintf("%s\n", line);
  }
}

// Full-bus scan + compass candidate check on a given SDA/SCL pin assignment.
// Returns true and initializes the chip if a compass is found.
static bool compassScanBus(uint8_t sdaPin, uint8_t sclPin) {
  gpio_pullup_en((gpio_num_t)sdaPin);
  gpio_pullup_en((gpio_num_t)sclPin);
  Wire.begin(sdaPin, sclPin);
  delay(10);

  uint8_t foundDevices[8];
  uint8_t devCount = 0;
  for (uint8_t addr = 1; addr <= 126; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0 && devCount < 8)
      foundDevices[devCount++] = addr;
  }
  for (uint8_t i = 0; i < devCount; i++)
    logPrintf("Compass: I2C device found @0x%02X\n", foundDevices[i]);

  static const uint8_t candidates[] = {QMC5883P_ADDR, QMC5883L_ADDR,
                                       VCM5883L_ADDR, HMC5883L_ADDR};
  for (uint8_t addr : candidates) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    if (addr == HMC5883L_ADDR) {
      if (!compassWriteReg(addr, HMC5883L_CFGA, 0x70) || // 8 samples, 15 Hz
          !compassWriteReg(addr, HMC5883L_CFGB, 0x20) || // 1.3 Ga gain
          !compassWriteReg(addr, HMC5883L_MODE, 0x00))   // continuous
        return false;
      compassChip = COMPASS_HMC;
    } else if (addr == QMC5883P_ADDR) {
      // Verify the chip ID (0x80) before configuring a QMC5883P
      Wire.beginTransmission(addr);
      Wire.write(QMC5883P_CHIPID);
      if (Wire.endTransmission() != 0) return false;
      Wire.requestFrom((int)addr, 1);
      if (!Wire.available() || Wire.read() != 0x80) continue;
      if (!compassWriteReg(addr, QMC5883P_MODE, 0xCF) ||   // continuous 200 Hz
          !compassWriteReg(addr, QMC5883P_CONFIG, 0x08))   // +/-8G, set/reset
        return false;
      compassChip = COMPASS_P;
    } else {
      if (!compassWriteReg(addr, QMC5883L_CTRL1, 0x1D)) // cont 200Hz 8G 512osr
        return false;
      compassChip = COMPASS_QMC;
    }
    compassAddr = addr;
    compassReady = true;
    const char *name = (compassChip == COMPASS_HMC) ? "HMC5883L"
                      : (compassChip == COMPASS_P)  ? "QMC5883P"
                                                    : "QMC/VCM5883L";
    logPrintf("Compass: %s detected @0x%02X (SDA=%u, SCL=%u)\n",
              name, compassAddr, sdaPin, sclPin);
    return true;
  }

  logPrintf("Compass: orientation SDA=%u SCL=%u - %u device(s), no known "
            "compass address\n", sdaPin, sclPin, devCount);
  for (uint8_t i = 0; i < devCount; i++)
    compassDumpRegs(foundDevices[i]);
  return false;
}

bool initCompass() {
  // Try the normal assignment, then the mirrored one (module pin order 5=SCL,
  // 6=SDA vs. many boards labeling the other way) - no wire swapping needed.
  if (compassScanBus(COMPASS_SDA, COMPASS_SCL)) return true;
  if (compassScanBus(COMPASS_SCL, COMPASS_SDA)) return true;
  logPrintf("Compass: no compass chip found in either SDA/SCL orientation - "
            "check wire continuity from module pins 5/6\n");
  return false;
}

int16_t compassRawX = 0, compassRawY = 0, compassRawZ = 0;

// Hard-iron calibration: X/Y/Z offsets removed from the raw readings before
// the heading is computed. Captured live while the user rotates the device
// (min/max tracking) and persisted to NVS ("cfg" namespace, CMP_CAL_*).
int16_t COMPASS_CAL_X = 0, COMPASS_CAL_Y = 0, COMPASS_CAL_Z = 0;
// Tilt compensation: unit "up" axis (rotation-axis normal fitted during
// calibration) in sensor coordinates, scaled by 32767. Default (0,0,1) = flat
// mount, which degrades to plain atan2(y,x). Persisted as CMP_TILT_*.
int16_t COMPASS_CAL_TX = 0, COMPASS_CAL_TY = 0, COMPASS_CAL_TZ = 32767;
// Soft-iron scale correction: per-axis multipliers applied after the offset
// subtraction so the (soft-iron-distorted) reading sphere becomes round
// again. Captured from the calibration spans and persisted as CMP_SCALE_*.
// Default 1.0 = no correction.
float COMPASS_CAL_SCALE_X = 1.0f, COMPASS_CAL_SCALE_Y = 1.0f,
      COMPASS_CAL_SCALE_Z = 1.0f;
// COMPASS_TILT_COMP: 0 = flat assumption (robust to a changing tilt, default
// for a handlebar-mounted dashboard); 1 = use the calibrated tilt axis (only
// correct while the mounting tilt is constant).
int COMPASS_TILT_COMP = 0;
volatile bool compassCalActive = false;
unsigned long compassCalEndTime = 0;
int16_t compassCalMinX = 0, compassCalMaxX = 0;
int16_t compassCalMinY = 0, compassCalMaxY = 0;
int16_t compassCalMinZ = 0, compassCalMaxZ = 0;
char compassCalResult[128] = "";

// Raw (X,Y,Z) ring buffer captured during calibration, used to fit the plane
// of the rotation circle (its normal = sensor "up" axis = tilt).
#define COMPASS_CAL_MAX_SAMPLES 600
static int16_t calSamples[COMPASS_CAL_MAX_SAMPLES][3];
static uint16_t calSampleCount = 0;
static unsigned long lastCalSampleMs = 0;

// qsort comparator for int16_t.
static int cmpInt16(const void *a, const void *b) {
  return (int)*(const int16_t *)a - (int)*(const int16_t *)b;
}

// Per-axis outlier trim: sort the captured samples and discard the top and
// bottom COMPASS_CAL_TRIM_PCT percent before taking min/max, so a single
// spike (motor interference, ...) can't corrupt the offset.
#define COMPASS_CAL_TRIM_PCT 5
static int16_t calSortBuf[COMPASS_CAL_MAX_SAMPLES];
static void trimmedMinMax(uint8_t axis, uint16_t n, int16_t &lo, int16_t &hi) {
  for (uint16_t i = 0; i < n; i++) calSortBuf[i] = calSamples[i][axis];
  qsort(calSortBuf, n, sizeof(int16_t), cmpInt16);
  uint16_t cut = n * COMPASS_CAL_TRIM_PCT / 100;
  lo = calSortBuf[cut];
  hi = calSortBuf[n - 1 - cut];
}

void compassCalStart(unsigned int seconds) {
  if (!compassReady || seconds == 0) return;
  compassCalActive = true;
  compassCalEndTime = millis() + (unsigned long)seconds * 1000UL;
  compassCalMinX = compassCalMaxX = compassRawX;
  compassCalMinY = compassCalMaxY = compassRawY;
  compassCalMinZ = compassCalMaxZ = compassRawZ;
  calSampleCount = 0;
  lastCalSampleMs = 0;
  compassCalResult[0] = 0;
  logPrintf("Compass: calibration started - rotate the device slowly for "
            "%us around a VERTICAL axis, keeping the module at its mounting "
            "angle (keep metal/magnets away)\n", seconds);
}

void compassCalCancel() {
  compassCalActive = false;
  logPrintf("Compass: calibration cancelled\n");
}

static void compassCalFinish() {
  compassCalActive = false;
  if (calSampleCount < 32) {
    snprintf(compassCalResult, sizeof(compassCalResult),
             "Calibration aborted: too few samples (%d)", (int)calSampleCount);
    logPrintf("Compass: %s\n", compassCalResult);
    return;
  }
  // Outlier-trimmed min/max per axis (spike rejection).
  int16_t loX, hiX, loY, hiY, loZ, hiZ;
  trimmedMinMax(0, calSampleCount, loX, hiX);
  trimmedMinMax(1, calSampleCount, loY, hiY);
  trimmedMinMax(2, calSampleCount, loZ, hiZ);
  int32_t offX = ((int32_t)loX + (int32_t)hiX) / 2;
  int32_t offY = ((int32_t)loY + (int32_t)hiY) / 2;
  int32_t offZ = ((int32_t)loZ + (int32_t)hiZ) / 2;
  int32_t spanX = (int32_t)hiX - loX;
  int32_t spanY = (int32_t)hiY - loY;
  bool lowSpan = (spanX < 200 || spanY < 200);
  if (lowSpan) {
    snprintf(compassCalResult, sizeof(compassCalResult),
             "Calibration FAILED (span %ld/%ld) - rotate the unit in full "
             "circles, 30s", (long)spanX, (long)spanY);
    logPrintf("Compass: %s\n", compassCalResult);
    return;  // don't save garbage
  }

  // Soft-iron scale correction: equalize the X/Y spans (the rotation circle).
  // The Z span is set by the mounting tilt, not by soft iron, so it isn't
  // updated here (kept as is; default 1.0).
  int32_t avgXY = (spanX + spanY) / 2;
  COMPASS_CAL_SCALE_X = (float)avgXY / (float)spanX;
  COMPASS_CAL_SCALE_Y = (float)avgXY / (float)spanY;
  // Safety clamp: scales outside this range are not plausible.
  if (COMPASS_CAL_SCALE_X < 0.2f || COMPASS_CAL_SCALE_X > 5.0f) COMPASS_CAL_SCALE_X = 1.0f;
  if (COMPASS_CAL_SCALE_Y < 0.2f || COMPASS_CAL_SCALE_Y > 5.0f) COMPASS_CAL_SCALE_Y = 1.0f;

  COMPASS_CAL_X = (int16_t)offX;
  COMPASS_CAL_Y = (int16_t)offY;
  COMPASS_CAL_Z = (int16_t)offZ;
  { Preferences p; p.begin("cfg", false);
    p.putInt("CMP_CAL_X", COMPASS_CAL_X);
    p.putInt("CMP_CAL_Y", COMPASS_CAL_Y);
    p.putInt("CMP_CAL_Z", COMPASS_CAL_Z);
    p.putFloat("CMP_SCALE_X", COMPASS_CAL_SCALE_X);
    p.putFloat("CMP_SCALE_Y", COMPASS_CAL_SCALE_Y);
    p.putFloat("CMP_SCALE_Z", COMPASS_CAL_SCALE_Z);
    p.end(); }

  // Tilt fit: the samples trace a circle in 3D whose plane is perpendicular to
  // the rotation (vertical) axis. Consecutive edge vectors d_i, d_{i+1}
  // both lie in that plane, so d_i x d_{i+1} points along its normal; the
  // sum over the whole rotation cancels wobble and yields the "up" axis in
  // sensor coordinates. A flat calibration naturally fits (0,0,1). The fit
  // runs on the offset- and scale-corrected samples, matching the heading
  // math.
  char tiltTxt[64] = "";
  float scx = COMPASS_CAL_SCALE_X, scy = COMPASS_CAL_SCALE_Y, scz = COMPASS_CAL_SCALE_Z;
  {
    int64_t nx = 0, ny = 0, nz = 0;
    for (uint16_t i = 1; i + 1 < calSampleCount; i++) {
      float x0 = (float)(calSamples[i - 1][0] - COMPASS_CAL_X) * scx;
      float y0 = (float)(calSamples[i - 1][1] - COMPASS_CAL_Y) * scy;
      float z0 = (float)(calSamples[i - 1][2] - COMPASS_CAL_Z) * scz;
      float x1 = (float)(calSamples[i][0] - COMPASS_CAL_X) * scx;
      float y1 = (float)(calSamples[i][1] - COMPASS_CAL_Y) * scy;
      float z1 = (float)(calSamples[i][2] - COMPASS_CAL_Z) * scz;
      float x2 = (float)(calSamples[i + 1][0] - COMPASS_CAL_X) * scx;
      float y2 = (float)(calSamples[i + 1][1] - COMPASS_CAL_Y) * scy;
      float z2 = (float)(calSamples[i + 1][2] - COMPASS_CAL_Z) * scz;
      float dx1 = x1 - x0, dy1 = y1 - y0, dz1 = z1 - z0;
      float dx2 = x2 - x1, dy2 = y2 - y1, dz2 = z2 - z1;
      nx += (int64_t)dy1 * dz2 - (int64_t)dz1 * dy2;
      ny += (int64_t)dz1 * dx2 - (int64_t)dx1 * dz2;
      nz += (int64_t)dx1 * dy2 - (int64_t)dy1 * dx2;
    }
    float nf[3] = {(float)nx, (float)ny, (float)nz};
    float mag = sqrtf(nf[0] * nf[0] + nf[1] * nf[1] + nf[2] * nf[2]);
    if (mag > 1.0f) {
      nf[0] /= mag;
      nf[1] /= mag;
      nf[2] /= mag;
      // Planarity check: mean |p . n| vs mean |p| over the centered samples.
      // Near 0 = clean circle; >= ~0.6 = the module was wobbled, fit is junk.
      float sumDot = 0.0f, sumR = 0.0f;
      for (uint16_t i = 0; i < calSampleCount; i++) {
        float px = (float)(calSamples[i][0] - COMPASS_CAL_X) * scx;
        float py = (float)(calSamples[i][1] - COMPASS_CAL_Y) * scy;
        float pz = (float)(calSamples[i][2] - COMPASS_CAL_Z) * scz;
        sumDot += fabsf(px * nf[0] + py * nf[1] + pz * nf[2]);
        sumR += sqrtf(px * px + py * py + pz * pz);
      }
      float ratio = (sumR > 0.01f) ? sumDot / sumR : 1.0f;
      if (ratio < 0.6f) {
        COMPASS_CAL_TX = (int16_t)(nf[0] * 32767.0f);
        COMPASS_CAL_TY = (int16_t)(nf[1] * 32767.0f);
        COMPASS_CAL_TZ = (int16_t)(nf[2] * 32767.0f);
        { Preferences p; p.begin("cfg", false);
          p.putInt("CMP_TILT_X", COMPASS_CAL_TX);
          p.putInt("CMP_TILT_Y", COMPASS_CAL_TY);
          p.putInt("CMP_TILT_Z", COMPASS_CAL_TZ);
          p.end(); }
        snprintf(tiltTxt, sizeof(tiltTxt), " tilt=%.2f/%.2f/%.2f",
                 nf[0], nf[1], nf[2]);
      } else {
        snprintf(tiltTxt, sizeof(tiltTxt),
                 " TILT FIT FAILED (wobble=%.2f, keep tilt fixed)", ratio);
      }
    } else {
      snprintf(tiltTxt, sizeof(tiltTxt), " TILT FIT FAILED (low motion)");
    }
  }

  snprintf(compassCalResult, sizeof(compassCalResult),
           "Calibration saved: X=%d Y=%d Z=%d S=%.3f/%.3f/%.3f%s (span %ld/%ld)",
           COMPASS_CAL_X, COMPASS_CAL_Y, COMPASS_CAL_Z,
           COMPASS_CAL_SCALE_X, COMPASS_CAL_SCALE_Y, COMPASS_CAL_SCALE_Z,
           tiltTxt, (long)spanX, (long)spanY);
  logPrintf("Compass: %s\n", compassCalResult);
}
void processCompassSensor() {
  int16_t x, y, z;
  bool ok = false;
  if (ENABLE_DEMO_MODE) {
    // Simulated rotating field. The calibration offsets are added back to the
    // raw axes so the offset-subtraction and tilt-compensation math in the
    // heading calculation below still operates on centered values.
    unsigned long t = millis();
    float rad = demoSimHeadingDeg(t) * (M_PI / 180.0f);
    x = COMPASS_CAL_X + (int16_t)(400.0f * cosf(rad));
    y = COMPASS_CAL_Y + (int16_t)(400.0f * sinf(rad));
    z = COMPASS_CAL_Z + (int16_t)(60.0f * sinf((float)t / 7000.0f));
    ok = true;
  } else {
    if (!compassReady) return;
    if (compassChip == COMPASS_HMC)
      ok = compassRead6(compassAddr, HMC5883L_X_MSB, true, x, y, z);
    else if (compassChip == COMPASS_P)
      ok = compassRead6(compassAddr, QMC5883P_DATA, false, x, y, z);
    else
      ok = compassRead6(compassAddr, QMC5883L_X_LSB, false, x, y, z);
    if (!ok) return;
  }
  compassRawX = x;
  compassRawY = y;
  compassRawZ = z;

  // Calibration capture: track min/max over the window; offsets and the tilt
  // axis are derived and saved when it elapses.
  if (compassCalActive) {
    if (x < compassCalMinX) compassCalMinX = x;
    if (x > compassCalMaxX) compassCalMaxX = x;
    if (y < compassCalMinY) compassCalMinY = y;
    if (y > compassCalMaxY) compassCalMaxY = y;
    if (z < compassCalMinZ) compassCalMinZ = z;
    if (z > compassCalMaxZ) compassCalMaxZ = z;
    unsigned long nowMs = millis();
    if (calSampleCount < COMPASS_CAL_MAX_SAMPLES &&
        nowMs - lastCalSampleMs >= 40) {
      lastCalSampleMs = nowMs;
      calSamples[calSampleCount][0] = x;
      calSamples[calSampleCount][1] = y;
      calSamples[calSampleCount][2] = z;
      calSampleCount++;
    }
    if (nowMs >= compassCalEndTime) compassCalFinish();
  }

  // Heading: offset- and scale-correct the field, then take the angle of the
  // horizontal component.
  //
  // COMPASS_TILT_COMP selects the plane the corrected field is projected
  // onto:
  //   1 = plane perpendicular to the calibrated "up" axis (COMPASS_CAL_TX/Y/Z).
  //     Only correct while the mounting tilt is constant - a 3-axis
  //     magnetometer cannot sense the current tilt, so if the mounting tilt
  //     changes (handlebars tilting while riding) the projection swings
  //     wildly.
  //   0 = default: plane perpendicular to the sensor Z axis (flat-mount
  //     assumption), h = atan2(py, px). The error is bounded by the tilt
  //     angle, so it degrades gracefully instead of going crazy.
  float px = ((float)x - (float)COMPASS_CAL_X) * COMPASS_CAL_SCALE_X;
  float py = ((float)y - (float)COMPASS_CAL_Y) * COMPASS_CAL_SCALE_Y;
  float pz = ((float)z - (float)COMPASS_CAL_Z) * COMPASS_CAL_SCALE_Z;
  float h;
  if (COMPASS_TILT_COMP) {
    float ux = (float)COMPASS_CAL_TX / 32767.0f;
    float uy = (float)COMPASS_CAL_TY / 32767.0f;
    float uz = (float)COMPASS_CAL_TZ / 32767.0f;
    float ulen = sqrtf(ux * ux + uy * uy + uz * uz);
    if (ulen > 0.001f) { ux /= ulen; uy /= ulen; uz /= ulen; }
    float dot = px * ux + py * uy + pz * uz;
    h = atan2f(py - dot * uy, px - dot * ux) * (180.0f / M_PI);
  } else {
    h = atan2f(py, px) * (180.0f / M_PI);
  }
  if (h < 0) h += 360.0f;
  h += COMPASS_DECLINATION_DEG;
  if (h < 0.0f) h += 360.0f;
  else if (h >= 360.0f) h -= 360.0f;
  // EMA smoothing with wraparound handling (jump 359 -> 0)
  float diff = h - currentHeading;
  if (diff > 180.0f) diff -= 360.0f;
  else if (diff < -180.0f) diff += 360.0f;
  currentHeading += diff * 0.15f;
  if (currentHeading < 0.0f) currentHeading += 360.0f;
  else if (currentHeading >= 360.0f) currentHeading -= 360.0f;
}

// ----------------------------------------------------------------------------
// BZGNSS P25 Pro (u-blox M10) UBX-only operation
// The module streams UBX NAV-PVT frames (0x01 0x07) at its configured rate.
// We parse those directly and synthesize NMEA for TinyGPSPlus - no module
// configuration is required or attempted, so an unresponsive RX line or a
// UBX-only input protocol cannot break anything.
// ----------------------------------------------------------------------------
static void ubxSend(const uint8_t *payload, uint8_t cls, uint8_t id, uint8_t len) {
  uint8_t ckA = 0, ckB = 0;
  for (uint8_t i = 0; i < len; i++) {
    ckA += payload[i];
    ckB += ckA;
  }
  ckA += cls; ckB += ckA;
  ckA += id;  ckB += ckA;
  ckA += len; ckB += ckA;
  gpsSerial.write(0xB5);
  gpsSerial.write(0x62);
  gpsSerial.write(cls);
  gpsSerial.write(id);
  gpsSerial.write(len);
  gpsSerial.write(payload, len);
  gpsSerial.write(ckA);
  gpsSerial.write(ckB);
}

// Scans the RX line for a sync pattern at the current baud. Returns true and
// sets *isUbx when NMEA ('$G..'/'$P..') or UBX (0xB5 0x62) traffic is seen.
// Requires TWO sync hits within the window: a single random byte pair in
// noise could otherwise false-positive the baud sweep at a wrong rate.
static bool gpsWaitForSync(unsigned long ms, bool &isUbx) {
  isUbx = false;
  unsigned long start = millis();
  int state = 0; // 0=idle, 1=seen '$', 2=seen 0xB5
  int hits = 0;
  bool ubxHit = false;
  while (millis() - start < ms) {
    if (gpsSerial.available() > 0) {
      uint8_t b = gpsSerial.read();
      if (state == 0) {
        if (b == '$') state = 1;
        else if (b == 0xB5) state = 2;
      } else if (state == 1) {
        if (b == 'G' || b == 'P' || b == 'A' || b == 'B' || b == 'L' || b == 'N') {
          if (++hits >= 2) { isUbx = false; return true; }
          state = 0;
        } else {
          state = 0;
        }
      } else {
        if (b == 0x62) {
          ubxHit = true;
          if (++hits >= 2) { isUbx = true; return true; }
          state = 0;
        } else {
          state = 0;
        }
      }
    } else {
      delay(1);
    }
  }
  return false;
}

// Sweeps the common baud rates looking for NMEA ('$') or UBX (0xB5 0x62)
// sync bytes. Returns the detected baud or 0.
static uint32_t gpsSweepBaud(bool &ubxSeen) {
  static const uint32_t candidates[] = {115200, 9600, 38400, 57600, 230400, 4800, 460800};
  for (uint32_t baud : candidates) {
    gpsSerial.begin(baud, SERIAL_8N1, RXD2, TXD2);
    delay(30);
    while (gpsSerial.available()) gpsSerial.read();
    bool isUbx = false;
    if (gpsWaitForSync(700, isUbx)) {
      ubxSeen = isUbx;
      logPrintf("GNSS: sweep @ %lu baud -> %s sync found\n", baud,
                isUbx ? "UBX" : "NMEA");
      return baud;
    }
    logPrintf("GNSS: sweep @ %lu baud -> no sync\n", baud);
  }
  return 0;
}

void configureGNSS() {
  // 1. Auto-detect the module's baud rate; store it if it changed
  bool ubxSeen = false;
  uint32_t detectedBaud = gpsSweepBaud(ubxSeen);

  if (detectedBaud == 0) {
    // 1b. Nothing received. Try to revive a module whose UART output was
    //     disabled by a previous bad config (CFG-CFG clear + soft reset),
    //     then sweep again.
    logPrintf("GNSS: no traffic - sending factory reset (CFG-CFG clear + "
              "soft reset) at 115200...\n");
    gpsSerial.begin(115200, SERIAL_8N1, RXD2, TXD2);
    uint8_t clr[13] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02};
    ubxSend(clr, 0x06, 0x09, sizeof(clr)); // CFG-CFG: clear all stored config
    uint8_t rst[4] = {0x00, 0x00, 0x01, 0x00};
    ubxSend(rst, 0x06, 0x04, sizeof(rst)); // CFG-RST: controlled software reset
    delay(2500); // module reboots with factory defaults
    while (gpsSerial.available()) gpsSerial.read();
    detectedBaud = gpsSweepBaud(ubxSeen);
    if (detectedBaud == 0) {
      gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
      logPrintf("GNSS: still no traffic after factory reset - using saved "
                "GPS_BAUD=%d. Verify the module itself with u-center2 and "
                "check wiring: module TX->ESP GPIO16, module RX->ESP GPIO17, "
                "shared GND, VCC 3.3-5V\n", GPS_BAUD);
      return;
    }
    logPrintf("GNSS: module revived by factory reset @ %lu baud\n",
              detectedBaud);
  }

  if (detectedBaud != (uint32_t)GPS_BAUD) {
    logPrintf("GNSS: module detected at %lu baud (%s) - updating GPS_BAUD\n",
              detectedBaud, ubxSeen ? "UBX" : "NMEA");
    GPS_BAUD = (int)detectedBaud;
    Preferences pref;
    pref.begin("cfg", false);
    pref.putInt("GPS_BAUD", GPS_BAUD);
    pref.end();
  }

  logPrintf("GNSS: %s mode @ %d baud (UBX NAV-PVT parsed, NMEA synthesized)\n",
            ubxSeen ? "UBX" : "NMEA", GPS_BAUD);
}

// ----------------------------------------------------------------------------
// Shared state (defined here)
// ----------------------------------------------------------------------------
uint16_t DEBUG_BOX_COLOR = TFT_MAGENTA;
float cpuUsagePct = 0.0f;
float currentMeasuredFps = 0.0f;
float fpsHistory[FPS_AVG_SAMPLES] = {0.0f};
uint8_t fpsHistoryIndex = 0;
uint8_t fpsHistoryCount = 0;
float currentAverageFps = 0.0f;

unsigned long lastDisplayUpdate = 0;
float filteredReading = 950.0f;
int rawFuelADC = 0;
int rawBatteryADC = 0;
int rawTempADC = 0;
int rawLightADC = 0;
float fuelLiters = 0.0f;
int fuelPercentage = 0;
float batteryVoltage = 0.0f;
float engineTemperature = 0.0f;
double totalDistanceKm = 0.0;
double lastSavedOdo = 0.0;
double lastLat = 0.0;
double lastLon = 0.0;
bool hasLastPos = false;
int splashCurrentProgress = 0;
float currentCachedSpeed = 0.0f;
float currentHeading = 0.0f;

portMUX_TYPE hallMux = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long lastHallPulseTimeUs = 0;
volatile unsigned long hallPulseIntervalUs = 0;
volatile unsigned long hallPulseCount = 0;

// Hysteresis-held speed source (0=hall, 1=GPS, 2=fused). Written by
// updateSpeedSourceMode() in sensorTask (core 1), read cross-core by
// updateFilteredSpeed() in gpsTask (core 0) - atomic 32-bit read/write.
volatile int heldSpeedSourceMode = 0;

double tripDistanceKm = 0.0;
float tripStartFuelLiters = -1.0f;
float tripFuelConsumedLiters = 0.0f;
float instantKml = 0.0f;
float averageKml = 0.0f;
float averageSpeed = 0.0f;
// Session peak of the fused/filtered speed (km/h). RAM-only: resets to 0 on
// every reboot (the "max speed till reboot" behavior - no NVS, no manual
// reset). Written in updateFilteredSpeed() (core 0), read under g_stateMutex
// in the sensor snapshot (core 1); the 32-bit float write is atomic.
float maxSpeed = 0.0f;
unsigned long movingTimeMs = 0;

void setOdometerKm(double km) {
  if (km < 0.0) km = 0.0;
  totalDistanceKm = km;
  lastSavedOdo = km;
  g_sensorData.totalDistanceKm = km;
  preferences.begin("dashboard", false);
  preferences.putDouble("odo", km);
  preferences.end();
  logPrintf("Odometer set to %.1f km\n", km);
}

TimerState accelState = READY;
unsigned long accelStartTime = 0;
float accelResultTime = 0.0f;

SensorSnapshot g_sensorData;
SemaphoreHandle_t g_stateMutex = NULL;
volatile unsigned long g_sensorLastTickMs = 0;

// ----------------------------------------------------------------------------
// Hall sensor (ISR + speed)
// ----------------------------------------------------------------------------
void IRAM_ATTR hallSensorISR() {
  constexpr unsigned long DEBOUNCE_US = 10000;
  // Between two real rotations the wheel period changes by far less than
  // HALL_PERIOD_GUARD (1 g of acceleration shifts it <2% per rotation).
  // Guard both directions: fast blips are EMI/bounce on the hall pin;
  // the slow side rejects the multi-second gap after a stop, which
  // otherwise entered the interval history as one giant "rotation" and
  // reported the last driving speed for seconds after the vehicle stopped.
  // WebUI-tunable; <=1 disables the guard (raw single-interval behavior).
  unsigned long now = micros();
  unsigned long gap = now - lastHallPulseTimeUs;
  if (gap > DEBOUNCE_US) {
    portENTER_CRITICAL_ISR(&hallMux);
    unsigned long last = hallPulseIntervalUs;
    int guard = HALL_PERIOD_GUARD;
    if (guard > 1 && last != 0 &&
        ((unsigned long long)gap > (unsigned long long)last * (unsigned)guard ||
         (unsigned long long)gap * (unsigned)guard < last)) {
      // Non-physical period: ignore the pulse entirely (keep lastHallPulseTimeUs
      // and the interval history untouched so the 2 s stale check stays honest)
      portEXIT_CRITICAL_ISR(&hallMux);
      return;
    }
    hallPulseIntervalUs = gap;
    lastHallPulseTimeUs = now;
    hallPulseCount++;
    hallIntervalHist[hallHistWriteIdx] = gap;
    hallHistWriteIdx = (hallHistWriteIdx + 1u) % HALL_MEDIAN_MAX;
    portEXIT_CRITICAL_ISR(&hallMux);
  }
}

inline float getHallSpeed() {
  unsigned long lastTimeUs;
  int n = HALL_MEDIAN_SAMPLES; // WebUI-tunable; 1 = raw, zero lag
  if (n < 1) n = 1;
  if (n > HALL_MEDIAN_MAX) n = HALL_MEDIAN_MAX;
  unsigned long hist[HALL_MEDIAN_MAX];
  portENTER_CRITICAL(&hallMux);
  lastTimeUs = lastHallPulseTimeUs;
  // Newest first: write index points at the next free slot, so the last
  // accepted interval sits one behind it (wrapping around the ring).
  int idx = (hallHistWriteIdx == 0 ? HALL_MEDIAN_MAX : hallHistWriteIdx) - 1;
  for (int i = 0; i < n; i++) {
    hist[i] = hallIntervalHist[idx];
    if (--idx < 0)
      idx = HALL_MEDIAN_MAX - 1;
  }
  portEXIT_CRITICAL(&hallMux);
  if (micros() - lastTimeUs > 2000000UL)
    return 0.0f; // no real motion (pulses stale)
  // Median of the non-zero samples: with N=9 one or a few noise blips can no
  // longer move the reading; a corrupt majority needs >=N/2+1 bad samples.
  unsigned long vals[HALL_MEDIAN_MAX];
  int m = 0;
  for (int i = 0; i < n; i++)
    if (hist[i] != 0)
      vals[m++] = hist[i];
  if (m == 0)
    return 0.0f;
  for (int a = 1; a < m; a++) { // insertion sort (tiny N)
    unsigned long key = vals[a];
    int b = a - 1;
    while (b >= 0 && vals[b] > key) {
      vals[b + 1] = vals[b];
      b--;
    }
    vals[b + 1] = key;
  }
  return WHEEL_SPEED_FACTOR / (float)vals[m / 2];
}

// Which sensor the displayed speed comes from: 0=hall, 1=GPS, 2=fused (G+H).
// Pure function of the instantaneous sensor state; updateSpeedSourceMode()
// adds the hysteresis that decides when a new value actually takes effect.
int computeSpeedSourceMode(float hallSpeed, float gpsSpeed, int sats,
                           bool isGpsValid) {
  if (GPS_ONLY_MODE && isGpsValid)
    return 1; // user-forced GPS-only (e.g. no hall sensor installed)
  if (!isGpsValid || hallSpeed <= 0.0f)
    return 0;
  float delta = fabsf(gpsSpeed - hallSpeed);
  if (delta > MAX_SPEED_DELTA_KMH)
    return 0; // GPS contradicts hall: reject GPS
  if (delta < GPS_MIN_DEV_KMH)
    return 1; // agreement zone: trust GPS
  return 2; // in between: weighted fusion
}

// The raw candidate flips HAL<->G+H every 20 ms whenever delta hovers around
// the GPS_MIN_DEV/MAX_SPEED_DELTA boundaries: GPS speed jitters +-1..3 km/h
// at ~1 Hz while the hall speed is rock-stable, and a single hall noise blip
// used to push delta past MAX_SPEED_DELTA_KMH for one tick. The badge (and
// the raw speed selection in updateFilteredSpeed) only follow the candidate
// once it has held for SPEED_SOURCE_HOLD_MS, so borderline readings settle
// on one source instead of flickering.
void updateSpeedSourceMode() {
  static int pendingMode = -1;
  static unsigned long pendingSince = 0;
  float hallSpeed = getHallSpeed();
  float gpsSpeed = gps.speed.isValid() ? (float)gps.speed.kmph() : 0.0f;
  int sats = gps.satellites.value();
  bool isGpsValid = gps.speed.isValid() && (sats >= MIN_SATELLITES);
  int candidate = computeSpeedSourceMode(hallSpeed, gpsSpeed, sats,
                                         isGpsValid);
  if (candidate == heldSpeedSourceMode) {
    pendingMode = -1;
    return;
  }
  if (candidate != pendingMode) {
    pendingMode = candidate;
    pendingSince = millis();
  } else if (millis() - pendingSince >= (unsigned long)SPEED_SOURCE_HOLD_MS) {
    heldSpeedSourceMode = candidate;
    pendingMode = -1;
  }
}

void updateFilteredSpeed() {
  float hallSpeed = getHallSpeed();
  int sats = gps.satellites.value();
  bool isGpsValid = gps.speed.isValid() && (sats >= MIN_SATELLITES);

  // Follow the hysteresis-held mode so the displayed number always matches
  // the source badge. GPS dropping below MIN_SATELLITES mid-hold falls back
  // to the hall value for the raw number; the badge switches within
  // SPEED_SOURCE_HOLD_MS.
  int mode = heldSpeedSourceMode;
  if (mode != 0 && !isGpsValid)
    mode = 0;
  float raw;
  if (mode == 0) {
    raw = hallSpeed;
  } else if (mode == 1) {
    raw = (float)gps.speed.kmph();
  } else {
    float gpsSpeed = (float)gps.speed.kmph();
    // Weight by satellite count; clamped because sats can dip below
    // MIN_SATELLITES before the hold timer switches us over to hall.
    float gpsWeight = (float)(sats - MIN_SATELLITES + 1) /
                      (float)(OPTIMAL_SATELLITES - MIN_SATELLITES + 1);
    gpsWeight = constrain(gpsWeight, 0.0f, 1.0f);
    raw = gpsSpeed * gpsWeight + hallSpeed * (1.0f - gpsWeight);
  }

  if (mode == 0) {
    // Hall-only: pulses are real motion, keep the simple threshold
    currentCachedSpeed = (raw < MIN_SPEED_THRESHOLD) ? 0.0f : raw;
    return;
  }

  // GPS-derived speed: apply hysteresis so stationary GPS noise (typically
  // 0-2 km/h jitter) can't make the display flicker. Start showing speed
  // only above GPS_START_KMH, and return to 0 only after the speed has
  // stayed below MIN_SPEED_THRESHOLD for GPS_STOP_SETTLE_MS.
  static bool isMoving = false;
  static unsigned long belowStopSince = 0;
  if (raw >= GPS_START_KMH) {
    isMoving = true;
    belowStopSince = 0;
  } else if (raw <= MIN_SPEED_THRESHOLD) {
    if (belowStopSince == 0)
      belowStopSince = millis();
    if (isMoving && millis() - belowStopSince >= (unsigned long)GPS_STOP_SETTLE_MS) {
      isMoving = false;
      belowStopSince = 0;
    }
  } else {
    belowStopSince = 0; // between thresholds: hold current state
  }
  currentCachedSpeed = isMoving ? raw : 0.0f;
  // Track the session's maximum achieved speed (RAM-only, resets on reboot).
  // Sampled here every tick (~20 ms) so a peak is not missed between slower
  // consumer updates. Cross-core: core 0 writes, core 1 reads under mutex.
  if (currentCachedSpeed > maxSpeed)
    maxSpeed = currentCachedSpeed;
}

// ----------------------------------------------------------------------------
// Analog sensors
// ----------------------------------------------------------------------------
void processBatterySensor() {
  int rawADC;
  if (ENABLE_DEMO_MODE) {
    // Simulated raw ADC: 12.2..13.8V with a charging ripple - the real
    // voltage conversion below turns it back into a voltage.
    unsigned long t = millis();
    rawADC = demoAdcForBatteryVoltage(
        12.2f + 1.6f * (0.5f + 0.5f * sinf((float)t / 24000.0f)));
  } else {
    rawADC = analogRead(BATTERY_SENSE_PIN);
  }
  rawBatteryADC = rawADC;
  batteryVoltage = ((float)rawADC * ADC_VOLTS_FACTOR * BATTERY_SCALE) + BATTERY_OFFSET;
  if (batteryVoltage < 2.0f)
    batteryVoltage = 0.0f;
}

void processTemperatureSensor() {
  static float filteredEngineTemp = -1000.0f;
  int rawADC;
  if (ENABLE_DEMO_MODE) {
    // Simulated raw ADC: 20C cold start, ~90s warmup to ~85C with ripple.
    unsigned long t = millis();
    float warm = fminf((float)t / 90000.0f, 1.0f);
    rawADC = demoAdcForTempC(
        20.0f + 65.0f * warm + 2.5f * sinf((float)t / 15000.0f));
  } else {
    rawADC = analogRead(TEMP_SENSE_PIN);
  }
  rawTempADC = rawADC;
  if (rawADC <= 100 || rawADC >= 4000) {
    filteredEngineTemp = -1000.0f;
    engineTemperature = 0.0f;
    return;
  }
  float vOut = (float)rawADC * ADC_VOLTS_FACTOR;
  float resistance = NTC_R_BALANCE * (vOut / (3.3f - vOut));
  float instantTemp = (1.0f / (logf(resistance / NTC_R25) / NTC_BETA +
                               NTC_INV_ROOM_KELVIN)) -
                      273.15f + NTC_TEMP_OFFSET;
  // EMA smoothing (same pattern as the light sensor): the raw NTC ADC jitters
  // by a few °C per 20ms read, which made the sidebar number flicker between
  // adjacent values at full redraw rate.
  static const float alpha = 0.2f;
  if (filteredEngineTemp < -500.0f)
    filteredEngineTemp = instantTemp;
  else
    filteredEngineTemp = (instantTemp * alpha) + (filteredEngineTemp * (1.0f - alpha));
  engineTemperature = filteredEngineTemp;
}

void processLightSensor() {
  static const float alpha = 0.15f;
  int raw;
  if (ENABLE_DEMO_MODE) {
    // Simulated raw ADC: slow day/night cycle so the ambient-value EMA and
    // auto-brightness run for real.
    unsigned long t = millis();
    raw = 300 + (int)(3400.0f * (0.5f + 0.5f * sinf((float)t / 45000.0f)));
  } else {
    analogRead(LIGHT_SENSOR_PIN);  // settle: first reading is discarded
    raw = analogRead(LIGHT_SENSOR_PIN);
  }
  ambientLightValue = raw;
  rawLightADC = raw;
  if (filteredAmbientValue < 1.0f)
    filteredAmbientValue = (float)raw;
  else
    filteredAmbientValue = ((float)raw * alpha) + (filteredAmbientValue * (1.0f - alpha));
}

void initFuelSensor() {
  if (ENABLE_DEMO_MODE) {
    filteredReading = (float)demoAdcForFuelLiters(5.5f);
    rawFuelADC = (int)filteredReading;
    return;
  }

  // Initialize and configure ESP32 RTC Touch Pad hardware driver for GPIO32 (Touch channel 9).
  // Increase measurement window (0xFFFF) and charge slope to maximum to handle higher
  // baseline parasitic capacitance from long wiring runs.
  touch_pad_init();
  touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_0V);
  touch_pad_config(TOUCH_PAD_NUM9, 0);
  touch_pad_set_cnt_mode(TOUCH_PAD_NUM9, TOUCH_PAD_SLOPE_7, TOUCH_PAD_TIE_OPT_LOW);
  touch_pad_set_meas_time(0x100, 0xFFFF);

  // Take an initial instant reading to prime the EMA filter
  uint16_t initialVal = 0;
  if (touch_pad_read(TOUCH_PAD_NUM9, &initialVal) == ESP_OK && initialVal > 0) {
    rawFuelADC = (int)initialVal;
    filteredReading = (float)initialVal;
  } else {
    rawFuelADC = (int)touchRead(FUEL_TOUCH_PIN);
    filteredReading = (float)rawFuelADC;
  }
}

void processFuelSensor() {
  int instantReading = 0;
  if (ENABLE_DEMO_MODE) {
    // Simulated fuel: burns in proportion to the distance driven (~6 L per
    // 100 km, so the km/L and average consumption calculations downstream
    // produce realistic numbers), refills slowly while parked, and carries a
    // tiny slosh so the gauge is not dead-still. The touch-table interpolation
    // + EMA below convert the raw ADC back to liters/percent.
    unsigned long t = millis();
    static float demoFuelLevel = 5.5f;
    static unsigned long lastFuelSimMs = 0;
    if (lastFuelSimMs == 0) lastFuelSimMs = t;
    float dtS = (float)(t - lastFuelSimMs) / 1000.0f;
    lastFuelSimMs = t;
    float vNow = demoSimSpeedKmph(t);
    if (vNow > 0.5f)
      demoFuelLevel -= (vNow * dtS / 3600.0f) * 0.06f; // burn while driving
    else
      demoFuelLevel += 0.05f * dtS; // refuel while parked
    demoFuelLevel = constrain(demoFuelLevel, 0.3f, 5.5f);
    instantReading =
        demoAdcForFuelLiters(demoFuelLevel + 0.03f * sinf((float)t / 5000.0f));
  } else {
    uint16_t val = 0;
    if (touch_pad_read(TOUCH_PAD_NUM9, &val) == ESP_OK) {
      instantReading = (int)val;
    } else {
      instantReading = (int)touchRead(FUEL_TOUCH_PIN);
    }
  }
  rawFuelADC = instantReading;
  filteredReading = ((float)instantReading * FUEL_FILTER_ALPHA) +
                    (filteredReading * (1.0f - FUEL_FILTER_ALPHA));

  // Determine whether calibration table is descending (capacitive) or ascending (resistive)
  bool isDescending = (touchTable[0] >= touchTable[FUEL_TOUCH_POINTS - 1]);

  if (isDescending) {
    if (filteredReading >= touchTable[0]) {
      fuelLiters = 0.0f;
      fuelPercentage = 0;
      return;
    }
    if (filteredReading <= touchTable[FUEL_TOUCH_POINTS - 1]) {
      fuelLiters = (float)(FUEL_TOUCH_POINTS - 1);
      fuelPercentage = 100;
      return;
    }
    for (int i = 0; i < FUEL_TOUCH_POINTS - 1; i++) {
      if (filteredReading <= touchTable[i] &&
          filteredReading >= touchTable[i + 1]) {
        float span = (float)(touchTable[i + 1] - touchTable[i]);
        if (span != 0.0f) {
          fuelLiters = (float)i + ((filteredReading - touchTable[i]) / span);
        } else {
          fuelLiters = (float)i;
        }
        fuelPercentage = constrain(
            (int)((fuelLiters / (float)(FUEL_TOUCH_POINTS - 1)) * 100.0f), 0, 100);
        return;
      }
    }
  } else {
    if (filteredReading <= touchTable[0]) {
      fuelLiters = 0.0f;
      fuelPercentage = 0;
      return;
    }
    if (filteredReading >= touchTable[FUEL_TOUCH_POINTS - 1]) {
      fuelLiters = (float)(FUEL_TOUCH_POINTS - 1);
      fuelPercentage = 100;
      return;
    }
    for (int i = 0; i < FUEL_TOUCH_POINTS - 1; i++) {
      if (filteredReading >= touchTable[i] &&
          filteredReading <= touchTable[i + 1]) {
        float span = (float)(touchTable[i + 1] - touchTable[i]);
        if (span != 0.0f) {
          fuelLiters = (float)i + ((filteredReading - touchTable[i]) / span);
        } else {
          fuelLiters = (float)i;
        }
        fuelPercentage = constrain(
            (int)((fuelLiters / (float)(FUEL_TOUCH_POINTS - 1)) * 100.0f), 0, 100);
        return;
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Odometer (Hall pulses + GPS distance)
// ----------------------------------------------------------------------------
void updateGPSOdometer() {
  // Demo mode never sleeps on the power-sense pin (no real power module).
  if (ENABLE_POWER_SENSE && !ENABLE_DEMO_MODE &&
      digitalRead(POWER_SENSE_PIN) == LOW) {
    pendingSleep = true;
    while (1)
      vTaskDelay(pdMS_TO_TICKS(100));
  }
  unsigned long pulses = 0;
  portENTER_CRITICAL(&hallMux);
  pulses = hallPulseCount;
  hallPulseCount = 0;
  portEXIT_CRITICAL(&hallMux);
  bool isGpsValid =
      (gps.location.isValid() && gps.satellites.value() >= MIN_SATELLITES);
  // In demo mode the odometer math still runs, but the simulated distance is
  // accumulated into demoOdoKm and the NVS-backed totalDistanceKm is never
  // touched - demo mileage can never corrupt the stored real odometer.
  bool isDemo = ENABLE_DEMO_MODE;

  if (!isGpsValid && pulses > 0) {
    double dKm = (double)pulses * WHEEL_DIST_PER_PULSE_KM;
    tripDistanceKm += dKm;
    if (isDemo) {
      demoOdoKm += dKm;
    } else {
      totalDistanceKm += dKm;
      if (totalDistanceKm - lastSavedOdo >= 1.0) {
        preferences.begin("dashboard", false);
        preferences.putDouble("odo", totalDistanceKm);
        preferences.end();
        lastSavedOdo = totalDistanceKm;
      }
    }
  }
  if (isGpsValid && gps.location.isUpdated()) {
    if (getFilteredSpeed() > 0.0f && hasLastPos) {
      double distanceMeters = TinyGPSPlus::distanceBetween(
          gps.location.lat(), gps.location.lng(), lastLat, lastLon);
      if (distanceMeters < 500.0) {
        double dKm = (distanceMeters / 1000.0);
        tripDistanceKm += dKm;
        if (isDemo) {
          demoOdoKm += dKm;
        } else {
          totalDistanceKm += dKm;
          if (totalDistanceKm - lastSavedOdo >= 1.0) {
            preferences.begin("dashboard", false);
            preferences.putDouble("odo", totalDistanceKm);
            preferences.end();
            lastSavedOdo = totalDistanceKm;
          }
        }
      }
    }
    lastLat = gps.location.lat();
    lastLon = gps.location.lng();
    hasLastPos = true;
  }
}

// ----------------------------------------------------------------------------
// Fuel consumption + acceleration timer
// ----------------------------------------------------------------------------
void processFuelConsumption() {
  if (tripStartFuelLiters < 0.0f && fuelLiters > 0.0f)
    tripStartFuelLiters = fuelLiters;
  if (tripStartFuelLiters > 0.0f) {
    float consumed = tripStartFuelLiters - fuelLiters;
    if (consumed > 0.0f)
      tripFuelConsumedLiters = consumed;
    // A fuel-level rise beyond the user-set threshold (REFUEL_RESET_LITERS,
    // webui "Refuel Reset Threshold") counts as a refuel: reset the trip
    // consumption instead of subtracting the rising level.
    else if (consumed < -REFUEL_RESET_LITERS) {
      tripStartFuelLiters = fuelLiters;
      tripFuelConsumedLiters = 0.0f;
    }
  }
  // Consumption is computed internally as L/100km (the physically natural
  // unit, fuel per distance) and converted to km/L for display:
  // km/L = 100 / (L/100km). The 99.9 km/L display cap corresponds to
  // 1.001 L/100km.
  float avgL100 =
      (tripDistanceKm > 0.05 && tripFuelConsumedLiters > 0.01f)
          ? (float)((tripFuelConsumedLiters / tripDistanceKm) * 100.0)
          : 0.0f;
  averageKml = (avgL100 > 1.001f) ? (100.0f / avgL100) : 99.9f;

  static unsigned long lastInstSampleTime = 0;
  static double lastInstDistKm = 0.0;
  static float lastInstFuelLiters = 0.0f;
  if (millis() - lastInstSampleTime >= 3000) {
    lastInstSampleTime = millis();
    double dDist = tripDistanceKm - lastInstDistKm;
    float dFuel = lastInstFuelLiters - fuelLiters;
    lastInstDistKm = tripDistanceKm;
    lastInstFuelLiters = fuelLiters;
    if (getFilteredSpeed() > 0.0f && dDist > 0.005 && dFuel > 0.001f) {
      // Same L/100km intermediate as the average above, then km/L.
      float instL100 = (float)((dFuel / dDist) * 100.0);
      float rawInst = (instL100 > 1.001f) ? (100.0f / instL100) : 99.9f;
      instantKml = (rawInst * 0.4f) + (instantKml * 0.6f);
    } else if (getFilteredSpeed() == 0.0f)
      instantKml = 0.0f;
  }
}

// ----------------------------------------------------------------------------
// Average speed (moving time)
// ----------------------------------------------------------------------------
void updateAverageSpeed() {
  static unsigned long lastMovingTimeCheck = 0;
  unsigned long nowMs = millis();
  if (getFilteredSpeed() > 0.0f) {
    if (lastMovingTimeCheck != 0)
      movingTimeMs += (nowMs - lastMovingTimeCheck);
  }
  lastMovingTimeCheck = nowMs;

  float movingHours = movingTimeMs / 3600000.0f;
  averageSpeed = (tripDistanceKm > 0.05 && movingHours > 0.001f)
                     ? (float)(tripDistanceKm / movingHours)
                     : 0.0f;
  if (averageSpeed > 299.9f)
    averageSpeed = 299.9f;
}

void updateAccelTimer() {
  float currentSpeed = getFilteredSpeed();
  static bool readyForNextRun = false;
  switch (accelState) {
  case READY:
    accelResultTime = 0.0f;
    if (currentSpeed >= ACCEL_START_SPEED) {
      accelState = RUNNING;
      accelStartTime = millis();
      readyForNextRun = false;
    }
    break;
  case RUNNING:
    accelResultTime = (millis() - accelStartTime) / 1000.0f;
    if (currentSpeed >= ACCEL_TARGET_SPEED) {
      accelState = FINISHED;
      readyForNextRun = false;
    } else if (ACCEL_MAX_TIME > 0.0f && accelResultTime >= ACCEL_MAX_TIME) {
      accelState = FINISHED;
      readyForNextRun = false;
    } else if (currentSpeed < ACCEL_START_SPEED) {
      accelState = READY;
      accelResultTime = 0.0f;
    }
    break;
  case FINISHED:
    if (currentSpeed < ACCEL_START_SPEED)
      readyForNextRun = true;
    if (readyForNextRun && currentSpeed >= ACCEL_START_SPEED) {
      accelState = RUNNING;
      accelStartTime = millis();
      accelResultTime = 0.0f;
      readyForNextRun = false;
    }
    break;
  }
}

// ----------------------------------------------------------------------------
// UBX NAV-PVT parser (primary mode)
// The module streams NAV-PVT (0x01 0x07) frames at its configured rate. We
// convert them into standard NMEA sentences and feed them to TinyGPSPlus so
// the dashboard works with the module in UBX mode. If a module instead emits
// real NMEA, TinyGPSPlus parses that directly and this parser stays idle.
// ----------------------------------------------------------------------------
static uint8_t ubxSt = 0;      // 0=idle,1=B5,2=62,3=cls,4=id,5=lenL,6=lenH,7=payload,8=ckA,9=ckB
static uint8_t ubxCls = 0, ubxId = 0;
static uint16_t ubxNeed = 0, ubxIdx = 0;
static uint8_t ubxPld[92];
static uint8_t ubxCkA = 0, ubxCkB = 0;
static bool ubxFallbackActive = false;

// Debug counters exposed to the on-screen overlay (gfx.cpp)
volatile uint32_t gpsRxBytes = 0;
volatile uint32_t ubxFramesParsed = 0;
volatile uint8_t ubxLastFixType = 0;
volatile uint8_t ubxLastNumSv = 0;
volatile double ubxLastLat = 0.0;
volatile double ubxLastLon = 0.0;
volatile uint32_t ubxSyncSeen = 0;   // 0xB5 sync bytes seen
volatile uint32_t ubxCkFail = 0;     // frames whose checksum failed
volatile uint32_t ubxOversize = 0;   // frames rejected (payload > 92)

static int32_t ubxI32(uint16_t off) {
  return (int32_t)((uint32_t)ubxPld[off] | ((uint32_t)ubxPld[off + 1] << 8) |
                   ((uint32_t)ubxPld[off + 2] << 16) |
                   ((uint32_t)ubxPld[off + 3] << 24));
}

static uint16_t ubxU16(uint16_t off) {
  return (uint16_t)(ubxPld[off] | (ubxPld[off + 1] << 8));
}

static void feedGpsLine(char *line) {
  uint8_t cs = 0;
  int i;
  for (i = 1; line[i] && line[i] != '*'; i++) cs ^= (uint8_t)line[i];
  // Write the checksum AFTER the '*' - overwriting it previously produced
  // "...*" -> "XX\r\n" with no asterisk, so TinyGPSPlus never committed
  // the sentence (satellites/time/speed stayed at 0).
  sprintf(line + i + 1, "%02X\r\n", cs);
  for (i = 0; line[i]; i++) gps.encode((char)line[i]);
}

// Build $GPRMC + $GPGGA from a validated NAV-PVT payload
static void ubxNavPvtToNmea() {
  if (!ubxFallbackActive) {
    ubxFallbackActive = true;
    logPrintf("GNSS: UBX NAV-PVT parsed (NMEA synthesized for dashboard): "
              "fixType=%d sv=%02d lat=%.6f lon=%.6f spd=%.1fkm/h\n",
              ubxPld[20], ubxPld[23], (double)ubxI32(28) / 1e7,
              (double)ubxI32(24) / 1e7,
              (double)ubxI32(60) / 1000.0 * 3.6);
  }
  uint16_t year = (uint16_t)(ubxPld[4] | (ubxPld[5] << 8));
  uint8_t mon = ubxPld[6], day = ubxPld[7];
  uint8_t hr = ubxPld[8], mn = ubxPld[9], sc = ubxPld[10];
  uint8_t valid = ubxPld[11];
  uint8_t fixType = ubxPld[20];
  uint8_t numSV = ubxPld[23];
  double lat = (double)ubxI32(28) / 1e7;
  double lon = (double)ubxI32(24) / 1e7;
  double altM = (double)ubxI32(36) / 1000.0;     // hMSL mm -> m
  double knots = (double)ubxI32(60) / 1000.0 * 1.943844; // gSpeed mm/s -> knots
  double course = (double)ubxI32(64) / 1e5;      // headMot 1e-5 deg
  double hdop = (double)ubxU16(76) / 100.0;      // pDOP
  bool hasFix = (fixType >= 2) && (valid & 0x02);

  ubxFramesParsed++;
  ubxLastFixType = fixType;
  ubxLastNumSv = numSV;
  ubxLastLat = lat;
  ubxLastLon = lon;

  char alat[20], alon[20];
  double aLat = fabs(lat);
  int dLat = (int)aLat;
  int mLat = (int)(((aLat - dLat) * 60.0) * 10000.0 + 0.5);
  if (mLat >= 600000) { mLat -= 600000; dLat++; }
  sprintf(alat, "%02d%02d.%04d", dLat, mLat / 10000, mLat % 10000);
  double aLon = fabs(lon);
  int dLon = (int)aLon;
  int mLon = (int)(((aLon - dLon) * 60.0) * 10000.0 + 0.5);
  if (mLon >= 600000) { mLon -= 600000; dLon++; }
  sprintf(alon, "%03d%02d.%04d", dLon, mLon / 10000, mLon % 10000);

  char line[130];
  sprintf(line, "$GPRMC,%02d%02d%02d.00,%c,%s,%c,%s,%c,%.2f,%.1f,%02d%02d%02d*",
          hr, mn, sc, hasFix ? 'A' : 'V', alat, lat < 0 ? 'S' : 'N',
          alon, lon < 0 ? 'W' : 'E', knots, course,
          day, mon, year % 100);
  feedGpsLine(line);

  sprintf(line, "$GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,%d,%02d,%.1f,%.1f,M,0.0,M,,*",
          hr, mn, sc, alat, lat < 0 ? 'S' : 'N', alon, lon < 0 ? 'W' : 'E',
          hasFix ? 1 : 0, numSV, hdop, altM);
  feedGpsLine(line);
}

// Feeds one received byte into the UBX frame state machine
static void ubxParseByte(uint8_t b) {
  switch (ubxSt) {
  case 0:
    if (b == 0xB5) { ubxSyncSeen++; ubxSt = 1; }
    break;
  case 1: ubxSt = (b == 0x62) ? 2 : 0; break;
  case 2: ubxCls = b; ubxSt = 3; break;
  case 3: ubxId = b; ubxSt = 4; break;
  case 4: ubxNeed = b; ubxSt = 5; break;
  case 5:
    ubxNeed |= (uint16_t)b << 8;
    if (ubxNeed > 92) { ubxOversize++; ubxSt = 0; break; }
    ubxIdx = 0;
    // Seed both accumulators by stepping the CK_A/CK_B algorithm through
    // EACH header byte (class, id, lenL, lenH) individually - seeding
    // ckB = ckA after a single combined sum produced wrong CK_B and made
    // every valid frame fail its checksum.
    {
      uint8_t ck = ubxCls;
      ubxCkB = ck;
      ck += ubxId; ubxCkB += ck;
      ck += (uint8_t)(ubxNeed & 0xFF); ubxCkB += ck;
      ck += b; ubxCkB += ck; // b = lenH
      ubxCkA = ck;
    }
    ubxSt = 6;
    break;
  case 6:
    ubxPld[ubxIdx++] = b;
    ubxCkA += b; ubxCkB += ubxCkA;
    if (ubxIdx >= ubxNeed) ubxSt = 7;
    break;
  case 7:
    if (b == ubxCkA) ubxSt = 8;
    else { ubxCkFail++; ubxSt = 0; }
    break;
  case 8:
    if (b == ubxCkB) {
      if (ubxCls == 0x01 && ubxId == 0x07)
        ubxNavPvtToNmea();
    } else {
      ubxCkFail++;
    }
    ubxSt = 0;
    break;
  }
}

// ----------------------------------------------------------------------------
// Shared local-time computation. Returns false when the system clock has not
// been set yet. Used both by the sensor task (once per tick) and by the
// display loop (every frame), so the on-screen clock keeps ticking even if the
// sensor task is stalled by core-0 network work.
// ----------------------------------------------------------------------------
bool systemTimeToLocal(int &hour, int &minute, int &day, int &month,
                       int &year) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (tv.tv_sec <= 1000000000) return false;
  time_t local = tv.tv_sec;
  struct tm *loc_tm = gmtime(&local);
  int dst = 0;
  if (TZ_DST_ENABLED) {
    int euroOff = getEuropeanOffset(loc_tm->tm_year + 1900, loc_tm->tm_mon + 1,
                                    loc_tm->tm_mday, loc_tm->tm_hour);
    dst = euroOff - 1;
  }
  local += ((TZ_OFFSET_HOURS + dst) * 3600);
  loc_tm = gmtime(&local);
  hour = loc_tm->tm_hour;
  minute = loc_tm->tm_min;
  day = loc_tm->tm_mday;
  month = loc_tm->tm_mon + 1;
  year = loc_tm->tm_year % 100;
  return true;
}

// ----------------------------------------------------------------------------
// Stage timing diagnostics. Logs any single sensor-driver stage that runs
// >200ms (I2C bus hang, NVS/flash commit, serial burst, ADC stall) so a
// core-0 stall can be attributed to the exact driver instead of guessing from
// the display-side sMaxGap.
// ----------------------------------------------------------------------------
static void sensorStageDiag(const char *name, unsigned long startMs) {
  unsigned long ms = millis() - startMs;
  if (ms > 200)
    logPrintf("SENS SLOW: %s %lums\n", name, (unsigned long)ms);
}

// ----------------------------------------------------------------------------
// GPS task. Quarantined on core 0 beside the WiFi stack. The UBX module
// streams ~1.1KB/s continuously, so a one-byte-at-a-time drain can never get
// ahead of the ring: a tick can park ~1s per 1024 bytes blocked in the read.
// On the old single sensor task that case froze every real-world value, and on
// core 1 it also starved the display. Here the httpd/TCP/IP/WiFi tasks (prios
// 5/18/23) preempt this task at will - GPS is 1Hz-tolerant data, so a stale
// drain only lags GPS values, never the sensors or the screen.
// ----------------------------------------------------------------------------
void gpsTask(void *pvParameters) {
  static bool gpsWatchdogLogged = false;
  static bool ubxDiagLogged = false;
  static bool rawDumpLogged = false;
  static unsigned long gpsWatchdogStart = 0;
  static uint8_t gpsRawBuf[256];
  static uint8_t gpsRawIdx = 0;
  for (;;) {
    // Cap GPS bytes parsed per tick. A module burst (baud mismatch, buffer
    // backlog, message dump) can otherwise keep the drain loop running for
    // SECONDS, freezing the whole sensor task (and every real-mode value with
    // it). 1024 B/tick = 51KB/s, >4x the 115200 baud peak, so normal-rate data
    // is never delayed - a flood just drains over a few ticks instead of
    // stalling one tick.
    // Bulk-read the ring in ONE driver call (capped at 1024 B/tick, so a
    // module burst drains over a few ticks instead of stalling one tick).
    // v13 instrumentation showed the per-byte wall cost was ~920us INSIDE
    // read()/uartReadBytes (encode/parse was 9us/byte and preemption-clean),
    // so 1024 individual reads could park this task ~1s even with data
    // available. One readBytes() for the exact available count is non-blocking
    // (all requested bytes are already in the ring) and costs ~us.
    unsigned long tGps = millis();
    uint32_t gpsBytesStart = gpsRxBytes;
    int64_t tGpsUs = esp_timer_get_time();
    uint32_t gpsReadLoopTotal = 0;
    uint32_t gpsEncUsTotal = 0;
    int gpsSlowestByteUs = 0;
    if (ENABLE_DEMO_MODE) {
      // Demo mode: no serial data - synthesize one NMEA sentence pair per
      // second and feed it through gps.encode() so TinyGPSPlus genuinely
      // parses satellites/speed/location/date/time (see demoGpsSentence).
      static unsigned long lastDemoNmeaMs = 0;
      if (millis() - lastDemoNmeaMs >= 1000) {
        lastDemoNmeaMs = millis();
        demoGpsSentence();
      }
    } else {
      uint8_t gpsBuf[1024];
      size_t gpsAvail = (size_t)gpsSerial.available();
      if (gpsAvail > sizeof(gpsBuf)) gpsAvail = sizeof(gpsBuf);
      if (gpsAvail > 0) {
        int64_t t0 = esp_timer_get_time();
        size_t got = gpsSerial.readBytes(gpsBuf, gpsAvail);
        int64_t t1 = esp_timer_get_time();
        gpsReadLoopTotal += (uint32_t)(t1 - t0);
        for (size_t j = 0; j < got; j++) {
          uint8_t b = gpsBuf[j];
          if (gpsRawIdx < sizeof(gpsRawBuf))
            gpsRawBuf[gpsRawIdx++] = b;
          gpsRxBytes++;
          int64_t t2 = esp_timer_get_time();
          gps.encode((char)b);
          ubxParseByte(b);
          int64_t t3 = esp_timer_get_time();
          gpsEncUsTotal += (uint32_t)(t3 - t2);
          int byt = (int)(t3 - t0);
          if (byt > gpsSlowestByteUs) gpsSlowestByteUs = byt;
        }
      }
    }
    {
      unsigned long gpsMs = millis() - tGps;
      int64_t gpsUs = esp_timer_get_time() - tGpsUs;
      if (gpsMs > 200)
        logPrintf("SENS SLOW: gpsParse %lums (drained=%lu avail=%d "
                  "loopUs=%llu readAvg=%luus encAvg=%luus slowestByte=%dus)\n",
                  (unsigned long)gpsMs, (unsigned long)(gpsRxBytes - gpsBytesStart),
                  (int)gpsSerial.available(), (unsigned long long)gpsUs,
                  (unsigned long)((gpsRxBytes - gpsBytesStart) ? gpsReadLoopTotal / (gpsRxBytes - gpsBytesStart) : 0),
                  (unsigned long)((gpsRxBytes - gpsBytesStart) ? gpsEncUsTotal / (gpsRxBytes - gpsBytesStart) : 0),
                  gpsSlowestByteUs);
    }
    if (gpsWatchdogStart == 0)
      gpsWatchdogStart = millis();
    if (!rawDumpLogged && gpsRawIdx >= 8 && millis() - gpsWatchdogStart > 3000) {
      rawDumpLogged = true;
      logPrintf("GNSS raw dump (%u bytes):\n", gpsRawIdx);
      for (uint16_t i = 0; i < gpsRawIdx; i++) {
        if (i % 16 == 0) logPrintf("%02X", gpsRawBuf[i]);
        else logPrintf(" %02X", gpsRawBuf[i]);
        if (i % 16 == 15) logPrintf("\n");
      }
      if (gpsRawIdx % 16 != 0) logPrintf("\n");
    }
    if (!ubxDiagLogged && millis() - gpsWatchdogStart > 5000) {
      ubxDiagLogged = true;
      logPrintf("UBX diag: rate=%luB/s sync=%lu ckfail=%lu oversize=%lu "
                "frames=%lu (baud=%d)\n",
                (unsigned long)(gpsRxBytes / 5), (unsigned long)ubxSyncSeen,
                (unsigned long)ubxCkFail, (unsigned long)ubxOversize,
                (unsigned long)ubxFramesParsed, GPS_BAUD);
    }
    if (!gpsWatchdogLogged && millis() - gpsWatchdogStart > 8000) {
      gpsWatchdogLogged = true;
      if (gps.charsProcessed() == 0) {
        logPrintf("GPS: NO NMEA data received - check TX/RX wiring and "
                  "module power (GPS_BAUD=%d)\n", GPS_BAUD);
      } else if (gps.satellites.value() == 0) {
        logPrintf("GPS: NMEA flowing (chars=%lu) but no satellites - check "
                  "antenna / sky view\n", gps.charsProcessed());
      } else {
        logPrintf("GPS: locked, %d satellites, %.6f/%.6f\n",
                  gps.satellites.value(), gps.location.lat(), gps.location.lng());
      }
    }
    updateFilteredSpeed();
    updateGPSOdometer();

    // GNSS date/time is only applied while a real position fix is valid AND
    // the module clock has been observed ticking in step with real time
    // (3 consecutive advances of ~1s). Receivers without a fix (or with a
    // dead RTC battery) keep emitting a stale/frozen date+time, typically the
    // time of the last fix. Without the ticking check, applying that frozen
    // value every few seconds snaps the system clock back to it right after
    // every NTP/manual sync, leaving the dashboard stuck on one time forever.
    static time_t gpsLastEpoch = 0;
    static unsigned long gpsLastEpochMs = 0;
    static int gpsTickCount = 0;
    bool gpsTimeReady = false;
    if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
      struct tm t = {0};
      t.tm_year = gps.date.year() - 1900;
      t.tm_mon = gps.date.month() - 1;
      t.tm_mday = gps.date.day();
      t.tm_hour = gps.time.hour();
      t.tm_min = gps.time.minute();
      t.tm_sec = gps.time.second();
      time_t epoch = mktime(&t);
      if (epoch > 1577836800 && epoch < 4102444800) { // sanity: 2020 .. 2100
        if (epoch != gpsLastEpoch) {
          unsigned long elapsedMs = millis() - gpsLastEpochMs;
          if (gpsLastEpoch != 0 && epoch == gpsLastEpoch + 1 &&
              elapsedMs >= 300 && elapsedMs <= 3000) {
            if (gpsTickCount < 3) gpsTickCount++;
          } else {
            gpsTickCount = 0;
          }
          gpsLastEpoch = epoch;
          gpsLastEpochMs = millis();
        }
        gpsTimeReady = (gpsTickCount >= 3);
      }
    }
    // Demo mode: the synthesized NMEA clock must never be applied to the real
    // system clock - it could clobber an NTP/sensor-synced RTC.
    if (!ENABLE_DEMO_MODE && gpsTimeReady) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      if (abs((long)(tv.tv_sec - gpsLastEpoch)) > 5) {
        tv.tv_sec = gpsLastEpoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ----------------------------------------------------------------------------
// Sensor task - core 1 beside the display loop. GPS parsing lives in gpsTask
// (core 0); this task only owns the short I2C/ADC sensors and the mutex
// snapshot, finishing in a few ms per 20ms tick, so it can never hold the
// display hostage like the old GPS drain did.
// ----------------------------------------------------------------------------
void sensorTask(void *pvParameters) {
  for (;;) {
    if (ENABLE_DEMO_MODE)
      simulateRawSensors(); // synthetic hall pulses; the analog/compass
                            // read sites inject their own simulated raw values
    {
      unsigned long tStage = millis();
      processCompassSensor();
      sensorStageDiag("compass", tStage);
    }
    {
      unsigned long tStage = millis();
      processLightSensor();
      sensorStageDiag("light", tStage);
    }
    {
      unsigned long tStage = millis();
      processFuelSensor();
      sensorStageDiag("fuel", tStage);
    }
    {
      unsigned long tStage = millis();
      processTemperatureSensor();
      sensorStageDiag("temp", tStage);
    }
    updateAccelTimer();

    static unsigned long lastSlowRead = 0;
    static unsigned long lastVerySlowRead = 0;
    unsigned long nowSensor = millis();
    if (nowSensor - lastSlowRead >= 500) {
      lastSlowRead = nowSensor;
      processBatterySensor();
    }
    if (nowSensor - lastVerySlowRead >= 1000) {
      lastVerySlowRead = nowSensor;
      processFuelConsumption();
      updateAverageSpeed();
    }

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      g_sensorData.currentSpeed = currentCachedSpeed;
      g_sensorData.fuelLiters = fuelLiters;
      g_sensorData.fuelPercentage = fuelPercentage;
      g_sensorData.batteryVoltage = batteryVoltage;
      g_sensorData.engineTemperature = engineTemperature;
      g_sensorData.satellites = gps.satellites.value();
      // Demo mode: the odometer math has accumulated simulated distance into
      // demoOdoKm (NVS untouched) - surface it as a display-only total.
      g_sensorData.totalDistanceKm =
          ENABLE_DEMO_MODE ? (totalDistanceKm + demoOdoKm) : totalDistanceKm;
      g_sensorData.accelResultTime = accelResultTime;
      g_sensorData.accelState = accelState;
      g_sensorData.instantKml = instantKml;
      g_sensorData.averageKml = averageKml;
      g_sensorData.averageSpeed = averageSpeed;
      g_sensorData.maxSpeed = maxSpeed;
      g_sensorData.heading = currentHeading;

      if (systemTimeToLocal(g_sensorData.localHour, g_sensorData.minute,
                            g_sensorData.day, g_sensorData.month,
                            g_sensorData.year)) {
        g_sensorData.timeValid = true;
        g_sensorData.dateValid = true;
      } else {
        g_sensorData.timeValid = false;
        g_sensorData.dateValid = false;
      }

      g_sensorData.isGpsSpeedValid =
          gps.speed.isValid() && (g_sensorData.satellites >= MIN_SATELLITES);
      updateSpeedSourceMode();
      g_sensorData.speedSourceMode = heldSpeedSourceMode;
      xSemaphoreGive(g_stateMutex);
    } else {
      logPrintf("SENS SKIP: state mutex busy\n");
    }
    g_sensorLastTickMs = millis();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
