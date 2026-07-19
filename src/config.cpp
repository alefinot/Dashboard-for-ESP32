#include "dashboard.h"

// ----------------------------------------------------------------------------
// Configuration variables (defined here, declared extern elsewhere)
// ----------------------------------------------------------------------------
int DISPLAY_ROTATION = 1;

String SPLASH_SIGNATURE = "by @ale.finot";
String REBOOT_SIGNATURE = "Dashboard++ by @ale.finot";
String DASHBOARD_SIGNATURE = "<<<<<<    Dashboard++ by @ale.finot    >>>>>>";

int TEMP_BAR_MIN = 10;
int TEMP_BAR_MAX = 110;
int TEMP_WARN_RED = 90;
int TEMP_WARN_YEL = 45;
int TEMP_WARN_GRN = 50;
int FUEL_WARN_RED = 20;
int FUEL_WARN_YEL = 45;

String COLOR_TEMP_NORM = "#00ffff";
String COLOR_TEMP_WARN = "#ff8c00";
String COLOR_TEMP_CRIT = "#ff0000";

String COLOR_FUEL_NORM = "#00ff00";
String COLOR_FUEL_WARN = "#ffff00";
String COLOR_FUEL_CRIT = "#ff0000";

uint16_t c_temp_norm, c_temp_warn, c_temp_crit;
uint16_t c_fuel_norm, c_fuel_warn, c_fuel_crit;

int DISPLAY_WIDTH = 480;
int DISPLAY_HEIGHT = 320;
int BOOT_TIME_MS = 5000;
int SHUTDOWN_TIME_MS = 3000;

int BIG_CENTER_X = 240;
int BIG_CENTER_Y = 160;

double INITIAL_ODOMETER_KM = 19000.0;
float WHEEL_CIRCUMFERENCE_MM = 1650.0f;
float FUEL_FILTER_ALPHA = 0.08f;

float NTC_R_BALANCE = 10000.0f;
float NTC_R_ROOM = 10000.0f;
float NTC_BETA = 3950.0f;

int MIN_SATELLITES = 5;
int OPTIMAL_SATELLITES = 8;
float MAX_SPEED_DELTA_KMH = 5.0f;
float MIN_SPEED_THRESHOLD = 1.0f;
float ACCEL_START_SPEED = 1.0f;
float ACCEL_TARGET_SPEED = 50.0f;
String ACCEL_BADGE_LINE1 = "0-50";
String ACCEL_BADGE_LINE2 = "km/h";

int OFFSET_BIG_TIME_X = -100;
int OFFSET_BIG_TIME_Y = -118;
int OFFSET_BIG_DATE_X = 40;
int OFFSET_BIG_DATE_Y = -118;
int OFFSET_BIG_SIGNATURE_X = 0;
int OFFSET_BIG_SIGNATURE_Y = -100;
int OFFSET_BIG_SPEED_NUM_X = 0;
int OFFSET_BIG_SPEED_NUM_Y = -20;
int OFFSET_BIG_SPEED_UNIT_X = 0;
int OFFSET_BIG_SPEED_UNIT_Y = 65;
int OFFSET_BIG_ODO_X = 0;
int OFFSET_BIG_ODO_Y = 140;
int OFFSET_BIG_SAT_X = -180;
int OFFSET_BIG_SAT_Y = -137;
int OFFSET_BIG_TMR_X = 0;
int OFFSET_BIG_TMR_Y = -245;
int OFFSET_BIG_BAT_X = 180;
int OFFSET_BIG_BAT_Y = -137;

int SIDEBAR_LEFT_X = 10;
int SIDEBAR_LEFT_Y = 60;
int SIDEBAR_RIGHT_X = 462;
int SIDEBAR_RIGHT_Y = 60;

int OFFSET_HALL_ICON_X = -182;
int OFFSET_HALL_ICON_Y = 135;
int OFFSET_GPS_ICON_X = 195;
int OFFSET_GPS_ICON_Y = 135;
int OFFSET_WHEEL_ICON_X = -220;
int OFFSET_WHEEL_ICON_Y = 135;
int OFFSET_WIFI_ICON_X = 210;
int OFFSET_WIFI_ICON_Y = -152;
int OFFSET_INST_KML_X = 172;
int OFFSET_INST_KML_Y = 41;
int OFFSET_AVG_KML_X = 172;
int OFFSET_AVG_KML_Y = 71;

bool SHOW_ELEMENT_BOUNDS = false;
bool ENABLE_POWER_SENSE = false;
bool ENABLE_CIRCLE_TEST = false;
bool ENABLE_DEMO_MODE = true;
bool ENABLE_SLEEP_AFTER_REBOOT = false;

bool SHOW_FPS_COUNTER_DEFAULT = true;
int OFFSET_BIG_FPS_X = 5;
int OFFSET_BIG_FPS_Y = 5;

int NIGHT_MODE_START_HOUR = 20;
int NIGHT_MODE_END_HOUR = 6;

int TARGET_FPS = 120;
int BACKLIGHT_BRIGHTNESS = 100;

bool ENABLE_DYNAMIC_CPU = true;
int MANUAL_CPU_FREQ = 240;

unsigned long DISPLAY_REFRESH_MS;
unsigned long TELEMETRY_REFRESH_MS = 500;
float WHEEL_SPEED_FACTOR;
double WHEEL_DIST_PER_PULSE_KM;
float NTC_INV_ROOM_KELVIN;
float ADC_VOLTS_FACTOR;
bool showFpsCounter = true;

int FUEL_TOUCH_POINTS = 8;
int touchTable[MAX_TOUCH_POINTS] = {950, 840, 750, 670, 600, 530, 460, 400};

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

#define CFG_STR(var, nvsKey, defVal)                                           \
  if (mode == 0) {                                                             \
    var = pref.getString(nvsKey, defVal);                                      \
  } else if (mode == 1) {                                                      \
    (*doc)[#var] = var;                                                        \
  } else if (mode == 2 && !(*doc)[#var].isNull()) {                            \
    var = (*doc)[#var].as<String>();                                           \
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

uint16_t hexToRGB565(String hex) {
  if (hex.length() == 0)
    return 0xFFFF;
  if (hex[0] == '#')
    hex = hex.substring(1);
  long rgb = strtol(hex.c_str(), nullptr, 16);
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
}

void processConfig(int mode, JsonDocument *doc) {
  Preferences pref;
  if (mode == 0 || mode == 2)
    pref.begin("cfg", false);

  CFG_INT(DISPLAY_ROTATION, "DISP_ROT", 1);
  CFG_INT(DISPLAY_WIDTH, "DISP_W", 480);
  CFG_INT(DISPLAY_HEIGHT, "DISP_H", 320);
  CFG_INT(TARGET_FPS, "TGT_FPS", 120);
  CFG_INT(BACKLIGHT_BRIGHTNESS, "BL_BRIGHT", 100);
  CFG_INT(BOOT_TIME_MS, "BOOT_TMS", 5000);
  CFG_INT(SHUTDOWN_TIME_MS, "SHUT_TMS", 3000);
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

  CFG_FLT(WHEEL_CIRCUMFERENCE_MM, "WHL_CIRC", 1650.0f);
  CFG_FLT(FUEL_FILTER_ALPHA, "FUEL_FILT", 0.08f);
  CFG_INT(FUEL_TOUCH_POINTS, "FTL_PTS", 8);
  CFG_FLT(NTC_R_BALANCE, "NTC_BAL", 10000.0f);
  CFG_FLT(NTC_BETA, "NTC_BETA", 3950.0f);
  CFG_INT(MIN_SATELLITES, "MIN_SAT", 5);
  CFG_INT(OPTIMAL_SATELLITES, "OPT_SAT", 8);
  CFG_FLT(MAX_SPEED_DELTA_KMH, "MAX_SPD_DELT", 5.0f);
  CFG_FLT(MIN_SPEED_THRESHOLD, "MIN_SPD_THR", 1.0f);
  CFG_FLT(ACCEL_START_SPEED, "ACC_STRT", 1.0f);
  CFG_FLT(ACCEL_TARGET_SPEED, "ACC_TGT", 50.0f);

  CFG_STR(ACCEL_BADGE_LINE1, "ACC_BDG_1", "0-50");
  CFG_STR(ACCEL_BADGE_LINE2, "ACC_BDG_2", "km/h");

  CFG_INT(BIG_CENTER_X, "BCX", 240);
  CFG_INT(BIG_CENTER_Y, "BCY", 160);

  CFG_INT(OFFSET_BIG_TIME_X, "O_BTIME_X", -100);
  CFG_INT(OFFSET_BIG_TIME_Y, "O_BTIME_Y", -118);
  CFG_INT(OFFSET_BIG_DATE_X, "O_BDATE_X", 40);
  CFG_INT(OFFSET_BIG_DATE_Y, "O_BDATE_Y", -118);
  CFG_INT(OFFSET_BIG_SIGNATURE_X, "O_BSIG_X", 0);
  CFG_INT(OFFSET_BIG_SIGNATURE_Y, "O_BSIG_Y", -100);
  CFG_INT(OFFSET_BIG_SPEED_NUM_X, "O_BSN_X", 0);
  CFG_INT(OFFSET_BIG_SPEED_NUM_Y, "O_BSN_Y", -20);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_X, "O_BSU_X", 0);
  CFG_INT(OFFSET_BIG_SPEED_UNIT_Y, "O_BSU_Y", 65);
  CFG_INT(OFFSET_BIG_ODO_X, "O_BODO_X", 0);
  CFG_INT(OFFSET_BIG_ODO_Y, "O_BODO_Y", 140);
  CFG_INT(OFFSET_BIG_SAT_X, "O_BSAT_X", -180);
  CFG_INT(OFFSET_BIG_SAT_Y, "O_BSAT_Y", -137);
  CFG_INT(OFFSET_BIG_TMR_X, "O_BTMR_X", 0);
  CFG_INT(OFFSET_BIG_TMR_Y, "O_BTMR_Y", -245);
  CFG_INT(OFFSET_BIG_BAT_X, "O_BBAT_X", 180);
  CFG_INT(OFFSET_BIG_BAT_Y, "O_BBAT_Y", -137);
  CFG_INT(SIDEBAR_LEFT_X, "SBAR_L_X", 10);
  CFG_INT(SIDEBAR_LEFT_Y, "SBAR_L_Y", 60);
  CFG_INT(SIDEBAR_RIGHT_X, "SBAR_R_X", 462);
  CFG_INT(SIDEBAR_RIGHT_Y, "SBAR_R_Y", 60);
  CFG_INT(OFFSET_HALL_ICON_X, "O_HALL_X", -182);
  CFG_INT(OFFSET_HALL_ICON_Y, "O_HALL_Y", 135);
  CFG_INT(OFFSET_GPS_ICON_X, "O_GPS_X", 195);
  CFG_INT(OFFSET_GPS_ICON_Y, "O_GPS_Y", 135);
  CFG_INT(OFFSET_WHEEL_ICON_X, "O_WHEEL_X", -220);
  CFG_INT(OFFSET_WHEEL_ICON_Y, "O_WHEEL_Y", 135);
  CFG_INT(OFFSET_WIFI_ICON_X, "O_WIFI_X", 210);
  CFG_INT(OFFSET_WIFI_ICON_Y, "O_WIFI_Y", -152);
  CFG_INT(OFFSET_INST_KML_X, "O_INST_X", 172);
  CFG_INT(OFFSET_INST_KML_Y, "O_INST_Y", 41);
  CFG_INT(OFFSET_AVG_KML_X, "O_AVG_X", 172);
  CFG_INT(OFFSET_AVG_KML_Y, "O_AVG_Y", 71);

  CFG_BOOL(SHOW_ELEMENT_BOUNDS, "SHW_BNDS", false);
  CFG_BOOL(ENABLE_POWER_SENSE, "PWR_SNS", false);
  CFG_BOOL(ENABLE_CIRCLE_TEST, "CIRC_TST", false);
  CFG_BOOL(ENABLE_DEMO_MODE, "DEMO_MODE", true);
  CFG_BOOL(ENABLE_SLEEP_AFTER_REBOOT, "SLP_RBT", false);
  CFG_BOOL(SHOW_FPS_COUNTER_DEFAULT, "SHW_FPS", true);
  CFG_BOOL(ENABLE_DYNAMIC_CPU, "DYN_CPU", true);
  CFG_INT(MANUAL_CPU_FREQ, "MAN_CPU", 240);
  CFG_INT(NIGHT_MODE_START_HOUR, "NGHT_SRT", 20);
  CFG_INT(NIGHT_MODE_END_HOUR, "NGHT_END", 6);
  CFG_INT(OFFSET_BIG_FPS_X, "O_FPS_X", 5);
  CFG_INT(OFFSET_BIG_FPS_Y, "O_FPS_Y", 5);

  if (mode == 0 || mode == 2) {
    if (FUEL_TOUCH_POINTS < 2) FUEL_TOUCH_POINTS = 2;
    if (FUEL_TOUCH_POINTS > MAX_TOUCH_POINTS) FUEL_TOUCH_POINTS = MAX_TOUCH_POINTS;
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
  } else if (mode == 2 && !(*doc)["touchTable"].isNull()) {
    JsonArray arr = (*doc)["touchTable"].as<JsonArray>();
    char key[8];
    for (int i = 0; i < FUEL_TOUCH_POINTS && i < (int)arr.size(); i++) {
      touchTable[i] = arr[i].as<int>();
      snprintf(key, sizeof(key), "TCH_%d", i);
      pref.putInt(key, touchTable[i]);
    }
  }

  if (mode == 0 || mode == 2)
    pref.end();
  applyColors();
}

void recalculateDerivedParams() {
  DISPLAY_REFRESH_MS = (TARGET_FPS > 0) ? (1000U / (unsigned long)TARGET_FPS) : 33U;
  WHEEL_SPEED_FACTOR = WHEEL_CIRCUMFERENCE_MM * 3600.0f;
  WHEEL_DIST_PER_PULSE_KM = (double)WHEEL_CIRCUMFERENCE_MM / 1000000.0;
  NTC_INV_ROOM_KELVIN = 1.0f / (25.0f + 273.15f);
  ADC_VOLTS_FACTOR = 3.3f / 4095.0f;
  showFpsCounter = SHOW_FPS_COUNTER_DEFAULT;
}
