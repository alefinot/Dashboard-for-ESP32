#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>
#include <LittleFS.h>

#include <esp32-hal-ledc.h>
#include <SPI.h>
#include <algorithm>
#include <utility>
#include <vector>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <HardwareSerial.h>
#include <Wire.h>
#include <Preferences.h>
#include <TinyGPS++.h>
#include <cmath>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

// ----------------------------------------------------------------------------
// Forward type definitions (must precede any extern usage below)
// ----------------------------------------------------------------------------
enum TimerState { READY, RUNNING, FINISHED };

struct SensorSnapshot {
  float currentSpeed = 0.0f;
  float fuelLiters = 0.0f;
  int fuelPercentage = 0;
  float batteryVoltage = 0.0f;
  float engineTemperature = 0.0f;
  int satellites = 0;
  double totalDistanceKm = 0.0;
  float accelResultTime = 0.0f;
  TimerState accelState = READY;
  float instantKml = 0.0f;
  float averageKml = 0.0f;
  float averageSpeed = 0.0f;
  float heading = 0.0f;
  int localHour = 0;
  int minute = 0;
  int day = 0;
  int month = 0;
  int year = 0;
  bool timeValid = false;
  bool dateValid = false;
  bool isGpsSpeedValid = false;
  int speedSourceMode = 0; // 0=Hall, 1=GPS, 2=G+H
};

class LGFX_ST7789_4 : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX_ST7789_4();

  void applyBusConfig();
  void loadVLWFont(const char *path);
  void getTextBounds(const char *string, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h);
  void getTextBounds(const String &str, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h);
};

// ----------------------------------------------------------------------------
// Pin assignment (ESP32 WROOM compatible)
// ----------------------------------------------------------------------------
extern uint32_t SPI_BUS_SPEED;
constexpr int HALL_SENSOR_PIN = 33;
#define SPI_DC 27
#define SPI_RST 14
#define CS_DISPLAY 5
#define BL_DISPLAY 12
#define BACKLIGHT_CHANNEL 0
// NOTE: Fuel sensor must NOT share the Hall sensor pin. The original code used
// pin 33 for both, which made analog fuel readings unreliable because the Hall
// interrupt also fires on that line. It is now assigned to a dedicated ADC pin.
#define FUEL_TOUCH_PIN 32
#define RXD2 16 // TEST: moved from 25 for pin-swap isolation test
#define TXD2 17 // TEST: moved from 26 for pin-swap isolation test
#define COMPASS_SDA 13 // TEST: moved from 21 (board silkscreen labels unreliable)
#define COMPASS_SCL 15 // TEST: moved from 22
#define POWER_SENSE_PIN 4
#define BATTERY_SENSE_PIN 35
#define TEMP_SENSE_PIN 36
#define LIGHT_SENSOR_PIN 34

// ----------------------------------------------------------------------------
// Alignment constants
// ----------------------------------------------------------------------------
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2

// ----------------------------------------------------------------------------
// Configuration constants & runtime variables
// ----------------------------------------------------------------------------
extern int DISPLAY_ROTATION;
extern String SPLASH_SIGNATURE;
extern String REBOOT_SIGNATURE;
extern String DASHBOARD_SIGNATURE;

extern int TEMP_BAR_MIN;
extern int TEMP_BAR_MAX;
extern int TEMP_WARN_RED;
extern int TEMP_WARN_YEL;
extern int TEMP_WARN_GRN;
extern int FUEL_WARN_RED;
extern int FUEL_WARN_YEL;

extern String COLOR_TEMP_NORM;
extern String COLOR_TEMP_WARN;
extern String COLOR_TEMP_CRIT;
extern String COLOR_FUEL_NORM;
extern String COLOR_FUEL_WARN;
extern String COLOR_FUEL_CRIT;

extern uint16_t c_temp_norm, c_temp_warn, c_temp_crit;
extern uint16_t c_fuel_norm, c_fuel_warn, c_fuel_crit;
extern uint16_t ghost_color;
extern String GHOST_COLOR_STR;

extern int DISPLAY_WIDTH;
extern int DISPLAY_HEIGHT;
extern int SHUTDOWN_TIME_MS;

extern int BIG_CENTER_X;
extern int BIG_CENTER_Y;

extern float WHEEL_CIRCUMFERENCE_MM;
extern float FUEL_FILTER_ALPHA;

extern float NTC_R_BALANCE;
extern float NTC_R_ROOM;
extern float NTC_BETA;

extern int GPS_BAUD;
extern int MIN_SATELLITES;
extern int OPTIMAL_SATELLITES;
extern float MAX_SPEED_DELTA_KMH;
extern float MIN_SPEED_THRESHOLD;
extern float GPS_START_KMH;
extern int GPS_STOP_SETTLE_MS;
extern float GPS_MIN_DEV_KMH;
extern bool GPS_ONLY_MODE;
extern float ACCEL_START_SPEED;
extern float ACCEL_TARGET_SPEED;
extern float ACCEL_MAX_TIME;
extern String ACCEL_BADGE_LINE1;
extern String ACCEL_BADGE_LINE2;

extern int OFFSET_BIG_TIME_X;
extern int OFFSET_BIG_TIME_Y;
extern int OFFSET_BIG_DATE_X;
extern int OFFSET_BIG_DATE_Y;
extern int OFFSET_BIG_SIGNATURE_X;
extern int OFFSET_BIG_SIGNATURE_Y;
extern int OFFSET_BIG_SPEED_NUM_X;
extern int OFFSET_BIG_SPEED_NUM_Y;
extern int OFFSET_BIG_SPEED_UNIT_X;
extern int OFFSET_BIG_SPEED_UNIT_Y;
extern int OFFSET_BIG_ODO_X;
extern int OFFSET_BIG_ODO_Y;
extern int OFFSET_BIG_SAT_X;
extern int OFFSET_BIG_SAT_Y;
extern int OFFSET_BIG_TMR_X;
extern int OFFSET_BIG_TMR_Y;
extern int OFFSET_BIG_BAT_X;
extern int OFFSET_BIG_BAT_Y;

extern int SIDEBAR_LEFT_X;
extern int SIDEBAR_LEFT_Y;
extern int SIDEBAR_RIGHT_X;
extern int SIDEBAR_RIGHT_Y;
extern int SIDEBAR_BAR_WIDTH;
extern int SIDEBAR_BAR_HEIGHT;

extern int OFFSET_HALL_ICON_X;
extern int OFFSET_HALL_ICON_Y;
extern int OFFSET_WIFI_ICON_X;
extern int OFFSET_WIFI_ICON_Y;
extern int OFFSET_INST_KML_X;
extern int OFFSET_INST_KML_Y;
extern int OFFSET_AVG_KML_X;
extern int OFFSET_AVG_KML_Y;
extern int OFFSET_FUEL_LTRS_X;
extern int OFFSET_FUEL_LTRS_Y;
extern int OFFSET_AVG_SPEED_X;
extern int OFFSET_AVG_SPEED_Y;
extern int OFFSET_COMPASS_X;
extern int OFFSET_COMPASS_Y;

extern int ALIGN_BIG_SPEED_NUM;
extern int ALIGN_BIG_SAT;
extern int ALIGN_BIG_TMR;
extern int ALIGN_BIG_BAT;
extern int ALIGN_INST_KML;
extern int ALIGN_AVG_KML;
extern int ALIGN_FUEL_LTRS;
extern int ALIGN_AVG_SPEED;

extern bool SHOW_ELEMENT_BOUNDS;
extern bool ENABLE_POWER_SENSE;
extern bool ENABLE_CIRCLE_TEST;
extern bool ENABLE_DEMO_MODE;
extern bool ENABLE_ANTIALIASING;
extern float AA_SHARPNESS;
 
extern bool SHOW_FPS_COUNTER_DEFAULT;
extern bool GPS_DEBUG_DEFAULT;
extern int OFFSET_BIG_FPS_X;
extern int OFFSET_BIG_FPS_Y;

extern bool ENABLE_NIGHT_MODE;
extern int NIGHT_MODE_START_HOUR;
extern int NIGHT_MODE_END_HOUR;
extern bool DISPLAY_INVERT_COLORS;

extern int TARGET_FPS;
extern int BACKLIGHT_BRIGHTNESS;
extern bool ENABLE_AUTO_BRIGHTNESS;
extern int LIGHT_SENSOR_DARK_VAL;
extern int LIGHT_SENSOR_BRIGHT_VAL;
extern int AUTO_BRIGHT_DARK;
extern int AUTO_BRIGHT_LIGHT;
extern int AUTO_BRIGHT_FADE_MS;
extern int ambientLightValue;
extern float filteredAmbientValue;
extern int FADE_DURATION_MS;
extern int currentBrightnessTarget;

extern int REFRESH_SPEED_MS;
extern int REFRESH_SAT_MS;
extern int REFRESH_TMR_MS;
extern int REFRESH_BAT_MS;
extern int REFRESH_INST_MS;
extern int REFRESH_AVG_MS;
extern int REFRESH_FUEL_MS;
extern int REFRESH_ODO_MS;
extern int REFRESH_TIME_MS;
extern int REFRESH_SIDEBAR_TEMP_MS;
extern int REFRESH_SIDEBAR_FUEL_MS;
extern int REFRESH_AVG_SPEED_MS;
extern int REFRESH_COMPASS_MS;
extern float COMPASS_DECLINATION_DEG;

extern int SPEED_DIGITS;
extern int SAT_DIGITS;
extern int TMR_INT_DIGITS;
extern int TMR_DEC_DIGITS;
extern int BAT_INT_DIGITS;
extern int BAT_DEC_DIGITS;
extern int INST_INT_DIGITS;
extern int INST_DEC_DIGITS;
extern int AVG_INT_DIGITS;
extern int AVG_DEC_DIGITS;
extern int FUEL_INT_DIGITS;
extern int FUEL_DEC_DIGITS;
extern int AVG_SPEED_INT_DIGITS;
extern int AVG_SPEED_DEC_DIGITS;
extern int HEADING_DIGITS;
extern int ODO_INT_DIGITS;
extern int ODO_DEC_DIGITS;

extern bool ENABLE_DYNAMIC_CPU;
extern int MANUAL_CPU_FREQ;
extern bool ENABLE_CPU_THROTTLE;
extern int CPU_THROTTLE_TEMP_WARN;
extern int CPU_THROTTLE_TEMP_CRIT;

extern unsigned long DISPLAY_REFRESH_MS;
extern unsigned long TELEMETRY_REFRESH_MS;
extern float WHEEL_SPEED_FACTOR;
extern double WHEEL_DIST_PER_PULSE_KM;
extern float NTC_INV_ROOM_KELVIN;
extern float ADC_VOLTS_FACTOR;
extern bool showFpsCounter;
extern bool showGpsDebug;

extern String WIFI_SSID;
extern String WIFI_PASSWORD;
extern String WIFI_SSID_1;
extern String WIFI_PASSWORD_1;
extern String WIFI_SSID_2;
extern String WIFI_PASSWORD_2;
extern String WIFI_SSID_3;
extern String WIFI_PASSWORD_3;
extern String WIFI_SSID_4;
extern String WIFI_PASSWORD_4;
extern int WIFI_TX_POWER_DBM;

extern bool NTP_ENABLED;
extern String NTP_SERVER;
extern int TZ_OFFSET_HOURS;
extern bool TZ_DST_ENABLED;

extern bool OTA_PULL_ENABLED;
extern String OTA_PULL_URL;
extern int OTA_PULL_INTERVAL_HOURS;
extern String OTA_CURRENT_VERSION;
extern String CONFIG_PIN;

// ----------------------------------------------------------------------------
// Fuel touch table
// ----------------------------------------------------------------------------
constexpr int MAX_TOUCH_POINTS = 20;
extern int FUEL_TOUCH_POINTS;
extern int touchTable[MAX_TOUCH_POINTS];

// ----------------------------------------------------------------------------
// Logging ring buffer
// ----------------------------------------------------------------------------
#define LOG_BUF_SIZE 4096
extern char logBuf[LOG_BUF_SIZE];
extern volatile int logHead;
extern volatile int logTail;
extern volatile unsigned long logSequence;
void logPrintf(const char *fmt, ...);

// ----------------------------------------------------------------------------
// Shared state
// ----------------------------------------------------------------------------
extern Preferences preferences;
extern WebServer server;

extern bool forceFullRedraw;
extern volatile bool pendingSleep;
extern volatile bool pendingReboot;
extern volatile bool otaUpdateInProgress;
extern volatile bool pendingOtaScreen;
extern volatile int otaProgressFillW;
extern volatile int otaProgressTarget;

extern bool pendingInvertDisplay;
extern int pendingBacklightValue;

extern LGFX_ST7789_4 display;
extern TinyGPSPlus gps;
extern HardwareSerial gpsSerial;

extern uint16_t DEBUG_BOX_COLOR;

// GPS debug counters (sensors.cpp), read by the on-screen overlay (gfx.cpp)
extern volatile uint32_t gpsRxBytes;
extern volatile uint32_t ubxFramesParsed;
extern volatile uint8_t ubxLastFixType;
extern volatile uint8_t ubxLastNumSv;
extern volatile double ubxLastLat;
extern volatile double ubxLastLon;
extern volatile uint32_t ubxSyncSeen;
extern volatile uint32_t ubxCkFail;
extern volatile uint32_t ubxOversize;
extern float cpuUsagePct;
extern float currentMeasuredFps;
constexpr uint8_t FPS_AVG_SAMPLES = 5;
extern float fpsHistory[FPS_AVG_SAMPLES];
extern uint8_t fpsHistoryIndex;
extern uint8_t fpsHistoryCount;
extern float currentAverageFps;

extern unsigned long lastDisplayUpdate;
extern float filteredReading;
extern int rawFuelADC;
extern int rawBatteryADC;
extern int rawTempADC;
extern int rawLightADC;
extern int16_t compassRawX;
extern int16_t compassRawY;
extern int16_t compassRawZ;
extern float fuelLiters;
extern int fuelPercentage;
extern float batteryVoltage;
extern float engineTemperature;
extern double totalDistanceKm;
extern double lastSavedOdo;
void setOdometerKm(double km);
extern double lastLat;
extern double lastLon;
extern bool hasLastPos;
extern int splashCurrentProgress;
extern float currentCachedSpeed;
extern unsigned long g_startupTime;
extern float currentHeading;

extern portMUX_TYPE hallMux;
extern volatile unsigned long lastHallPulseTimeUs;
extern volatile unsigned long hallPulseIntervalUs;
extern volatile unsigned long hallPulseCount;

extern double tripDistanceKm;
extern float tripStartFuelLiters;
extern float tripFuelConsumedLiters;
extern unsigned long movingTimeMs;
extern float instantKml;
extern float averageKml;
extern float averageSpeed;

extern TimerState accelState;
extern unsigned long accelStartTime;
extern float accelResultTime;

extern SemaphoreHandle_t g_stateMutex;
extern SensorSnapshot g_sensorData;

// ----------------------------------------------------------------------------
// Config API
// ----------------------------------------------------------------------------
void applyColors();
void processConfig(int mode, JsonDocument *doc = nullptr);
void recalculateDerivedParams();
uint16_t hexToRGB565(String hex);

// ----------------------------------------------------------------------------
// GFX / drawing API
// ----------------------------------------------------------------------------
uint16_t blendColor(uint16_t fg, uint16_t bg, float alpha);
uint16_t blendColorLinear(uint16_t c1, uint16_t c2, float t);
uint16_t blendColorWithBlack(uint16_t color, float alpha);

template <typename T>
void drawAALine(T &disp, float x0, float y0, float x1, float y1, uint16_t color);
template <typename T>
void drawAACircle(T &disp, int cx, int cy, int r, uint16_t color);
template <typename T>
void drawAACornerArc(T &disp, int cx, int cy, int r, uint8_t corner,
                     uint16_t color);
template <typename T>
void drawAARoundRect(T &disp, int x, int y, int w, int h, int r, uint16_t color);
template <typename T>
void fillAARoundRect(T &disp, int x, int y, int w, int h, int r, uint16_t color,
                     uint16_t bg_top = 0x0000, uint16_t bg_bottom = 0x0000);

void drawBatteryIcon(int x, int y, float voltage, uint16_t color);
int getDayOfWeek(int y, int m, int d);
int getEuropeanOffset(int year, int month, int day, int hour);
void drawCalendarIcon(int x, int y, uint16_t color);
void drawClockIcon(int x, int y, uint16_t color);
void drawStopwatchIcon(int x, int y, uint16_t color);
void drawLocationIcon(int x, int y, uint16_t color);
void drawWifiIcon(int x, int y, uint16_t color, bool filled = false);
void drawBadge(const char *text, int offsetX, int offsetY, uint16_t color);

inline int applyAlign(int anchorX, int elementW, int align) {
  if (align == ALIGN_LEFT) return anchorX;
  if (align == ALIGN_RIGHT) return anchorX - elementW;
  return anchorX - (elementW / 2);
}
struct VFontData { const uint8_t *data; size_t len; };
VFontData getVLWData120();
void freeVLWData120();
void resetVLWFontCache();
void initFilesystem();
void drawSplashBase();
void updateSplashProgress(int targetProgress);
void showGoodbyeScreen(bool isSleep);
void showUpdatingScreen();
void updateOTAProgress(int progress, int total);
void drawFpsOverlay();
void drawGpsDebugOverlay();

template <typename T>
inline void drawDebugBox(T &disp, int x, int y, int w, int h,
                         uint16_t color = DEBUG_BOX_COLOR) {
  if (SHOW_ELEMENT_BOUNDS)
    disp.drawRect(x, y, w, h, color);
}

// ----------------------------------------------------------------------------
// Sensors API
// ----------------------------------------------------------------------------
void IRAM_ATTR hallSensorISR();
float getHallSpeed();
void updateFilteredSpeed();
inline float getFilteredSpeed() { return currentCachedSpeed; }

bool initCompass();
void processCompassSensor();
void configureGNSS();
extern bool compassReady;
void processBatterySensor();
void processTemperatureSensor();
void processLightSensor();
void updateGPSOdometer();
void processFuelConsumption();
void updateAverageSpeed();
void updateAccelTimer();
void processFuelSensor();
void sensorTask(void *pvParameters);

// ----------------------------------------------------------------------------
// Web API
// ----------------------------------------------------------------------------
void webServerTask(void *pvParameters);
void checkForFirmwareUpdate(bool manual = false);
void performFirmwareUpdate(const String &firmwareUrl, const String &newVersion);
void startOtaPull(bool manual);
void processOtaMemRelease();
extern volatile bool otaMemReleaseRequested;
extern volatile bool otaMemReleased;
extern const char *index_html;

// ----------------------------------------------------------------------------
// UI API
// ----------------------------------------------------------------------------
void updateBigDisplay(const SensorSnapshot &snap);
void checkNightMode(const SensorSnapshot &snap);

#endif // DASHBOARD_H
