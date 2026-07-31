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
String GHOST_COLOR_STR = "#212021";
uint16_t ghost_color;

uint32_t SPI_BUS_SPEED = 60000000;
int DISPLAY_WIDTH = 480;
int DISPLAY_HEIGHT = 320;
int SHUTDOWN_TIME_MS = 3000;

int BIG_CENTER_X = 240;
int BIG_CENTER_Y = 160;

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
float ACCEL_MAX_TIME = 30.0f;
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
int SIDEBAR_BAR_WIDTH = 8;
int SIDEBAR_BAR_HEIGHT = 200;

int OFFSET_HALL_ICON_X = -182;
int OFFSET_HALL_ICON_Y = 135;
int OFFSET_WIFI_ICON_X = 210;
int OFFSET_WIFI_ICON_Y = -152;
int OFFSET_INST_KML_X = 172;
int OFFSET_INST_KML_Y = 41;
int OFFSET_AVG_KML_X = 172;
int OFFSET_AVG_KML_Y = 71;
int OFFSET_AVG_SPEED_X = 172;
int OFFSET_AVG_SPEED_Y = 101;
int OFFSET_FUEL_LTRS_X = -172;
int OFFSET_FUEL_LTRS_Y = 41;

int ALIGN_BIG_SPEED_NUM = ALIGN_CENTER;
int ALIGN_BIG_SAT = ALIGN_CENTER;
int ALIGN_BIG_TMR = ALIGN_CENTER;
int ALIGN_BIG_BAT = ALIGN_CENTER;
int ALIGN_INST_KML = ALIGN_CENTER;
int ALIGN_AVG_KML = ALIGN_CENTER;
int ALIGN_AVG_SPEED = ALIGN_CENTER;
int ALIGN_FUEL_LTRS = ALIGN_CENTER;

bool SHOW_ELEMENT_BOUNDS = false;
bool ENABLE_POWER_SENSE = false;
bool ENABLE_CIRCLE_TEST = false;
bool ENABLE_DEMO_MODE = true;
bool ENABLE_ANTIALIASING = true;
float AA_SHARPNESS = 1.0f;
 
bool SHOW_FPS_COUNTER_DEFAULT = true;
int OFFSET_BIG_FPS_X = 5;
int OFFSET_BIG_FPS_Y = 5;

int NIGHT_MODE_START_HOUR = 20;
int NIGHT_MODE_END_HOUR = 6;
bool DISPLAY_INVERT_COLORS = false;

int REFRESH_SPEED_MS = 0;
int REFRESH_SAT_MS = 0;
int REFRESH_TMR_MS = 0;
int REFRESH_BAT_MS = 0;
int REFRESH_INST_MS = 0;
int REFRESH_AVG_MS = 5000;
int REFRESH_FUEL_MS = 0;
int REFRESH_ODO_MS = 0;
int REFRESH_TIME_MS = 0;
int REFRESH_SIDEBAR_TEMP_MS = 0;
int REFRESH_SIDEBAR_FUEL_MS = 0;
int REFRESH_AVG_SPEED_MS = 5000;

int SPEED_DIGITS = 3;
int SAT_DIGITS = 2;
int TMR_INT_DIGITS = 2;
int TMR_DEC_DIGITS = 2;
int BAT_INT_DIGITS = 2;
int BAT_DEC_DIGITS = 1;
int INST_INT_DIGITS = 2;
int INST_DEC_DIGITS = 1;
int AVG_INT_DIGITS = 2;
int AVG_DEC_DIGITS = 1;
int AVG_SPEED_INT_DIGITS = 3;
int AVG_SPEED_DEC_DIGITS = 0;
int FUEL_INT_DIGITS = 2;
int FUEL_DEC_DIGITS = 1;
int ODO_INT_DIGITS = 6;
int ODO_DEC_DIGITS = 1;

int TARGET_FPS = 60;
int BACKLIGHT_BRIGHTNESS = 100;
bool ENABLE_AUTO_BRIGHTNESS = false;
int LIGHT_SENSOR_DARK_VAL = 500;
int LIGHT_SENSOR_BRIGHT_VAL = 3000;
int AUTO_BRIGHT_DARK = 10;
int AUTO_BRIGHT_LIGHT = 100;
int AUTO_BRIGHT_FADE_MS = 1000;
int ambientLightValue = 0;
float filteredAmbientValue = 0.0f;
int FADE_DURATION_MS = 1000;

bool ENABLE_NIGHT_MODE = true;
bool ENABLE_DYNAMIC_CPU = false;
int MANUAL_CPU_FREQ = 240;
bool ENABLE_CPU_THROTTLE = true;
int CPU_THROTTLE_TEMP_WARN = 60;
int CPU_THROTTLE_TEMP_CRIT = 70;

unsigned long DISPLAY_REFRESH_MS;
unsigned long TELEMETRY_REFRESH_MS = 500;
float WHEEL_SPEED_FACTOR;
double WHEEL_DIST_PER_PULSE_KM;
float NTC_INV_ROOM_KELVIN;
float ADC_VOLTS_FACTOR;
bool showFpsCounter = true;

String WIFI_SSID = "D-Link-627F3B";
String WIFI_PASSWORD = "GDk2DxjVDc";
String WIFI_SSID_1 = "";
String WIFI_PASSWORD_1 = "";
String WIFI_SSID_2 = "";
String WIFI_PASSWORD_2 = "";
String WIFI_SSID_3 = "";
String WIFI_PASSWORD_3 = "";
String WIFI_SSID_4 = "";
String WIFI_PASSWORD_4 = "";
int WIFI_TX_POWER_DBM = 11;

bool NTP_ENABLED = true;
String NTP_SERVER = "pool.ntp.org";
int TZ_OFFSET_HOURS = 1;
bool TZ_DST_ENABLED = true;

bool OTA_PULL_ENABLED = false;
String OTA_PULL_URL = "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest";
int OTA_PULL_INTERVAL_HOURS = 24;
String OTA_CURRENT_VERSION = "1.0.7";

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
  ghost_color = hexToRGB565(GHOST_COLOR_STR);
}

void processConfig(int mode, JsonDocument *doc) {
  Preferences pref;
  if (mode == 0 || mode == 2)
    pref.begin("cfg", false);

  CFG_INT(DISPLAY_ROTATION, "DISP_ROT", 1);
  CFG_INT(SPI_BUS_SPEED, "SPI_FREQ", 60000000);
  CFG_INT(DISPLAY_WIDTH, "DISP_W", 480);
  CFG_INT(DISPLAY_HEIGHT, "DISP_H", 320);
  CFG_INT(TARGET_FPS, "TGT_FPS", 60);
  CFG_INT(BACKLIGHT_BRIGHTNESS, "BL_BRIGHT", 100);
  CFG_BOOL(ENABLE_AUTO_BRIGHTNESS, "EN_AUTO_BL", false);
  CFG_INT(LIGHT_SENSOR_DARK_VAL, "LIGHT_DARK", 500);
  CFG_INT(LIGHT_SENSOR_BRIGHT_VAL, "LIGHT_BRIGHT", 3000);
  CFG_INT(AUTO_BRIGHT_DARK, "AB_DARK", 10);
  CFG_INT(AUTO_BRIGHT_LIGHT, "AB_LIGHT", 100);
  CFG_INT(AUTO_BRIGHT_FADE_MS, "AB_FADE", 1000);
  CFG_INT(FADE_DURATION_MS, "FADE_DUR", 1000);
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
  CFG_STR(GHOST_COLOR_STR, "GHOST_C", "#212021");

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
  CFG_FLT(ACCEL_MAX_TIME, "ACC_MAX_T", 30.0f);

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
  CFG_INT(OFFSET_WIFI_ICON_X, "O_WIFI_X", 210);
  CFG_INT(OFFSET_WIFI_ICON_Y, "O_WIFI_Y", -152);
  CFG_INT(OFFSET_INST_KML_X, "O_INST_X", 172);
  CFG_INT(OFFSET_INST_KML_Y, "O_INST_Y", 41);
  CFG_INT(OFFSET_AVG_KML_X, "O_AVG_X", 172);
  CFG_INT(OFFSET_AVG_KML_Y, "O_AVG_Y", 71);
  CFG_INT(OFFSET_AVG_SPEED_X, "O_AVG_SPD_X", 172);
  CFG_INT(OFFSET_AVG_SPEED_Y, "O_AVG_SPD_Y", 101);
  CFG_INT(OFFSET_FUEL_LTRS_X, "O_FLTRS_X", -172);
  CFG_INT(OFFSET_FUEL_LTRS_Y, "O_FLTRS_Y", 41);

  CFG_INT(SIDEBAR_BAR_WIDTH, "SBAR_W", 8);
  CFG_INT(SIDEBAR_BAR_HEIGHT, "SBAR_H", 200);
  CFG_BOOL(SHOW_ELEMENT_BOUNDS, "SHW_BNDS", false);
  CFG_BOOL(ENABLE_POWER_SENSE, "PWR_SNS", false);
  CFG_BOOL(ENABLE_CIRCLE_TEST, "CIRC_TST", false);
  CFG_BOOL(ENABLE_DEMO_MODE, "DEMO_MODE", true);
  CFG_BOOL(ENABLE_ANTIALIASING, "EN_AA", true);
  CFG_FLT(AA_SHARPNESS, "AA_SHARP", 1.0f);
  CFG_BOOL(SHOW_FPS_COUNTER_DEFAULT, "SHW_FPS", true);
  CFG_BOOL(ENABLE_DYNAMIC_CPU, "DYN_CPU", false);
  CFG_INT(MANUAL_CPU_FREQ, "MAN_CPU", 240);
  CFG_BOOL(ENABLE_CPU_THROTTLE, "CPU_THR_EN", true);
  CFG_INT(CPU_THROTTLE_TEMP_WARN, "CPU_THR_W", 60);
  CFG_INT(CPU_THROTTLE_TEMP_CRIT, "CPU_THR_C", 70);
  CFG_BOOL(ENABLE_NIGHT_MODE, "EN_NIGHT", true);
  CFG_INT(NIGHT_MODE_START_HOUR, "NGHT_SRT", 20);
  CFG_INT(NIGHT_MODE_END_HOUR, "NGHT_END", 6);
  CFG_BOOL(DISPLAY_INVERT_COLORS, "INV_COLORS", false);
  CFG_INT(OFFSET_BIG_FPS_X, "O_FPS_X", 5);
  CFG_INT(OFFSET_BIG_FPS_Y, "O_FPS_Y", 5);

  CFG_INT(REFRESH_SPEED_MS, "R_SPD", 0);
  CFG_INT(REFRESH_SAT_MS, "R_SAT", 0);
  CFG_INT(REFRESH_TMR_MS, "R_TMR", 0);
  CFG_INT(REFRESH_BAT_MS, "R_BAT", 0);
  CFG_INT(REFRESH_INST_MS, "R_INST", 0);
  CFG_INT(REFRESH_AVG_MS, "R_AVG", 5000);
  CFG_INT(REFRESH_FUEL_MS, "R_FUEL", 0);
  CFG_INT(REFRESH_ODO_MS, "R_ODO", 0);
  CFG_INT(REFRESH_TIME_MS, "R_TIME", 0);
  CFG_INT(REFRESH_SIDEBAR_TEMP_MS, "R_SBAR_T", 0);
  CFG_INT(REFRESH_SIDEBAR_FUEL_MS, "R_SBAR_F", 0);
  CFG_INT(REFRESH_AVG_SPEED_MS, "R_AVG_SPD", 5000);

  CFG_INT(SPEED_DIGITS, "SPD_DIG", 3);
  CFG_INT(SAT_DIGITS, "SAT_DIG", 2);
  CFG_INT(TMR_INT_DIGITS, "TMR_INT", 2);
  CFG_INT(TMR_DEC_DIGITS, "TMR_DEC", 2);
  CFG_INT(BAT_INT_DIGITS, "BAT_INT", 2);
  CFG_INT(BAT_DEC_DIGITS, "BAT_DEC", 1);
  CFG_INT(INST_INT_DIGITS, "INST_INT", 2);
  CFG_INT(INST_DEC_DIGITS, "INST_DEC", 1);
  CFG_INT(AVG_INT_DIGITS, "AVG_INT", 2);
  CFG_INT(AVG_DEC_DIGITS, "AVG_DEC", 1);
  CFG_INT(AVG_SPEED_INT_DIGITS, "AVG_SPD_INT", 3);
  CFG_INT(AVG_SPEED_DEC_DIGITS, "AVG_SPD_DEC", 0);
  CFG_INT(FUEL_INT_DIGITS, "FUEL_INT", 2);
  CFG_INT(FUEL_DEC_DIGITS, "FUEL_DEC", 1);
  CFG_INT(ODO_INT_DIGITS, "ODO_INT", 6);
  CFG_INT(ODO_DEC_DIGITS, "ODO_DEC", 1);

  CFG_STR(WIFI_SSID, "WIFI_SSID", "D-Link-627F3B");
  CFG_STR(WIFI_PASSWORD, "WIFI_PWD", "GDk2DxjVDc");
  CFG_STR(WIFI_SSID_1, "WIFI_S1", "");
  CFG_STR(WIFI_PASSWORD_1, "WIFI_P1", "");
  CFG_STR(WIFI_SSID_2, "WIFI_S2", "");
  CFG_STR(WIFI_PASSWORD_2, "WIFI_P2", "");
  CFG_STR(WIFI_SSID_3, "WIFI_S3", "");
  CFG_STR(WIFI_PASSWORD_3, "WIFI_P3", "");
  CFG_STR(WIFI_SSID_4, "WIFI_S4", "");
  CFG_STR(WIFI_PASSWORD_4, "WIFI_P4", "");
  CFG_INT(WIFI_TX_POWER_DBM, "WIFI_TXP", 11);
  CFG_BOOL(NTP_ENABLED, "NTP_EN", true);
  CFG_STR(NTP_SERVER, "NTP_SRV", "pool.ntp.org");
  CFG_INT(TZ_OFFSET_HOURS, "TZ_OFFSET", 1);
  CFG_BOOL(TZ_DST_ENABLED, "TZ_DST", true);

  CFG_BOOL(OTA_PULL_ENABLED, "OTA_PULL_EN", false);
  CFG_STR(OTA_PULL_URL, "OTA_PULL_URL", "https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest");
  CFG_INT(OTA_PULL_INTERVAL_HOURS, "OTA_PULL_INT", 24);
  CFG_STR(OTA_CURRENT_VERSION, "OTA_VER", "1.0.7");

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
  unsigned long frameBudget = (TARGET_FPS > 0) ? (1000U / (unsigned long)TARGET_FPS) : 33U;
  if (frameBudget < 2) frameBudget = 2;
  DISPLAY_REFRESH_MS = frameBudget;
  WHEEL_SPEED_FACTOR = WHEEL_CIRCUMFERENCE_MM * 3600.0f;
  WHEEL_DIST_PER_PULSE_KM = (double)WHEEL_CIRCUMFERENCE_MM / 1000000.0;
  NTC_INV_ROOM_KELVIN = 1.0f / (25.0f + 273.15f);
  ADC_VOLTS_FACTOR = 3.3f / 4095.0f;
  showFpsCounter = SHOW_FPS_COUNTER_DEFAULT;
}
