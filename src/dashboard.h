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
#include <Preferences.h>
#include <TinyGPS++.h>
#include <cmath>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "units.h"

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
  float maxSpeed = 0.0f;
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
  // Advance-based bounds: *x1 is always 0, *y1 = -baseline, *w = textWidth,
  // *h = font height. See the definition in gfx.cpp before using x1 (issue #44).
  void getTextBounds(const char *string, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h);
  void getTextBounds(const String &str, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h);
};

// ----------------------------------------------------------------------------
// Pin assignment (ESP32 WROOM compatible)
// ----------------------------------------------------------------------------
extern uint32_t SPI_BUS_SPEED;
// Accepted panel SPI clock band (issue #13). Classic ESP32 SPI is clocked from
// the 80 MHz APB, and the ILI9488 write cycle is rated 15 ns (~66 MHz), so
// 1-80 MHz is the usable range: 0 makes LovyanGFX's divider saturate (a near-
// zero clock) and a negative value wrapping to ~4.29e9 selects SPI_CLK_EQU_SYSCLK
// - either way the panel goes blank. Config sanitizer and bus setup share this
// band so they cannot drift apart.
constexpr uint32_t SPI_SPEED_MIN_HZ = 1000000UL;
constexpr uint32_t SPI_SPEED_MAX_HZ = 80000000UL;
constexpr int HALL_SENSOR_PIN = 33;
#define SPI_DC 27
#define SPI_RST 14
#define CS_DISPLAY 5
#define BL_DISPLAY 12 // backlight LEDC pin (3.x ledc* APIs are pin-based)
// NOTE: Fuel sensor must NOT share the Hall sensor pin. The original code used
// pin 33 for both, which made analog fuel readings unreliable because the Hall
// interrupt also fires on that line. It is now assigned to a dedicated ADC pin.
#define FUEL_TOUCH_PIN 32 // reserved: fuel sensor (capacitive touch removed;
                         // resistive sensor to be wired here)
#define GNSS_UART2_RX_PIN 16 // GNSS UART2 RX (was 25 before pin-swap test)
#define GNSS_UART2_TX_PIN 17 // GNSS UART2 TX (was 26 before pin-swap test)
#define POWER_SENSE_PIN 4
#define BATTERY_SENSE_PIN 35
#define TEMP_SENSE_PIN 36
#define LIGHT_SENSOR_PIN 34
// Physical trip-reset button (issue #17): a momentary push button from GPIO25 to
// GND, using the internal ~45k pull-up - no external parts needed. GPIO25 became
// free when the GNSS UART moved to 16/17, and it is not a strapping pin; GPIO0,
// GPIO2, GPIO5 (already CS_DISPLAY), MTDI/GPIO12 and MTDO/GPIO15 are, so a button
// to GND on those can hold the chip in the wrong boot mode. GPIO26 is left free
// as the spare for a second button.
#define TRIP_RESET_PIN 25
// Contact bounce window before a press or release is believed.
#define TRIP_RESET_DEBOUNCE_MS 50
// How long the on-screen trip-reset confirmation stays up.
#define TRIP_RESET_NOTICE_MS 3000

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
extern bool UNITS_IMPERIAL;
extern char SPLASH_SIGNATURE[48];
extern char REBOOT_SIGNATURE[48];
extern char DASHBOARD_SIGNATURE[96];

extern int TEMP_BAR_MIN;
extern int TEMP_BAR_MAX;
extern int TEMP_WARN_RED;
extern int TEMP_WARN_YEL;

extern int FUEL_WARN_RED;
extern int FUEL_WARN_YEL;

extern char COLOR_TEMP_NORM[8];
extern char COLOR_TEMP_WARN[8];
extern char COLOR_TEMP_CRIT[8];
extern char COLOR_FUEL_NORM[8];
extern char COLOR_FUEL_WARN[8];
extern char COLOR_FUEL_CRIT[8];

extern uint16_t c_temp_norm, c_temp_warn, c_temp_crit;
extern uint16_t c_fuel_norm, c_fuel_warn, c_fuel_crit;
extern uint16_t ghost_color;
extern char GHOST_COLOR_STR[8];

extern int DISPLAY_WIDTH;
extern int DISPLAY_HEIGHT;
constexpr int SHUTDOWN_TIME_MS = 3000;

extern int BIG_CENTER_X;
extern int BIG_CENTER_Y;

extern float WHEEL_CIRCUMFERENCE_MM;
extern float FUEL_FILTER_ALPHA;
extern int TRIP_RESET_HOLD_MS;  // hold time for the physical trip-reset button

extern float BATTERY_SCALE;
extern float BATTERY_OFFSET;
extern float NTC_R_BALANCE;
extern float NTC_R25;
extern float NTC_BETA;
extern float NTC_TEMP_OFFSET;

extern int GPS_BAUD;
extern int MIN_SATELLITES;
extern int OPTIMAL_SATELLITES;
extern float MAX_SPEED_DELTA_KMH;
extern float MIN_SPEED_THRESHOLD;
extern float GPS_START_KMH;
extern int GPS_STOP_SETTLE_MS;
extern float GPS_MIN_DEV_KMH;
extern int SPEED_SOURCE_MODE;
extern int SPEED_SOURCE_HOLD_MS;
extern int HALL_MEDIAN_SAMPLES;
extern int HALL_PERIOD_GUARD;
extern int HALL_PULSE_MIN_US;
extern float ACCEL_START_SPEED;
extern float ACCEL_TARGET_SPEED;
extern float ACCEL_MAX_TIME;
extern char ACCEL_BADGE_LINE1[16];
extern char ACCEL_BADGE_LINE2[16];

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
extern int OFFSET_MAX_SPEED_X;
extern int OFFSET_MAX_SPEED_Y;

extern int ALIGN_BIG_SPEED_NUM;
extern int ALIGN_BIG_SAT;
extern int ALIGN_BIG_TMR;
extern int ALIGN_BIG_BAT;
extern int ALIGN_INST_KML;
extern int ALIGN_AVG_KML;
extern int ALIGN_FUEL_LTRS;
extern int ALIGN_AVG_SPEED;
extern int ALIGN_MAX_SPEED;

extern bool SHOW_ELEMENT_BOUNDS;
// Display-only element visibility toggles. Sensors, calculations and trip
// tracking keep running regardless of these flags; only the rendered widgets
// on the dashboard are affected.
extern bool SHOW_ELEMENT_SPEED;
extern bool SHOW_ELEMENT_SPEED_UNIT;
extern bool SHOW_ELEMENT_SIGNATURE;
extern bool SHOW_ELEMENT_SPEED_SOURCE;
extern bool SHOW_ELEMENT_WIFI;
extern bool SHOW_ELEMENT_TIME;
extern bool SHOW_ELEMENT_DATE;
extern bool SHOW_ELEMENT_ODO;
extern bool SHOW_ELEMENT_SIDEBAR_TEMP;
extern bool SHOW_ELEMENT_SIDEBAR_FUEL;
extern bool SHOW_ELEMENT_SAT;
extern bool SHOW_ELEMENT_TMR;
extern bool SHOW_ELEMENT_BAT;
extern bool SHOW_ELEMENT_INST_KML;
extern bool SHOW_ELEMENT_AVG_KML;
extern bool SHOW_ELEMENT_AVG_SPEED;
extern bool SHOW_ELEMENT_MAX_SPEED;
extern bool SHOW_ELEMENT_FUEL_LTRS;
extern bool SHOW_GHOST_DIGITS;
extern bool ENABLE_POWER_SENSE;
// How long the power-sense line must stay LOW before the unit sleeps (#26).
extern int POWER_SENSE_OFF_MS;
extern bool ENABLE_CIRCLE_TEST;
extern bool ENABLE_DEMO_MODE;
extern bool ADV_MODE;
extern bool ENABLE_ANTIALIASING;
extern float AA_SHARPNESS;
 
extern bool SHOW_FPS_COUNTER_DEFAULT;
extern bool GPS_DEBUG_DEFAULT;
extern int OFFSET_BIG_FPS_X;
extern int OFFSET_BIG_FPS_Y;

extern bool ENABLE_NIGHT_MODE;
extern int NIGHT_MODE_START_HOUR;
extern int NIGHT_MODE_END_HOUR;
extern int NIGHT_BACKLIGHT;
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

// Backlight plumbing (issue #20): the LEDC channel is 8-bit while every caller
// thinks in percent, so the conversion and the write both live in one place.
// backlightDuty() is the only percent->duty maths, applyBacklight() the only
// direct writer.
int backlightDuty(int percent);
void applyBacklight(int percent);

extern int REFRESH_SPEED_MS;
extern int REFRESH_BAT_MS;
extern int REFRESH_INST_MS;
extern int REFRESH_MAX_SPEED_MS;
extern int REFRESH_FUEL_MS;

// Every numeric readout on the big display is drawn into a static cell array of
// this many glyph slots in ui.cpp (spdCellR, tmrCells, batCells, instCells,
// avgCells, avgSpdCells, maxSpdCells, fuelCells, odoCells). The *_DIGITS params
// below size the loops that fill those arrays, so they are validated against
// this constant in processConfig() (issue #6). Kept in the header so the config
// validator and the renderer cannot drift apart.
constexpr int UI_MAX_CELLS = 16;

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
extern int MAX_SPEED_INT_DIGITS;
extern int MAX_SPEED_DEC_DIGITS;
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

extern char WIFI_SSID[64];
extern char WIFI_PASSWORD[64];
extern char WIFI_SSID_1[64];
extern char WIFI_PASSWORD_1[64];
extern char WIFI_SSID_2[64];
extern char WIFI_PASSWORD_2[64];
extern char WIFI_SSID_3[64];
extern char WIFI_PASSWORD_3[64];
extern char WIFI_SSID_4[64];
extern char WIFI_PASSWORD_4[64];
extern char AP_PASSWORD[64];
// Per-network join window. The search policy itself is deliberately not a
// parameter: the STA search keeps cycling through the saved networks forever
// (a dashboard drives in and out of range all day, so an expired window only
// guarantees it stays offline until the next reboot), and TX power is fixed at
// the module maximum - see webServerTask() in web.cpp.
extern int WIFI_ATTEMPT_SECONDS;

// WiFi STA search diagnostics, written by the web task's search state machine
// and read by the diagnostic heartbeat: phase (0=init 1=backoff 2=try
// 3=attempt 4=gap 5=up 6=give-up), last STA disconnect reason, and the index of
// the network currently being tried (0 = primary).
extern volatile uint8_t staDbgPhase;
extern volatile uint8_t staDbgReason;
extern volatile int staDbgNetIdx;

extern bool NTP_ENABLED;
extern char NTP_SERVER[64];
extern int TZ_OFFSET_HOURS;
extern bool TZ_DST_ENABLED;
extern int TZ_DST_RULE;  // which rule TZ_DST_ENABLED applies: see TZ_DST_RULE_*

extern bool OTA_PULL_ENABLED;
extern char OTA_PULL_URL[192];
// Build identity (compile time) and the optional user override. See the
// comment at their definition in config.cpp: the manifest can never change
// what the device claims to be.
extern const char FW_VERSION[];
extern char VERSION_OVERRIDE[32];
const char *effectiveVersion();
int versionCmp(const char *a, const char *b);

// ----------------------------------------------------------------------------
// Fuel sender calibration table (issue #18 - marine resistive sender)
// ----------------------------------------------------------------------------
constexpr int MAX_TOUCH_POINTS = 20;
extern int FUEL_TOUCH_POINTS;
// One sender resistance per tank slot: index 0 = empty, index
// FUEL_TOUCH_POINTS-1 = full. Ohms, not ADC counts, so the same table means the
// same thing for a 10..180 ohm SAE sender and a 240..33 ohm European one.
extern float fuelCalOhms[MAX_TOUCH_POINTS];

// Resistive sender input (3V3 - FUEL_EXC_RES_OHM - GPIO32 - sender - GND).
extern bool FUEL_INPUT_ENABLED;   // false until the sender is actually wired
extern int FUEL_EXC_RES_OHM;      // excitation resistor
extern float FUEL_ADC_VREF;       // ADC reference / actual rail voltage
extern float FUEL_OHM_EMPTY;      // sender resistance when the tank is empty
extern float FUEL_OHM_FULL;       // sender resistance when the tank is full
extern int FUEL_OVERSAMPLE;       // ADC conversions averaged per sample

// Live sender state, read by the Web UI through /api/fuel.
#define FUEL_INPUT_OFF 0    // input disabled: nothing wired, gauge shows 0
#define FUEL_INPUT_OK 1     // reading inside the expected band
#define FUEL_INPUT_OPEN 2   // pin at the rail: unplugged sender / broken wire
#define FUEL_INPUT_SHORT 3  // pin at ground while the range says otherwise
extern float fuelMeasuredOhms;
extern uint8_t fuelInputState;

// ----------------------------------------------------------------------------
// Logging ring buffer
// ----------------------------------------------------------------------------
#define LOG_BUF_SIZE 4096
extern char logBuf[LOG_BUF_SIZE];
extern volatile int logHead;
extern volatile int logTail;
extern volatile unsigned long logSequence;
extern portMUX_TYPE logMux;
void logPrintf(const char *fmt, ...);

// ----------------------------------------------------------------------------
// Shared state
// ----------------------------------------------------------------------------
extern Preferences preferences;
// Guards the shared `preferences` object (single internal NVS handle):
// gpsTask writes the odometer, webServerTask handles POST /api/odo,
// loopTask saves on sleep/reboot. Without this, concurrent begin/put/end
// races can silently lose an NVS write.
extern SemaphoreHandle_t prefsMux;

// ----------------------------------------------------------------------------
// NVS access serialisation (issue #32)
//
// ESP-IDF's NVS layer has internal locking, so a torn write is unlikely. What
// is not protected is the *session*: task A's end() can close the handle task B
// is still using on the same namespace, which surfaces as an ESP_ERR_NVS_* from
// putInt()/putString() - and here as a setting that quietly reverted at the next
// boot. Some call sites wrapped their open in prefsMux, most did not, so the
// protection depended on which file the code lived in. NvsSession makes the
// locked pattern the default: it holds prefsMux for the whole begin()/end()
// pair (skipped while prefsMux is still NULL, i.e. during early boot) and
// closes on scope exit, so an early return cannot leak the handle.
//
// Nothing inside a locked region may call anything that opens NVS itself -
// prefsMux is a plain mutex, not recursive.
// ----------------------------------------------------------------------------
class NvsSession {
 public:
  // open=false takes the lock but leaves the namespace closed, for the call
  // sites that only open it on some code paths.
  NvsSession(const char *ns, bool readOnly, bool open = true) {
    if (prefsMux) {
      _locked = xSemaphoreTake(prefsMux, pdMS_TO_TICKS(3000)) == pdTRUE;
      if (!_locked)
        logPrintf("NVS: prefsMux wait timed out for '%s' - proceeding unlocked\n", ns);
    }
    if (open) begin(ns, readOnly);
  }
  ~NvsSession() {
    if (_opened) nvs.end();
    if (_locked) xSemaphoreGive(prefsMux);
  }
  bool begin(const char *ns, bool readOnly) {
    _opened = nvs.begin(ns, readOnly);
    if (!_opened)
      logPrintf("NVS: cannot open namespace '%s' - this access is skipped\n", ns);
    return _opened;
  }
  bool opened() const { return _opened; }
  Preferences nvs;

 private:
  bool _opened = false;
  bool _locked = false;
};

// Count of NVS writes that failed during the most recent processConfig(2) save
// (and the boot seed). Reported to the Web UI so a save that never reached flash
// cannot be shown as "saved successfully".
extern uint16_t cfgNvsWriteErrors;

// Which keys failed, comma-separated (fixed buffer, no heap - see the RAM rules
// in AGENTS.md). A count alone told the user something was lost but not what;
// the names are what separate "the partition is full" from "this one key is
// broken". Truncated with ",..." when they do not fit.
extern char cfgNvsFailedKeys[72];
void cfgNoteFailedKey(const char *key);  // records only while a config save is open

// Last NVS partition entry budget read by logNvsStats() (0 = never read). The
// save response reports `available` so a full partition is visible from the
// browser; on a fragmented-but-not-full partition it still looks roomy, which is
// itself the answer.
extern size_t nvsStatsUsed, nvsStatsAvailable, nvsStatsTotal, nvsStatsBytes;

// Refresh those numbers without printing them, at most once every maxAgeMs.
// The health endpoint serves them to the Web UI; walking every NVS page on a
// one-second poll would be wasted work, so this is throttled.
void refreshNvsStats(uint32_t maxAgeMs);

// Logs the NVS partition's entry budget (used / available / total). Free space
// is the first thing to rule out when writes start failing.
void logNvsStats();

// NVS write results have to be checked: the RAM copy changes, NVS does not, and
// the setting comes back at the next reboot with no trace of why (issue #32).
//
// The core does NOT return an esp_err_t here. In arduino-esp32 3.3.12
// (libraries/Preferences/src/Preferences.cpp) every put*() returns size_t: 0 on
// failure (nvs_set_* or nvs_commit failed) and 1 for the numeric types,
// strlen(value) for a string, on success. The issue #32 wrapper took an
// esp_err_t and compared it against ESP_OK, which inverted every result: a real
// write failure (0) was reported as success - silent config loss - while every
// successful write logged as a failure whose "error name" was really the length
// of the value (a 12-character SSID printed "failed (0xc)"). clear()/remove()
// do return bool, so they get their own overload; a bare esp_err_t argument is
// now ambiguous at compile time, which is exactly the mistake being prevented.
// The esp_err itself is still visible: the core's own log_e("nvs_set_str fail:
// ...") prints it above these lines at the default CORE_DEBUG_LEVEL.
inline bool nvsWriteFailed(const char *key, size_t written) {
  if (written) return false;
  logPrintf("NVS: write of %s FAILED - value is live now, a reboot restores the old one\n", key);
  cfgNoteFailedKey(key);
  return true;
}
inline bool nvsWriteFailed(const char *key, bool ok) {
  if (ok) return false;
  logPrintf("NVS: write of %s FAILED - value is live now, a reboot restores the old one\n", key);
  cfgNoteFailedKey(key);
  return true;
}

// A string write is the one case the core's count cannot answer on its own:
// putString() returns strlen(value), so a successful write of an empty value
// (a cleared SSID, an unused URL) also reads 0. Empty values are therefore not
// called failures here - the core's own log_e still prints a real one.
inline bool nvsStringWriteFailed(const char *key, const char *value, size_t written) {
  if (written || (value && value[0] == 0)) return false;
  return nvsWriteFailed(key, written);
}



// Logs what the WiFi credentials NVS actually handed back at boot: SSIDs and
// whether each password slot is filled - never the passwords themselves.
void logStoredWifiProfiles();

// Latest GPS state published by gpsTask after each NMEA commit. TinyGPS++
// location accessors are single-consumer (lat()/lng() clear the one-shot
// "updated" flag that isUpdated() reports) and their doubles must not be
// read cross-task: the weather fetch used to steal the odometer's pending
// fix, and unsynchronized 64-bit reads can tear mid-write. Every consumer
// outside gpsTask must use gpsFixSnapshot() (fix only) or gpsSnapshotCopy()
// (fix + satellites/speed/HDOP/altitude) instead of touching gps.* (issue #9).
struct GpsFixSnapshot {
  double lat = 0.0;
  double lon = 0.0;
  unsigned long seq = 0; // incremented once per published fix
  bool valid = false;    // location validity at publish time
  // Everything else gpsTask parses, published on the same tick (issue #9).
  // TinyGPS++ value()/kmph()/meters()/hdop() are NOT const - each call clears
  // the one-shot "updated" flag - so no other core may touch those objects:
  // a core-1 reader used to consume the flag gpsTask still needed and race the
  // commit (satellite flicker, speed-source flapping, torn 64-bit doubles).
  int satellites = 0;
  float speedKmh = 0.0f;
  bool speedValid = false;
  float hdop = 0.0f;      // decimal HDOP, as TinyGPS++ returns it
  bool hdopValid = false;
  float altitudeM = 0.0f; // metres MSL
  bool altValid = false;
  bool published = false; // true after the first gpsTask publish
};
extern GpsFixSnapshot g_gpsFix;
extern SemaphoreHandle_t gpsFixMux;

// Thread-safe copy of the latest published fix. Returns false (leaving the
// arguments untouched) while no fix has been published yet.
bool gpsFixSnapshot(double &lat, double &lon);
// Thread-safe copy of the whole GPS snapshot (fix + satellites/speed/hdop/
// altitude). The only supported way to read GPS state outside gpsTask.
bool gpsSnapshotCopy(GpsFixSnapshot &out);
extern WebServer server;

extern bool forceFullRedraw;
extern volatile bool pendingSleep;
// Debounced power-sense read, owned by sensors.cpp (issue #26).
bool powerSenseOffConfirmed();
extern volatile bool pendingReboot;
extern volatile bool otaUpdateInProgress;
extern volatile bool otaUpdateSuccess;
extern volatile bool pendingOtaScreen;
extern volatile int otaProgressFillW;
extern volatile int otaProgressTarget;

extern bool pendingInvertDisplay;
// Config-save handoff between the web task and the display loop (issue #10)
extern volatile bool pendingApplyBusConfig;
extern volatile bool pendingCpuReeval;
extern volatile bool configSaveInProgress;
extern volatile unsigned long configSaveStartMs;
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
extern float fuelLiters;
extern int fuelPercentage;
extern float batteryVoltage;
extern float engineTemperature;
// Odometer accessors (issue #14). The odometer doubles live in sensors.cpp and
// are 64-bit values shared across cores, so cross-task code must go through
// these - a raw read can mix the two 32-bit halves of different values.
double odoGet();
void odoSet(double km);
double odoAdd(double dKm);
double odoLastSaved();
void odoMarkSaved(double km);
void setOdometerKm(double km);
extern double lastLat;
extern double lastLon;
extern bool hasLastPos;
extern int splashCurrentProgress;
extern float currentCachedSpeed;
extern unsigned long g_startupTime;

extern portMUX_TYPE hallMux;
extern volatile unsigned long lastHallPulseTimeUs;
extern volatile unsigned long hallPulseIntervalUs;
extern volatile unsigned long hallStableIntervalUs;
extern volatile unsigned long hallPulseCount;
extern volatile bool hallRolling;
extern volatile bool hallSpeedConfirmed;
extern volatile int heldSpeedSourceMode;

extern double tripDistanceKm;
extern float tripStartFuelLiters;
extern float tripFuelConsumedLiters;
extern unsigned long movingTimeMs;
// Set by resetTripStats(); the display shows a short confirmation while it is
// younger than TRIP_RESET_NOTICE_MS.
extern volatile unsigned long tripResetNoticeMs;
// Set by the Web UI reset endpoint, consumed by the sensor task.
extern volatile bool pendingTripReset;
// Task handles for the /api/perf stack headroom report (issue #27).
extern TaskHandle_t sensorTaskHandle;
extern TaskHandle_t gpsTaskHandle;
extern TaskHandle_t webTaskHandle;
extern float instantKml;
extern float averageKml;
extern float averageSpeed;
extern float maxSpeed;

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
void seedNVSWithFactoryDefaults();
void recalculateDerivedParams();
uint16_t hexToRGB565(const char *hex);

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
// Battery glyph geometry (issue #48), shared by the drawing code in gfx.cpp and
// the battery readout layout in ui.cpp. (x, y) for drawBatteryIcon is the
// top-left of the whole glyph including the terminal; batteryIconTop() converts
// a visual centre into that top-left, so the *body* - the glyph the eye reads -
// sits on the requested axis and the terminal is an overhang above it.
constexpr int BATTERY_ICON_W = 10;
constexpr int BATTERY_ICON_H = 16;
constexpr int BATTERY_TERMINAL_W = 4;
constexpr int BATTERY_TERMINAL_H = 2;
int batteryIconTop(int centerY);
int getDayOfWeek(int y, int m, int d);
// Daylight-saving rules (issue #22): UTC date + the zone's standard offset in,
// extra hours out (0 = standard, 1 = daylight). See src/gfx.cpp.
#define TZ_DST_RULE_NONE 0
#define TZ_DST_RULE_EU 1
#define TZ_DST_RULE_US 2
int getEuropeanDst(int year, int month, int day, int hourUtc, int baseOff);
int getUSDst(int year, int month, int day, int hourUtc, int baseOff);
int getDstOffset(int year, int month, int day, int hourUtc, int baseOff);
void drawCalendarIcon(int x, int y, uint16_t color);
void drawClockIcon(int x, int y, uint16_t color);
void drawStopwatchIcon(int x, int y, uint16_t color);
void drawSatelliteIcon(int x, int y, uint16_t color);
void drawLocationIcon(int x, int y, uint16_t color);
void drawWifiIcon(int x, int y, uint16_t color, bool filled = false);
void drawBadge(const char *text, int offsetX, int offsetY, uint16_t color, int fixedW = 0);

inline int applyAlign(int anchorX, int elementW, int align) {
  if (align == ALIGN_LEFT) return anchorX;
  if (align == ALIGN_RIGHT) return anchorX - elementW;
  return anchorX - (elementW / 2);
}
struct VFontData { const uint8_t *data; size_t len; };
VFontData getVLWData120();
bool isVLW120FontReady();
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
void hallSensorISR();
float getHallSpeed();
void updateFilteredSpeed();
int computeSpeedSourceMode(float hallSpeed, float gpsSpeed, int sats,
                           bool isGpsValid);
void updateSpeedSourceMode(const GpsFixSnapshot &fix);
inline float getFilteredSpeed() { return currentCachedSpeed; }

void configureGNSS();
void processBatterySensor();
void processTemperatureSensor();
void processLightSensor();
void updateGPSOdometer();
void processFuelConsumption();
void updateAverageSpeed();
// The single trip-reset entry point (button, Web UI, factory reset) and the
// debounced button scan that runs in the sensor task.
void resetTripStats(const char *reason);
void processTripResetButton();
void updateAccelTimer();
void initFuelSensor();
void processFuelSensor();
void gpsTask(void *pvParameters);
void sensorTask(void *pvParameters);

// ----------------------------------------------------------------------------
// Web API
// ----------------------------------------------------------------------------
void webServerTask(void *pvParameters);
void checkForFirmwareUpdate(bool manual = false, bool skipThrottle = false);
void performFirmwareUpdate(const char *firmwareUrl, const char *newVersion);
void startOtaPull(bool manual, bool skipThrottle = false);
void processOtaMemRelease();
extern volatile bool otaMemReleaseRequested;
extern volatile bool otaMemReleased;
void processMemSaverRelease();
extern volatile bool memSaverRequested;
extern volatile bool memSaverActive;
void ensureSpeedSprite();
bool speedSpriteValid();
bool isSpeedFallback();
extern volatile unsigned long webLoopCount;
void factoryResetConfig();

// ----------------------------------------------------------------------------
// Weather Widget API & Data
// ----------------------------------------------------------------------------
struct WeatherData {
  float temperature = 0.0f;
  int humidity = 0;
  float windSpeed = 0.0f;
  float windDirection = 0.0f;
  int weatherCode = 0;
  int cloudCover = 0;
  // Fixed-size text buffers instead of Strings (issue #8): the weather fetch
  // task writes these while the display task reads them without the mutex, and
  // String::operator= frees the old buffer - a refresh landing between c_str()
  // and its use was a use-after-free. These arrays never reallocate, and every
  // writer leaves a NUL inside the bounds, so a sample taken mid-copy can at
  // worst show one garbled label; it can never dangle, leak or overrun.
  char sunsetTime[8] = ""; // "HH:MM"
  char sunriseTime[8] = "";
  char cityName[48] = "";  // same width as WEATHER_CITY
  bool valid = false;
  unsigned long lastUpdated = 0;
};

extern WeatherData g_weatherData;
extern bool SHOW_ELEMENT_WEATHER;
extern int OFFSET_WEATHER_X;
extern int OFFSET_WEATHER_Y;
extern char WEATHER_CITY[48];
extern float WEATHER_LAT;
extern float WEATHER_LON;
extern int WEATHER_REFRESH_MIN;
extern char WEATHER_LOCALE[16];

bool startWeatherFetch();
void updateWeather();
// Bounded, UTF-8-safe text copy for the weather strings (defined in web.cpp):
// truncation never splits a multi-byte character, so the display never sees a
// dangling continuation byte where a glyph should be.
void copyFixed(char *dst, size_t n, const char *src);

// ----------------------------------------------------------------------------
// UI API
// ----------------------------------------------------------------------------
void updateBigDisplay(const SensorSnapshot &snap);
void checkNightMode(const SensorSnapshot &snap);

// ----------------------------------------------------------------------------
// Sensor API
// ----------------------------------------------------------------------------
void simulateRawSensors();
bool systemTimeToLocal(int &hour, int &minute, int &day, int &month, int &year);
extern volatile unsigned long g_sensorLastTickMs;

#endif // DASHBOARD_H

