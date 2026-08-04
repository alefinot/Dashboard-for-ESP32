#include "dashboard.h"

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

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
  if (Wire.requestFrom(addr, 6) < 6) return false;
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
    Wire.requestFrom(addr, 16);
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
      Wire.requestFrom(addr, 1);
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
  int32_t offX = ((int32_t)compassCalMinX + compassCalMaxX) / 2;
  int32_t offY = ((int32_t)compassCalMinY + compassCalMaxY) / 2;
  int32_t offZ = ((int32_t)compassCalMinZ + compassCalMaxZ) / 2;
  COMPASS_CAL_X = offX;
  COMPASS_CAL_Y = offY;
  COMPASS_CAL_Z = offZ;
  int32_t spanX = (int32_t)compassCalMaxX - compassCalMinX;
  int32_t spanY = (int32_t)compassCalMaxY - compassCalMinY;
  bool lowSpan = (spanX < 200 || spanY < 200);
  { Preferences p; p.begin("cfg", false);
    p.putInt("CMP_CAL_X", COMPASS_CAL_X);
    p.putInt("CMP_CAL_Y", COMPASS_CAL_Y);
    p.putInt("CMP_CAL_Z", COMPASS_CAL_Z);
    p.end(); }

  // Tilt fit: the samples trace a circle in 3D whose plane is perpendicular to
  // the rotation (vertical) axis. Consecutive edge vectors d_i, d_{i+1} both
  // lie in that plane, so d_i x d_{i+1} points along its normal; the sum over
  // the whole rotation cancels wobble and yields the "up" axis in sensor
  // coordinates. A flat calibration naturally fits (0,0,1).
  char tiltTxt[64] = "";
  if (calSampleCount >= 32) {
    int64_t nx = 0, ny = 0, nz = 0;
    for (uint16_t i = 1; i + 1 < calSampleCount; i++) {
      int32_t dx1 = calSamples[i][0] - calSamples[i - 1][0];
      int32_t dy1 = calSamples[i][1] - calSamples[i - 1][1];
      int32_t dz1 = calSamples[i][2] - calSamples[i - 1][2];
      int32_t dx2 = calSamples[i + 1][0] - calSamples[i][0];
      int32_t dy2 = calSamples[i + 1][1] - calSamples[i][1];
      int32_t dz2 = calSamples[i + 1][2] - calSamples[i][2];
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
        float px = (float)calSamples[i][0] - offX;
        float py = (float)calSamples[i][1] - offY;
        float pz = (float)calSamples[i][2] - offZ;
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
  } else {
    snprintf(tiltTxt, sizeof(tiltTxt), " TILT FIT SKIPPED (few samples)");
  }

  snprintf(compassCalResult, sizeof(compassCalResult),
           "Calibration saved: X=%d Y=%d Z=%d%s (span %ld/%ld)%s",
           COMPASS_CAL_X, COMPASS_CAL_Y, COMPASS_CAL_Z, tiltTxt,
           (long)spanX, (long)spanY,
           lowSpan ? " - LOW SPAN, did you rotate?" : "");
  logPrintf("Compass: %s\n", compassCalResult);
}

void processCompassSensor() {
  if (!compassReady) return;
  int16_t x, y, z;
  bool ok = false;
  if (compassChip == COMPASS_HMC)
    ok = compassRead6(compassAddr, HMC5883L_X_MSB, true, x, y, z);
  else if (compassChip == COMPASS_P)
    ok = compassRead6(compassAddr, QMC5883P_DATA, false, x, y, z);
  else
    ok = compassRead6(compassAddr, QMC5883L_X_LSB, false, x, y, z);
  if (!ok) return;
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

  // Tilt-compensated heading: project the field onto the plane perpendicular
  // to the calibrated "up" axis, then atan2. Flat mount (0,0,1) degenerates
  // to the plain atan2(y - oy, x - ox).
  int32_t xc = (int32_t)x - COMPASS_CAL_X;
  int32_t yc = (int32_t)y - COMPASS_CAL_Y;
  int32_t zc = (int32_t)z - COMPASS_CAL_Z;
  float ux = (float)COMPASS_CAL_TX / 32767.0f;
  float uy = (float)COMPASS_CAL_TY / 32767.0f;
  float uz = (float)COMPASS_CAL_TZ / 32767.0f;
  float ulen = sqrtf(ux * ux + uy * uy + uz * uz);
  if (ulen > 0.001f) { ux /= ulen; uy /= ulen; uz /= ulen; }
  float px = (float)xc, py = (float)yc, pz = (float)zc;
  float dot = px * ux + py * uy + pz * uz;
  float h = atan2f(py - dot * uy, px - dot * ux) * (180.0f / M_PI);
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

double tripDistanceKm = 0.0;
float tripStartFuelLiters = -1.0f;
float tripFuelConsumedLiters = 0.0f;
float instantKml = 0.0f;
float averageKml = 0.0f;
float averageSpeed = 0.0f;
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

// ----------------------------------------------------------------------------
// Hall sensor (ISR + speed)
// ----------------------------------------------------------------------------
void IRAM_ATTR hallSensorISR() {
  constexpr unsigned long DEBOUNCE_US = 10000;
  unsigned long now = micros();
  if (now - lastHallPulseTimeUs > DEBOUNCE_US) {
    portENTER_CRITICAL_ISR(&hallMux);
    hallPulseIntervalUs = now - lastHallPulseTimeUs;
    lastHallPulseTimeUs = now;
    hallPulseCount++;
    portEXIT_CRITICAL_ISR(&hallMux);
  }
}

inline float getHallSpeed() {
  unsigned long lastTimeUs, intervalUs;
  portENTER_CRITICAL(&hallMux);
  lastTimeUs = lastHallPulseTimeUs;
  intervalUs = hallPulseIntervalUs;
  portEXIT_CRITICAL(&hallMux);
  if (micros() - lastTimeUs > 2000000UL || intervalUs == 0)
    return 0.0f;
  return WHEEL_SPEED_FACTOR / (float)intervalUs;
}

void updateFilteredSpeed() {
  float hallSpeed = getHallSpeed();
  int sats = gps.satellites.value();
  bool isGpsValid = gps.speed.isValid() && (sats >= MIN_SATELLITES);

  float raw = 0.0f;
  int mode = 0; // 0=hall, 1=gps, 2=fused

  if (!isGpsValid) {
    // Not enough satellites: GPS is untrusted, hall only
    raw = hallSpeed;
  } else if (GPS_ONLY_MODE) {
    // User-forced GPS-only (e.g. no hall sensor installed)
    raw = (float)gps.speed.kmph();
    mode = 1;
  } else if (hallSpeed > 0.0f) {
    // Always compare both sensors (unless satellites are insufficient)
    float gpsSpeed = (float)gps.speed.kmph();
    float delta = fabsf(gpsSpeed - hallSpeed);
    if (delta > MAX_SPEED_DELTA_KMH) {
      raw = hallSpeed; // GPS contradicts hall: reject GPS
    } else if (delta < GPS_MIN_DEV_KMH) {
      raw = gpsSpeed; // agreement zone: trust GPS
      mode = 1;
    } else {
      float gpsWeight = (float)(sats - MIN_SATELLITES + 1) /
                        (float)(OPTIMAL_SATELLITES - MIN_SATELLITES + 1);
      raw = gpsSpeed * gpsWeight + hallSpeed * (1.0f - gpsWeight);
      mode = 2;
    }
  }
  // else: no hall sensor and GPS-only disabled -> 0

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
}

// ----------------------------------------------------------------------------
// Analog sensors
// ----------------------------------------------------------------------------
void processBatterySensor() {
  int rawADC = analogRead(BATTERY_SENSE_PIN);
  rawBatteryADC = rawADC;
  batteryVoltage = ((float)rawADC * ADC_VOLTS_FACTOR * 5.7f) + 0.2f;
  if (batteryVoltage < 2.0f)
    batteryVoltage = 0.0f;
}

void processTemperatureSensor() {
  int rawADC = analogRead(TEMP_SENSE_PIN);
  rawTempADC = rawADC;
  if (rawADC <= 100 || rawADC >= 4000) {
    engineTemperature = 0.0f;
    return;
  }
  float vOut = (float)rawADC * ADC_VOLTS_FACTOR;
  float resistance = NTC_R_BALANCE * (vOut / (3.3f - vOut));
  engineTemperature = (1.0f / (logf(resistance / NTC_R_ROOM) / NTC_BETA +
                               NTC_INV_ROOM_KELVIN)) -
                      273.15f;
}

void processLightSensor() {
  static const float alpha = 0.15f;
  analogRead(LIGHT_SENSOR_PIN);
  int raw = analogRead(LIGHT_SENSOR_PIN);
  ambientLightValue = raw;
  rawLightADC = raw;
  if (filteredAmbientValue < 1.0f)
    filteredAmbientValue = (float)raw;
  else
    filteredAmbientValue = ((float)raw * alpha) + (filteredAmbientValue * (1.0f - alpha));
}

void processFuelSensor() {
  int sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(FUEL_TOUCH_PIN);
  }
  int instantReading = sum / 16;
  rawFuelADC = instantReading;
  filteredReading = ((float)instantReading * FUEL_FILTER_ALPHA) +
                    (filteredReading * (1.0f - FUEL_FILTER_ALPHA));
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
      fuelLiters = (float)i + ((filteredReading - touchTable[i]) /
                               (float)(touchTable[i + 1] - touchTable[i]));
      fuelPercentage = constrain(
          (int)((fuelLiters / (float)(FUEL_TOUCH_POINTS - 1)) * 100.0f), 0, 100);
      return;
    }
  }
}

// ----------------------------------------------------------------------------
// Odometer (Hall pulses + GPS distance)
// ----------------------------------------------------------------------------
void updateGPSOdometer() {
  if (ENABLE_POWER_SENSE && digitalRead(POWER_SENSE_PIN) == LOW) {
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

  if (!isGpsValid && pulses > 0) {
    totalDistanceKm += (double)pulses * WHEEL_DIST_PER_PULSE_KM;
    tripDistanceKm += (double)pulses * WHEEL_DIST_PER_PULSE_KM;
    if (totalDistanceKm - lastSavedOdo >= 1.0) {
      preferences.begin("dashboard", false);
      preferences.putDouble("odo", totalDistanceKm);
      preferences.end();
      lastSavedOdo = totalDistanceKm;
    }
  }
  if (isGpsValid && gps.location.isUpdated()) {
    if (getFilteredSpeed() > 0.0f && hasLastPos) {
      double distanceMeters = TinyGPSPlus::distanceBetween(
          gps.location.lat(), gps.location.lng(), lastLat, lastLon);
      if (distanceMeters < 500.0) {
        double dKm = (distanceMeters / 1000.0);
        totalDistanceKm += dKm;
        tripDistanceKm += dKm;
        if (totalDistanceKm - lastSavedOdo >= 1.0) {
          preferences.begin("dashboard", false);
          preferences.putDouble("odo", totalDistanceKm);
          preferences.end();
          lastSavedOdo = totalDistanceKm;
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
    else if (consumed < -0.5f) {
      tripStartFuelLiters = fuelLiters;
      tripFuelConsumedLiters = 0.0f;
    }
  }
  averageKml = (tripDistanceKm > 0.05 && tripFuelConsumedLiters > 0.01f)
                   ? (float)(tripDistanceKm / tripFuelConsumedLiters)
                   : 0.0f;
  if (averageKml > 99.9f)
    averageKml = 99.9f;

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
      float rawInst = (float)(dDist / dFuel);
      if (rawInst > 99.9f)
        rawInst = 99.9f;
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
// Sensor task
// ----------------------------------------------------------------------------
void sensorTask(void *pvParameters) {
  static bool gpsWatchdogLogged = false;
  static bool ubxDiagLogged = false;
  static bool rawDumpLogged = false;
  static unsigned long gpsWatchdogStart = 0;
  static uint8_t gpsRawBuf[256];
  static uint8_t gpsRawIdx = 0;
  for (;;) {
    while (gpsSerial.available() > 0) {
      uint8_t b = gpsSerial.read();
      if (gpsRawIdx < sizeof(gpsRawBuf))
        gpsRawBuf[gpsRawIdx++] = b;
      gpsRxBytes++;
      gps.encode((char)b);
      ubxParseByte(b);
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
    processCompassSensor();
    processLightSensor();
    processFuelSensor();
    processTemperatureSensor();
    updateGPSOdometer();
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
    if (gpsTimeReady) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      if (abs((long)(tv.tv_sec - gpsLastEpoch)) > 5) {
        tv.tv_sec = gpsLastEpoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
      }
    }

    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (ENABLE_DEMO_MODE) {
        unsigned long t = millis();
        float simSpeed = 60.0f + 50.0f * sinf(t / 2000.0f);
        g_sensorData.currentSpeed = (simSpeed < 0.0f) ? 0.0f : simSpeed;
        g_sensorData.fuelLiters = 5.0f + 5.0f * sinf(t / 5000.0f);
        g_sensorData.fuelPercentage =
            (int)((g_sensorData.fuelLiters / 10.0f) * 100.0f);
        g_sensorData.batteryVoltage = 12.5f + 1.5f * sinf(t / 3000.0f);
        g_sensorData.engineTemperature = 10.0f + 100.0f * (0.5f + 0.5f * sinf(t / 8000.0f));
        g_sensorData.satellites = 8 + (int)(3.0f * sinf(t / 10000.0f));
        g_sensorData.totalDistanceKm = totalDistanceKm + (t / 10000.0);
        g_sensorData.accelResultTime = 4.5f;
        g_sensorData.accelState = FINISHED;
        static float demoInstKml = 15.0f;
        static float demoAvgKml = 18.5f;
        static unsigned long lastDemoKmlUpdate = 0;
        if (t - lastDemoKmlUpdate >= 1000) {
          lastDemoKmlUpdate = t;
          demoInstKml = 15.0f + 5.0f * cosf(t / 2000.0f);
          demoAvgKml = 18.0f + 2.0f * sinf(t / 6000.0f);
        }
        g_sensorData.instantKml = demoInstKml;
        g_sensorData.averageKml = demoAvgKml;
        g_sensorData.averageSpeed = simSpeed * 0.8f;
        g_sensorData.timeValid = true;
        g_sensorData.dateValid = true;
        g_sensorData.isGpsSpeedValid = true;
        g_sensorData.speedSourceMode = 1;
        g_sensorData.heading = fmodf((float)t * 0.02f, 360.0f); // steady 20°/s rotation
        ambientLightValue = 500 + (int)(2500.0f * (0.5f + 0.5f * sinf(t / 3000.0f)));
        g_sensorData.localHour = 10;
        g_sensorData.minute = (t / 1000) % 60;
        g_sensorData.day = 16;
        g_sensorData.month = 7;
        g_sensorData.year = 26;
      } else {
        g_sensorData.currentSpeed = currentCachedSpeed;
        g_sensorData.fuelLiters = fuelLiters;
        g_sensorData.fuelPercentage = fuelPercentage;
        g_sensorData.batteryVoltage = batteryVoltage;
        g_sensorData.engineTemperature = engineTemperature;
        g_sensorData.satellites = gps.satellites.value();
        g_sensorData.totalDistanceKm = totalDistanceKm;
        g_sensorData.accelResultTime = accelResultTime;
        g_sensorData.accelState = accelState;
        g_sensorData.instantKml = instantKml;
        g_sensorData.averageKml = averageKml;
        g_sensorData.averageSpeed = averageSpeed;
        g_sensorData.heading = currentHeading;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (tv.tv_sec > 1000000000) {
          g_sensorData.timeValid = true;
          g_sensorData.dateValid = true;
          time_t local = tv.tv_sec;
          struct tm *loc_tm = gmtime(&local);
          int dst = 0;
          if (TZ_DST_ENABLED) {
            int euroOff = getEuropeanOffset(loc_tm->tm_year + 1900, loc_tm->tm_mon + 1,
                                            loc_tm->tm_mday, loc_tm->tm_hour);
            dst = euroOff - 1;
          }
          int offset = TZ_OFFSET_HOURS + dst;
          local += (offset * 3600);
          loc_tm = gmtime(&local);

          g_sensorData.localHour = loc_tm->tm_hour;
          g_sensorData.minute = loc_tm->tm_min;
          g_sensorData.day = loc_tm->tm_mday;
          g_sensorData.month = loc_tm->tm_mon + 1;
          g_sensorData.year = loc_tm->tm_year % 100;
        } else {
          g_sensorData.timeValid = false;
          g_sensorData.dateValid = false;
        }

        g_sensorData.isGpsSpeedValid =
            gps.speed.isValid() && (g_sensorData.satellites >= MIN_SATELLITES);
        float hallSpeedNow = getHallSpeed();
        if (GPS_ONLY_MODE && g_sensorData.isGpsSpeedValid) {
          g_sensorData.speedSourceMode = 1;
        } else if (g_sensorData.isGpsSpeedValid && hallSpeedNow > 0.0f) {
          float gpsSpeedNow = (float)gps.speed.kmph();
          float deltaNow = fabsf(gpsSpeedNow - hallSpeedNow);
          if (deltaNow > MAX_SPEED_DELTA_KMH)
            g_sensorData.speedSourceMode = 0;
          else if (deltaNow < GPS_MIN_DEV_KMH)
            g_sensorData.speedSourceMode = 1;
          else
            g_sensorData.speedSourceMode = 2;
        } else {
          g_sensorData.speedSourceMode = 0;
        }
      }
      xSemaphoreGive(g_stateMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
