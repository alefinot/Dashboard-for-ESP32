#include "dashboard.h"
#include <nvs.h>  // nvs_get_stats: the partition's entry budget, for failed writes

// ----------------------------------------------------------------------------
// Configuration variables (defined here, declared extern elsewhere)
// ----------------------------------------------------------------------------
int DISPLAY_ROTATION = 1;
bool UNITS_IMPERIAL = false;

char SPLASH_SIGNATURE[48] = "by @ale.finot";
char REBOOT_SIGNATURE[48] = "Dashboard++ by @ale.finot";
char DASHBOARD_SIGNATURE[96] = "<<<<<<    Dashboard++ by @ale.finot    >>>>>>";

int TEMP_BAR_MIN = 10;
int TEMP_BAR_MAX = 110;
int TEMP_WARN_RED = 90;
int TEMP_WARN_YEL = 45;
int FUEL_WARN_RED = 20;
int FUEL_WARN_YEL = 45;

char COLOR_TEMP_NORM[8] = "#00ff00";
char COLOR_TEMP_WARN[8] = "#ffff00";
char COLOR_TEMP_CRIT[8] = "#ff0000";

char COLOR_FUEL_NORM[8] = "#00ff00";
char COLOR_FUEL_WARN[8] = "#ffff00";
char COLOR_FUEL_CRIT[8] = "#ff0000";

uint16_t c_temp_norm, c_temp_warn, c_temp_crit;
uint16_t c_fuel_norm, c_fuel_warn, c_fuel_crit;
char GHOST_COLOR_STR[8] = "#474747";
uint16_t ghost_color;

uint32_t SPI_BUS_SPEED = 60000000;
int DISPLAY_WIDTH = 480;
int DISPLAY_HEIGHT = 320;

int BIG_CENTER_X = 240;
int BIG_CENTER_Y = 160;

float WHEEL_CIRCUMFERENCE_MM = 1650.0f;
float FUEL_FILTER_ALPHA = 0.08f;
// How long the physical trip-reset button must be held, in ms (issue #17).
int TRIP_RESET_HOLD_MS = 1500;

float NTC_R_BALANCE = 10000.0f;
float NTC_R25 = 10000.0f;
float NTC_BETA = 3950.0f;
float NTC_TEMP_OFFSET = 0.0f;
float BATTERY_SCALE = 5.7f;
float BATTERY_OFFSET = 0.2f;

int GPS_BAUD = 115200;
int MIN_SATELLITES = 8;
int OPTIMAL_SATELLITES = 12;
float MAX_SPEED_DELTA_KMH = 5.0f;
float MIN_SPEED_THRESHOLD = 1.0f;
float GPS_START_KMH = 3.0f;
int GPS_STOP_SETTLE_MS = 1500;
float GPS_MIN_DEV_KMH = 1.0f;
// 0 = Hall only, 1 = GPS only, 2 = Sensor fusion (default). Replaces GPS_ONLY_MODE.
int SPEED_SOURCE_MODE = 2;
int SPEED_SOURCE_HOLD_MS = 500;
int HALL_MEDIAN_SAMPLES = 3;
// 8x keeps single EMI blips out (a 30 ms blip is ~10x the 297 ms period at 20 km/h);
// real driving shifts the period <2%/rotation, so 8x never rejects a real pulse.
int HALL_PERIOD_GUARD = 8;
// Pulse-width qualification window (Layer 1): after a FALLING edge the ISR
// waits this long and re-samples the pin. A genuine magnet pass is LOW for
// hundreds of microseconds (one revolution at 200 km/h is 29.7 ms), while
// spark-plug EMI rings and collapses back HIGH in <10 us, so 150 us rejects
// ignition noise with an order of magnitude of physical margin to spare.
int HALL_PULSE_MIN_US = 150;
float ACCEL_START_SPEED = 1.0f;
float ACCEL_TARGET_SPEED = 50.0f;
float ACCEL_MAX_TIME = 9.99f;
char ACCEL_BADGE_LINE1[16] = "0-50";
char ACCEL_BADGE_LINE2[16] = "km/h";

int OFFSET_BIG_TIME_X = 107;
int OFFSET_BIG_TIME_Y = -91;
int OFFSET_BIG_DATE_X = -131;
int OFFSET_BIG_DATE_Y = -91;
int OFFSET_BIG_SIGNATURE_X = 0;
int OFFSET_BIG_SIGNATURE_Y = -75;
int OFFSET_BIG_SPEED_NUM_X = 0;
int OFFSET_BIG_SPEED_NUM_Y = -3;
int OFFSET_BIG_SPEED_UNIT_X = 106;
int OFFSET_BIG_SPEED_UNIT_Y = 56;
int OFFSET_BIG_ODO_X = 22;
int OFFSET_BIG_ODO_Y = 126;
int OFFSET_BIG_SAT_X = 179;
int OFFSET_BIG_SAT_Y = -114;
int OFFSET_BIG_TMR_X = -53;
int OFFSET_BIG_TMR_Y = -46;
int OFFSET_BIG_BAT_X = -112;
int OFFSET_BIG_BAT_Y = 123;

int SIDEBAR_LEFT_X = 10;
int SIDEBAR_LEFT_Y = 95;
int SIDEBAR_RIGHT_X = 462;
int SIDEBAR_RIGHT_Y = 95;
int SIDEBAR_BAR_WIDTH = 8;
int SIDEBAR_BAR_HEIGHT = 190;

int OFFSET_HALL_ICON_X = 0;
int OFFSET_HALL_ICON_Y = -100;
int OFFSET_WIFI_ICON_X = 204;
int OFFSET_WIFI_ICON_Y = -108;
int OFFSET_INST_KML_X = 60;
int OFFSET_INST_KML_Y = -25;
int OFFSET_AVG_KML_X = 160;
int OFFSET_AVG_KML_Y = -25;
int OFFSET_AVG_SPEED_X = -163;
int OFFSET_AVG_SPEED_Y = -25;
int OFFSET_MAX_SPEED_X = -60;
int OFFSET_MAX_SPEED_Y = -25;
int OFFSET_FUEL_LTRS_X = 132;
int OFFSET_FUEL_LTRS_Y = 123;

int ALIGN_BIG_SPEED_NUM = ALIGN_CENTER;
int ALIGN_BIG_SAT = ALIGN_CENTER;
int ALIGN_BIG_TMR = ALIGN_CENTER;
int ALIGN_BIG_BAT = ALIGN_CENTER;
int ALIGN_INST_KML = ALIGN_CENTER;
int ALIGN_AVG_KML = ALIGN_CENTER;
int ALIGN_AVG_SPEED = ALIGN_CENTER;
int ALIGN_MAX_SPEED = ALIGN_CENTER;
int ALIGN_FUEL_LTRS = ALIGN_CENTER;

bool SHOW_ELEMENT_BOUNDS = false;
bool SHOW_ELEMENT_SPEED = true;
bool SHOW_ELEMENT_SPEED_UNIT = true;
bool SHOW_ELEMENT_SIGNATURE = true;
bool SHOW_ELEMENT_SPEED_SOURCE = true;
bool SHOW_ELEMENT_WIFI = true;
bool SHOW_ELEMENT_TIME = true;
bool SHOW_ELEMENT_DATE = true;
bool SHOW_ELEMENT_ODO = true;
bool SHOW_ELEMENT_SIDEBAR_TEMP = true;
bool SHOW_ELEMENT_SIDEBAR_FUEL = true;
bool SHOW_ELEMENT_SAT = true;
bool SHOW_ELEMENT_TMR = true;
bool SHOW_ELEMENT_BAT = true;
bool SHOW_ELEMENT_INST_KML = true;
bool SHOW_ELEMENT_AVG_KML = true;
bool SHOW_ELEMENT_AVG_SPEED = true;
bool SHOW_ELEMENT_MAX_SPEED = true;
bool SHOW_ELEMENT_FUEL_LTRS = true;
bool SHOW_GHOST_DIGITS = true;
bool SHOW_ELEMENT_WEATHER = true;
int OFFSET_WEATHER_X = 0;
int OFFSET_WEATHER_Y = 146;
char WEATHER_CITY[48] = "";
float WEATHER_LAT = 0.0f;
float WEATHER_LON = 0.0f;
int WEATHER_REFRESH_MIN = 1;
char WEATHER_LOCALE[16] = "it";
WeatherData g_weatherData;
bool ENABLE_POWER_SENSE = false;
int POWER_SENSE_OFF_MS = 10000;
bool ENABLE_CIRCLE_TEST = false;
bool ENABLE_DEMO_MODE = false;
bool ADV_MODE = false;
bool ENABLE_ANTIALIASING = true;
float AA_SHARPNESS = 0.2f;
 
bool SHOW_FPS_COUNTER_DEFAULT = false;
bool GPS_DEBUG_DEFAULT = false;
int OFFSET_BIG_FPS_X = -9;
int OFFSET_BIG_FPS_Y = -7;

int NIGHT_MODE_START_HOUR = 23;
int NIGHT_MODE_END_HOUR = 0;
int NIGHT_BACKLIGHT = 29;
bool DISPLAY_INVERT_COLORS = false;

int REFRESH_SPEED_MS = 250;
int REFRESH_BAT_MS = 2500;
int REFRESH_INST_MS = 500;
int REFRESH_MAX_SPEED_MS = 500;
int REFRESH_FUEL_MS = 1000;

int SPEED_DIGITS = 2;
int SAT_DIGITS = 2;
int TMR_INT_DIGITS = 1;
int TMR_DEC_DIGITS = 2;
int BAT_INT_DIGITS = 2;
int BAT_DEC_DIGITS = 1;
int INST_INT_DIGITS = 2;
int INST_DEC_DIGITS = 1;
int AVG_INT_DIGITS = 2;
int AVG_DEC_DIGITS = 1;
int AVG_SPEED_INT_DIGITS = 2;
int AVG_SPEED_DEC_DIGITS = 0;
int MAX_SPEED_INT_DIGITS = 3;
int MAX_SPEED_DEC_DIGITS = 0;
int FUEL_INT_DIGITS = 1;
int FUEL_DEC_DIGITS = 1;
int ODO_INT_DIGITS = 5;
int ODO_DEC_DIGITS = 1;

int TARGET_FPS = 60;
int BACKLIGHT_BRIGHTNESS = 100;
bool ENABLE_AUTO_BRIGHTNESS = true;
int LIGHT_SENSOR_DARK_VAL = 432;
int LIGHT_SENSOR_BRIGHT_VAL = 2851;
int AUTO_BRIGHT_DARK = 13;
int AUTO_BRIGHT_LIGHT = 100;
int AUTO_BRIGHT_FADE_MS = 4000;
int ambientLightValue = 0;
float filteredAmbientValue = 0.0f;
int FADE_DURATION_MS = 700;

bool ENABLE_NIGHT_MODE = false;
bool ENABLE_DYNAMIC_CPU = false;
int MANUAL_CPU_FREQ = 240;
bool ENABLE_CPU_THROTTLE = false;
int CPU_THROTTLE_TEMP_WARN = 50;
int CPU_THROTTLE_TEMP_CRIT = 60;

unsigned long DISPLAY_REFRESH_MS;
unsigned long TELEMETRY_REFRESH_MS = 500;
float WHEEL_SPEED_FACTOR;
double WHEEL_DIST_PER_PULSE_KM;
float NTC_INV_ROOM_KELVIN;
float ADC_VOLTS_FACTOR;
bool showFpsCounter = true;
bool showGpsDebug = false;

char WIFI_SSID[64] = "";
char WIFI_PASSWORD[64] = "";
char WIFI_SSID_1[64] = "";
char WIFI_PASSWORD_1[64] = "";
char WIFI_SSID_2[64] = "";
char WIFI_PASSWORD_2[64] = "";
char WIFI_SSID_3[64] = "";
char WIFI_PASSWORD_3[64] = "";
char WIFI_SSID_4[64] = "";
char WIFI_PASSWORD_4[64] = "";
// Shipped hotspot passphrase. Also the fallback used whenever a stored
// AP_PASSWORD is unusable (see sanitizeStringParams() below), so it is named
// once instead of being repeated as a literal in three places.
const char AP_PASSWORD_DEFAULT[] = "12345678";
char AP_PASSWORD[64] = "12345678";
// How long one network gets before the search gives up on it and tries the next
// slot. A WPA2 join on a cold radio is a band scan (1-3s) plus authentication,
// association and DHCP; the compiled-in 5s window used to cut off attempts that
// were still working, which showed up as a timeout followed by a refused config
// change for the next network.
int WIFI_ATTEMPT_SECONDS = 12;

bool NTP_ENABLED = true;
char NTP_SERVER[64] = "pool.ntp.org";
int TZ_OFFSET_HOURS = 1;
bool TZ_DST_ENABLED = true;
// Which calendar TZ_DST_ENABLED applies: 0 none, 1 EU/EEA, 2 US/Canada.
// Default 1 keeps the behaviour of every unit shipped before #22.
int TZ_DST_RULE = TZ_DST_RULE_EU;

bool OTA_PULL_ENABLED = false;
char OTA_PULL_URL[192] = "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest";

// ----------------------------------------------------------------------------
// Firmware identity
// ----------------------------------------------------------------------------
// FW_VERSION is the build identity: compiled in, never stored in NVS, never
// writable by the update manifest, a config restore or the Web API. It is the
// one place the release version is written (AGENTS.md rule 3).
//
// VERSION_OVERRIDE is a user-set label for display and for testing update
// pulls against an arbitrary version string. It is deliberately separate from
// the build identity, and nothing fetched from the network ever writes it -
// the previous single value (OTA_CURRENT_VERSION / NVS key "OTA_VER") was
// rewritten from the release manifest after every pull, so the device ended up
// *claiming* whatever the server said and could decide it was permanently
// up-to-date (or hold a version brought back by a config backup).
const char FW_VERSION[] = "1.4.1";
char VERSION_OVERRIDE[32] = "";

int FUEL_TOUCH_POINTS = 8;

// Fuel calibration table, one entry per level slot: index 0 = empty, index
// FUEL_TOUCH_POINTS-1 = full. The unit is **ohms**, the quantity a marine
// resistive sender actually produces (issue #18) - it used to be raw ADC counts
// from the removed capacitive pad. Keeping the table in the sender's own domain
// is what makes any ohm range work: the excitation resistor only decides how the
// ADC sees the sender, never what a calibration point means.
// Default = linear 10..180 ohm ramp (the common US marine sender); points past
// index 7 continue the ramp for a taller tank. Recalibrate against the real tank
// with the Web UI capture flow.
float fuelCalOhms[MAX_TOUCH_POINTS] = {
    10.0f,   34.3f,  58.6f,  82.9f,  107.1f, 131.4f, 155.7f, 180.0f,
    204.3f,  228.6f, 252.9f, 277.1f, 301.4f, 325.7f, 350.0f, 374.3f,
    398.6f,  422.9f, 447.1f, 471.4f};

// Resistive fuel sender wiring (issue #18): 3V3 - FUEL_EXC_RES_OHM - GPIO32 -
// sender - GND. The pin voltage gives the sender resistance, so the sender range
// is user-entered and nothing is hard-wired to a particular standard.
bool FUEL_INPUT_ENABLED = false;  // pin is floating until the sender is fitted
int FUEL_EXC_RES_OHM = 220;
float FUEL_ADC_VREF = 3.30f;
float FUEL_OHM_EMPTY = 10.0f;
float FUEL_OHM_FULL = 180.0f;
int FUEL_OVERSAMPLE = 16;

// ----------------------------------------------------------------------------
// NVS config macros
// ----------------------------------------------------------------------------
// Every numeric parameter carries its own allowed band next to its default
// value (issue #21: CFG_INT/CFG_FLT applied whatever the source handed them -
// a negative wheel circumference, TARGET_FPS 0, an 8-digit odometer - and that
// value then went straight into division by zero, buffer sizing, the display
// layout and the CPU/SPI clocks). The band is enforced on both paths that can
// feed a value in:
//   mode 0 - the NVS load at boot (protects against a corrupted or hand-edited
//            namespace, and against a value written by an older firmware),
//   mode 2 - POST /api/config and the backup restore, before the value is
//            written back to NVS, so a rejected value is never stored.
// Out-of-range values are clamped and logged, never rejected: a unit that was
// handed a bad backup must still boot and stay reachable on the config portal.
// sanitizeConfigPairs() below adds the cross-field rules a single band cannot
// express (min/max ordering, satellite thresholds, standard baud and CPU clock
// steps) and sanitizeDigitCounts() keeps the digit-count budget.
static void clampCfgInt(const char *name, int *v, int lo, int hi) {
  if (*v < lo || *v > hi) {
    logPrintf("Config: %s=%d out of range [%d..%d], clamped to %d\n", name, *v,
              lo, hi, (*v < lo) ? lo : hi);
    *v = (*v < lo) ? lo : hi;
  }
}

// Floats need their own path: a NaN compares false against everything, so
// constrain() would pass it straight through and it would then poison every
// later computation (a NaN wheel circumference makes the odometer NaN forever).
// A non-finite value falls back to the parameter's default.
static void clampCfgFloat(const char *name, float *v, float lo, float hi,
                          float dflt) {
  if (!std::isfinite((double)*v)) {
    logPrintf("Config: %s is not a finite number, using default %.3f\n", name,
              (double)dflt);
    *v = dflt;
    return;
  }
  if (*v < lo || *v > hi) {
    logPrintf("Config: %s=%.3f out of range [%.3f..%.3f], clamped to %.3f\n",
              name, (double)*v, (double)lo, (double)hi,
              (double)(*v < lo ? lo : hi));
    *v = (*v < lo) ? lo : hi;
  }
}

// Unsigned parameters are validated through a signed window: a value posted as
// negative wraps to a huge unsigned number, which would clamp to the top of the
// band instead of the bottom. The SPI clock is the case this protects.
static uint32_t clampCfgUnsigned(const char *name, long long v, long long lo,
                                 long long hi) {
  if (v < lo || v > hi) {
    logPrintf("Config: %s=%lld out of range [%lld..%lld], clamped to %lld\n",
              name, v, lo, hi, v < lo ? lo : hi);
    v = (v < lo) ? lo : hi;
  }
  return (uint32_t)v;
}

// Writes that failed are counted so a save can say so out loud instead of
// looking like it worked (issue #32).
uint16_t cfgNvsWriteErrors = 0;
char cfgNvsFailedKeys[72];
static bool cfgSaveActive = false;

// Which keys failed, so a save can name them instead of only counting them.
// Recorded only while processConfig(2) is open: a write from another task (the
// odometer, the reboot tag) must not end up blamed on the user's save. The list
// is fixed-size (RAM rules) and tail-truncated when it overflows.
void cfgNoteFailedKey(const char *key) {
  if (!cfgSaveActive) return;
  size_t used = strlen(cfgNvsFailedKeys);
  size_t need = strlen(key) + (used ? 2 : 1);
  if (used + need > sizeof(cfgNvsFailedKeys)) {
    if (sizeof(cfgNvsFailedKeys) - used >= 6) strcat(cfgNvsFailedKeys, ",...");
    return;
  }
  if (used) strcat(cfgNvsFailedKeys, ",");
  strcat(cfgNvsFailedKeys, key);
}

// The entry budget of the NVS partition. A failing write on a unit that still
// has hundreds of available entries points at page fragmentation (a string
// needs one page with room for the whole value), not at a full partition.
size_t nvsStatsUsed = 0, nvsStatsAvailable = 0, nvsStatsTotal = 0, nvsStatsBytes = 0;
static size_t nvsStatsFree = 0;        // free_entries: includes the spare page
static size_t nvsStatsNamespaces = 0;  // namespaces currently stored
static uint32_t nvsStatsAt = 0;        // millis() of the last successful read

// One read of the partition budget into the globals. Kept separate from the log
// line because /api/health polls the numbers continuously: walking every NVS
// page on a 1 s poll is pointless work, so display callers go through
// refreshNvsStats() and only logNvsStats() prints.
static bool readNvsStats() {
  nvs_stats_t st;
  if (nvs_get_stats("nvs", &st) != ESP_OK) return false;
  nvsStatsUsed = st.used_entries;
  nvsStatsAvailable = st.available_entries;
  nvsStatsTotal = st.total_entries;
  nvsStatsFree = st.free_entries;
  nvsStatsNamespaces = st.namespace_count;
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  nvsStatsBytes = part ? part->size : 0;
  nvsStatsAt = millis();
  return true;
}

void refreshNvsStats(uint32_t maxAgeMs) {
  if (nvsStatsAt && millis() - nvsStatsAt < maxAgeMs) return;
  readNvsStats();
}

void logNvsStats() {
  // The byte size is printed too because a unit updated over OTA keeps the
  // partition table it was flashed with: the same firmware runs against 20 KB
  // or 80 KB of NVS depending on how that board was last flashed, and the log
  // is the only place that difference is visible. `available` is the number that
  // matters — free minus the page NVS must keep for its own tidying. A partition
  // can report hundreds of slots free and still fail a write; that is what 503
  // used / 125 free / 1 available looked like.
  if (readNvsStats())
    logPrintf("NVS 'nvs' %u bytes: %u entries used, %u available, %u free, %u total, %u namespace(s)\n",
              (unsigned)nvsStatsBytes, (unsigned)nvsStatsUsed,
              (unsigned)nvsStatsAvailable, (unsigned)nvsStatsFree,
              (unsigned)nvsStatsTotal, (unsigned)nvsStatsNamespaces);
  else
    logPrintf("NVS: nvs_get_stats failed\n");
}

// A save that rewrites every parameter spends the NVS budget on churn rather
// than on values: re-storing a string of a different length strands its old
// items, and those are only erased when a whole page is left with nothing live.
// On the old 5-page partition ~200 keys were spread over every page, so garbage
// accumulated until the partition had one entry free and the next write failed -
// silently, with the Web UI saying "saved". Reading first and writing only what
// changed is both the space fix and the wear rule (AGENTS 12). The extra reads
// are cheap: NVS caches pages in RAM once opened.
static char cfgStrScratch[196];  // fixed buffer (AGENTS 14), CFG_STR only

static bool cfgIntChanged(Preferences &pref, const char *key, int v) {
  // A value no config band can reach stands in for "key not stored".
  return pref.getInt(key, v == INT_MIN ? INT_MIN + 1 : INT_MIN) != v;
}

static bool cfgFloatChanged(Preferences &pref, const char *key, float v) {
  // A missing key reads back NaN, which fails the comparison and gets written.
  return !(pref.getFloat(key, NAN) == v);
}

static bool cfgBoolChanged(Preferences &pref, const char *key, bool v) {
  return pref.getBool(key, !v) != v;
}

static bool cfgStrChanged(Preferences &pref, const char *key, const char *v) {
  cfgStrScratch[0] = 0;
  if (pref.getString(key, cfgStrScratch, sizeof(cfgStrScratch)) == 0)
    return v[0] != 0;  // nothing stored, or stored empty: only a real value is worth a write
  return strcmp(cfgStrScratch, v) != 0;
}

// Same rule for the fuel ramp, which is written by several paths per save.
// Returns what putFloat returns (bytes written), and 1 for "nothing to do".
static bool cfgPutFloat(Preferences &pref, const char *key, float v) {
  if (!cfgFloatChanged(pref, key, v)) return true;
  return pref.putFloat(key, v);
}

#define CFG_INT(var, nvsKey, defVal, lo, hi)                                   \
  if (mode == 0) {                                                             \
    var = pref.getInt(nvsKey, defVal);                                         \
    clampCfgInt(#var, &var, lo, hi);                                           \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<int>();                                              \
    clampCfgInt(#var, &var, lo, hi);                                           \
    if (cfgIntChanged(pref, nvsKey, var) &&                                    \
        nvsWriteFailed(nvsKey, pref.putInt(nvsKey, var))) cfgNvsWriteErrors++;  \
  }

#define CFG_UINT(var, nvsKey, defVal, lo, hi)                                   \
  if (mode == 0) {                                                             \
    var = clampCfgUnsigned(#var, pref.getInt(nvsKey, defVal), lo, hi);          \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = clampCfgUnsigned(#var, (*doc)[#var].as<long long>(), lo, hi);         \
    if (cfgIntChanged(pref, nvsKey, (int)var) &&                               \
        nvsWriteFailed(nvsKey, pref.putInt(nvsKey, (int)var))) cfgNvsWriteErrors++; \
  }

#define CFG_FLT(var, nvsKey, defVal, lo, hi)                                   \
  if (mode == 0) {                                                             \
    var = pref.getFloat(nvsKey, defVal);                                       \
    clampCfgFloat(#var, &var, lo, hi, (float)defVal);                          \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<float>();                                            \
    clampCfgFloat(#var, &var, lo, hi, (float)defVal);                          \
    if (cfgFloatChanged(pref, nvsKey, var) &&                                  \
        nvsWriteFailed(nvsKey, pref.putFloat(nvsKey, var))) cfgNvsWriteErrors++; \
  }

// String config values live in fixed char[] buffers (no String objects, no
// per-request heap allocations in the save/load path). mode 1 serializes the
// buffer, mode 2 writes the posted value in place. The load in mode 0 uses
// the getString(key, char*, maxLen) buffer overload (nvs_get_str): getBytes
// reads via nvs_get_blob, which returns TYPE_MISMATCH for values stored with
// putString (nvs_set_str), silently falling back to the default on every
// boot and making saved settings appear to vanish.
#define CFG_STR(var, nvsKey, defVal)                                           \
  if (mode == 0) {                                                             \
    size_t cfgLen = pref.getString(nvsKey, var, sizeof(var));                  \
    if (cfgLen == 0) {                                                         \
      strncpy(var, defVal, sizeof(var) - 1);                                   \
      var[sizeof(var) - 1] = 0;                                                \
    } else {                                                                   \
      var[sizeof(var) - 1] = 0;                                                \
    }                                                                          \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && (*doc)[#var].is<const char *>()) {                   \
    snprintf(var, sizeof(var), "%s", (*doc)[#var].as<const char *>());         \
    if (cfgStrChanged(pref, nvsKey, var) &&                                    \
        nvsStringWriteFailed(nvsKey, var, pref.putString(nvsKey, var))) cfgNvsWriteErrors++; \
  }

#define CFG_BOOL(var, nvsKey, defVal)                                          \
  if (mode == 0) {                                                             \
    var = pref.getBool(nvsKey, defVal);                                        \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<bool>();                                             \
    if (cfgBoolChanged(pref, nvsKey, var) &&                                   \
        nvsWriteFailed(nvsKey, pref.putBool(nvsKey, var))) cfgNvsWriteErrors++; \
  }

// The version this unit reports: the user override when one is set, otherwise
// the compiled-in build identity. Display paths and the update check both read
// this, so a bench override still steers a pull test.
const char *effectiveVersion() {
  return (VERSION_OVERRIDE[0] != 0) ? VERSION_OVERRIDE : FW_VERSION;
}

// Pulls the next numeric group out of a dotted version string, skipping 'v',
// dots and any other separator ("v1.3.10" -> 1, 3, 10).
static long nextVersionGroup(const char **s) {
  const char *p = *s;
  while (*p && (*p < '0' || *p > '9')) p++;
  char *end = nullptr;
  long v = strtol(p, &end, 10);
  *s = end ? end : p;
  return v;
}

// Numeric compare for version strings: -1 if a < b, 0 if equal, +1 if a > b.
// Compares the first four numeric groups (missing groups count as 0), which
// fixes the string compare the update check used to do - "1.3.10" is *newer*
// than "1.3.9", and "1.3.8" vs "1.3.8.0" is the same release, not a new one.
int versionCmp(const char *a, const char *b) {
  if (!a) a = "";
  if (!b) b = "";
  for (int i = 0; i < 4; i++) {
    long ga = nextVersionGroup(&a);
    long gb = nextVersionGroup(&b);
    if (ga != gb) return (ga < gb) ? -1 : 1;
  }
  return 0;
}

uint16_t hexToRGB565(const char *hex) {
  if (hex == nullptr || hex[0] == 0)
    return 0xFFFF;
  if (hex[0] == '#')
    hex++;
  long rgb = strtol(hex, nullptr, 16);
  uint8_t r = (rgb >> 16) & 0xFF;
  uint8_t g = (rgb >> 8) & 0xFF;
  uint8_t b = rgb & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void applyColors() {
  c_temp_norm = hexToRGB565(COLOR_TEMP_NORM);
  c_temp_warn = hexToRGB565(COLOR_TEMP_WARN);
  c_temp_crit = hexToRGB565(COLOR_TEMP_CRIT);

  c_fuel_norm = hexToRGB565(COLOR_FUEL_NORM);
  c_fuel_warn = hexToRGB565(COLOR_FUEL_WARN);
  c_fuel_crit = hexToRGB565(COLOR_FUEL_CRIT);
  ghost_color = hexToRGB565(GHOST_COLOR_STR);
}

// ----------------------------------------------------------------------------
// Digit-count validation (issue #6 - memory corruption)
// ----------------------------------------------------------------------------
// The *_DIGITS params size the loops that fill the fixed UI_MAX_CELLS-slot cell
// arrays in ui.cpp and the batPat/tmrPat pattern buffers on the display task's
// stack. They used to be read back from NVS, from POST /api/config and from a
// config backup with no range check at all: {"ODO_INT_DIGITS":50} made
// measureDs15Cells() write 52 ints into an int[16] - .bss overwrite, then a
// crash in the display task.
//
// Two layers, both live:
//  1. every digit param is clamped to its own sane range (a count that large
//     could not be laid out on a 480x320 panel anyway);
//  2. the shared budget int + dec + 1 <= UI_MAX_CELLS (+1 is the decimal point
//     slot) is enforced per readout, trimming the decimals first and then the
//     integers, so no combination of two individually-valid values can size a
//     fill loop past its array.
// ui.cpp keeps its own hard guard (clampCells()/measureDs15Cells()) as a second
// line of defence for any count that reaches the display unclamped.
// Clamping is RAM-only - no extra NVS writes: an out-of-range stored value is
// re-clamped on every load, and the next save stores the clamped one.
void sanitizeDigitCounts() {
  struct Range {
    const char *name;
    int *var;
    int lo, hi;
  };
  static const Range ranges[] = {
      {"SPEED_DIGITS", &SPEED_DIGITS, 1, 4},
      {"SAT_DIGITS", &SAT_DIGITS, 1, 3},
      {"TMR_INT_DIGITS", &TMR_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"TMR_DEC_DIGITS", &TMR_DEC_DIGITS, 0, 4},
      {"BAT_INT_DIGITS", &BAT_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"BAT_DEC_DIGITS", &BAT_DEC_DIGITS, 0, 4},
      {"INST_INT_DIGITS", &INST_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"INST_DEC_DIGITS", &INST_DEC_DIGITS, 0, 4},
      {"AVG_INT_DIGITS", &AVG_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"AVG_DEC_DIGITS", &AVG_DEC_DIGITS, 0, 4},
      {"AVG_SPEED_INT_DIGITS", &AVG_SPEED_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"AVG_SPEED_DEC_DIGITS", &AVG_SPEED_DEC_DIGITS, 0, 4},
      {"MAX_SPEED_INT_DIGITS", &MAX_SPEED_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"MAX_SPEED_DEC_DIGITS", &MAX_SPEED_DEC_DIGITS, 0, 4},
      {"FUEL_INT_DIGITS", &FUEL_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"FUEL_DEC_DIGITS", &FUEL_DEC_DIGITS, 0, 4},
      {"ODO_INT_DIGITS", &ODO_INT_DIGITS, 1, UI_MAX_CELLS - 2},
      {"ODO_DEC_DIGITS", &ODO_DEC_DIGITS, 0, 4},
  };
  for (const Range &r : ranges) {
    int c = constrain(*r.var, r.lo, r.hi);
    if (c != *r.var) {
      logPrintf("Config: %s=%d out of range [%d..%d], clamped to %d\n", r.name,
                *r.var, r.lo, r.hi, c);
      *r.var = c;
    }
  }

  struct Group {
    const char *name;
    int *i;
    int *d;
  };
  static const Group groups[] = {
      {"TMR", &TMR_INT_DIGITS, &TMR_DEC_DIGITS},
      {"BAT", &BAT_INT_DIGITS, &BAT_DEC_DIGITS},
      {"INST", &INST_INT_DIGITS, &INST_DEC_DIGITS},
      {"AVG", &AVG_INT_DIGITS, &AVG_DEC_DIGITS},
      {"AVG_SPEED", &AVG_SPEED_INT_DIGITS, &AVG_SPEED_DEC_DIGITS},
      {"MAX_SPEED", &MAX_SPEED_INT_DIGITS, &MAX_SPEED_DEC_DIGITS},
      {"FUEL", &FUEL_INT_DIGITS, &FUEL_DEC_DIGITS},
      {"ODO", &ODO_INT_DIGITS, &ODO_DEC_DIGITS},
  };
  for (const Group &g : groups) {
    if (*g.i + *g.d + 1 <= UI_MAX_CELLS) continue;
    int oi = *g.i, od = *g.d;
    while (*g.i + *g.d + 1 > UI_MAX_CELLS) {
      if (*g.d > 0)
        (*g.d)--;
      else if (*g.i > 1)
        (*g.i)--;
      else
        break;  // unreachable: the clamps above guarantee int >= 1, dec >= 0
    }
    logPrintf("Config: %s digits %d+%d exceed %d cells, using %d+%d\n", g.name,
              oi, od, UI_MAX_CELLS, *g.i, *g.d);
  }
}

// ----------------------------------------------------------------------------
// Cross-field and enumerated config validation (issue #21)
// ----------------------------------------------------------------------------
// A per-parameter band cannot express the rules that involve two parameters, or
// the ones where only a handful of values are meaningful. Both cases exist in
// this config:
//   - a bar whose maximum is below its minimum renders as a permanently full or
//     permanently empty gauge, and a warning threshold outside the bar band can
//     never be reached (or is always reached);
//   - GPS_BAUD and MANUAL_CPU_FREQ are enumerations wearing integer clothes:
//     the UART only works at standard rates and setCpuFrequencyMhz() only
//     accepts 80/160/240 - an arbitrary value in between silently breaks the
//     link or leaves the CPU at the previous clock.
// Like sanitizeDigitCounts(), corrections are RAM-only: an out-of-range stored
// value is corrected again on every load, and the next save stores the
// corrected one.
static void snapToNearest(const char *name, int *v, const int *options,
                          int count, int lo, int hi) {
  if (*v < lo || *v > hi) return;  // the band check already reported this one
  for (int i = 0; i < count; i++)
    if (options[i] == *v) return;
  int best = options[0];
  for (int i = 1; i < count; i++) {
    if (abs(options[i] - *v) < abs(best - *v)) best = options[i];
  }
  logPrintf("Config: %s=%d is not a supported value, using %d\n", name, *v,
            best);
  *v = best;
}

// The fuel calibration table is read as a monotonic ramp: the gauge walk in
// sensors.cpp takes its direction from the two ends and then assumes every
// interior point follows it. A point out of order leaves a gap that matches no
// segment and the needle stops moving (issue #19). Repair the table wherever it
// enters the device - sorted in the direction its own ends already imply, so a
// half-typed calibration file degrades to a usable ramp instead of a dead gauge.
// Returns the number of points that were out of order (0 = table untouched).
static int repairFuelTable(const char *when) {
  const int n = constrain(FUEL_TOUCH_POINTS, 2, MAX_TOUCH_POINTS);
  const bool descending = (fuelCalOhms[0] >= fuelCalOhms[n - 1]);
  int outOfOrder = 0;
  for (int i = 0; i + 1 < n; i++) {
    bool ok = descending ? (fuelCalOhms[i] >= fuelCalOhms[i + 1])
                         : (fuelCalOhms[i] <= fuelCalOhms[i + 1]);
    if (!ok) outOfOrder++;
  }
  if (outOfOrder == 0) return 0;
  // Insertion sort: at most MAX_TOUCH_POINTS entries, no heap (rule 14).
  for (int i = 1; i < n; i++) {
    float v = fuelCalOhms[i];
    int j = i - 1;
    while (j >= 0 &&
           (descending ? (fuelCalOhms[j] < v) : (fuelCalOhms[j] > v))) {
      fuelCalOhms[j + 1] = fuelCalOhms[j];
      j--;
    }
    fuelCalOhms[j + 1] = v;
  }
  logPrintf("Config: %d fuel-table point(s) out of order (%s), sorted %s - "
            "recalibrate if the gauge looks wrong\n",
            outOfOrder, when, descending ? "descending" : "ascending");
  return outOfOrder;
}

// Rebuild the table as a straight empty->full ramp in the ohm domain. Used when
// the point count changes (the stored ramp no longer has the right number of
// slots, and slots past the old end would otherwise sit at an unrelated value)
// and by the Web UI "fill from empty/full ohms" action.
static void fillFuelTableFromOhms(const char *why) {
  const int n = constrain(FUEL_TOUCH_POINTS, 2, MAX_TOUCH_POINTS);
  float e = FUEL_OHM_EMPTY, f = FUEL_OHM_FULL;
  if (!(fabsf(f - e) > 0.5f)) f = e + 170.0f;  // degenerate range guard
  for (int i = 0; i < n; i++)
    fuelCalOhms[i] = e + (f - e) * (float)i / (float)(n - 1);
  logPrintf("Fuel: %d-point table rebuilt as a linear ramp (%s, %.1f..%.1f "
            "ohm) - recalibrate against the tank for accuracy\n", n, why, e, f);
}

void sanitizeConfigPairs() {
  // Bar ranges: min must stay below max.
  if (TEMP_BAR_MIN >= TEMP_BAR_MAX) {
    logPrintf("Config: TEMP_BAR_MIN=%d >= TEMP_BAR_MAX=%d, using bar %d..%d\n",
              TEMP_BAR_MIN, TEMP_BAR_MAX, TEMP_BAR_MIN, TEMP_BAR_MIN + 1);
    TEMP_BAR_MAX = TEMP_BAR_MIN + 1;
  }
  // Colour thresholds are hot-side markers: RED is the hotter of the two and
  // both live inside the bar band, otherwise a colour can never show.
  if (TEMP_WARN_YEL > TEMP_WARN_RED) {
    logPrintf("Config: TEMP_WARN_YEL=%d above TEMP_WARN_RED=%d, lowered\n",
              TEMP_WARN_YEL, TEMP_WARN_RED);
    TEMP_WARN_YEL = TEMP_WARN_RED;
  }
  if (TEMP_WARN_RED < TEMP_BAR_MIN || TEMP_WARN_RED > TEMP_BAR_MAX)
    logPrintf("Config: TEMP_WARN_RED=%d outside the bar %d..%d, it will never "
              "show\n",
              TEMP_WARN_RED, TEMP_BAR_MIN, TEMP_BAR_MAX);
  // Fuel is the mirror image: the gauge turns red BELOW its marker, so the red
  // threshold has to sit under the yellow one.
  if (FUEL_WARN_RED > FUEL_WARN_YEL) {
    logPrintf("Config: FUEL_WARN_RED=%d above FUEL_WARN_YEL=%d, lowered\n",
              FUEL_WARN_RED, FUEL_WARN_YEL);
    FUEL_WARN_RED = FUEL_WARN_YEL;
  }
  // Automatic brightness compares the light sensor against these two values.
  if (LIGHT_SENSOR_DARK_VAL >= LIGHT_SENSOR_BRIGHT_VAL) {
    logPrintf("Config: LIGHT_SENSOR_DARK_VAL=%d >= LIGHT_SENSOR_BRIGHT_VAL=%d, "
              "using %d..%d\n",
              LIGHT_SENSOR_DARK_VAL, LIGHT_SENSOR_BRIGHT_VAL,
              LIGHT_SENSOR_DARK_VAL, LIGHT_SENSOR_DARK_VAL + 1);
    LIGHT_SENSOR_BRIGHT_VAL = LIGHT_SENSOR_DARK_VAL + 1;
  }
  // The GPS quality ramp: optimal must not sit below the minimum.
  if (OPTIMAL_SATELLITES < MIN_SATELLITES) {
    logPrintf("Config: OPTIMAL_SATELLITES=%d below MIN_SATELLITES=%d, raised\n",
              OPTIMAL_SATELLITES, MIN_SATELLITES);
    OPTIMAL_SATELLITES = MIN_SATELLITES;
  }
  // CPU throttling: the warning has to come before the critical step-down.
  if (CPU_THROTTLE_TEMP_CRIT < CPU_THROTTLE_TEMP_WARN) {
    logPrintf("Config: CPU_THROTTLE_TEMP_CRIT=%d below WARN=%d, raised\n",
              CPU_THROTTLE_TEMP_CRIT, CPU_THROTTLE_TEMP_WARN);
    CPU_THROTTLE_TEMP_CRIT = CPU_THROTTLE_TEMP_WARN;
  }
  // The dashboard centre has to be on the panel.
  if (BIG_CENTER_X > DISPLAY_WIDTH || BIG_CENTER_Y > DISPLAY_HEIGHT) {
    logPrintf("Config: BIG_CENTER %d,%d outside the %dx%d panel\n",
              BIG_CENTER_X, BIG_CENTER_Y, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    BIG_CENTER_X = constrain(BIG_CENTER_X, 0, DISPLAY_WIDTH);
    BIG_CENTER_Y = constrain(BIG_CENTER_Y, 0, DISPLAY_HEIGHT);
  }
  // UART rates the u-blox side actually uses, and the three CPU clocks the
  // Arduino core supports.
  static const int BAUDS[] = {1200,   2400,   4800,   9600,   19200,  38400,
                              57600,  74400,  115200, 230400, 460800, 921600};
  snapToNearest("GPS_BAUD", &GPS_BAUD, BAUDS,
                (int)(sizeof(BAUDS) / sizeof(BAUDS[0])), 1200, 921600);
  static const int CPU_MHZ[] = {80, 160, 240};
  snapToNearest("MANUAL_CPU_FREQ", &MANUAL_CPU_FREQ, CPU_MHZ,
                (int)(sizeof(CPU_MHZ) / sizeof(CPU_MHZ[0])), 80, 240);
}

void processConfig(int mode, JsonDocument *doc) {
  // The "cfg" namespace is opened by the boot load, by the Web UI save (web
  // task) and by the GNSS baud autodetect (sensor task). Every NVS session now
  // holds prefsMux for its whole begin()/end() pair, so a save can no longer
  // interleave with another task's open and lose writes (issue #32).
  NvsSession session("cfg", false, (mode == 0 || mode == 2));
  Preferences &pref = session.nvs;
  if (mode == 2) {
    cfgNvsWriteErrors = 0;
    cfgNvsFailedKeys[0] = 0;
    cfgSaveActive = true;
  }
  // Issue #18: a point-count change invalidates the stored ramp, and the new
  // count is only known after the CFG_* block below has run.
  const int prevFuelPoints = FUEL_TOUCH_POINTS;

  CFG_INT(DISPLAY_ROTATION, "DISP_ROT", 1, 0, 3);
  CFG_BOOL(UNITS_IMPERIAL, "UNITS_IMP", false);
  CFG_BOOL(ADV_MODE, "ADV_MODE", false);
  CFG_UINT(SPI_BUS_SPEED, "SPI_FREQ", 60000000, SPI_SPEED_MIN_HZ,
           SPI_SPEED_MAX_HZ);
  CFG_INT(DISPLAY_WIDTH, "DISP_W", 480, 16, 2048);
  CFG_INT(DISPLAY_HEIGHT, "DISP_H", 320, 16, 2048);
  CFG_INT(TARGET_FPS, "TGT_FPS", 60, 5, 120);
  CFG_INT(BACKLIGHT_BRIGHTNESS, "BL_BRIGHT", 100, 0, 100);
  CFG_BOOL(ENABLE_AUTO_BRIGHTNESS, "EN_AUTO_BL", true);
  CFG_INT(LIGHT_SENSOR_DARK_VAL, "LIGHT_DARK", 432, 0, 4095);
  CFG_INT(LIGHT_SENSOR_BRIGHT_VAL, "LIGHT_BRIGHT", 2851, 0, 4095);
  CFG_INT(AUTO_BRIGHT_DARK, "AB_DARK", 13, 0, 100);
  CFG_INT(AUTO_BRIGHT_LIGHT, "AB_LIGHT", 100, 0, 100);
  CFG_INT(AUTO_BRIGHT_FADE_MS, "AB_FADE", 4000, 0, 60000);
  CFG_INT(FADE_DURATION_MS, "FADE_DUR", 700, 0, 10000);
  CFG_STR(SPLASH_SIGNATURE, "SPLASH_SIG", "by @ale.finot");
  CFG_STR(REBOOT_SIGNATURE, "REBOOT_SIG", "Dashboard++ by @ale.finot");
  CFG_STR(DASHBOARD_SIGNATURE, "DASH_SIG",
          "<<<<<<    Dashboard++ by @ale.finot    >>>>>>");

  CFG_INT(TEMP_BAR_MIN, "TMP_BAR_MIN", 10, -40, 300);
  CFG_INT(TEMP_BAR_MAX, "TMP_BAR_MAX", 110, -40, 300);
  CFG_INT(TEMP_WARN_RED, "TMP_WRN_R", 90, -40, 300);
  CFG_INT(TEMP_WARN_YEL, "TMP_WRN_Y", 45, -40, 300);
  CFG_INT(FUEL_WARN_RED, "FUL_WRN_R", 20, 0, 100);
  CFG_INT(FUEL_WARN_YEL, "FUL_WRN_Y", 45, 0, 100);

  CFG_STR(COLOR_TEMP_NORM, "C_TMP_N", "#00ff00");
  CFG_STR(COLOR_TEMP_WARN, "C_TMP_W", "#ffff00");
  CFG_STR(COLOR_TEMP_CRIT, "C_TMP_C", "#ff0000");
  CFG_STR(COLOR_FUEL_NORM, "C_FUL_N", "#00ff00");
  CFG_STR(COLOR_FUEL_WARN, "C_FUL_W", "#ffff00");
  CFG_STR(COLOR_FUEL_CRIT, "C_FUL_C", "#ff0000");
  CFG_STR(GHOST_COLOR_STR, "GHOST_C", "#474747");

  CFG_FLT(WHEEL_CIRCUMFERENCE_MM, "WHL_CIRC", 1650.0f, 50.0f, 10000.0f);
  CFG_FLT(FUEL_FILTER_ALPHA, "FUEL_FILT", 0.08f, 0.001f, 1.0f);
  CFG_INT(TRIP_RESET_HOLD_MS, "TRP_RST_H", 1500, 200, 10000);
  CFG_INT(FUEL_TOUCH_POINTS, "FTL_PTS", 8, 2, 20);
  CFG_BOOL(FUEL_INPUT_ENABLED, "FUEL_EN", false);
  CFG_INT(FUEL_EXC_RES_OHM, "FUEL_EXR", 220, 10, 100000);
  CFG_FLT(FUEL_ADC_VREF, "FUEL_VRF", 3.30f, 2.5f, 4.2f);
  CFG_FLT(FUEL_OHM_EMPTY, "FUEL_OE", 10.0f, 0.0f, 100000.0f);
  CFG_FLT(FUEL_OHM_FULL, "FUEL_OF", 180.0f, 0.0f, 100000.0f);
  CFG_INT(FUEL_OVERSAMPLE, "FUEL_OSM", 16, 1, 64);
  CFG_FLT(BATTERY_SCALE, "BAT_SCALE", 5.7f, 0.01f, 1000.0f);
  CFG_FLT(BATTERY_OFFSET, "BAT_OFFS", 0.2f, -50.0f, 50.0f);
  CFG_FLT(NTC_R_BALANCE, "NTC_BAL", 10000.0f, 100.0f, 10000000.0f);
  CFG_FLT(NTC_R25, "NTC_R25", 10000.0f, 100.0f, 10000000.0f);
  CFG_FLT(NTC_BETA, "NTC_BETA", 3950.0f, 1000.0f, 20000.0f);
  CFG_FLT(NTC_TEMP_OFFSET, "NTC_OFFS", 0.0f, -50.0f, 50.0f);
  CFG_INT(GPS_BAUD, "GPS_BAUD", 115200, 1200, 921600);
  CFG_INT(MIN_SATELLITES, "MIN_SAT", 8, 1, 32);
  CFG_INT(OPTIMAL_SATELLITES, "OPT_SAT", 12, 1, 32);
  CFG_FLT(MAX_SPEED_DELTA_KMH, "MAX_SPD_DELT", 5.0f, 0.01f, 100.0f);
  CFG_FLT(MIN_SPEED_THRESHOLD, "MIN_SPD_THR", 1.0f, 0.0f, 50.0f);
  CFG_FLT(GPS_START_KMH, "GPS_START", 3.0f, 0.0f, 200.0f);
  CFG_INT(GPS_STOP_SETTLE_MS, "GPS_STL_MS", 1500, 0, 60000);
  CFG_FLT(GPS_MIN_DEV_KMH, "GPS_MIN_DV", 1.0f, 0.0f, 50.0f);
  CFG_INT(SPEED_SOURCE_MODE, "SPD_SRC_MODE", 2, 0, 2);
  CFG_INT(SPEED_SOURCE_HOLD_MS, "SPD_SRC_HOLD", 500, 0, 10000);
  CFG_INT(HALL_MEDIAN_SAMPLES, "HALL_MED_N", 3, 1, 31);
  CFG_INT(HALL_PERIOD_GUARD, "HALL_PRD_GRD", 8, 1, 100);
  CFG_INT(HALL_PULSE_MIN_US, "HALL_PL_MIN", 150, 10, 1000);
  CFG_FLT(ACCEL_START_SPEED, "ACC_STRT", 1.0f, 0.0f, 200.0f);
  CFG_FLT(ACCEL_TARGET_SPEED, "ACC_TGT", 50.0f, 1.0f, 400.0f);
  CFG_FLT(ACCEL_MAX_TIME, "ACC_MAX_T", 9.99f, 0.1f, 999.99f);

  CFG_STR(ACCEL_BADGE_LINE1, "ACC_BDG_1", "0-50");
  CFG_STR(ACCEL_BADGE_LINE2, "ACC_BDG_2", "km/h");

  CFG_INT(BIG_CENTER_X, "BCX", 240, 0, 4095);
  CFG_INT(BIG_CENTER_Y, "BCY", 160, 0, 4095);

  CFG_INT(OFFSET_BIG_TIME_X, "O_BTIME_X", 107, -4096, 4096);
  CFG_INT(OFFSET_BIG_TIME_Y, "O_BTIME_Y", -91, -4096, 4096);
  CFG_INT(OFFSET_BIG_DATE_X, "O_BDATE_X", -131, -4096, 4096);
  CFG_INT(OFFSET_BIG_DATE_Y, "O_BDATE_Y", -91, -4096, 4096);
  CFG_INT(OFFSET_BIG_SIGNATURE_X, "O_BSIG_X", 0, -4096, 4096);
  CFG_INT(OFFSET_BIG_SIGNATURE_Y, "O_BSIG_Y", -75, -4096, 4096);
  CFG_INT(OFFSET_BIG_SPEED_NUM_X, "O_BSN_X", 0, -4096, 4096);
  CFG_INT(OFFSET_BIG_SPEED_NUM_Y, "O_BSN_Y", -3, -4096, 4096);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_X, "O_BSU_X", 106, -4096, 4096);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_Y, "O_BSU_Y", 56, -4096, 4096);
  CFG_INT(OFFSET_BIG_ODO_X, "O_BODO_X", 22, -4096, 4096);
  CFG_INT(OFFSET_BIG_ODO_Y, "O_BODO_Y", 126, -4096, 4096);
  CFG_INT(OFFSET_BIG_SAT_X, "O_BSAT_X", 179, -4096, 4096);
  CFG_INT(OFFSET_BIG_SAT_Y, "O_BSAT_Y", -114, -4096, 4096);
  CFG_INT(OFFSET_BIG_TMR_X, "O_BTMR_X", -53, -4096, 4096);
  CFG_INT(OFFSET_BIG_TMR_Y, "O_BTMR_Y", -46, -4096, 4096);
  CFG_INT(OFFSET_BIG_BAT_X, "O_BBAT_X", -112, -4096, 4096);
  CFG_INT(OFFSET_BIG_BAT_Y, "O_BBAT_Y", 123, -4096, 4096);
  CFG_INT(SIDEBAR_LEFT_X, "SBAR_L_X", 10, -4096, 4096);
  CFG_INT(SIDEBAR_LEFT_Y, "SBAR_L_Y", 95, -4096, 4096);
  CFG_INT(SIDEBAR_RIGHT_X, "SBAR_R_X", 462, -4096, 4096);
  CFG_INT(SIDEBAR_RIGHT_Y, "SBAR_R_Y", 95, -4096, 4096);
  CFG_INT(OFFSET_HALL_ICON_X, "O_HALL_X", 0, -4096, 4096);
  CFG_INT(OFFSET_HALL_ICON_Y, "O_HALL_Y", -100, -4096, 4096);
  CFG_INT(OFFSET_WIFI_ICON_X, "O_WIFI_X", 204, -4096, 4096);
  CFG_INT(OFFSET_WIFI_ICON_Y, "O_WIFI_Y", -108, -4096, 4096);
  CFG_INT(OFFSET_INST_KML_X, "O_INST_X", 60, -4096, 4096);
  CFG_INT(OFFSET_INST_KML_Y, "O_INST_Y", -25, -4096, 4096);
  CFG_INT(OFFSET_AVG_KML_X, "O_AVG_X", 160, -4096, 4096);
  CFG_INT(OFFSET_AVG_KML_Y, "O_AVG_Y", -25, -4096, 4096);
  CFG_INT(OFFSET_AVG_SPEED_X, "O_AVG_SPD_X", -163, -4096, 4096);
  CFG_INT(OFFSET_AVG_SPEED_Y, "O_AVG_SPD_Y", -25, -4096, 4096);
  CFG_INT(OFFSET_MAX_SPEED_X, "O_MAX_SPD_X", -60, -4096, 4096);
  CFG_INT(OFFSET_MAX_SPEED_Y, "O_MAX_SPD_Y", -25, -4096, 4096);
  CFG_INT(OFFSET_FUEL_LTRS_X, "O_FLTRS_X", 132, -4096, 4096);
  CFG_INT(OFFSET_FUEL_LTRS_Y, "O_FLTRS_Y", 123, -4096, 4096);

  CFG_INT(SIDEBAR_BAR_WIDTH, "SBAR_W", 8, 1, 2048);
  CFG_INT(SIDEBAR_BAR_HEIGHT, "SBAR_H", 190, 1, 2048);
  CFG_BOOL(SHOW_ELEMENT_BOUNDS, "SHW_BNDS", false);
  CFG_BOOL(SHOW_ELEMENT_SPEED, "SH_SPD", true);
  CFG_BOOL(SHOW_ELEMENT_SPEED_UNIT, "SH_SPD_UN", true);
  CFG_BOOL(SHOW_ELEMENT_SIGNATURE, "SH_SIG", true);
  CFG_BOOL(SHOW_ELEMENT_SPEED_SOURCE, "SH_SPD_SRC", true);
  CFG_BOOL(SHOW_ELEMENT_WIFI, "SH_WIFI", true);
  CFG_BOOL(SHOW_ELEMENT_TIME, "SH_TIME", true);
  CFG_BOOL(SHOW_ELEMENT_DATE, "SH_DATE", true);
  CFG_BOOL(SHOW_ELEMENT_ODO, "SH_ODO", true);
  CFG_BOOL(SHOW_ELEMENT_SIDEBAR_TEMP, "SH_SB_TMP", true);
  CFG_BOOL(SHOW_ELEMENT_SIDEBAR_FUEL, "SH_SB_FUL", true);
  CFG_BOOL(SHOW_ELEMENT_SAT, "SH_SAT", true);
  CFG_BOOL(SHOW_ELEMENT_TMR, "SH_TMR", true);
  CFG_BOOL(SHOW_ELEMENT_BAT, "SH_BAT", true);
  CFG_BOOL(SHOW_ELEMENT_INST_KML, "SH_INST", true);
  CFG_BOOL(SHOW_ELEMENT_AVG_KML, "SH_AVG", true);
  CFG_BOOL(SHOW_ELEMENT_AVG_SPEED, "SH_AVG_SPD", true);
  CFG_BOOL(SHOW_ELEMENT_MAX_SPEED, "SH_MAX_SPD", true);
  CFG_BOOL(SHOW_ELEMENT_FUEL_LTRS, "SH_FUL", true);
  CFG_BOOL(SHOW_GHOST_DIGITS, "SH_GHOST", true);
  CFG_BOOL(SHOW_ELEMENT_WEATHER, "SH_WEATH", true);
  CFG_INT(OFFSET_WEATHER_X, "O_WEATH_X", 0, -4096, 4096);
  CFG_INT(OFFSET_WEATHER_Y, "O_WEATH_Y", 146, -4096, 4096);
  CFG_STR(WEATHER_CITY, "WEATH_CITY", "");
  CFG_FLT(WEATHER_LAT, "WEATH_LAT", 0.0f, -90.0f, 90.0f);
  CFG_FLT(WEATHER_LON, "WEATH_LON", 0.0f, -180.0f, 180.0f);
  CFG_INT(WEATHER_REFRESH_MIN, "WEATH_RFR", 1, 1, 1440);
  CFG_STR(WEATHER_LOCALE, "WEATH_LOCALE", "it");
  CFG_BOOL(ENABLE_POWER_SENSE, "PWR_SNS", false);
  CFG_INT(POWER_SENSE_OFF_MS, "PWR_OFF_MS", 10000, 500, 120000);
  CFG_BOOL(ENABLE_CIRCLE_TEST, "CIRC_TST", false);
  CFG_BOOL(ENABLE_DEMO_MODE, "DEMO_MODE", false);
  CFG_BOOL(ENABLE_ANTIALIASING, "EN_AA", true);
  CFG_FLT(AA_SHARPNESS, "AA_SHARP", 0.2f, 0.0f, 1.0f);
  CFG_BOOL(SHOW_FPS_COUNTER_DEFAULT, "SHW_FPS", false);
  CFG_BOOL(GPS_DEBUG_DEFAULT, "GPS_DBG", false);
  CFG_BOOL(ENABLE_DYNAMIC_CPU, "DYN_CPU", false);
  CFG_INT(MANUAL_CPU_FREQ, "MAN_CPU", 240, 80, 240);
  CFG_BOOL(ENABLE_CPU_THROTTLE, "CPU_THR_EN", false);
  CFG_INT(CPU_THROTTLE_TEMP_WARN, "CPU_THR_W", 50, -50, 150);
  CFG_INT(CPU_THROTTLE_TEMP_CRIT, "CPU_THR_C", 60, -50, 150);
  CFG_BOOL(ENABLE_NIGHT_MODE, "EN_NIGHT", false);
  CFG_INT(NIGHT_MODE_START_HOUR, "NGHT_SRT", 23, 0, 23);
  CFG_INT(NIGHT_MODE_END_HOUR, "NGHT_END", 0, 0, 23);
  CFG_INT(NIGHT_BACKLIGHT, "NGHT_BL", 29, 0, 100);
  CFG_BOOL(DISPLAY_INVERT_COLORS, "INV_COLORS", false);
  CFG_INT(OFFSET_BIG_FPS_X, "O_FPS_X", -9, -4096, 4096);
  CFG_INT(OFFSET_BIG_FPS_Y, "O_FPS_Y", -7, -4096, 4096);

  CFG_INT(REFRESH_SPEED_MS, "R_SPD", 250, 10, 60000);
  CFG_INT(REFRESH_BAT_MS, "R_BAT", 2500, 10, 600000);
  CFG_INT(REFRESH_INST_MS, "R_INST", 500, 10, 60000);
  CFG_INT(REFRESH_MAX_SPEED_MS, "R_MAX_SPD", 500, 10, 60000);
  CFG_INT(REFRESH_FUEL_MS, "R_FUEL", 1000, 10, 60000);

  CFG_INT(SPEED_DIGITS, "SPD_DIG", 2, 1, 4);
  CFG_INT(SAT_DIGITS, "SAT_DIG", 2, 1, 3);
  CFG_INT(TMR_INT_DIGITS, "TMR_INT", 1, 1, 14);
  CFG_INT(TMR_DEC_DIGITS, "TMR_DEC", 2, 0, 4);
  CFG_INT(BAT_INT_DIGITS, "BAT_INT", 2, 1, 14);
  CFG_INT(BAT_DEC_DIGITS, "BAT_DEC", 1, 0, 4);
  CFG_INT(INST_INT_DIGITS, "INST_INT", 2, 1, 14);
  CFG_INT(INST_DEC_DIGITS, "INST_DEC", 1, 0, 4);
  CFG_INT(AVG_INT_DIGITS, "AVG_INT", 2, 1, 14);
  CFG_INT(AVG_DEC_DIGITS, "AVG_DEC", 1, 0, 4);
  CFG_INT(AVG_SPEED_INT_DIGITS, "AVG_SPD_INT", 2, 1, 14);
  CFG_INT(AVG_SPEED_DEC_DIGITS, "AVG_SPD_DEC", 0, 0, 4);
  CFG_INT(MAX_SPEED_INT_DIGITS, "MAX_SPD_INT", 3, 1, 14);
  CFG_INT(MAX_SPEED_DEC_DIGITS, "MAX_SPD_DEC", 0, 0, 4);
  CFG_INT(FUEL_INT_DIGITS, "FUEL_INT", 1, 1, 14);
  CFG_INT(FUEL_DEC_DIGITS, "FUEL_DEC", 1, 0, 4);
  CFG_INT(ODO_INT_DIGITS, "ODO_INT", 5, 1, 14);
  CFG_INT(ODO_DEC_DIGITS, "ODO_DEC", 1, 0, 4);

  CFG_STR(WIFI_SSID, "WIFI_SSID", "");
  // WiFi passwords are handled manually (below): never serialized back to the
  // web API, and an empty posted value means "keep the stored one".
  CFG_STR(WIFI_SSID_1, "WIFI_S1", "");
  CFG_STR(WIFI_SSID_2, "WIFI_S2", "");
  CFG_STR(WIFI_SSID_3, "WIFI_S3", "");
  CFG_STR(WIFI_SSID_4, "WIFI_S4", "");
  CFG_STR(AP_PASSWORD, "AP_PWD", AP_PASSWORD_DEFAULT);
  CFG_INT(WIFI_ATTEMPT_SECONDS, "WIFI_ATTEM_S", 12, 5, 60);
  CFG_BOOL(NTP_ENABLED, "NTP_EN", true);
  CFG_STR(NTP_SERVER, "NTP_SRV", "pool.ntp.org");
  CFG_INT(TZ_OFFSET_HOURS, "TZ_OFFSET", 1, -14, 14);
  CFG_BOOL(TZ_DST_ENABLED, "TZ_DST", true);
  CFG_INT(TZ_DST_RULE, "TZ_RULE", 1, 0, 2);  // 1 = TZ_DST_RULE_EU

  CFG_BOOL(OTA_PULL_ENABLED, "OTA_PULL_EN", false);
  CFG_STR(OTA_PULL_URL, "OTA_PULL_URL", "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest");
  CFG_STR(VERSION_OVERRIDE, "VER_OVR", "");

  // WiFi passwords: mode 1 sends empty strings so they never leave the device,
  // mode 2 keeps the stored value when the posted password is empty.
  {
    char *pwds[5] = { WIFI_PASSWORD, WIFI_PASSWORD_1, WIFI_PASSWORD_2,
                      WIFI_PASSWORD_3, WIFI_PASSWORD_4 };
    const char *keys[5] = { "WIFI_PASSWORD", "WIFI_PASSWORD_1",
                            "WIFI_PASSWORD_2", "WIFI_PASSWORD_3",
                            "WIFI_PASSWORD_4" };
    const char *nvs[5] = { "WIFI_PWD", "WIFI_P1", "WIFI_P2", "WIFI_P3",
                           "WIFI_P4" };
    const char *defs[5] = { "", "", "", "", "" };
    for (int i = 0; i < 5; i++) {
      if (mode == 0) {
        size_t cb = pref.getString(nvs[i], pwds[i], 64);
        pwds[i][63] = 0;
        if (cb == 0) {
          strncpy(pwds[i], defs[i], 63);
          pwds[i][63] = 0;
        }
      } else if (mode == 1) {
        (*doc)[keys[i]] = "";
      } else if (mode == 2 && (*doc)[keys[i]].is<const char *>()) {
        const char *v = (*doc)[keys[i]].as<const char *>();
        if (v && strlen(v) > 0) {
          strncpy(pwds[i], v, 63);
          pwds[i][63] = 0;
          // Checked like every other write: a password that never reached flash
          // used to be invisible, and the network then fails to join after a
          // reboot with nothing in the log to point at it. Unchanged passwords
          // are not re-stored - they are the longest strings in the namespace
          // and rewriting them on every save was the churn that filled it.
          if (cfgStrChanged(pref, nvs[i], pwds[i]) &&
              nvsWriteFailed(nvs[i], pref.putString(nvs[i], pwds[i])))
            cfgNvsWriteErrors++;
        }
      }
    }
  }

  if (mode == 0 || mode == 2) {
    sanitizeDigitCounts();
    // Bands live in the CFG_INT/CFG_FLT/CFG_UINT lines above - the same table
    // covers the NVS load, POST /api/config and the backup restore. What is
    // left here is the validation that needs more than one parameter, plus the
    // string checks that no numeric band can express.
    sanitizeConfigPairs();

    // The Dashboard_Config hotspot is started on every boot, and the Wi-Fi
    // stack refuses a 1-7 character passphrase outright (framework-
    // arduinoespressif32 3.3.12, libraries/WiFi/src/AP.cpp: "passphrase too
    // short!" -> softAP() returns false). A short password would therefore
    // kill the only way back into a misconfigured unit, with nothing but a
    // Serial log line to show for it. Empty means an open network, which is
    // allowed but warned about in the WebUI. Reject 1-7 characters on both
    // load and save, fall back to the shipped default, and say why.
    size_t apPwdLen = strlen(AP_PASSWORD);
    if (apPwdLen > 0 && apPwdLen < 8) {
      logPrintf("Config: AP_PASSWORD too short (%d chars) - using default\n",
                (int)apPwdLen);
      strncpy(AP_PASSWORD, AP_PASSWORD_DEFAULT, sizeof(AP_PASSWORD) - 1);
      AP_PASSWORD[sizeof(AP_PASSWORD) - 1] = 0;
      // Mode 2 already wrote the rejected value to NVS through CFG_STR,
      // so overwrite it here rather than re-sanitizing on every boot.
      if (mode == 2 && nvsWriteFailed("AP_PWD", pref.putString("AP_PWD", AP_PASSWORD)))
        cfgNvsWriteErrors++;
    }
  }

  // The fuel table moved to the ohm domain with issue #18 and got fresh NVS keys
  // (FCO_n). The legacy capacitive-era keys (TCH_n, raw ADC counts) are simply
  // never read again - CFG_VER is deliberately NOT bumped for it, because that
  // would wipe every other user setting on existing units. An old backup file
  // cannot re-inject ADC counts either: it carries "touchTable", which this code
  // ignores.
  if (mode == 0) {
    char key[8];
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%d", i);
      float v = pref.getFloat(key, fuelCalOhms[i]);
      fuelCalOhms[i] = constrain(v, 0.0f, 100000.0f);
    }
  } else if (mode == 1) {
    JsonArray arr = (*doc)["fuelCalOhms"].to<JsonArray>();
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++)
      arr.add(roundf(fuelCalOhms[i] * 10.0f) / 10.0f);
  } else if (mode == 2) {
    char key[8];
    int written = 0;
    if (!(*doc)["fuelCalOhms"].isNull()) {
      JsonArray arr = (*doc)["fuelCalOhms"].as<JsonArray>();
      written = (int)arr.size();
      if (written > FUEL_TOUCH_POINTS) written = FUEL_TOUCH_POINTS;
      for (int i = 0; i < written; i++) {
        // Calibration points are sender resistances: a negative or absurd value
        // can never be measured and would silently kill the segment it belongs
        // to (issue #21).
        float v = (*doc)["fuelCalOhms"][i].as<float>();
        if (!(v >= 0.0f)) v = 0.0f;  // also catches NaN
        fuelCalOhms[i] = constrain(v, 0.0f, 100000.0f);
        snprintf(key, sizeof(key), "FCO_%d", i);
        if (nvsWriteFailed(key, cfgPutFloat(pref, key, fuelCalOhms[i])))
          cfgNvsWriteErrors++;
      }
    }
    // Points beyond the uploaded array (or a missing array) keep their
    // previous values; write them to NVS so FUEL_TOUCH_POINTS above the
    // array length never leaves missing keys (a missing key loads as 0
    // and breaks the fuel gauge).
    for (int i = written; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%d", i);
      if (nvsWriteFailed(key, cfgPutFloat(pref, key, fuelCalOhms[i])))
        cfgNvsWriteErrors++;
    }
  }

  // Changing the point count invalidates the stored ramp (the extra slots hold
  // values from a different tank height), so rebuild it from the entered
  // empty/full ohms instead of letting a stale or zero slot define full. Only on
  // a save: at boot the NVS count and the NVS ramp were written together, so
  // regenerating there would wipe a calibration the user already did.
  if (mode == 2 && FUEL_TOUCH_POINTS != prevFuelPoints) {
    fillFuelTableFromOhms("point count changed");
    char key[8];
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%d", i);
      if (nvsWriteFailed(key, cfgPutFloat(pref, key, fuelCalOhms[i])))
        cfgNvsWriteErrors++;
    }
  }

  // A table that is not monotonic in one direction leaves gaps the gauge walk
  // cannot match (issue #19): repair it before it reaches NVS, so a broken
  // upload or an old NVS ramp is fixed once and stays fixed across reboots.
  if (mode == 0 || mode == 2) {
    if (repairFuelTable(mode == 0 ? "loaded from NVS at boot"
                                  : "uploaded through the Web UI")) {
      char key[8];
      for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
        snprintf(key, sizeof(key), "FCO_%d", i);
        if (nvsWriteFailed(key, cfgPutFloat(pref, key, fuelCalOhms[i])))
          cfgNvsWriteErrors++;
      }
    }
  }

  // The namespace itself closes when `session` goes out of scope (and takes
  // prefsMux with it).
  if (mode == 2 && cfgNvsWriteErrors) {
    logPrintf("Config save: %d parameter(s) failed to reach NVS (%s); they will revert on reboot\n",
              cfgNvsWriteErrors, cfgNvsFailedKeys[0] ? cfgNvsFailedKeys : "names not recorded");
    logNvsStats();
  }
  cfgSaveActive = false;
  applyColors();
}

// ----------------------------------------------------------------------------
// Factory default configuration (dashboard_backup.json)
// ----------------------------------------------------------------------------
// The reference backup export of the standard configuration. Seeded into NVS
// on first boot (and once for units still on the pre-factory-defaults
// config), so every unit starts from exactly these values.
// WiFi credentials are deliberately absent from this list: a factory-default
// seed must not strand a unit without a network - the same reason
// factoryResetConfig() puts the SSID/password keys back after wiping NVS, and
// the reason the password keys were already left out. Listing the SSIDs as
// empty strings meant every boot that found CFG_VER < 5 cleared all five SSIDs
// in RAM and NVS, and because the CFG_VER stamp was never verified (see
// seedNVSWithFactoryDefaults) a lost stamp repeated that wipe on every boot:
// passwords survived, SSIDs did not.
const char FACTORY_DEFAULT_JSON[] = R"({
  "DISPLAY_ROTATION": 1,
  "UNITS_IMPERIAL": false,
  "SPI_BUS_SPEED": 60000000,
  "DISPLAY_WIDTH": 480,
  "DISPLAY_HEIGHT": 320,
  "TARGET_FPS": 60,
  "BACKLIGHT_BRIGHTNESS": 100,
  "ENABLE_AUTO_BRIGHTNESS": true,
  "LIGHT_SENSOR_DARK_VAL": 432,
  "LIGHT_SENSOR_BRIGHT_VAL": 2851,
  "AUTO_BRIGHT_DARK": 13,
  "AUTO_BRIGHT_LIGHT": 100,
  "AUTO_BRIGHT_FADE_MS": 4000,
  "FADE_DURATION_MS": 700,
  "SPLASH_SIGNATURE": "by @ale.finot",
  "REBOOT_SIGNATURE": "Dashboard++ by @ale.finot",
  "DASHBOARD_SIGNATURE": "<<<<<<    Dashboard++ by @ale.finot    >>>>>>",
  "TEMP_BAR_MIN": 10,
  "TEMP_BAR_MAX": 110,
  "TEMP_WARN_RED": 90,
  "TEMP_WARN_YEL": 45,
  "FUEL_WARN_RED": 20,
  "FUEL_WARN_YEL": 45,
  "COLOR_TEMP_NORM": "#00ff00",
  "COLOR_TEMP_WARN": "#ffff00",
  "COLOR_TEMP_CRIT": "#ff0000",
  "COLOR_FUEL_NORM": "#00ff00",
  "COLOR_FUEL_WARN": "#ffff00",
  "COLOR_FUEL_CRIT": "#ff0000",
  "GHOST_COLOR_STR": "#474747",
  "WHEEL_CIRCUMFERENCE_MM": 1650,
  "FUEL_FILTER_ALPHA": 0.08,
  "TRIP_RESET_HOLD_MS": 1500,
  "FUEL_TOUCH_POINTS": 8,
  "FUEL_INPUT_ENABLED": false,
  "FUEL_EXC_RES_OHM": 220,
  "FUEL_ADC_VREF": 3.3,
  "FUEL_OHM_EMPTY": 10.0,
  "FUEL_OHM_FULL": 180.0,
  "FUEL_OVERSAMPLE": 16,
  "BATTERY_SCALE": 5.7,
  "BATTERY_OFFSET": 0.2,
  "NTC_R_BALANCE": 10000,
  "NTC_R25": 10000,
  "NTC_BETA": 3950,
  "NTC_TEMP_OFFSET": 0,
  "GPS_BAUD": 115200,
  "MIN_SATELLITES": 8,
  "OPTIMAL_SATELLITES": 12,
  "MAX_SPEED_DELTA_KMH": 5,
  "MIN_SPEED_THRESHOLD": 1,
  "GPS_START_KMH": 3,
  "GPS_STOP_SETTLE_MS": 1500,
  "GPS_MIN_DEV_KMH": 1,
  "SPEED_SOURCE_MODE": 2,
  "SPEED_SOURCE_HOLD_MS": 500,
  "HALL_MEDIAN_SAMPLES": 3,
  "HALL_PERIOD_GUARD": 8,
  "HALL_PULSE_MIN_US": 150,
  "ACCEL_START_SPEED": 1,
  "ACCEL_TARGET_SPEED": 50,
  "ACCEL_MAX_TIME": 9.99,
  "ACCEL_BADGE_LINE1": "0-50",
  "ACCEL_BADGE_LINE2": "km/h",
  "BIG_CENTER_X": 240,
  "BIG_CENTER_Y": 160,
  "OFFSET_BIG_TIME_X": 107,
  "OFFSET_BIG_TIME_Y": -91,
  "OFFSET_BIG_DATE_X": -131,
  "OFFSET_BIG_DATE_Y": -91,
  "OFFSET_BIG_SIGNATURE_X": 0,
  "OFFSET_BIG_SIGNATURE_Y": -75,
  "OFFSET_BIG_SPEED_NUM_X": 0,
  "OFFSET_BIG_SPEED_NUM_Y": -3,
  "OFFSET_BIG_SPEED_UNIT_X": 106,
  "OFFSET_BIG_SPEED_UNIT_Y": 56,
  "OFFSET_BIG_ODO_X": 22,
  "OFFSET_BIG_ODO_Y": 126,
  "OFFSET_BIG_SAT_X": 179,
  "OFFSET_BIG_SAT_Y": -114,
  "OFFSET_BIG_TMR_X": -53,
  "OFFSET_BIG_TMR_Y": -46,
  "OFFSET_BIG_BAT_X": -112,
  "OFFSET_BIG_BAT_Y": 123,
  "SIDEBAR_LEFT_X": 10,
  "SIDEBAR_LEFT_Y": 95,
  "SIDEBAR_RIGHT_X": 462,
  "SIDEBAR_RIGHT_Y": 95,
  "OFFSET_HALL_ICON_X": 0,
  "OFFSET_HALL_ICON_Y": -100,
  "OFFSET_WIFI_ICON_X": 204,
  "OFFSET_WIFI_ICON_Y": -108,
  "OFFSET_INST_KML_X": 60,
  "OFFSET_INST_KML_Y": -25,
  "OFFSET_AVG_KML_X": 160,
  "OFFSET_AVG_KML_Y": -25,
  "OFFSET_AVG_SPEED_X": -163,
  "OFFSET_AVG_SPEED_Y": -25,
  "OFFSET_MAX_SPEED_X": -60,
  "OFFSET_MAX_SPEED_Y": -25,
  "OFFSET_FUEL_LTRS_X": 132,
  "OFFSET_FUEL_LTRS_Y": 123,
  "SIDEBAR_BAR_WIDTH": 8,
  "SIDEBAR_BAR_HEIGHT": 190,
  "SHOW_ELEMENT_BOUNDS": false,
  "SHOW_ELEMENT_SPEED": true,
  "SHOW_ELEMENT_SPEED_UNIT": true,
  "SHOW_ELEMENT_SIGNATURE": true,
  "SHOW_ELEMENT_SPEED_SOURCE": true,
  "SHOW_ELEMENT_WIFI": true,
  "SHOW_ELEMENT_TIME": true,
  "SHOW_ELEMENT_DATE": true,
  "SHOW_ELEMENT_ODO": true,
  "SHOW_ELEMENT_SIDEBAR_TEMP": true,
  "SHOW_ELEMENT_SIDEBAR_FUEL": true,
  "SHOW_ELEMENT_SAT": true,
  "SHOW_ELEMENT_TMR": true,
  "SHOW_ELEMENT_BAT": true,
  "SHOW_ELEMENT_INST_KML": true,
  "SHOW_ELEMENT_AVG_KML": true,
  "SHOW_ELEMENT_AVG_SPEED": true,
  "SHOW_ELEMENT_MAX_SPEED": true,
  "SHOW_ELEMENT_FUEL_LTRS": true,
  "SHOW_GHOST_DIGITS": true,
  "SHOW_ELEMENT_WEATHER": true,
  "OFFSET_WEATHER_X": 0,
  "OFFSET_WEATHER_Y": 146,
  "WEATHER_CITY": "",
  "WEATHER_LAT": 0,
  "WEATHER_LON": 0,
  "WEATHER_REFRESH_MIN": 1,
  "WEATHER_LOCALE": "it",
  "ENABLE_POWER_SENSE": false,
  "POWER_SENSE_OFF_MS": 10000,
  "ENABLE_CIRCLE_TEST": false,
  "ENABLE_DEMO_MODE": false,
  "ADV_MODE": false,
  "ENABLE_ANTIALIASING": true,
  "AA_SHARPNESS": 0.2,
  "SHOW_FPS_COUNTER_DEFAULT": false,
  "GPS_DEBUG_DEFAULT": false,
  "ENABLE_DYNAMIC_CPU": false,
  "MANUAL_CPU_FREQ": 240,
  "ENABLE_CPU_THROTTLE": false,
  "CPU_THROTTLE_TEMP_WARN": 50,
  "CPU_THROTTLE_TEMP_CRIT": 60,
  "ENABLE_NIGHT_MODE": false,
  "NIGHT_MODE_START_HOUR": 23,
  "NIGHT_MODE_END_HOUR": 0,
  "NIGHT_BACKLIGHT": 29,
  "DISPLAY_INVERT_COLORS": false,
  "OFFSET_BIG_FPS_X": -9,
  "OFFSET_BIG_FPS_Y": -7,
  "REFRESH_SPEED_MS": 250,
  "REFRESH_BAT_MS": 2500,
  "REFRESH_INST_MS": 500,
  "REFRESH_MAX_SPEED_MS": 500,
  "REFRESH_FUEL_MS": 1000,
  "SPEED_DIGITS": 2,
  "SAT_DIGITS": 2,
  "TMR_INT_DIGITS": 1,
  "TMR_DEC_DIGITS": 2,
  "BAT_INT_DIGITS": 2,
  "BAT_DEC_DIGITS": 1,
  "INST_INT_DIGITS": 2,
  "INST_DEC_DIGITS": 1,
  "AVG_INT_DIGITS": 2,
  "AVG_DEC_DIGITS": 1,
  "AVG_SPEED_INT_DIGITS": 2,
  "AVG_SPEED_DEC_DIGITS": 0,
  "MAX_SPEED_INT_DIGITS": 3,
  "MAX_SPEED_DEC_DIGITS": 0,
  "FUEL_INT_DIGITS": 1,
  "FUEL_DEC_DIGITS": 1,
  "ODO_INT_DIGITS": 5,
  "ODO_DEC_DIGITS": 1,
  "WIFI_ATTEMPT_SECONDS": 12,
  "NTP_ENABLED": true,
  "NTP_SERVER": "pool.ntp.org",
  "TZ_OFFSET_HOURS": 1,
  "TZ_DST_ENABLED": true,
  "TZ_DST_RULE": 1,
  "OTA_PULL_ENABLED": false,
  "OTA_PULL_URL": "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest",
  "VERSION_OVERRIDE": "",
  "fuelCalOhms": [
    10.0,
    34.3,
    58.6,
    82.9,
    107.1,
    131.4,
    155.7,
    180.0
  ]
})";

// Write the factory defaults above into NVS (and RAM). WiFi passwords are
// deliberately not part of the seed: they stay whatever is already stored.
void seedNVSWithFactoryDefaults() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, FACTORY_DEFAULT_JSON);
  if (err) {
    logPrintf("Config seed: factory defaults JSON parse error: %s\n", err.c_str());
    return;
  }
  processConfig(2, &doc);
  NvsSession session("cfg", false);
  if (session.opened()) {
    // The stamp is what keeps this a one-shot. A lost stamp used to re-seed the
    // whole config on every boot; the WiFi credentials are no longer part of the
    // seed, but a repeated seed would still undo every other setting.
    if (nvsWriteFailed("CFG_VER", session.nvs.putInt("CFG_VER", 5)))
      logPrintf("Config: CFG_VER not stored - the factory seed will run again at next boot\n");
    // Seed every calibration key so FUEL_TOUCH_POINTS can be raised above 8
    // without missing NVS entries (a missing key loads as 0).
    char key[8];
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%d", i);
      if (nvsWriteFailed(key, session.nvs.putFloat(key, fuelCalOhms[i])))
        cfgNvsWriteErrors++;
    }
  }
  if (cfgNvsWriteErrors)
    logPrintf("Config v5 seed: %d write(s) failed - see the NVS lines above\n", cfgNvsWriteErrors);
  else
    logPrintf("Config v5: NVS seeded with factory defaults (dashboard_backup.json)\n");
}

// Boot-time answer to "did the network credentials actually get stored?": the
// SSIDs as NVS handed them back, plus whether each password slot is filled. The
// passwords themselves are never logged.
void logStoredWifiProfiles() {
  const char *ssids[5] = {WIFI_SSID, WIFI_SSID_1, WIFI_SSID_2, WIFI_SSID_3, WIFI_SSID_4};
  const char *pwds[5] = {WIFI_PASSWORD, WIFI_PASSWORD_1, WIFI_PASSWORD_2,
                         WIFI_PASSWORD_3, WIFI_PASSWORD_4};
  char line[192];
  int n = 0;
  line[0] = 0;
  for (int i = 0; i < 5; i++) {
    int written = snprintf(line + n, sizeof(line) - n, " [%d]=%s%s", i,
                           ssids[i][0] ? ssids[i] : "(no SSID)",
                           pwds[i][0] ? "/pw set" : "/pw empty");
    if (written < 0 || (size_t)written >= sizeof(line) - n) break;
    n += written;
  }
  logPrintf("WiFi credentials from NVS:%s\n", line);
  logNvsStats();
}

void recalculateDerivedParams() {
  // TARGET_FPS is band-checked to 5..120 (issue #36), so the frame budget is
  // always 8..200 ms: the old "0 means unlimited" branch and the <2 ms clamp
  // could never be reached, and a 2 ms budget would only saturate the display
  // core.
  DISPLAY_REFRESH_MS = 1000U / (unsigned long)TARGET_FPS;
  WHEEL_SPEED_FACTOR = WHEEL_CIRCUMFERENCE_MM * 3600.0f;
  WHEEL_DIST_PER_PULSE_KM = (double)WHEEL_CIRCUMFERENCE_MM / 1000000.0;
  NTC_INV_ROOM_KELVIN = 1.0f / (25.0f + 273.15f);
  ADC_VOLTS_FACTOR = 3.3f / 4095.0f;
  showFpsCounter = SHOW_FPS_COUNTER_DEFAULT;
  showGpsDebug = GPS_DEBUG_DEFAULT;
}
