#include "dashboard.h"

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ----------------------------------------------------------------------------
// QMC5883L compass driver (I2C)
// ----------------------------------------------------------------------------
#define QMC5883L_ADDR   0x0D
#define QMC5883L_X_LSB  0x00
#define QMC5883L_CTRL1  0x0B

bool compassReady = false;

static void qmcWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMC5883L_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

static bool qmcReadRaw(int16_t &x, int16_t &y, int16_t &z) {
  Wire.beginTransmission(QMC5883L_ADDR);
  Wire.write(QMC5883L_X_LSB);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(QMC5883L_ADDR, 6) < 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  x = (int16_t)(b[1] << 8 | b[0]);
  y = (int16_t)(b[3] << 8 | b[2]);
  z = (int16_t)(b[5] << 8 | b[4]);
  return true;
}

bool initCompass() {
  Wire.beginTransmission(QMC5883L_ADDR);
  if (Wire.endTransmission() != 0) return false;
  qmcWrite(QMC5883L_CTRL1, 0x1D); // cont 200Hz 8G 512osr
  delay(10);
  compassReady = true;
  return true;
}

int16_t compassRawX = 0, compassRawY = 0, compassRawZ = 0;

void processCompassSensor() {
  if (!compassReady) return;
  int16_t x, y, z;
  if (!qmcReadRaw(x, y, z)) return;
  compassRawX = x;
  compassRawY = y;
  compassRawZ = z;
  float h = atan2f((float)y, (float)x) * (180.0f / M_PI);
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
// BZGNSS P25 Pro (u-blox M10) configuration
// The module ships as NMEA/UBX dual-protocol at 115200 baud / 10 Hz by
// default, but some units leave the factory in UBX mode for flight
// controllers. These UBX commands force UART1 to plain NMEA at GPS_BAUD with
// a 10 Hz update rate so TinyGPS++ always sees standard sentences.
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

void configureGNSS() {
  while (gpsSerial.available()) gpsSerial.read();
  gpsSerial.flush();

  // UBX-CFG-PRT: UART1 -> NMEA in/out, 8N1 @ GPS_BAUD
  uint8_t prt[20];
  memset(prt, 0, sizeof(prt));
  prt[0] = 1; // portID = UART1
  uint32_t mode = 0x000008D0; // 8 data bits, no parity, 1 stop bit
  uint32_t baud = (uint32_t)GPS_BAUD;
  uint16_t proto = 0x0001;    // NMEA only
  prt[4] = mode & 0xFF;         prt[5] = (mode >> 8) & 0xFF;
  prt[6] = (mode >> 16) & 0xFF; prt[7] = (mode >> 24) & 0xFF;
  prt[8] = baud & 0xFF;         prt[9] = (baud >> 8) & 0xFF;
  prt[10] = (baud >> 16) & 0xFF; prt[11] = (baud >> 24) & 0xFF;
  prt[12] = proto & 0xFF;       prt[13] = (proto >> 8) & 0xFF; // inProtoMask
  prt[14] = proto & 0xFF;       prt[15] = (proto >> 8) & 0xFF; // outProtoMask
  ubxSend(prt, 0x06, 0x00, sizeof(prt));
  delay(50);

  // UBX-CFG-RATE: 10 Hz navigation/measurement rate
  uint8_t rate[6] = {0x64, 0x00, 0x01, 0x00, 0x00, 0x00};
  ubxSend(rate, 0x06, 0x08, sizeof(rate));
  delay(50);

  while (gpsSerial.available()) gpsSerial.read();
  logPrintf("GNSS: forced NMEA @ %d baud, 10 Hz\n", GPS_BAUD);
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
  if (hallSpeed == 0.0f) {
    currentCachedSpeed = 0.0f;
    return;
  }
  if (!isGpsValid) {
    currentCachedSpeed = (hallSpeed < MIN_SPEED_THRESHOLD) ? 0.0f : hallSpeed;
    return;
  }
  float gpsSpeed = (float)gps.speed.kmph();
  if (sats >= OPTIMAL_SATELLITES) {
    currentCachedSpeed = (gpsSpeed < MIN_SPEED_THRESHOLD) ? 0.0f : gpsSpeed;
    return;
  }
  float delta = fabsf(gpsSpeed - hallSpeed);
  if (delta > MAX_SPEED_DELTA_KMH) {
    currentCachedSpeed = (hallSpeed < MIN_SPEED_THRESHOLD) ? 0.0f : hallSpeed;
    return;
  }
  float gpsWeight = (float)(sats - MIN_SATELLITES + 1) /
                    (float)(OPTIMAL_SATELLITES - MIN_SATELLITES + 1);
  float fusedSpeed = (gpsSpeed * gpsWeight) + (hallSpeed * (1.0f - gpsWeight));
  currentCachedSpeed = (fusedSpeed < MIN_SPEED_THRESHOLD) ? 0.0f : fusedSpeed;
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
  for (int i = 0; i < 64; i++) {
    sum += analogRead(FUEL_TOUCH_PIN);
  }
  int instantReading = sum / 64;
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
// Sensor task
// ----------------------------------------------------------------------------
void sensorTask(void *pvParameters) {
  for (;;) {
    while (gpsSerial.available() > 0)
      gps.encode(gpsSerial.read());
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
    if (gps.date.isValid() && gps.time.isValid()) {
      struct tm t = {0};
      t.tm_year = gps.date.year() - 1900;
      t.tm_mon = gps.date.month() - 1;
      t.tm_mday = gps.date.day();
      t.tm_hour = gps.time.hour();
      t.tm_min = gps.time.minute();
      t.tm_sec = gps.time.second();
      time_t epoch = mktime(&t);
      struct timeval tv;
      gettimeofday(&tv, NULL);
      if (abs((long)(tv.tv_sec - epoch)) > 5) {
        tv.tv_sec = epoch;
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
        g_sensorData.heading = 180.0f + 180.0f * sinf(t / 5000.0f);
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
        if (g_sensorData.isGpsSpeedValid) {
          if (g_sensorData.satellites >= OPTIMAL_SATELLITES)
            g_sensorData.speedSourceMode = 1;
          else if (fabsf((float)gps.speed.kmph() - getHallSpeed()) <= MAX_SPEED_DELTA_KMH)
            g_sensorData.speedSourceMode = 2;
          else
            g_sensorData.speedSourceMode = 0;
        } else {
          g_sensorData.speedSourceMode = 0;
        }
      }
      xSemaphoreGive(g_stateMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
