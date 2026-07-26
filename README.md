# Dashboard++ for ESP32

A real-time motorcycle dashboard running on the ESP32 WROOM, displaying speed, odometer, fuel, temperature, battery, and more on a 4-inch ILI9488 TFT display (480×320). The device also serves a web configuration interface over WiFi.

## Architecture Overview

```
┌───────────────────────────────────────────────────────┐
│ ESP32 WROOM (Dual-Core)                                                        │
├───────────────────────────────────────────────────────┐
│ Core 0: SensorTask                                                     │
│   • TinyGPS++ NMEA parser (UART2 RX/TX)                     │
│   • Hall sensor interrupt → speed calculation               │
│   • QMC5883L compass (I2C)                              │
│   • Fuel, battery, temp analog sensors                  │
│   • Odometer (Hall pulses + GPS distance)                 │
│   • Acceleration timer state machine                    │
│   • Sensor data → g_sensorData (mutex-protected)          │
├───────────────────────────────────────────────────────┐
│ Core 1: WebServerTask                                                     │
│   • WiFi AP + optional STA connection                   │
│   • ArduinoOTA firmware updates                     │
│   • REST API endpoints (config, time, reboot, serial)     │
│   • Full web UI for configuration and monitoring          │
├───────────────────────────────────────────────────────┐
│ Main Loop (Core 1)                                                     │
│   • Display rendering (LovyanGFX / ILI9488)                   │
│   • CPU frequency scaling (80/160/240 MHz)                  │
│   • Power management (deep sleep, reboot)                 │
│   • Telemetry logging to ring buffer                    │
└───────────────────────────────────────────────────────┐
```

## Features

### Speed & Odometer
- **Dual-source fusion** — Hall sensor (GPIO33 interrupt) + GPS speed from TinyGPS++. Weighted fusion based on satellite count, cross-checked for sensor disagreement (threshold: configurable km/h delta → falls back to Hall-only).
- **Odometer** — Dual Hall-pulse and GPS-distance tracking, persisted to NVS every 1 km.
- Configurable wheel circumference, speed thresholds, satellite count thresholds.

### GPS
- UART2 (RX=25, TX=26) at 115200 baud, TinyGPSPlus parser.
- Lat/lon tracking, speed validity, satellite count display.
- **Time sync** — GPS epoch → `settimeofday()` with European DST offset, 5s hysteresis.

### Compass (QMC5883L)
- I2C on GPIO21/22, 200 Hz continuous mode, `atan2(y,x)` heading normalized to 0–360°.
- Telemetry output, data available for future UI integration.

### Sensors
- **Fuel** — ADC on GPIO32, exponential moving average, configurable calibration table (up to 20 ADC → liter points).
- **Engine temp** — NTC thermistor on GPIO36, Steinhart-Hart math, configurable R/BETA values.
- **Battery** — ADC on GPIO35, formula: `raw × 3.3/4095 × 5.7 + 0.2`, sampled every 500 ms.
- **Acceleration timer** — 3-state machine (READY/RUNNING/FINISHED) with configurable start/target speeds, auto-rearm.
- **Fuel economy** — Instant KM/L (3 s samples) + average KM/L (trip-based), refuel detection.

### UI (ILI9488 480×320, LovyanGFX)
- **Dashboard:** Large speed (50 pt DS_DIGIT font), odometer, clock/date, satellite count, battery icon + voltage, acceleration timer, instant/average KM/L.
- **Sidebars:** Left = engine temp vertical bar (gradient, ~10–110°C), Right = fuel level bar (gradient, 0–100%).
- **Badges:** HALL, GPS, WiFi status indicators.
- **Icons:** Battery, calendar, clock, map pin, WiFi, wheel — all procedurally drawn.
- **Anti-aliased primitives:** Lines, circles, arcs, rounded rects, color blending utilities.
- **Night mode:** Auto-dims backlight between configurable hours.
- **Self-test:** Overrides display to "8/88" patterns with backlight ramp.
- **FPS overlay:** Toggleable, shows instant/avg FPS + CPU temp.

### Web Server (Soft AP `Dashboard_Config`)
- 8 REST endpoints — config GET/POST, time sync, reboot, sleep, factory reset, serial monitor stream.
- Full config UI with 8 collapsible sections, interactive sliders for layout offsets, autosave (2 s debounce), backup/import, factory reset.

### Configuration (NVS)
- 50+ parameters: display rotation/speed/colors, sensor tuning (ADC mapping, wheel circumference, NTC params), UI layout offsets, signatures, fuel/temp bar ranges and colors.
- `processConfig()` 3-mode loader/serializer/deserializer.

### System
- **CPU scaling** — Dynamic 80/160/240 MHz based on FPS vs target; WiFi forces 240 MHz.
- **Power management** — GPIO34 ignition sense → deep sleep with EXT0 wakeup, reboot.
- **Deep sleep / reboot** screens with progress animation.
- **Demo mode** — Full sensor simulation (oscillating values for all fields).
- **Telemetry logging** — 4096-byte ring buffer, 500 ms output, web serial monitor.
- **2-core task architecture** — Sensors on Core 0, display/loop on Core 1.

## Pinout

| Pin | Function |
|-----|----------|
| 5   | CS (display) |
| 12  | Backlight PWM |
| 14  | Display RST |
| 18  | SPI clock |
| 21  | Compass SDA |
| 22  | Compass SCL |
| 23  | SPI MOSI |
| 25  | GPS RX |
| 26  | GPS TX |
| 27  | Display DC |
| 32  | Fuel ADC |
| 33  | Hall sensor interrupt |
| 34  | Power/ignition sense |
| 35  | Battery ADC |
| 36  | Temp NTC ADC |

## Build & Flash

```bash
# Install PlatformIO: pip install platformio
platformio run          # Build and flash to USB device
platformio run --target upload  # OTA update via WiFi
platformio device monitor   # Serial console
```

## Source Code Structure

| Directory | Contents |
|-----------|----------|
| `src/` | Main application source code |
| `include/` | Custom header files (font bitmaps) |
| `lib/` | Arduino libraries (ArduinoJson, TinyGPS++, etc.) |
| `chips/` | Custom chip definitions (st7789) |
| `test/` | Unit tests |

## Source Code Breakdown

### `src/main.cpp` — Application entry point
- **`setup()`** — Initializes display, sensors, GPS, compass, preferences. Loads configuration from NVS and applies it before display initialization. Creates two tasks: `sensorTask` (Core 0) and `webServerTask` (Core 1).
- **`loop()`** — Main loop that handles pending state changes (sleep, reboot), renders the display at the target FPS rate, logs telemetry every 500 ms, and performs CPU frequency scaling based on measured performance.

### `src/dashboard.h` — Global declarations
- Defines all configuration variables as externs so they can be modified by `config.cpp`.
- Declares the sensor snapshot structure, display device class, and all API functions for drawing, sensors, web, and UI.
- Includes font bitmaps (Conthrax and DS_DIGIT) and key libraries.

### `src/config.cpp` — Configuration management
- **Default values** — All configuration parameters have sensible defaults defined at module level.
- **NVS macros** (`CFG_INT`, `CFG_FLT`, `CFG_STR`, `CFG_BOOL`) — Generic loaders that handle 3 modes: load from NVS (mode 0), serialize to JSON (mode 1), or deserialize and save back (mode 2).
- **`processConfig()`** — Loads all parameters, applies them, recalculates derived values.
- **`recalculateDerivedParams()`** — Computes display refresh interval from target FPS, wheel speed factor, NTC constants, etc.

### `src/gfx.cpp` — Graphics primitives and icons
- **Text bounds calculation** — Custom glyph metrics for the loaded GFXfonts (handles newlines, character ranges).
- **Display device class (`LGFX_ST7789_4`)** — Wraps LovyanGFX with SPI bus configuration. The `applyBusConfig()` method allows reconfiguring the SPI frequency after NVS loading.
- **Color blending** — `blendColor()`, `blendColorLinear()`, `blendColorWithBlack()` for alpha-based color mixing (used in anti-alienated rendering).
- **Anti-aliased primitives** (`drawAALine`, `drawAACircle`, `drawAACornerArc`, `drawAARoundRect`, `fillAARoundRect`) — Templates that draw lines, circles, arcs, and rounded rectangles with fractional pixel precision to reduce visual degradation at small scales.
- **Icons** — Procedurally drawn battery, calendar, clock, location, WiFi, wheel icons using the display's basic drawing API.
- **Splash/OTA screens** — Boot splash with progress bar, OTA update screen, goodbye/reboot screen.

### `src/sensors.cpp` — Sensor drivers and data collection
- **QMC5883L compass driver** — I2C read/write helpers, initialization, and heading calculation using `atan2f`.
- **Hall sensor (ISR)** — Interrupt handler on GPIO33 with debounce filtering. Speed is calculated from pulse intervals multiplied by the wheel speed factor.
- **Speed fusion** — Combines Hall speed and GPS speed based on satellite count quality. Falls back to Hall-only or GPS-only when one source is unreliable.
- **Analog sensors** — Battery (ADC), temperature (NTC thermistor with Steinhart-Hart equation), fuel (ADC with exponential smoothing).
- **Odometer** — Tracks distance via both Hall pulses and GPS position changes, persists to NVS every 1 km.
- **Fuel consumption & acceleration timer** — Trip-based fuel economy calculations and a state machine for measuring time-to-reach-target speed.
- **Sensor task** — Runs on Core 0, continuously reads all sensors, computes derived values, and writes the snapshot to `g_sensorData` under mutex protection. Includes demo mode with simulated oscillating sensor data.

### `src/ui.cpp` — Dashboard rendering
- **`updateBigDisplay()`** — The main render function called every frame (or on-demand for specific elements). Uses a dirty-rendering strategy: only redraws components that have changed since the last frame, minimizing display writes and improving performance.
  - Time/date with clock/calendar icons
  - Speed number using DS_DIGIT50 font with ghost-digit technique
  - Odometer with KM unit
  - Satellite count, battery voltage, acceleration timer badge
  - Sidebars for engine temperature (left) and fuel level (right)
  - Instant/average KM/L indicators
- **`checkNightMode()`** — Automatically dims the backlight between configurable hours.

### `src/web.cpp` — Web server and OTA updates
- **WiFi setup** — Creates a soft AP (`Dashboard_Config`) with optional STA connection to fallback networks (up to 4 configured). Uses mDNS for local hostname resolution.
- **REST API endpoints:**
  - `/api/config` GET/POST — Load/save configuration as JSON
  - `/api/time` POST — Sync time from browser clock
  - `/api/reboot`, `/api/sleep` — Control device power state
  - `/api/reset` — Factory reset (clears all NVS)
  - `/api/ota` POST — Firmware OTA update via file upload
  - `/api/serial` GET — Stream serial log output
  - `/api/perf` GET — Performance monitoring data (CPU, memory, FPS, WiFi status)
- **ArduinoOTA** integration for firmware updates with progress reporting.

### `src/web.cpp` (continued) — Web UI
The web interface is embedded as a raw HTML string (`index_html`) and served at `/`. It provides:
- A responsive dark-themed configuration page with 8 collapsible sections covering system, WiFi, display, sensors, GNSS, layout offsets, performance monitoring, and serial console.
- Interactive controls including color pickers, range sliders with number inputs, touch table editors for fuel calibration, and XY offset pairs for UI positioning.
- Real-time performance monitoring panel showing CPU usage/memory/flash bars, FPS counters, WiFi status, and a live serial log viewer.
- Backup/import functionality via JSON file download/upload.

## Configuration Parameters (NVS)

| Category | Key Range | Description |
|-----------|----------|------------|
| System | `TARGET_FPS`, `BOOT_TIME_MS`, `SHUTDOWN_TIME_MS` | Display refresh rate, boot/shutdown durations |
| CPU | `MANUAL_CPU_FREQ`, `ENABLE_DYNAMIC_CPU`, `ENABLE_CPU_THROTTLE` | Frequency scaling and thermal throttling thresholds |
| WiFi | `WIFI_SSID[1-4]`, `WIFI_PASSWORD[1-4]`, `WIFI_TX_POWER_DBM` | Network credentials and transmit power |
| Display | `DISPLAY_ROTATION`, `SPI_BUS_SPEED`, `ENABLE_ANTIALIASING` | Screen rotation, SPI frequency, anti-alienation toggle |
| Colors | `COLOR_TEMP_NORM/WARN/CRIT`, `COLOR_FUEL_NORM/WARN/CRIT` | Hex color strings for gradient bars |
| Sensors | `WHEEL_CIRCUMFERENCE_MM`, `NTC_R_BALANCE`, `NTC_BETA` | Vehicle and thermistor tuning parameters |
| Layout | `OFFSET_BIG_*_X/Y`, `BIG_CENTER_X/Y` | Pixel offsets for every UI element |

## License

© alefinot — Dashboard++