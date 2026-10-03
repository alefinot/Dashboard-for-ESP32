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
int TEMP_WARN_LOW = 20;
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
// Card size in pixels. 480x28 is what the renderer used to hardcode, so a unit
// that never touches the new sliders keeps the picture it already has.
int WEATHER_WIDTH = 480;
int WEATHER_HEIGHT = 28;
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
const char FW_VERSION[] = "1.4.2";
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

// ----------------------------------------------------------------------------
// The parameter table
// ----------------------------------------------------------------------------
// The same three operations run over every parameter: mode 0 loads it from NVS
// at boot, mode 1 serializes it into GET /api/config, mode 2 applies a posted
// value (POST /api/config, a backup restore, the factory defaults) and writes it
// back to NVS only when it really changed. Each parameter used to be written out
// inline as a CFG_INT/CFG_FLT/CFG_STR/CFG_BOOL line - 197 of them, three modes
// and a clamp per line, ~39 KB of .text for what is one switch. The rules are
// unchanged: bands enforced on both the load path and the save path, non-finite
// floats fall back to the shipped default, unsigned values are validated through
// a signed window, and a value that already matches NVS is never rewritten.
enum CfgKind : uint8_t {
  CK_INT,
  CK_UINT,
  CK_FLT,
  CK_BOOL,
  CK_STR,
  CK_SECRET,  // WiFi passphrases: mode 1 sends an empty string so they never
              // leave the device, and a blank posted value means "keep what is
              // stored" instead of "erase it".
};

struct CfgParam {
  void       *ptr;      // storage of the global
  const char *name;     // JSON key, == the C++ identifier; also the log name
  const char *nvsKey;   // key inside the "cfg" NVS namespace
  const char *dstr;     // default for CK_STR / CK_SECRET
  int32_t     lo, hi;   // band for CK_INT / CK_UINT
  int32_t     di;       // default for CK_INT / CK_UINT / CK_BOOL
  float       flo, fhi; // band for CK_FLT
  float       df;       // default for CK_FLT
  uint16_t    size;     // buffer size for CK_STR / CK_SECRET (0 otherwise)
  uint8_t     kind;     // CfgKind
};

// Row order is the order the old CFG_* calls ran in: it decides the order of the
// NVS reads and writes, the order of the clamp log lines, and the order of the
// keys in the JSON the WebUI receives. The rows were generated from that call
// list and both lists were diffed field by field (global, NVS key, default,
// band) before the macros were removed - see scripts/cfg_table_migration.py.
static const CfgParam CFG_TABLE[] = {
  { &DISPLAY_ROTATION, "DISPLAY_ROTATION", "DISP_ROT", nullptr, 0, 3, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &UNITS_IMPERIAL, "UNITS_IMPERIAL", "UNITS_IMP", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &ADV_MODE, "ADV_MODE", "ADV_MODE", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SPI_BUS_SPEED, "SPI_BUS_SPEED", "SPI_FREQ", nullptr, SPI_SPEED_MIN_HZ, SPI_SPEED_MAX_HZ, 60000000, 0.0f, 0.0f, 0.0f, 0, CK_UINT },

  { &DISPLAY_WIDTH, "DISPLAY_WIDTH", "DISP_W", nullptr, 16, 2048, 480, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &DISPLAY_HEIGHT, "DISPLAY_HEIGHT", "DISP_H", nullptr, 16, 2048, 320, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TARGET_FPS, "TARGET_FPS", "TGT_FPS", nullptr, 5, 120, 60, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &BACKLIGHT_BRIGHTNESS, "BACKLIGHT_BRIGHTNESS", "BL_BRIGHT", nullptr, 0, 100, 100, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ENABLE_AUTO_BRIGHTNESS, "ENABLE_AUTO_BRIGHTNESS", "EN_AUTO_BL", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &LIGHT_SENSOR_DARK_VAL, "LIGHT_SENSOR_DARK_VAL", "LIGHT_DARK", nullptr, 0, 4095, 432, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &LIGHT_SENSOR_BRIGHT_VAL, "LIGHT_SENSOR_BRIGHT_VAL", "LIGHT_BRIGHT", nullptr, 0, 4095, 2851, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AUTO_BRIGHT_DARK, "AUTO_BRIGHT_DARK", "AB_DARK", nullptr, 0, 100, 13, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AUTO_BRIGHT_LIGHT, "AUTO_BRIGHT_LIGHT", "AB_LIGHT", nullptr, 0, 100, 100, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AUTO_BRIGHT_FADE_MS, "AUTO_BRIGHT_FADE_MS", "AB_FADE", nullptr, 0, 60000, 4000, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FADE_DURATION_MS, "FADE_DURATION_MS", "FADE_DUR", nullptr, 0, 10000, 700, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SPLASH_SIGNATURE, "SPLASH_SIGNATURE", "SPLASH_SIG", "by @ale.finot", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(SPLASH_SIGNATURE), CK_STR },

  { &REBOOT_SIGNATURE, "REBOOT_SIGNATURE", "REBOOT_SIG", "Dashboard++ by @ale.finot", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(REBOOT_SIGNATURE), CK_STR },

  { &DASHBOARD_SIGNATURE, "DASHBOARD_SIGNATURE", "DASH_SIG", "<<<<<<    Dashboard++ by @ale.finot    >>>>>>", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(DASHBOARD_SIGNATURE), CK_STR },

  { &TEMP_BAR_MIN, "TEMP_BAR_MIN", "TMP_BAR_MIN", nullptr, -40, 300, 10, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TEMP_BAR_MAX, "TEMP_BAR_MAX", "TMP_BAR_MAX", nullptr, -40, 300, 110, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TEMP_WARN_RED, "TEMP_WARN_RED", "TMP_WRN_R", nullptr, -40, 300, 90, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TEMP_WARN_YEL, "TEMP_WARN_YEL", "TMP_WRN_Y", nullptr, -40, 300, 45, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TEMP_WARN_LOW, "TEMP_WARN_LOW", "TMP_WRN_L", nullptr, -40, 300, 20, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_WARN_RED, "FUEL_WARN_RED", "FUL_WRN_R", nullptr, 0, 100, 20, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_WARN_YEL, "FUEL_WARN_YEL", "FUL_WRN_Y", nullptr, 0, 100, 45, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &COLOR_TEMP_NORM, "COLOR_TEMP_NORM", "C_TMP_N", "#00ff00", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_TEMP_NORM), CK_STR },

  { &COLOR_TEMP_WARN, "COLOR_TEMP_WARN", "C_TMP_W", "#ffff00", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_TEMP_WARN), CK_STR },

  { &COLOR_TEMP_CRIT, "COLOR_TEMP_CRIT", "C_TMP_C", "#ff0000", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_TEMP_CRIT), CK_STR },

  { &COLOR_FUEL_NORM, "COLOR_FUEL_NORM", "C_FUL_N", "#00ff00", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_FUEL_NORM), CK_STR },

  { &COLOR_FUEL_WARN, "COLOR_FUEL_WARN", "C_FUL_W", "#ffff00", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_FUEL_WARN), CK_STR },

  { &COLOR_FUEL_CRIT, "COLOR_FUEL_CRIT", "C_FUL_C", "#ff0000", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(COLOR_FUEL_CRIT), CK_STR },

  { &GHOST_COLOR_STR, "GHOST_COLOR_STR", "GHOST_C", "#474747", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(GHOST_COLOR_STR), CK_STR },

  { &WHEEL_CIRCUMFERENCE_MM, "WHEEL_CIRCUMFERENCE_MM", "WHL_CIRC", nullptr, 0, 0, 0, 50.0f, 10000.0f, 1650.0f, 0, CK_FLT },

  { &FUEL_FILTER_ALPHA, "FUEL_FILTER_ALPHA", "FUEL_FILT", nullptr, 0, 0, 0, 0.001f, 1.0f, 0.08f, 0, CK_FLT },

  { &TRIP_RESET_HOLD_MS, "TRIP_RESET_HOLD_MS", "TRP_RST_H", nullptr, 200, 10000, 1500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_TOUCH_POINTS, "FUEL_TOUCH_POINTS", "FTL_PTS", nullptr, 2, 20, 8, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_INPUT_ENABLED, "FUEL_INPUT_ENABLED", "FUEL_EN", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &FUEL_EXC_RES_OHM, "FUEL_EXC_RES_OHM", "FUEL_EXR", nullptr, 10, 100000, 220, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_ADC_VREF, "FUEL_ADC_VREF", "FUEL_VRF", nullptr, 0, 0, 0, 2.5f, 4.2f, 3.30f, 0, CK_FLT },

  { &FUEL_OHM_EMPTY, "FUEL_OHM_EMPTY", "FUEL_OE", nullptr, 0, 0, 0, 0.0f, 100000.0f, 10.0f, 0, CK_FLT },

  { &FUEL_OHM_FULL, "FUEL_OHM_FULL", "FUEL_OF", nullptr, 0, 0, 0, 0.0f, 100000.0f, 180.0f, 0, CK_FLT },

  { &FUEL_OVERSAMPLE, "FUEL_OVERSAMPLE", "FUEL_OSM", nullptr, 1, 64, 16, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &BATTERY_SCALE, "BATTERY_SCALE", "BAT_SCALE", nullptr, 0, 0, 0, 0.01f, 1000.0f, 5.7f, 0, CK_FLT },

  { &BATTERY_OFFSET, "BATTERY_OFFSET", "BAT_OFFS", nullptr, 0, 0, 0, -50.0f, 50.0f, 0.2f, 0, CK_FLT },

  { &NTC_R_BALANCE, "NTC_R_BALANCE", "NTC_BAL", nullptr, 0, 0, 0, 100.0f, 10000000.0f, 10000.0f, 0, CK_FLT },

  { &NTC_R25, "NTC_R25", "NTC_R25", nullptr, 0, 0, 0, 100.0f, 10000000.0f, 10000.0f, 0, CK_FLT },

  { &NTC_BETA, "NTC_BETA", "NTC_BETA", nullptr, 0, 0, 0, 1000.0f, 20000.0f, 3950.0f, 0, CK_FLT },

  { &NTC_TEMP_OFFSET, "NTC_TEMP_OFFSET", "NTC_OFFS", nullptr, 0, 0, 0, -50.0f, 50.0f, 0.0f, 0, CK_FLT },

  { &GPS_BAUD, "GPS_BAUD", "GPS_BAUD", nullptr, 1200, 921600, 115200, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &MIN_SATELLITES, "MIN_SATELLITES", "MIN_SAT", nullptr, 1, 32, 8, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OPTIMAL_SATELLITES, "OPTIMAL_SATELLITES", "OPT_SAT", nullptr, 1, 32, 12, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &MAX_SPEED_DELTA_KMH, "MAX_SPEED_DELTA_KMH", "MAX_SPD_DELT", nullptr, 0, 0, 0, 0.01f, 100.0f, 5.0f, 0, CK_FLT },

  { &MIN_SPEED_THRESHOLD, "MIN_SPEED_THRESHOLD", "MIN_SPD_THR", nullptr, 0, 0, 0, 0.0f, 50.0f, 1.0f, 0, CK_FLT },

  { &GPS_START_KMH, "GPS_START_KMH", "GPS_START", nullptr, 0, 0, 0, 0.0f, 200.0f, 3.0f, 0, CK_FLT },

  { &GPS_STOP_SETTLE_MS, "GPS_STOP_SETTLE_MS", "GPS_STL_MS", nullptr, 0, 60000, 1500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &GPS_MIN_DEV_KMH, "GPS_MIN_DEV_KMH", "GPS_MIN_DV", nullptr, 0, 0, 0, 0.0f, 50.0f, 1.0f, 0, CK_FLT },

  { &SPEED_SOURCE_MODE, "SPEED_SOURCE_MODE", "SPD_SRC_MODE", nullptr, 0, 2, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SPEED_SOURCE_HOLD_MS, "SPEED_SOURCE_HOLD_MS", "SPD_SRC_HOLD", nullptr, 0, 10000, 500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &HALL_MEDIAN_SAMPLES, "HALL_MEDIAN_SAMPLES", "HALL_MED_N", nullptr, 1, 31, 3, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &HALL_PERIOD_GUARD, "HALL_PERIOD_GUARD", "HALL_PRD_GRD", nullptr, 1, 100, 8, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &HALL_PULSE_MIN_US, "HALL_PULSE_MIN_US", "HALL_PL_MIN", nullptr, 10, 1000, 150, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ACCEL_START_SPEED, "ACCEL_START_SPEED", "ACC_STRT", nullptr, 0, 0, 0, 0.0f, 200.0f, 1.0f, 0, CK_FLT },

  { &ACCEL_TARGET_SPEED, "ACCEL_TARGET_SPEED", "ACC_TGT", nullptr, 0, 0, 0, 1.0f, 400.0f, 50.0f, 0, CK_FLT },

  { &ACCEL_MAX_TIME, "ACCEL_MAX_TIME", "ACC_MAX_T", nullptr, 0, 0, 0, 0.1f, 999.99f, 9.99f, 0, CK_FLT },

  { &ACCEL_BADGE_LINE1, "ACCEL_BADGE_LINE1", "ACC_BDG_1", "0-50", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(ACCEL_BADGE_LINE1), CK_STR },

  { &ACCEL_BADGE_LINE2, "ACCEL_BADGE_LINE2", "ACC_BDG_2", "km/h", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(ACCEL_BADGE_LINE2), CK_STR },

  { &BIG_CENTER_X, "BIG_CENTER_X", "BCX", nullptr, 0, 4095, 240, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &BIG_CENTER_Y, "BIG_CENTER_Y", "BCY", nullptr, 0, 4095, 160, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_TIME_X, "OFFSET_BIG_TIME_X", "O_BTIME_X", nullptr, -4096, 4096, 107, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_TIME_Y, "OFFSET_BIG_TIME_Y", "O_BTIME_Y", nullptr, -4096, 4096, -91, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_DATE_X, "OFFSET_BIG_DATE_X", "O_BDATE_X", nullptr, -4096, 4096, -131, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_DATE_Y, "OFFSET_BIG_DATE_Y", "O_BDATE_Y", nullptr, -4096, 4096, -91, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SIGNATURE_X, "OFFSET_BIG_SIGNATURE_X", "O_BSIG_X", nullptr, -4096, 4096, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SIGNATURE_Y, "OFFSET_BIG_SIGNATURE_Y", "O_BSIG_Y", nullptr, -4096, 4096, -75, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SPEED_NUM_X, "OFFSET_BIG_SPEED_NUM_X", "O_BSN_X", nullptr, -4096, 4096, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SPEED_NUM_Y, "OFFSET_BIG_SPEED_NUM_Y", "O_BSN_Y", nullptr, -4096, 4096, -3, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SPEED_UNIT_X, "OFFSET_BIG_SPEED_UNIT_X", "O_BSU_X", nullptr, -4096, 4096, 106, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SPEED_UNIT_Y, "OFFSET_BIG_SPEED_UNIT_Y", "O_BSU_Y", nullptr, -4096, 4096, 56, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_ODO_X, "OFFSET_BIG_ODO_X", "O_BODO_X", nullptr, -4096, 4096, 22, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_ODO_Y, "OFFSET_BIG_ODO_Y", "O_BODO_Y", nullptr, -4096, 4096, 126, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SAT_X, "OFFSET_BIG_SAT_X", "O_BSAT_X", nullptr, -4096, 4096, 179, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_SAT_Y, "OFFSET_BIG_SAT_Y", "O_BSAT_Y", nullptr, -4096, 4096, -114, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_TMR_X, "OFFSET_BIG_TMR_X", "O_BTMR_X", nullptr, -4096, 4096, -53, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_TMR_Y, "OFFSET_BIG_TMR_Y", "O_BTMR_Y", nullptr, -4096, 4096, -46, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_BAT_X, "OFFSET_BIG_BAT_X", "O_BBAT_X", nullptr, -4096, 4096, -112, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_BAT_Y, "OFFSET_BIG_BAT_Y", "O_BBAT_Y", nullptr, -4096, 4096, 123, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_LEFT_X, "SIDEBAR_LEFT_X", "SBAR_L_X", nullptr, -4096, 4096, 10, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_LEFT_Y, "SIDEBAR_LEFT_Y", "SBAR_L_Y", nullptr, -4096, 4096, 95, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_RIGHT_X, "SIDEBAR_RIGHT_X", "SBAR_R_X", nullptr, -4096, 4096, 462, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_RIGHT_Y, "SIDEBAR_RIGHT_Y", "SBAR_R_Y", nullptr, -4096, 4096, 95, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_HALL_ICON_X, "OFFSET_HALL_ICON_X", "O_HALL_X", nullptr, -4096, 4096, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_HALL_ICON_Y, "OFFSET_HALL_ICON_Y", "O_HALL_Y", nullptr, -4096, 4096, -100, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_WIFI_ICON_X, "OFFSET_WIFI_ICON_X", "O_WIFI_X", nullptr, -4096, 4096, 204, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_WIFI_ICON_Y, "OFFSET_WIFI_ICON_Y", "O_WIFI_Y", nullptr, -4096, 4096, -108, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_INST_KML_X, "OFFSET_INST_KML_X", "O_INST_X", nullptr, -4096, 4096, 60, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_INST_KML_Y, "OFFSET_INST_KML_Y", "O_INST_Y", nullptr, -4096, 4096, -25, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_AVG_KML_X, "OFFSET_AVG_KML_X", "O_AVG_X", nullptr, -4096, 4096, 160, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_AVG_KML_Y, "OFFSET_AVG_KML_Y", "O_AVG_Y", nullptr, -4096, 4096, -25, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_AVG_SPEED_X, "OFFSET_AVG_SPEED_X", "O_AVG_SPD_X", nullptr, -4096, 4096, -163, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_AVG_SPEED_Y, "OFFSET_AVG_SPEED_Y", "O_AVG_SPD_Y", nullptr, -4096, 4096, -25, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_MAX_SPEED_X, "OFFSET_MAX_SPEED_X", "O_MAX_SPD_X", nullptr, -4096, 4096, -60, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_MAX_SPEED_Y, "OFFSET_MAX_SPEED_Y", "O_MAX_SPD_Y", nullptr, -4096, 4096, -25, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_FUEL_LTRS_X, "OFFSET_FUEL_LTRS_X", "O_FLTRS_X", nullptr, -4096, 4096, 132, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_FUEL_LTRS_Y, "OFFSET_FUEL_LTRS_Y", "O_FLTRS_Y", nullptr, -4096, 4096, 123, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_BAR_WIDTH, "SIDEBAR_BAR_WIDTH", "SBAR_W", nullptr, 1, 2048, 8, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SIDEBAR_BAR_HEIGHT, "SIDEBAR_BAR_HEIGHT", "SBAR_H", nullptr, 1, 2048, 190, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SHOW_ELEMENT_BOUNDS, "SHOW_ELEMENT_BOUNDS", "SHW_BNDS", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SPEED, "SHOW_ELEMENT_SPEED", "SH_SPD", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SPEED_UNIT, "SHOW_ELEMENT_SPEED_UNIT", "SH_SPD_UN", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SIGNATURE, "SHOW_ELEMENT_SIGNATURE", "SH_SIG", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SPEED_SOURCE, "SHOW_ELEMENT_SPEED_SOURCE", "SH_SPD_SRC", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_WIFI, "SHOW_ELEMENT_WIFI", "SH_WIFI", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_TIME, "SHOW_ELEMENT_TIME", "SH_TIME", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_DATE, "SHOW_ELEMENT_DATE", "SH_DATE", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_ODO, "SHOW_ELEMENT_ODO", "SH_ODO", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SIDEBAR_TEMP, "SHOW_ELEMENT_SIDEBAR_TEMP", "SH_SB_TMP", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SIDEBAR_FUEL, "SHOW_ELEMENT_SIDEBAR_FUEL", "SH_SB_FUL", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_SAT, "SHOW_ELEMENT_SAT", "SH_SAT", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_TMR, "SHOW_ELEMENT_TMR", "SH_TMR", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_BAT, "SHOW_ELEMENT_BAT", "SH_BAT", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_INST_KML, "SHOW_ELEMENT_INST_KML", "SH_INST", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_AVG_KML, "SHOW_ELEMENT_AVG_KML", "SH_AVG", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_AVG_SPEED, "SHOW_ELEMENT_AVG_SPEED", "SH_AVG_SPD", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_MAX_SPEED, "SHOW_ELEMENT_MAX_SPEED", "SH_MAX_SPD", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_FUEL_LTRS, "SHOW_ELEMENT_FUEL_LTRS", "SH_FUL", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_GHOST_DIGITS, "SHOW_GHOST_DIGITS", "SH_GHOST", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &SHOW_ELEMENT_WEATHER, "SHOW_ELEMENT_WEATHER", "SH_WEATH", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &OFFSET_WEATHER_X, "OFFSET_WEATHER_X", "O_WEATH_X", nullptr, -4096, 4096, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_WEATHER_Y, "OFFSET_WEATHER_Y", "O_WEATH_Y", nullptr, -4096, 4096, 146, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &WEATHER_WIDTH, "WEATHER_WIDTH", "WEATH_W", nullptr, 160, 480, 480, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &WEATHER_HEIGHT, "WEATHER_HEIGHT", "WEATH_H", nullptr, 20, 120, 28, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &WEATHER_CITY, "WEATHER_CITY", "WEATH_CITY", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WEATHER_CITY), CK_STR },

  { &WEATHER_LAT, "WEATHER_LAT", "WEATH_LAT", nullptr, 0, 0, 0, -90.0f, 90.0f, 0.0f, 0, CK_FLT },

  { &WEATHER_LON, "WEATHER_LON", "WEATH_LON", nullptr, 0, 0, 0, -180.0f, 180.0f, 0.0f, 0, CK_FLT },

  { &WEATHER_REFRESH_MIN, "WEATHER_REFRESH_MIN", "WEATH_RFR", nullptr, 1, 1440, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &WEATHER_LOCALE, "WEATHER_LOCALE", "WEATH_LOCALE", "it", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WEATHER_LOCALE), CK_STR },

  { &ENABLE_POWER_SENSE, "ENABLE_POWER_SENSE", "PWR_SNS", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &POWER_SENSE_OFF_MS, "POWER_SENSE_OFF_MS", "PWR_OFF_MS", nullptr, 500, 120000, 10000, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ENABLE_CIRCLE_TEST, "ENABLE_CIRCLE_TEST", "CIRC_TST", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &ENABLE_DEMO_MODE, "ENABLE_DEMO_MODE", "DEMO_MODE", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &ENABLE_ANTIALIASING, "ENABLE_ANTIALIASING", "EN_AA", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &AA_SHARPNESS, "AA_SHARPNESS", "AA_SHARP", nullptr, 0, 0, 0, 0.0f, 1.0f, 0.2f, 0, CK_FLT },

  { &SHOW_FPS_COUNTER_DEFAULT, "SHOW_FPS_COUNTER_DEFAULT", "SHW_FPS", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &GPS_DEBUG_DEFAULT, "GPS_DEBUG_DEFAULT", "GPS_DBG", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &ENABLE_DYNAMIC_CPU, "ENABLE_DYNAMIC_CPU", "DYN_CPU", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &MANUAL_CPU_FREQ, "MANUAL_CPU_FREQ", "MAN_CPU", nullptr, 80, 240, 240, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ENABLE_CPU_THROTTLE, "ENABLE_CPU_THROTTLE", "CPU_THR_EN", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &CPU_THROTTLE_TEMP_WARN, "CPU_THROTTLE_TEMP_WARN", "CPU_THR_W", nullptr, -50, 150, 50, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &CPU_THROTTLE_TEMP_CRIT, "CPU_THROTTLE_TEMP_CRIT", "CPU_THR_C", nullptr, -50, 150, 60, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ENABLE_NIGHT_MODE, "ENABLE_NIGHT_MODE", "EN_NIGHT", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &NIGHT_MODE_START_HOUR, "NIGHT_MODE_START_HOUR", "NGHT_SRT", nullptr, 0, 23, 23, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &NIGHT_MODE_END_HOUR, "NIGHT_MODE_END_HOUR", "NGHT_END", nullptr, 0, 23, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &NIGHT_BACKLIGHT, "NIGHT_BACKLIGHT", "NGHT_BL", nullptr, 0, 100, 29, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &DISPLAY_INVERT_COLORS, "DISPLAY_INVERT_COLORS", "INV_COLORS", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &OFFSET_BIG_FPS_X, "OFFSET_BIG_FPS_X", "O_FPS_X", nullptr, -4096, 4096, -9, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OFFSET_BIG_FPS_Y, "OFFSET_BIG_FPS_Y", "O_FPS_Y", nullptr, -4096, 4096, -7, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &REFRESH_SPEED_MS, "REFRESH_SPEED_MS", "R_SPD", nullptr, 10, 60000, 250, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &REFRESH_BAT_MS, "REFRESH_BAT_MS", "R_BAT", nullptr, 10, 600000, 2500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &REFRESH_INST_MS, "REFRESH_INST_MS", "R_INST", nullptr, 10, 60000, 500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &REFRESH_MAX_SPEED_MS, "REFRESH_MAX_SPEED_MS", "R_MAX_SPD", nullptr, 10, 60000, 500, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &REFRESH_FUEL_MS, "REFRESH_FUEL_MS", "R_FUEL", nullptr, 10, 60000, 1000, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SPEED_DIGITS, "SPEED_DIGITS", "SPD_DIG", nullptr, 1, 4, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &SAT_DIGITS, "SAT_DIGITS", "SAT_DIG", nullptr, 1, 3, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TMR_INT_DIGITS, "TMR_INT_DIGITS", "TMR_INT", nullptr, 1, 14, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TMR_DEC_DIGITS, "TMR_DEC_DIGITS", "TMR_DEC", nullptr, 0, 4, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &BAT_INT_DIGITS, "BAT_INT_DIGITS", "BAT_INT", nullptr, 1, 14, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &BAT_DEC_DIGITS, "BAT_DEC_DIGITS", "BAT_DEC", nullptr, 0, 4, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &INST_INT_DIGITS, "INST_INT_DIGITS", "INST_INT", nullptr, 1, 14, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &INST_DEC_DIGITS, "INST_DEC_DIGITS", "INST_DEC", nullptr, 0, 4, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AVG_INT_DIGITS, "AVG_INT_DIGITS", "AVG_INT", nullptr, 1, 14, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AVG_DEC_DIGITS, "AVG_DEC_DIGITS", "AVG_DEC", nullptr, 0, 4, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AVG_SPEED_INT_DIGITS, "AVG_SPEED_INT_DIGITS", "AVG_SPD_INT", nullptr, 1, 14, 2, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &AVG_SPEED_DEC_DIGITS, "AVG_SPEED_DEC_DIGITS", "AVG_SPD_DEC", nullptr, 0, 4, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &MAX_SPEED_INT_DIGITS, "MAX_SPEED_INT_DIGITS", "MAX_SPD_INT", nullptr, 1, 14, 3, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &MAX_SPEED_DEC_DIGITS, "MAX_SPEED_DEC_DIGITS", "MAX_SPD_DEC", nullptr, 0, 4, 0, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_INT_DIGITS, "FUEL_INT_DIGITS", "FUEL_INT", nullptr, 1, 14, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &FUEL_DEC_DIGITS, "FUEL_DEC_DIGITS", "FUEL_DEC", nullptr, 0, 4, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ODO_INT_DIGITS, "ODO_INT_DIGITS", "ODO_INT", nullptr, 1, 14, 5, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &ODO_DEC_DIGITS, "ODO_DEC_DIGITS", "ODO_DEC", nullptr, 0, 4, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &WIFI_SSID, "WIFI_SSID", "WIFI_SSID", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_SSID), CK_STR },

  { &WIFI_SSID_1, "WIFI_SSID_1", "WIFI_S1", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_SSID_1), CK_STR },

  { &WIFI_SSID_2, "WIFI_SSID_2", "WIFI_S2", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_SSID_2), CK_STR },

  { &WIFI_SSID_3, "WIFI_SSID_3", "WIFI_S3", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_SSID_3), CK_STR },

  { &WIFI_SSID_4, "WIFI_SSID_4", "WIFI_S4", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_SSID_4), CK_STR },

  { &AP_PASSWORD, "AP_PASSWORD", "AP_PWD", AP_PASSWORD_DEFAULT, 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(AP_PASSWORD), CK_STR },

  { &WIFI_ATTEMPT_SECONDS, "WIFI_ATTEMPT_SECONDS", "WIFI_ATTEM_S", nullptr, 5, 60, 12, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &NTP_ENABLED, "NTP_ENABLED", "NTP_EN", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &NTP_SERVER, "NTP_SERVER", "NTP_SRV", "pool.ntp.org", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(NTP_SERVER), CK_STR },

  { &TZ_OFFSET_HOURS, "TZ_OFFSET_HOURS", "TZ_OFFSET", nullptr, -14, 14, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &TZ_DST_ENABLED, "TZ_DST_ENABLED", "TZ_DST", nullptr, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &TZ_DST_RULE, "TZ_DST_RULE", "TZ_RULE", nullptr, 0, 2, 1, 0.0f, 0.0f, 0.0f, 0, CK_INT },

  { &OTA_PULL_ENABLED, "OTA_PULL_ENABLED", "OTA_PULL_EN", nullptr, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, CK_BOOL },

  { &OTA_PULL_URL, "OTA_PULL_URL", "OTA_PULL_URL", "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(OTA_PULL_URL), CK_STR },

  { &VERSION_OVERRIDE, "VERSION_OVERRIDE", "VER_OVR", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(VERSION_OVERRIDE), CK_STR },

  { &WIFI_PASSWORD, "WIFI_PASSWORD", "WIFI_PWD", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_PASSWORD), CK_SECRET },

  { &WIFI_PASSWORD_1, "WIFI_PASSWORD_1", "WIFI_P1", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_PASSWORD_1), CK_SECRET },

  { &WIFI_PASSWORD_2, "WIFI_PASSWORD_2", "WIFI_P2", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_PASSWORD_2), CK_SECRET },

  { &WIFI_PASSWORD_3, "WIFI_PASSWORD_3", "WIFI_P3", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_PASSWORD_3), CK_SECRET },

  { &WIFI_PASSWORD_4, "WIFI_PASSWORD_4", "WIFI_P4", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, (uint16_t)sizeof(WIFI_PASSWORD_4), CK_SECRET },
};

static void cfgApply(int mode, Preferences &pref, JsonDocument *doc) {
  for (const CfgParam &p : CFG_TABLE) {
    switch (p.kind) {
    case CK_INT: {
      int *v = static_cast<int *>(p.ptr);
      if (mode == 0) {
        *v = pref.getInt(p.nvsKey, p.di);
        clampCfgInt(p.name, v, p.lo, p.hi);
      } else if (mode == 1) {
        (*doc)[p.name] = *v;
      } else if (!(*doc)[p.name].isNull()) {
        *v = (*doc)[p.name].as<int>();
        clampCfgInt(p.name, v, p.lo, p.hi);
        if (cfgIntChanged(pref, p.nvsKey, *v) &&
            nvsWriteFailed(p.nvsKey, pref.putInt(p.nvsKey, *v)))
          cfgNvsWriteErrors++;
      }
      break;
    }
    case CK_UINT: {
      uint32_t *v = static_cast<uint32_t *>(p.ptr);
      if (mode == 0) {
        *v = clampCfgUnsigned(p.name, pref.getInt(p.nvsKey, p.di), p.lo, p.hi);
      } else if (mode == 1) {
        (*doc)[p.name] = *v;
      } else if (!(*doc)[p.name].isNull()) {
        *v = clampCfgUnsigned(p.name, (*doc)[p.name].as<long long>(), p.lo,
                              p.hi);
        if (cfgIntChanged(pref, p.nvsKey, (int)*v) &&
            nvsWriteFailed(p.nvsKey, pref.putInt(p.nvsKey, (int)*v)))
          cfgNvsWriteErrors++;
      }
      break;
    }
    case CK_FLT: {
      float *v = static_cast<float *>(p.ptr);
      if (mode == 0) {
        *v = pref.getFloat(p.nvsKey, p.df);
        clampCfgFloat(p.name, v, p.flo, p.fhi, p.df);
      } else if (mode == 1) {
        (*doc)[p.name] = *v;
      } else if (!(*doc)[p.name].isNull()) {
        *v = (*doc)[p.name].as<float>();
        clampCfgFloat(p.name, v, p.flo, p.fhi, p.df);
        if (cfgFloatChanged(pref, p.nvsKey, *v) &&
            nvsWriteFailed(p.nvsKey, pref.putFloat(p.nvsKey, *v)))
          cfgNvsWriteErrors++;
      }
      break;
    }
    case CK_BOOL: {
      bool *v = static_cast<bool *>(p.ptr);
      if (mode == 0) {
        *v = pref.getBool(p.nvsKey, p.di != 0);
      } else if (mode == 1) {
        (*doc)[p.name] = *v;
      } else if (!(*doc)[p.name].isNull()) {
        *v = (*doc)[p.name].as<bool>();
        if (cfgBoolChanged(pref, p.nvsKey, *v) &&
            nvsWriteFailed(p.nvsKey, pref.putBool(p.nvsKey, *v)))
          cfgNvsWriteErrors++;
      }
      break;
    }
    case CK_STR:
    case CK_SECRET: {
      char *buf = static_cast<char *>(p.ptr);
      if (mode == 0) {
        // getString(key, char*, maxLen) - the nvs_get_str path. getBytes goes
        // through nvs_get_blob, which reports TYPE_MISMATCH for values stored
        // with putString and would silently fall back to the default on every
        // boot (saved settings appearing to vanish).
        size_t n = pref.getString(p.nvsKey, buf, p.size);
        if (n == 0) {
          strncpy(buf, p.dstr, p.size - 1);
          buf[p.size - 1] = 0;
        } else {
          buf[p.size - 1] = 0;
        }
      } else if (mode == 1) {
        if (p.kind == CK_SECRET) (*doc)[p.name] = "";
        else (*doc)[p.name] = buf;
      } else if ((*doc)[p.name].is<const char *>()) {
        const char *v = (*doc)[p.name].as<const char *>();
        if (p.kind == CK_SECRET && (v == nullptr || v[0] == 0)) continue;
        snprintf(buf, p.size, "%s", v);
        // Unchanged strings are not re-stored: they are the longest items in
        // the namespace, and rewriting them on every save is what used to fill
        // the partition with stranded pages.
        if (cfgStrChanged(pref, p.nvsKey, buf) &&
            nvsStringWriteFailed(p.nvsKey, buf, pref.putString(p.nvsKey, buf)))
          cfgNvsWriteErrors++;
      }
      break;
    }
    }
  }
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
  // Colour fade points, cold to hot: LOW -> YEL -> RED, all inside the bar band.
  // The bar is flat COLOR_TEMP_NORM below LOW and flat COLOR_TEMP_CRIT above RED,
  // so the two bar ends are scale only. Equal neighbours are legal (the band
  // collapses to a hard switch); an out-of-order triple would flip a blend
  // direction, so the whole chain is repaired here into
  // BAR_MIN <= LOW <= YEL <= RED <= BAR_MAX.
  if (TEMP_WARN_YEL < TEMP_BAR_MIN) {
    logPrintf("Config: TEMP_WARN_YEL=%d below TEMP_BAR_MIN=%d, raised into the bar\n",
              TEMP_WARN_YEL, TEMP_BAR_MIN);
    TEMP_WARN_YEL = TEMP_BAR_MIN;
  }
  if (TEMP_WARN_RED < TEMP_BAR_MIN) {
    logPrintf("Config: TEMP_WARN_RED=%d below TEMP_BAR_MIN=%d, raised into the bar - "
              "it would never show\n",
              TEMP_WARN_RED, TEMP_BAR_MIN);
    TEMP_WARN_RED = TEMP_BAR_MIN;
  }
  if (TEMP_WARN_RED > TEMP_BAR_MAX) {
    logPrintf("Config: TEMP_WARN_RED=%d above TEMP_BAR_MAX=%d, lowered - the bar "
              "would never reach COLOR_TEMP_CRIT\n",
              TEMP_WARN_RED, TEMP_BAR_MAX);
    TEMP_WARN_RED = TEMP_BAR_MAX;
  }
  if (TEMP_WARN_YEL > TEMP_WARN_RED) {
    logPrintf("Config: TEMP_WARN_YEL=%d above TEMP_WARN_RED=%d, lowered\n",
              TEMP_WARN_YEL, TEMP_WARN_RED);
    TEMP_WARN_YEL = TEMP_WARN_RED;
  }
  if (TEMP_WARN_LOW < TEMP_BAR_MIN) {
    logPrintf("Config: TEMP_WARN_LOW=%d below TEMP_BAR_MIN=%d, raised\n",
              TEMP_WARN_LOW, TEMP_BAR_MIN);
    TEMP_WARN_LOW = TEMP_BAR_MIN;
  }
  if (TEMP_WARN_LOW > TEMP_WARN_YEL) {
    logPrintf("Config: TEMP_WARN_LOW=%d above TEMP_WARN_YEL=%d, lowered\n",
              TEMP_WARN_LOW, TEMP_WARN_YEL);
    TEMP_WARN_LOW = TEMP_WARN_YEL;
  }
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

  // Every scalar parameter, in the order the CFG_* calls used to run.
  cfgApply(mode, pref, doc);

  // The five Wi-Fi passphrases are CK_SECRET rows at the end of CFG_TABLE:
  // never serialized back, and a blank posted value keeps the stored one.

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
    char key[10];  // "FCO_" + 2 digits + NUL: room the compiler can prove
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
      float v = pref.getFloat(key, fuelCalOhms[i]);
      fuelCalOhms[i] = constrain(v, 0.0f, 100000.0f);
    }
  } else if (mode == 1) {
    JsonArray arr = (*doc)["fuelCalOhms"].to<JsonArray>();
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++)
      arr.add(roundf(fuelCalOhms[i] * 10.0f) / 10.0f);
  } else if (mode == 2) {
    char key[10];  // "FCO_" + 2 digits + NUL: room the compiler can prove
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
        snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
        if (nvsWriteFailed(key, cfgPutFloat(pref, key, fuelCalOhms[i])))
          cfgNvsWriteErrors++;
      }
    }
    // Points beyond the uploaded array (or a missing array) keep their
    // previous values; write them to NVS so FUEL_TOUCH_POINTS above the
    // array length never leaves missing keys (a missing key loads as 0
    // and breaks the fuel gauge).
    for (int i = written; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
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
    char key[10];  // "FCO_" + 2 digits + NUL: room the compiler can prove
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
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
      char key[10];  // "FCO_" + 2 digits + NUL: room the compiler can prove
      for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
        snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
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
// Factory default configuration
// ----------------------------------------------------------------------------
// Seeded into NVS on first boot (and once for units still on the pre-factory-
// defaults config), so every unit starts from the shipped defaults - the same
// values dashboard_backup.json is meant to carry.
//
// This used to be a 5.6 KB JSON document of all 187 keys, pushed through the
// same mode-2 path as a Web UI save. Every value in it was the shipped default
// of the matching CFG_TABLE row (the two were diffed field by field before the
// blob was deleted, and no default sits outside its own band, so the mode-2
// clamp it went through was a no-op). The blob was a second copy of data the
// table already holds, and the copy was the half that could drift.
//
// WiFi credentials are deliberately absent from the seed: a factory-default
// seed must not strand a unit without a network - the same reason
// factoryResetConfig() puts the SSID/password keys back after wiping NVS, and
// the reason the password keys were already left out. Listing the SSIDs as
// empty strings meant every boot that found CFG_VER < 5 cleared all five SSIDs
// in RAM and NVS, and because the CFG_VER stamp was never verified a lost stamp
// repeated that wipe on every boot: passwords survived, SSIDs did not.
static const char *const CFG_NO_SEED[] = {
    "WIFI_SSID", "WIFI_SSID_1", "WIFI_SSID_2", "WIFI_SSID_3", "WIFI_SSID_4",
    "AP_PASSWORD"};

static bool cfgNoSeed(const char *name) {
  for (const char *k : CFG_NO_SEED)
    if (strcmp(k, name) == 0) return true;
  return false;
}

// Defaults go to RAM as well as NVS: the seed runs after the boot load, and a
// unit that had lost its CFG_VER stamp used to come back up with factory values
// in this boot too, not only in the next one.
static void cfgSeedDefaults(Preferences &pref) {
  for (const CfgParam &p : CFG_TABLE) {
    if (p.kind == CK_SECRET || cfgNoSeed(p.name)) continue;
    bool failed = false;
    switch (p.kind) {
    case CK_INT:
      *static_cast<int *>(p.ptr) = p.di;
      failed = nvsWriteFailed(p.nvsKey, pref.putInt(p.nvsKey, p.di));
      break;
    case CK_UINT:
      *static_cast<uint32_t *>(p.ptr) = static_cast<uint32_t>(p.di);
      failed = nvsWriteFailed(p.nvsKey, pref.putInt(p.nvsKey, p.di));
      break;
    case CK_FLT:
      *static_cast<float *>(p.ptr) = p.df;
      failed = nvsWriteFailed(p.nvsKey, pref.putFloat(p.nvsKey, p.df));
      break;
    case CK_BOOL:
      *static_cast<bool *>(p.ptr) = (p.di != 0);
      failed = nvsWriteFailed(p.nvsKey, pref.putBool(p.nvsKey, p.di != 0));
      break;
    case CK_STR:
      snprintf(static_cast<char *>(p.ptr), p.size, "%s", p.dstr);
      failed = nvsStringWriteFailed(p.nvsKey, p.dstr,
                                    pref.putString(p.nvsKey, p.dstr));
      break;
    case CK_SECRET:
      break;  // unreachable: filtered above, kept so the switch stays total
    }
    if (failed) cfgNvsWriteErrors++;
  }
}

// Write the shipped defaults into NVS (and RAM), stamp the config version, and
// make sure every fuel-calibration key exists. WiFi passwords are deliberately
// not part of the seed: they stay whatever is already stored.
void seedNVSWithFactoryDefaults() {
  cfgNvsWriteErrors = 0;
  NvsSession session("cfg", false);
  if (!session.opened()) {
    // Seeding without NVS would put the RAM globals out of step with storage.
    logPrintf("Config seed: NVS unavailable - factory defaults not applied\n");
    return;
  }
  cfgSeedDefaults(session.nvs);
  // The stamp is what keeps this a one-shot. A lost stamp used to re-seed the
  // whole config on every boot; the WiFi credentials are no longer part of the
  // seed, but a repeated seed would still undo every other setting.
  if (nvsWriteFailed("CFG_VER", session.nvs.putInt("CFG_VER", 5)))
    logPrintf("Config: CFG_VER not stored - the factory seed will run again at next boot\n");
  // The fuel table is not a CFG_TABLE row, and the old mode-2 seed did put it
  // back: the factory ramp, rebuilt from the seeded empty/full ohms. On a fresh
  // unit the compiled table already starts at FUEL_OHM_EMPTY and ends at
  // FUEL_OHM_FULL, so nothing is rebuilt and nothing is logged; only a unit that
  // had a real calibration in NVS gets the ramp (and the log line).
  const int nf = constrain(FUEL_TOUCH_POINTS, 2, MAX_TOUCH_POINTS);
  if (fuelCalOhms[0] != FUEL_OHM_EMPTY || fuelCalOhms[nf - 1] != FUEL_OHM_FULL)
    fillFuelTableFromOhms("factory seed");
  // Seed every calibration key so FUEL_TOUCH_POINTS can be raised above 8
  // without missing NVS entries (a missing key loads as 0).
  char key[10];  // "FCO_" + 2 digits + NUL: room the compiler can prove
  for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
    snprintf(key, sizeof(key), "FCO_%u", (unsigned)(uint8_t)i);
    if (nvsWriteFailed(key, session.nvs.putFloat(key, fuelCalOhms[i])))
      cfgNvsWriteErrors++;
  }
  // The mode-2 path this replaces ended inside processConfig() with the
  // cross-parameter checks and the colour parse. The seeded values pass them,
  // but a shipped default that ever stops passing one should be caught here,
  // not at the next boot.
  sanitizeDigitCounts();
  sanitizeConfigPairs();
  applyColors();
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
