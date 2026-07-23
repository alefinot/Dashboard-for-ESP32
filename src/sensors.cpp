#include "dashboard.h"

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ----------------------------------------------------------------------------
// QMC5883L compass driver (I2C)
// ----------------------------------------------------------------------------
#define QMC5883L_ADDR   0x0D
#define QMC5883L_X_LSB  0x00
#define QMC5883L_STATUS 0x09
#define QMC5883L_CTRL1  0x0B

static bool compassReady = false;

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

void processCompassSensor() {
  if (!compassReady) return;
  int16_t x, y, z;
  if (!qmcReadRaw(x, y, z)) return;
  float h = atan2f((float)y, (float)x) * (180.0f / M_PI);
  if (h < 0) h += 360.0f;
  currentHeading = h;
}

// ----------------------------------------------------------------------------
// Shared state (defined here)
// ----------------------------------------------------------------------------
uint16_t DEBUG_BOX_COLOR = TFT_MAGENTA;
float currentMeasuredFps = 0.0f;
float fpsHistory[FPS_AVG_SAMPLES] = {0.0f};
uint8_t fpsHistoryIndex = 0;
uint8_t fpsHistoryCount = 0;
float currentAverageFps = 0.0f;

unsigned long lastDisplayUpdate = 0;
float filteredReading = 950.0f;
float fuelLiters = 0.0f;
int fuelPercentage = 0;
float batteryVoltage = 0.0f;
float engineTemperature = 0.0f;
int currentTimezoneOffset = 1;
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

bool isSelfTestActive = false;
bool overrideTimeDateStr = false;
int overrideSpeed = 188;
double overrideOdo = 888888.8;
int overrideSat = 88;
float overrideBat = 18.8f;
float overrideTimer = 18.88f;

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
  batteryVoltage = ((float)rawADC * ADC_VOLTS_FACTOR * 5.7f) + 0.2f;
  if (batteryVoltage < 2.0f)
    batteryVoltage = 0.0f;
}

void processTemperatureSensor() {
  int rawADC = analogRead(TEMP_SENSE_PIN);
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

void processFuelSensor() {
  int instantReading = analogRead(FUEL_TOUCH_PIN);
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
        g_sensorData.timeValid = true;
        g_sensorData.dateValid = true;
        g_sensorData.isGpsSpeedValid = true;
        g_sensorData.heading = 180.0f + 180.0f * sinf(t / 5000.0f);
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
        g_sensorData.heading = currentHeading;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        if (tv.tv_sec > 1000000000) {
          g_sensorData.timeValid = true;
          g_sensorData.dateValid = true;
          time_t local = tv.tv_sec;
          struct tm *loc_tm = gmtime(&local);
          int offset =
              getEuropeanOffset(loc_tm->tm_year + 1900, loc_tm->tm_mon + 1,
                                loc_tm->tm_mday, loc_tm->tm_hour);
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
      }
      xSemaphoreGive(g_stateMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
