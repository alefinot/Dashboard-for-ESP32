# Web UI Settings Reorganization Plan

Goal: make the five settings dropdowns in `src/webui.html` coherent, so that each
dropdown contains exactly one concern. **The dropdowns themselves stay** (same five
sections, same `<details>` mechanics, same Performance Monitor panel) — only what is
*inside* them is reorganized.

No backend keys change, no item `id` changes, no changes to `DEFAULTS`, backup import,
save/reset flows, or the C-side `processConfig()`. All edits are inside the `configMap`
array (plus moving whole `card_header` content objects).

## Current problems

| # | Problem | Where today |
|---|---------|-------------|
| 1 | Weather *data* settings (city/lat/lon/refresh/locale) live in **WiFi and Connectivity** — nothing to do with the network | WiFi and Connectivity |
| 2 | Time settings (NTP sync, NTP server, DST, time zone) live in **WiFi and Connectivity** | WiFi and Connectivity |
| 3 | Auto-brightness is split: enable toggle in **Display & Colors**, but sensor calibration (ADC values, brightness mapping, transition speed) in **Sensors Tuning** | Display & Colors + Sensors Tuning |
| 4 | Fuel smoothing alpha (`FUEL_FILTER_ALPHA`) sits in the **Odometer** card in Sensors Tuning | Sensors Tuning |
| 5 | Weather widget: visibility + position in **UI Layout**, content settings in **WiFi and Connectivity** | UI Layout + WiFi and Connectivity |

## Target structure

### 1. System & General (unchanged contents + gains Time)
- **General**: `TARGET_FPS`, `ENABLE_DEMO_MODE`, `ENABLE_POWER_SENSE`, `SHOW_ELEMENT_BOUNDS`
- **CPU**: `MANUAL_CPU_FREQ`, `ENABLE_DYNAMIC_CPU`, `ENABLE_CPU_THROTTLE`, `CPU_THROTTLE_TEMP_WARN`, `CPU_THROTTLE_TEMP_CRIT`
- **Time** *(moved from WiFi and Connectivity)*: `NTP_ENABLED`, `NTP_SERVER`, `TZ_DST_ENABLED`, `TZ_OFFSET_HOURS`
- **Custom Signatures**: `SPLASH_SIGNATURE`, `REBOOT_SIGNATURE`, `DASHBOARD_SIGNATURE`
- **Firmware Update**: OTA buttons, `OTA_PULL_ENABLED`, `OTA_PULL_URL`, `OTA_PULL_INTERVAL_HOURS`, `OTA_CURRENT_VERSION`

### 2. WiFi and Connectivity (now purely network)
- **Network Settings**: `WIFI_TX_POWER_DBM`, `WIFI_RETRY_MODE`, `WIFI_RETRY_SECONDS`
- **Primary Network**: `WIFI_SSID`, `WIFI_PASSWORD`
- **Fallback Networks**: Network 1–4 (`WIFI_SSID_N` / `WIFI_PASSWORD_N`)
- *(Weather and Time sections removed)*

### 3. Display & Colors (gains auto-brightness calibration)
- **Display**: `DISPLAY_ROTATION`, `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`, `SPI_BUS_SPEED`, `DISPLAY_INVERT_COLORS`
- **Rendering**: `ENABLE_ANTIALIASING`, `AA_SHARPNESS`
- **Brightness**: `BACKLIGHT_BRIGHTNESS`, `FADE_DURATION_MS`
- **Auto Brightness** *(moved from Sensors Tuning)*: card content (live reading + Capture Dark/Capture Bright buttons), `LIGHT_SENSOR_DARK_VAL`, `LIGHT_SENSOR_BRIGHT_VAL`, `AUTO_BRIGHT_DARK`, `AUTO_BRIGHT_LIGHT`, `AUTO_BRIGHT_FADE_MS`
- **Night Mode**: `ENABLE_NIGHT_MODE`, `NIGHT_MODE_START_HOUR`, `NIGHT_MODE_END_HOUR`

### 4. Sensors Tuning (hardware + data acquisition only)
- **Fuel Sensor**: touch-table + `REFUEL_RESET_LITERS`, `FUEL_TOUCH_POINTS`; **gains** `FUEL_FILTER_ALPHA` (from the Odometer card)
- **Temperature Sensor**: `NTC_R_BALANCE`, `NTC_BETA`
- **Odometer**: set-value UI, `WHEEL_CIRCUMFERENCE_MM`, `MIN_SPEED_THRESHOLD` *(fuel alpha removed)*
- **Polling Rates**: `REFRESH_SPEED_MS`, `REFRESH_BAT_MS`, `REFRESH_INST_MS`, `REFRESH_FUEL_MS`
- **GNSS**: `GPS_BAUD`, `MIN_SATELLITES`, `OPTIMAL_SATELLITES`, `MAX_SPEED_DELTA_KMH`, `GPS_MIN_DEV_KMH`, `GPS_ONLY_MODE`, `GPS_START_KMH`, `GPS_STOP_SETTLE_MS`, `GPS_DEBUG_DEFAULT`
- **Compass**: calibrate/status buttons, `COMPASS_DECLINATION_DEG`, `COMPASS_CAL_X/Y/Z`, `COMPASS_CAL_TX/TY/TZ`
- *(Auto Brightness card removed — moved to Display & Colors)*

### 5. UI Layout (visibility / digits / position / thresholds — one card per widget)
- **System**: note, Viewport Center, Ghost Digits, FPS Counter, Signature
- **Speed & Odometer**: Acceleration Timer (visible/digits/position), Speed Source Icon, Main Speed Number, Speed Unit, Average Speed, Odometer
- **Fuel & Battery**: Average KM/L, Instant KM/L, Fuel Liters, Battery
- **Clock & Date**: Clock, Date
- **WiFi & Satellites**: Satellites, WiFi Icon
- **Compass**: Heading Indicator
- **Weather Widget** *(merged: gains the data settings moved from WiFi)*: `SHOW_ELEMENT_WEATHER`, `WEATHER_CITY`, `WEATHER_LAT`, `WEATHER_LON`, `WEATHER_REFRESH_MIN`, `WEATHER_LOCALE`, note, Position (x/y)
- **Sidebars**: Left Bar, Engine Temperature Thresholds, Right Bar, Fuel Level Thresholds, Bar Dimensions

### Splits kept by design (logic vs. appearance pattern)
These stay in two dropdowns, following the consistent rule "tuning/logic in Sensors
Tuning, appearance/visibility/position in UI Layout":
- Acceleration Timer: `ACCEL_*` tuning (Sensors Tuning) vs. visible/digits/position (UI Layout)
- Temperature: NTC hardware `NTC_R_BALANCE`/`NTC_BETA` (Sensors Tuning) vs. bar thresholds `TEMP_BAR_*`/`TEMP_WARN_*` (UI Layout / Sidebars)
- Compass: calibration (Sensors Tuning) vs. heading display (UI Layout)
- Signatures: strings (System & General) vs. dashboard signature visible/position (UI Layout)

## Moves (implementation checklist)

1. Move the `Time Settings` card items (`NTP_ENABLED`, `NTP_SERVER`, `TZ_DST_ENABLED`,
   `TZ_OFFSET_HOURS`) from the "WiFi and Connectivity" group to "System & General"
   (new `card_header` "Time").
2. Move the weather items (`SHOW_ELEMENT_WEATHER`, `WEATHER_CITY`, `WEATHER_LAT`,
   `WEATHER_LON`, `WEATHER_REFRESH_MIN`, `WEATHER_LOCALE`, plus the note) from
   "WiFi and Connectivity" into the UI Layout **Weather Widget** card, above the
   existing Position xy pair.
3. Move the **Auto Brightness Sensor** block from "Sensors Tuning" to "Display &
   Colors": the `card_header` content object (contains `id="ambient-reading"` + the two
   capture buttons) and the five fields `LIGHT_SENSOR_DARK_VAL`, `LIGHT_SENSOR_BRIGHT_VAL`,
   `AUTO_BRIGHT_DARK`, `AUTO_BRIGHT_LIGHT`, `AUTO_BRIGHT_FADE_MS`. Place it after
   **Brightness**, before **Night Mode**.
4. Move `FUEL_FILTER_ALPHA` from the Odometer card to the Fuel Sensor card in
   "Sensors Tuning".
5. Leave everything else in place.

## Safety constraints

- Item `id`s are the contract with the backend (`processConfig`), `DEFAULTS`,
  `applyDefault()`, backup export/import, and search (`REBOOT_REQUIRED_KEYS`,
  `filterConfig()`) — **do not rename any id**.
- Card content HTML is moved as a whole object so its element ids keep working:
  `ambient-reading`, `odo-reading`, `compass-cal-status`, `otaBtn`, `otaPullBtn`,
  and the onclick handlers (`calibrateDark()`, `calibrateBright()`, `setOdometer()`,
  `compassCalStart()`, `doOta()`, `doOtaPull()`).
- Group order, titles, icons, and the `<details>` rendering code (`renderConfigMap`)
  are not touched — only item lists within groups.
- `webui_html_gz.h` is regenerated automatically by the prebuild script
  (`scripts/gzip_webui.py`) at build time; no manual sync needed.

## Verification

1. **Static (this machine, no browser):**
   - `node` syntax-check the inline `<script>` blocks of `webui.html`.
   - Python brace-matching parse of the `DEFAULTS` and `configMap` structures
     (same approach used for the factory-defaults change): every backup key appears
     in exactly one item across `configMap` (except `ambientLightValue` — runtime state
     shown via the live `ambient-reading` span, `FUEL_TOUCH_POINTS` — controlled by
     the touch-table `pointCount`, and `ENABLE_CIRCLE_TEST` — hidden debug flag);
     no id lost or duplicated by the move.
2. **Build + flash** (known-good flow):
   `python -m platformio run -e esp32dev --target upload`
   (PlatformIO lives in the Python 3.13 env; `--target run` is not available).
3. **On device:** open the config page, check every dropdown renders its moved
   content, search still filters across all groups, reset-to-default buttons work,
   no JS console errors; save a config and confirm export backup still round-trips.
