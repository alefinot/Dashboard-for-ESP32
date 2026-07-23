# Dashboard++ for ESP32

A motorcycle/dash display for ESP32 with a 4-inch ILI9488 TFT (480x320).

Built with PlatformIO + Arduino, using LovyanGFX for graphics, TinyGPS++ for NMEA parsing, and ArduinoJson for the web API.

## Features

### Speed & Odometer
- **Dual-source fusion** — Hall sensor (GPIO33 interrupt) + GPS speed from TinyGPS++. Weighted fusion based on satellite count, cross-checked for sensor disagreement (threshold: configurable km/h delta → falls back to Hall-only)
- **Odometer** — Dual Hall-pulse and GPS-distance tracking, persisted to NVS every 1 km
- Configurable wheel circumference, speed thresholds, satellite count thresholds

### GPS
- UART2 (RX=25, TX=26) at 115200 baud, TinyGPSPlus parser
- Lat/lon tracking, speed validity, satellite count display
- **Time sync** — GPS epoch → `settimeofday()` with European DST offset, 5s hysteresis

### Compass (QMC5883L)
- I2C on GPIO21/22, 200 Hz continuous mode, `atan2(y,x)` heading normalized to 0–360°
- Telemetry output, data available for future UI integration

### Sensors
- **Fuel** — ADC on GPIO32, exponential moving average, configurable calibration table (up to 20 ADC → liter points)
- **Engine temp** — NTC thermistor on GPIO36, Steinhart-Hart math, configurable R/BETA values
- **Battery** — ADC on GPIO35, formula: `raw × 3.3/4095 × 5.7 + 0.2`, sampled every 500 ms
- **Acceleration timer** — 3-state machine (READY/RUNNING/FINISHED) with configurable start/target speeds, auto-rearm
- **Fuel economy** — Instant KM/L (3 s samples) + average KM/L (trip-based), refuel detection

### UI (ILI9488 480×320, LovyanGFX)
- **Dashboard:** Large speed (50 pt DS_DIGIT font), odometer, clock/date, satellite count, battery icon + voltage, acceleration timer, instant/average KM/L
- **Sidebars:** Left = engine temp vertical bar (gradient, ~10–110°C), Right = fuel level bar (gradient, 0–100%)
- **Badges:** HALL, GPS, WiFi status indicators
- **Icons:** Battery, calendar, clock, map pin, WiFi, wheel — all procedurally drawn
- **Anti-aliased primitives:** Lines, circles, arcs, rounded rects, color blending utilities
- **Night mode:** Auto-dims backlight between configurable hours
- **Self-test:** Overrides display to "8/88" patterns with backlight ramp
- **FPS overlay:** Toggleable, shows instant/avg FPS + CPU temp

### Web Server (Soft AP `Dashboard_Config`)
- 8 REST endpoints — config GET/POST, time sync, reboot, sleep, factory reset, serial monitor stream
- Full config UI with 8 collapsible sections, interactive sliders for layout offsets, autosave (2 s debounce), backup/import, factory reset

### Configuration (NVS)
- 50+ parameters: display rotation/speed/colors, sensor tuning (ADC mapping, wheel circumference, NTC params), UI layout offsets, signatures, fuel/temp bar ranges and colors
- `processConfig()` 3-mode loader/serializer/deserializer

### System
- **CPU scaling** — Dynamic 80/160/240 MHz based on FPS vs target; WiFi forces 240 MHz
- **Power management** — GPIO34 ignition sense → deep sleep with EXT0 wakeup, reboot
- **Deep sleep / reboot** screens with progress animation
- **Demo mode** — Full sensor simulation (oscillating values for all fields)
- **Telemetry logging** — 4096-byte ring buffer, 500 ms output, web serial monitor
- **2-core task architecture** — Sensors on Core 0, display/loop on Core 1

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

## Build

```bash
platformio run
platformio run --target upload
platformio device monitor
```

## License

© alefinot — Dashboard++
