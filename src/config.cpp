#include "dashboard.h"

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
int TEMP_WARN_GRN = 50;
int FUEL_WARN_RED = 20;
int FUEL_WARN_YEL = 45;

char COLOR_TEMP_NORM[8] = "#00ffff";
char COLOR_TEMP_WARN[8] = "#ff8c00";
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
float REFUEL_RESET_LITERS = 2.0f;

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
bool GPS_ONLY_MODE = true;
int SPEED_SOURCE_HOLD_MS = 500;
int HALL_MEDIAN_SAMPLES = 9;
int HALL_PERIOD_GUARD = 32;
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
int OFFSET_COMPASS_X = 0;
int OFFSET_COMPASS_Y = -130;
int OFFSET_FUEL_LTRS_X = 132;
int OFFSET_FUEL_LTRS_Y = 123;

int ALIGN_BIG_SPEED_NUM = ALIGN_CENTER;
int ALIGN_BIG_SAT = ALIGN_CENTER;
int ALIGN_BIG_TMR = ALIGN_CENTER;
int ALIGN_BIG_BAT = ALIGN_CENTER;
int ALIGN_INST_KML = ALIGN_CENTER;
int ALIGN_AVG_KML = ALIGN_CENTER;
int ALIGN_AVG_SPEED = ALIGN_CENTER;
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
bool SHOW_ELEMENT_FUEL_LTRS = true;
bool SHOW_ELEMENT_COMPASS = true;
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
bool ENABLE_CIRCLE_TEST = false;
bool ENABLE_DEMO_MODE = false;
bool ENABLE_ANTIALIASING = true;
float AA_SHARPNESS = 0.2f;
 
bool SHOW_FPS_COUNTER_DEFAULT = false;
bool GPS_DEBUG_DEFAULT = false;
int OFFSET_BIG_FPS_X = -9;
int OFFSET_BIG_FPS_Y = -7;

int NIGHT_MODE_START_HOUR = 23;
int NIGHT_MODE_END_HOUR = 0;
bool DISPLAY_INVERT_COLORS = false;

int REFRESH_SPEED_MS = 250;
int REFRESH_BAT_MS = 2500;
int REFRESH_INST_MS = 500;
int REFRESH_FUEL_MS = 1000;
float COMPASS_DECLINATION_DEG = 0.0f;

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
int HEADING_DIGITS = 3;
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
int WIFI_TX_POWER_DBM = 20;
int WIFI_RETRY_MODE = 1;
int WIFI_RETRY_SECONDS = 60;

bool NTP_ENABLED = true;
char NTP_SERVER[64] = "pool.ntp.org";
int TZ_OFFSET_HOURS = 1;
bool TZ_DST_ENABLED = true;

bool OTA_PULL_ENABLED = false;
char OTA_PULL_URL[192] = "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest";
char OTA_CURRENT_VERSION[32] = "1.3.3";

int FUEL_TOUCH_POINTS = 8;
int touchTable[MAX_TOUCH_POINTS] = {950, 840, 750, 670, 600, 530, 460, 400,
                                     350, 310, 275, 245, 220, 195, 170, 145,
                                     120, 95, 70, 45};

// ----------------------------------------------------------------------------
// NVS config macros
// ----------------------------------------------------------------------------
#define CFG_INT(var, nvsKey, defVal)                                           \
  if (mode == 0) {                                                             \
    var = pref.getInt(nvsKey, defVal);                                         \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<int>();                                              \
    pref.putInt(nvsKey, var);                                                  \
  }

#define CFG_FLT(var, nvsKey, defVal)                                           \
  if (mode == 0) {                                                             \
    var = pref.getFloat(nvsKey, defVal);                                       \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<float>();                                            \
    pref.putFloat(nvsKey, var);                                                \
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
    pref.putString(nvsKey, var);                                               \
  }

#define CFG_BOOL(var, nvsKey, defVal)                                          \
  if (mode == 0) {                                                             \
    var = pref.getBool(nvsKey, defVal);                                        \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<bool>();                                             \
    pref.putBool(nvsKey, var);                                                 \
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

void processConfig(int mode, JsonDocument *doc) {
  Preferences pref;
  if (mode == 0 || mode == 2)
    pref.begin("cfg", false);

  CFG_INT(DISPLAY_ROTATION, "DISP_ROT", 1);
  CFG_BOOL(UNITS_IMPERIAL, "UNITS_IMP", false);
  CFG_INT(SPI_BUS_SPEED, "SPI_FREQ", 60000000);
  CFG_INT(DISPLAY_WIDTH, "DISP_W", 480);
  CFG_INT(DISPLAY_HEIGHT, "DISP_H", 320);
  CFG_INT(TARGET_FPS, "TGT_FPS", 60);
  CFG_INT(BACKLIGHT_BRIGHTNESS, "BL_BRIGHT", 100);
  CFG_BOOL(ENABLE_AUTO_BRIGHTNESS, "EN_AUTO_BL", true);
  CFG_INT(LIGHT_SENSOR_DARK_VAL, "LIGHT_DARK", 432);
  CFG_INT(LIGHT_SENSOR_BRIGHT_VAL, "LIGHT_BRIGHT", 2851);
  CFG_INT(AUTO_BRIGHT_DARK, "AB_DARK", 13);
  CFG_INT(AUTO_BRIGHT_LIGHT, "AB_LIGHT", 100);
  CFG_INT(AUTO_BRIGHT_FADE_MS, "AB_FADE", 4000);
  CFG_INT(FADE_DURATION_MS, "FADE_DUR", 700);
  CFG_STR(SPLASH_SIGNATURE, "SPLASH_SIG", "by @ale.finot");
  CFG_STR(REBOOT_SIGNATURE, "REBOOT_SIG", "Dashboard++ by @ale.finot");
  CFG_STR(DASHBOARD_SIGNATURE, "DASH_SIG",
          "<<<<<<    Dashboard++ by @ale.finot    >>>>>>");

  CFG_INT(TEMP_BAR_MIN, "TMP_BAR_MIN", 10);
  CFG_INT(TEMP_BAR_MAX, "TMP_BAR_MAX", 110);
  CFG_INT(TEMP_WARN_RED, "TMP_WRN_R", 90);
  CFG_INT(TEMP_WARN_YEL, "TMP_WRN_Y", 45);
  CFG_INT(FUEL_WARN_RED, "FUL_WRN_R", 20);
  CFG_INT(FUEL_WARN_YEL, "FUL_WRN_Y", 45);

  CFG_STR(COLOR_TEMP_NORM, "C_TMP_N", "#00ffff");
  CFG_STR(COLOR_TEMP_WARN, "C_TMP_W", "#ff8c00");
  CFG_STR(COLOR_TEMP_CRIT, "C_TMP_C", "#ff0000");
  CFG_STR(COLOR_FUEL_NORM, "C_FUL_N", "#00ff00");
  CFG_STR(COLOR_FUEL_WARN, "C_FUL_W", "#ffff00");
  CFG_STR(COLOR_FUEL_CRIT, "C_FUL_C", "#ff0000");
  CFG_STR(GHOST_COLOR_STR, "GHOST_C", "#474747");

  CFG_FLT(WHEEL_CIRCUMFERENCE_MM, "WHL_CIRC", 1650.0f);
  CFG_FLT(FUEL_FILTER_ALPHA, "FUEL_FILT", 0.08f);
  CFG_FLT(REFUEL_RESET_LITERS, "RFUEL_RST", 2.0f);
  CFG_INT(FUEL_TOUCH_POINTS, "FTL_PTS", 8);
  CFG_FLT(BATTERY_SCALE, "BAT_SCALE", 5.7f);
  CFG_FLT(BATTERY_OFFSET, "BAT_OFFS", 0.2f);
  CFG_FLT(NTC_R_BALANCE, "NTC_BAL", 10000.0f);
  CFG_FLT(NTC_R25, "NTC_R25", 10000.0f);
  CFG_FLT(NTC_BETA, "NTC_BETA", 3950.0f);
  CFG_FLT(NTC_TEMP_OFFSET, "NTC_OFFS", 0.0f);
  CFG_INT(GPS_BAUD, "GPS_BAUD", 115200);
  CFG_INT(MIN_SATELLITES, "MIN_SAT", 8);
  CFG_INT(OPTIMAL_SATELLITES, "OPT_SAT", 12);
  CFG_FLT(MAX_SPEED_DELTA_KMH, "MAX_SPD_DELT", 5.0f);
  CFG_FLT(MIN_SPEED_THRESHOLD, "MIN_SPD_THR", 1.0f);
  CFG_FLT(GPS_START_KMH, "GPS_START", 3.0f);
  CFG_INT(GPS_STOP_SETTLE_MS, "GPS_STL_MS", 1500);
  CFG_FLT(GPS_MIN_DEV_KMH, "GPS_MIN_DV", 1.0f);
  CFG_BOOL(GPS_ONLY_MODE, "GPS_ONLY", true);
  CFG_INT(SPEED_SOURCE_HOLD_MS, "SPD_SRC_HOLD", 500);
  CFG_INT(HALL_MEDIAN_SAMPLES, "HALL_MED_N", 9);
  CFG_INT(HALL_PERIOD_GUARD, "HALL_PRD_GRD", 32);
  CFG_FLT(ACCEL_START_SPEED, "ACC_STRT", 1.0f);
  CFG_FLT(ACCEL_TARGET_SPEED, "ACC_TGT", 50.0f);
  CFG_FLT(ACCEL_MAX_TIME, "ACC_MAX_T", 9.99f);

  CFG_STR(ACCEL_BADGE_LINE1, "ACC_BDG_1", "0-50");
  CFG_STR(ACCEL_BADGE_LINE2, "ACC_BDG_2", "km/h");

  CFG_INT(BIG_CENTER_X, "BCX", 240);
  CFG_INT(BIG_CENTER_Y, "BCY", 160);

  CFG_INT(OFFSET_BIG_TIME_X, "O_BTIME_X", 107);
  CFG_INT(OFFSET_BIG_TIME_Y, "O_BTIME_Y", -91);
  CFG_INT(OFFSET_BIG_DATE_X, "O_BDATE_X", -131);
  CFG_INT(OFFSET_BIG_DATE_Y, "O_BDATE_Y", -91);
  CFG_INT(OFFSET_BIG_SIGNATURE_X, "O_BSIG_X", 0);
  CFG_INT(OFFSET_BIG_SIGNATURE_Y, "O_BSIG_Y", -75);
  CFG_INT(OFFSET_BIG_SPEED_NUM_X, "O_BSN_X", 0);
  CFG_INT(OFFSET_BIG_SPEED_NUM_Y, "O_BSN_Y", -3);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_X, "O_BSU_X", 106);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_Y, "O_BSU_Y", 56);
  CFG_INT(OFFSET_BIG_ODO_X, "O_BODO_X", 22);
  CFG_INT(OFFSET_BIG_ODO_Y, "O_BODO_Y", 126);
  CFG_INT(OFFSET_BIG_SAT_X, "O_BSAT_X", 179);
  CFG_INT(OFFSET_BIG_SAT_Y, "O_BSAT_Y", -114);
  CFG_INT(OFFSET_BIG_TMR_X, "O_BTMR_X", -53);
  CFG_INT(OFFSET_BIG_TMR_Y, "O_BTMR_Y", -46);
  CFG_INT(OFFSET_BIG_BAT_X, "O_BBAT_X", -112);
  CFG_INT(OFFSET_BIG_BAT_Y, "O_BBAT_Y", 123);
  CFG_INT(SIDEBAR_LEFT_X, "SBAR_L_X", 10);
  CFG_INT(SIDEBAR_LEFT_Y, "SBAR_L_Y", 95);
  CFG_INT(SIDEBAR_RIGHT_X, "SBAR_R_X", 462);
  CFG_INT(SIDEBAR_RIGHT_Y, "SBAR_R_Y", 95);
  CFG_INT(OFFSET_HALL_ICON_X, "O_HALL_X", 0);
  CFG_INT(OFFSET_HALL_ICON_Y, "O_HALL_Y", -100);
  CFG_INT(OFFSET_WIFI_ICON_X, "O_WIFI_X", 204);
  CFG_INT(OFFSET_WIFI_ICON_Y, "O_WIFI_Y", -108);
  CFG_INT(OFFSET_INST_KML_X, "O_INST_X", 60);
  CFG_INT(OFFSET_INST_KML_Y, "O_INST_Y", -25);
  CFG_INT(OFFSET_AVG_KML_X, "O_AVG_X", 160);
  CFG_INT(OFFSET_AVG_KML_Y, "O_AVG_Y", -25);
  CFG_INT(OFFSET_AVG_SPEED_X, "O_AVG_SPD_X", -163);
  CFG_INT(OFFSET_AVG_SPEED_Y, "O_AVG_SPD_Y", -25);
  CFG_INT(OFFSET_COMPASS_X, "O_CMP_X", 0);
  CFG_INT(OFFSET_COMPASS_Y, "O_CMP_Y", -130);
  CFG_INT(OFFSET_FUEL_LTRS_X, "O_FLTRS_X", 132);
  CFG_INT(OFFSET_FUEL_LTRS_Y, "O_FLTRS_Y", 123);
  CFG_FLT(COMPASS_DECLINATION_DEG, "CMP_DECL", 0.0f);
  CFG_INT(COMPASS_CAL_X, "CMP_CAL_X", -79);
  CFG_INT(COMPASS_CAL_Y, "CMP_CAL_Y", -2040);
  CFG_INT(COMPASS_CAL_Z, "CMP_CAL_Z", -515);
  CFG_INT(COMPASS_CAL_TX, "CMP_TILT_X", -15193);
  CFG_INT(COMPASS_CAL_TY, "CMP_TILT_Y", -26464);
  CFG_INT(COMPASS_CAL_TZ, "CMP_TILT_Z", 11935);
  CFG_FLT(COMPASS_CAL_SCALE_X, "CMP_SCALE_X", 1.0f);
  CFG_FLT(COMPASS_CAL_SCALE_Y, "CMP_SCALE_Y", 1.0f);
  CFG_FLT(COMPASS_CAL_SCALE_Z, "CMP_SCALE_Z", 1.0f);
  // 0 = flat assumption (robust to a changing tilt); 1 = use the calibrated
  // tilt axis (only correct while the mounting tilt is constant).
  CFG_INT(COMPASS_TILT_COMP, "CMP_TILT_COMP", 0);

  CFG_INT(SIDEBAR_BAR_WIDTH, "SBAR_W", 8);
  CFG_INT(SIDEBAR_BAR_HEIGHT, "SBAR_H", 190);
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
  CFG_BOOL(SHOW_ELEMENT_FUEL_LTRS, "SH_FUL", true);
  CFG_BOOL(SHOW_ELEMENT_COMPASS, "SH_CMP", true);
  CFG_BOOL(SHOW_GHOST_DIGITS, "SH_GHOST", true);
  CFG_BOOL(SHOW_ELEMENT_WEATHER, "SH_WEATH", true);
  CFG_INT(OFFSET_WEATHER_X, "O_WEATH_X", 0);
  CFG_INT(OFFSET_WEATHER_Y, "O_WEATH_Y", 146);
  CFG_STR(WEATHER_CITY, "WEATH_CITY", "");
  CFG_FLT(WEATHER_LAT, "WEATH_LAT", 0.0f);
  CFG_FLT(WEATHER_LON, "WEATH_LON", 0.0f);
  CFG_INT(WEATHER_REFRESH_MIN, "WEATH_RFR", 1);
  CFG_STR(WEATHER_LOCALE, "WEATH_LOCALE", "it");
  CFG_BOOL(ENABLE_POWER_SENSE, "PWR_SNS", false);
  CFG_BOOL(ENABLE_CIRCLE_TEST, "CIRC_TST", false);
  CFG_BOOL(ENABLE_DEMO_MODE, "DEMO_MODE", false);
  CFG_BOOL(ENABLE_ANTIALIASING, "EN_AA", true);
  CFG_FLT(AA_SHARPNESS, "AA_SHARP", 0.2f);
  CFG_BOOL(SHOW_FPS_COUNTER_DEFAULT, "SHW_FPS", false);
  CFG_BOOL(GPS_DEBUG_DEFAULT, "GPS_DBG", false);
  CFG_BOOL(ENABLE_DYNAMIC_CPU, "DYN_CPU", false);
  CFG_INT(MANUAL_CPU_FREQ, "MAN_CPU", 240);
  CFG_BOOL(ENABLE_CPU_THROTTLE, "CPU_THR_EN", false);
  CFG_INT(CPU_THROTTLE_TEMP_WARN, "CPU_THR_W", 50);
  CFG_INT(CPU_THROTTLE_TEMP_CRIT, "CPU_THR_C", 60);
  CFG_BOOL(ENABLE_NIGHT_MODE, "EN_NIGHT", false);
  CFG_INT(NIGHT_MODE_START_HOUR, "NGHT_SRT", 23);
  CFG_INT(NIGHT_MODE_END_HOUR, "NGHT_END", 0);
  CFG_BOOL(DISPLAY_INVERT_COLORS, "INV_COLORS", false);
  CFG_INT(OFFSET_BIG_FPS_X, "O_FPS_X", -9);
  CFG_INT(OFFSET_BIG_FPS_Y, "O_FPS_Y", -7);

  CFG_INT(REFRESH_SPEED_MS, "R_SPD", 250);
  CFG_INT(REFRESH_BAT_MS, "R_BAT", 2500);
  CFG_INT(REFRESH_INST_MS, "R_INST", 500);
  CFG_INT(REFRESH_FUEL_MS, "R_FUEL", 1000);

  CFG_INT(SPEED_DIGITS, "SPD_DIG", 2);
  CFG_INT(SAT_DIGITS, "SAT_DIG", 2);
  CFG_INT(TMR_INT_DIGITS, "TMR_INT", 1);
  CFG_INT(TMR_DEC_DIGITS, "TMR_DEC", 2);
  CFG_INT(BAT_INT_DIGITS, "BAT_INT", 2);
  CFG_INT(BAT_DEC_DIGITS, "BAT_DEC", 1);
  CFG_INT(INST_INT_DIGITS, "INST_INT", 2);
  CFG_INT(INST_DEC_DIGITS, "INST_DEC", 1);
  CFG_INT(AVG_INT_DIGITS, "AVG_INT", 2);
  CFG_INT(AVG_DEC_DIGITS, "AVG_DEC", 1);
  CFG_INT(AVG_SPEED_INT_DIGITS, "AVG_SPD_INT", 2);
  CFG_INT(AVG_SPEED_DEC_DIGITS, "AVG_SPD_DEC", 0);
  CFG_INT(HEADING_DIGITS, "HEAD_DIG", 3);
  CFG_INT(FUEL_INT_DIGITS, "FUEL_INT", 1);
  CFG_INT(FUEL_DEC_DIGITS, "FUEL_DEC", 1);
  CFG_INT(ODO_INT_DIGITS, "ODO_INT", 5);
  CFG_INT(ODO_DEC_DIGITS, "ODO_DEC", 1);

  CFG_STR(WIFI_SSID, "WIFI_SSID", "");
  // WiFi passwords are handled manually (below): never serialized back to the
  // web API, and an empty posted value means "keep the stored one".
  CFG_STR(WIFI_SSID_1, "WIFI_S1", "");
  CFG_STR(WIFI_SSID_2, "WIFI_S2", "");
  CFG_STR(WIFI_SSID_3, "WIFI_S3", "");
  CFG_STR(WIFI_SSID_4, "WIFI_S4", "");
  CFG_INT(WIFI_TX_POWER_DBM, "WIFI_TXP", 20);
  CFG_INT(WIFI_RETRY_MODE, "WIFI_RETRY_M", 1);
  CFG_INT(WIFI_RETRY_SECONDS, "WIFI_RETRY_S", 60);
  CFG_BOOL(NTP_ENABLED, "NTP_EN", true);
  CFG_STR(NTP_SERVER, "NTP_SRV", "pool.ntp.org");
  CFG_INT(TZ_OFFSET_HOURS, "TZ_OFFSET", 1);
  CFG_BOOL(TZ_DST_ENABLED, "TZ_DST", true);

  CFG_BOOL(OTA_PULL_ENABLED, "OTA_PULL_EN", false);
  CFG_STR(OTA_PULL_URL, "OTA_PULL_URL", "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest");
  CFG_STR(OTA_CURRENT_VERSION, "OTA_VER", "1.3.3");

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
          pref.putString(nvs[i], pwds[i]);
        }
      }
    }
  }

  if (mode == 0 || mode == 2) {
    if (FUEL_TOUCH_POINTS < 2) FUEL_TOUCH_POINTS = 2;
    if (FUEL_TOUCH_POINTS > MAX_TOUCH_POINTS) FUEL_TOUCH_POINTS = MAX_TOUCH_POINTS;
    if (WEATHER_REFRESH_MIN < 1) WEATHER_REFRESH_MIN = 1;
    if (WEATHER_REFRESH_MIN > 1440) WEATHER_REFRESH_MIN = 1440;
    if (WIFI_RETRY_MODE < 0) WIFI_RETRY_MODE = 0;
    if (WIFI_RETRY_MODE > 2) WIFI_RETRY_MODE = 2;
    if (WIFI_RETRY_SECONDS < 1) WIFI_RETRY_SECONDS = 1;
    if (WIFI_RETRY_SECONDS > 86400) WIFI_RETRY_SECONDS = 86400;
  }

  if (mode == 0) {
    char key[8];
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "TCH_%d", i);
      touchTable[i] = pref.getInt(key, touchTable[i]);
    }
  } else if (mode == 1) {
    JsonArray arr = (*doc)["touchTable"].to<JsonArray>();
    for (int i = 0; i < FUEL_TOUCH_POINTS; i++)
      arr.add(touchTable[i]);
  } else if (mode == 2) {
    char key[8];
    int written = 0;
    if (!(*doc)["touchTable"].isNull()) {
      JsonArray arr = (*doc)["touchTable"].as<JsonArray>();
      written = (int)arr.size();
      if (written > FUEL_TOUCH_POINTS) written = FUEL_TOUCH_POINTS;
      for (int i = 0; i < written; i++) {
        touchTable[i] = arr[i].as<int>();
        snprintf(key, sizeof(key), "TCH_%d", i);
        pref.putInt(key, touchTable[i]);
      }
    }
    // Points beyond the uploaded array (or a missing array) keep their
    // previous values; write them to NVS so FUEL_TOUCH_POINTS above the
    // array length never leaves missing keys (a missing key loads as 0
    // and breaks the fuel gauge).
    for (int i = written; i < FUEL_TOUCH_POINTS; i++) {
      snprintf(key, sizeof(key), "TCH_%d", i);
      pref.putInt(key, touchTable[i]);
    }
  }

  if (mode == 0 || mode == 2)
    pref.end();
  applyColors();
}

// ----------------------------------------------------------------------------
// Factory default configuration (dashboard_backup.json)
// ----------------------------------------------------------------------------
// The reference backup export of the standard configuration. Seeded into NVS
// on first boot (and once for units still on the pre-factory-defaults
// config), so every unit starts from exactly these values.
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
  "COLOR_TEMP_NORM": "#00ffff",
  "COLOR_TEMP_WARN": "#ff8c00",
  "COLOR_TEMP_CRIT": "#ff0000",
  "COLOR_FUEL_NORM": "#00ff00",
  "COLOR_FUEL_WARN": "#ffff00",
  "COLOR_FUEL_CRIT": "#ff0000",
  "GHOST_COLOR_STR": "#474747",
  "WHEEL_CIRCUMFERENCE_MM": 1650,
  "FUEL_FILTER_ALPHA": 0.08,
  "REFUEL_RESET_LITERS": 2,
  "FUEL_TOUCH_POINTS": 8,
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
  "GPS_ONLY_MODE": true,
  "SPEED_SOURCE_HOLD_MS": 500,
  "HALL_MEDIAN_SAMPLES": 9,
  "HALL_PERIOD_GUARD": 32,
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
  "OFFSET_COMPASS_X": 0,
  "OFFSET_COMPASS_Y": -130,
  "OFFSET_FUEL_LTRS_X": 132,
  "OFFSET_FUEL_LTRS_Y": 123,
  "COMPASS_DECLINATION_DEG": 0,
  "COMPASS_CAL_X": -79,
  "COMPASS_CAL_Y": -2040,
  "COMPASS_CAL_Z": -515,
  "COMPASS_CAL_TX": -15193,
  "COMPASS_CAL_TY": -26464,
  "COMPASS_CAL_TZ": 11935,
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
  "SHOW_ELEMENT_FUEL_LTRS": true,
  "SHOW_ELEMENT_COMPASS": true,
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
  "ENABLE_CIRCLE_TEST": false,
  "ENABLE_DEMO_MODE": false,
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
  "DISPLAY_INVERT_COLORS": false,
  "OFFSET_BIG_FPS_X": -9,
  "OFFSET_BIG_FPS_Y": -7,
  "REFRESH_SPEED_MS": 250,
  "REFRESH_BAT_MS": 2500,
  "REFRESH_INST_MS": 500,
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
  "HEADING_DIGITS": 3,
  "FUEL_INT_DIGITS": 1,
  "FUEL_DEC_DIGITS": 1,
  "ODO_INT_DIGITS": 5,
  "ODO_DEC_DIGITS": 1,
  "WIFI_SSID": "",
  "WIFI_SSID_1": "",
  "WIFI_SSID_2": "",
  "WIFI_SSID_3": "",
  "WIFI_SSID_4": "",
  "WIFI_TX_POWER_DBM": 20,
  "WIFI_RETRY_MODE": 1,
  "WIFI_RETRY_SECONDS": 60,
  "NTP_ENABLED": true,
  "NTP_SERVER": "pool.ntp.org",
  "TZ_OFFSET_HOURS": 1,
  "TZ_DST_ENABLED": true,
  "OTA_PULL_ENABLED": false,
  "OTA_PULL_URL": "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest",
  "OTA_CURRENT_VERSION": "1.3.3",
  "touchTable": [
    950,
    840,
    750,
    670,
    600,
    530,
    460,
    400
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
  Preferences pref;
  pref.begin("cfg", false);
  pref.putInt("CFG_VER", 5);
  // Seed every touch-table key so FUEL_TOUCH_POINTS can be raised above 8
  // without missing NVS entries (a missing key loads as 0).
  char key[8];
  for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
    snprintf(key, sizeof(key), "TCH_%d", i);
    pref.putInt(key, touchTable[i]);
  }
  pref.end();
  logPrintf("Config v5: NVS seeded with factory defaults (dashboard_backup.json)\n");
}

void recalculateDerivedParams() {
  unsigned long frameBudget = (TARGET_FPS > 0) ? (1000U / (unsigned long)TARGET_FPS) : 33U;
  if (frameBudget < 2) frameBudget = 2;
  DISPLAY_REFRESH_MS = frameBudget;
  WHEEL_SPEED_FACTOR = WHEEL_CIRCUMFERENCE_MM * 3600.0f;
  WHEEL_DIST_PER_PULSE_KM = (double)WHEEL_CIRCUMFERENCE_MM / 1000000.0;
  NTC_INV_ROOM_KELVIN = 1.0f / (25.0f + 273.15f);
  ADC_VOLTS_FACTOR = 3.3f / 4095.0f;
  showFpsCounter = SHOW_FPS_COUNTER_DEFAULT;
  showGpsDebug = GPS_DEBUG_DEFAULT;
}
