#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>
#include <SPI.h>
#include <algorithm>
#include <utility>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <HardwareSerial.h>
#include <Preferences.h>
#include <TinyGPS++.h>
#include <cmath>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Conthrax_SemiBold12pt7b.h"
#include "Conthrax_SemiBold4pt7b.h"
#include "Conthrax_SemiBold7pt7b.h"
#include "DS_DIGIT15pt7b.h"
#include "DS_DIGIT50pt7b.h"

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
  int localHour = 0;
  int minute = 0;
  int day = 0;
  int month = 0;
  int year = 0;
  bool timeValid = false;
  bool dateValid = false;
  bool isGpsSpeedValid = false;
};

class LGFX_ST7789_4 : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  const GFXfont *_currentGfxFont = nullptr;

public:
  LGFX_ST7789_4();

  // Re-applies the SPI bus config (e.g. SPI_BUS_SPEED loaded from NVS by
  // processConfig) after the global constructor has already run. Call this
  // between processConfig() and display.init().
  void applyBusConfig();

  void setFont(const GFXfont *f);
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
// NOTE: Fuel sensor must NOT share the Hall sensor pin. The original code used
// pin 33 for both, which made analog fuel readings unreliable because the Hall
// interrupt also fires on that line. It is now assigned to a dedicated ADC pin.
#define FUEL_TOUCH_PIN 32
#define RXD2 25
#define TXD2 26
#define POWER_SENSE_PIN 34
#define BATTERY_SENSE_PIN 35
#define TEMP_SENSE_PIN 36

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

extern int DISPLAY_WIDTH;
extern int DISPLAY_HEIGHT;
extern int BOOT_TIME_MS;
extern int SHUTDOWN_TIME_MS;

extern int BIG_CENTER_X;
extern int BIG_CENTER_Y;

extern double INITIAL_ODOMETER_KM;
extern float WHEEL_CIRCUMFERENCE_MM;
extern float FUEL_FILTER_ALPHA;

extern float NTC_R_BALANCE;
extern float NTC_R_ROOM;
extern float NTC_BETA;

extern int MIN_SATELLITES;
extern int OPTIMAL_SATELLITES;
extern float MAX_SPEED_DELTA_KMH;
extern float MIN_SPEED_THRESHOLD;
extern float ACCEL_START_SPEED;
extern float ACCEL_TARGET_SPEED;
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

extern int OFFSET_HALL_ICON_X;
extern int OFFSET_HALL_ICON_Y;
extern int OFFSET_GPS_ICON_X;
extern int OFFSET_GPS_ICON_Y;
extern int OFFSET_WHEEL_ICON_X;
extern int OFFSET_WHEEL_ICON_Y;
extern int OFFSET_WIFI_ICON_X;
extern int OFFSET_WIFI_ICON_Y;
extern int OFFSET_INST_KML_X;
extern int OFFSET_INST_KML_Y;
extern int OFFSET_AVG_KML_X;
extern int OFFSET_AVG_KML_Y;

extern bool SHOW_ELEMENT_BOUNDS;
extern bool ENABLE_POWER_SENSE;
extern bool ENABLE_CIRCLE_TEST;
extern bool ENABLE_DEMO_MODE;
extern bool ENABLE_SLEEP_AFTER_REBOOT;
 
extern bool SHOW_FPS_COUNTER_DEFAULT;
extern int OFFSET_BIG_FPS_X;
extern int OFFSET_BIG_FPS_Y;

extern int NIGHT_MODE_START_HOUR;
extern int NIGHT_MODE_END_HOUR;
extern bool DISPLAY_INVERT_COLORS;

extern int TARGET_FPS;
extern int BACKLIGHT_BRIGHTNESS;



extern bool ENABLE_DYNAMIC_CPU;
extern int MANUAL_CPU_FREQ;

extern unsigned long DISPLAY_REFRESH_MS;
extern unsigned long TELEMETRY_REFRESH_MS;
extern float WHEEL_SPEED_FACTOR;
extern double WHEEL_DIST_PER_PULSE_KM;
extern float NTC_INV_ROOM_KELVIN;
extern float ADC_VOLTS_FACTOR;
extern bool showFpsCounter;

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

extern LGFX_ST7789_4 display;
extern TinyGPSPlus gps;
extern HardwareSerial gpsSerial;

extern uint16_t DEBUG_BOX_COLOR;
extern float currentMeasuredFps;
constexpr uint8_t FPS_AVG_SAMPLES = 5;
extern float fpsHistory[FPS_AVG_SAMPLES];
extern uint8_t fpsHistoryIndex;
extern uint8_t fpsHistoryCount;
extern float currentAverageFps;

extern unsigned long lastDisplayUpdate;
extern float filteredReading;
extern float fuelLiters;
extern int fuelPercentage;
extern float batteryVoltage;
extern float engineTemperature;
extern int currentTimezoneOffset;
extern double totalDistanceKm;
extern double lastSavedOdo;
extern double lastLat;
extern double lastLon;
extern bool hasLastPos;
extern int splashCurrentProgress;
extern float currentCachedSpeed;

extern portMUX_TYPE hallMux;
extern volatile unsigned long lastHallPulseTimeUs;
extern volatile unsigned long hallPulseIntervalUs;
extern volatile unsigned long hallPulseCount;

extern double tripDistanceKm;
extern float tripStartFuelLiters;
extern float tripFuelConsumedLiters;
extern float instantKml;
extern float averageKml;

extern bool isSelfTestActive;
extern bool overrideTimeDateStr;
extern int overrideSpeed;
extern double overrideOdo;
extern int overrideSat;
extern float overrideBat;
extern float overrideTimer;

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
void drawLocationIcon(int x, int y, uint16_t color);
void drawWifiIcon(int x, int y, uint16_t color);
void drawWheelIcon(int x, int y, uint16_t color);
void drawBadge(const char *text, int offsetX, int offsetY, uint16_t color);
void drawSplashBase();
void updateSplashProgress(int targetProgress);
void runDisplaySelfTest();
void showGoodbyeScreen(bool isSleep);
void drawFpsOverlay();

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

void processBatterySensor();
void processTemperatureSensor();
void updateGPSOdometer();
void processFuelConsumption();
void updateAccelTimer();
void processFuelSensor();
void sensorTask(void *pvParameters);

// ----------------------------------------------------------------------------
// Web API
// ----------------------------------------------------------------------------
void webServerTask(void *pvParameters);
extern const char *index_html;

// ----------------------------------------------------------------------------
// UI API
// ----------------------------------------------------------------------------
void updateBigDisplay(const SensorSnapshot &snap);
void checkNightMode(const SensorSnapshot &snap);

#endif // DASHBOARD_H
