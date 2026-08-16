# Dashboard++ for ESP32

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Core-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)
[![MCU](https://img.shields.io/badge/MCU-ESP32--WROOM--32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Display](https://img.shields.io/badge/Display-ILI9488--TFT--480x320-red.svg)](https://github.com/lovyan03/LovyanGFX)
[![License](https://img.shields.io/badge/License-MIT-brightgreen.svg)]()

**Dashboard++** is an ultra-high-performance, production-grade automotive digital instrument cluster and telemetry solution designed for motorcycles, cars, and custom electric/combustion vehicles powered by the **ESP32 WROOM-32** dual-core microcontroller.

Featuring a 4.0-inch ILI9488 TFT display (480×320 resolution) driven over a 60 MHz SPI bus using **LovyanGFX** with PROGMEM-mapped VLW fonts, Dashboard++ blends real-time multi-sensor fusion algorithms, sprite-based anti-aliased digit rendering, dirty-rendering optimizations, hysteresis-based dynamic CPU frequency scaling, persistent NVS configuration management, an Open-Meteo live weather widget, and a complete single-page Web application served over WiFi with REST API control, NVS backup/restore, and automatic cloud OTA pull.

---

## Technical Table of Contents
1. [Executive Overview](#executive-overview)
2. [System Architecture & Task Allocation](#system-architecture--task-allocation)
3. [Hardware Component Specifications](#hardware-component-specifications)
4. [Hardware Pinout Matrix](#hardware-pinout-matrix)
5. [Software Architecture & Mathematical Models](#software-architecture--mathematical-models)
   - [Dual-Source Speed Fusion & Odometer Engine](#1-dual-source-speed-fusion--odometer-engine)
   - [Anti-Aliased GFX Engine & 7-Segment Fonts](#2-anti-aliased-gfx-engine--7-segment-fonts)
   - [Piecewise Linear Fuel Calibration & Filtering](#3-piecewise-linear-fuel-calibration--filtering)
   - [Steinhart-Hart Coolant Temperature Math](#4-steinhart-hart-coolant-temperature-math)
   - [Fuel Economy & Performance Drag Timer](#5-fuel-economy--performance-drag-timer)
   - [Power Management & Dynamic CPU Scaling](#6-power-management--dynamic-cpu-scaling)
6. [Embedded Web UI & REST API Reference](#embedded-web-ui--rest-api-reference)
7. [NVS Configuration Parameter Reference](#nvs-configuration-parameter-reference)
8. [Codebase Architecture & File Map](#codebase-architecture--file-map)
9. [Build, Installation & Flashing Guide](#build-installation--flashing-guide)
10. [Simulation & Demo Mode](#simulation--demo-mode)
11. [License & Credits](#license--credits)

---

## Executive Overview

Dashboard++ replaces legacy analog or basic digital gauges with an automotive-grade telemetry console. Key capabilities include:

- **Dual Speed Fusion (Hall Effect + GNSS):** Dynamically fuses microsecond-level hardware interrupt pulses with NMEA GPS speed vectors, adjusting confidence based on satellite lock quality ($N_{\text{sat}}$) and cross-checking delta errors.
- **Sprite-Based Anti-Aliased Speed Rendering:** The main speed readout is pre-rendered to an off-screen LGFX_Sprite using a 120px VLW 7-segment digital font, then pushed to the display in a single DMA transfer — reducing SPI bus contention and eliminating per-digit draw calls.
- **PROGMEM VLW Font System:** All fonts (Conthrax SemiBold and DS-DIGIT variants) are compiled into flash as PROGMEM byte arrays (generated from the `.vlw` sources in `data/Fonts/` by `scripts/vlw_to_header.py`), loaded at build time via `loadVLWFont()` — zero DRAM per glyph, no runtime filesystem lookups.
- **Dirty-Rendering Frame Pipeline:** Element state tracking ensures only mutated visual regions are drawn to the SPI bus, with dedicated per-element refresh rate throttles for speed, satellite count, timer, battery, fuel economy, and average speed.
- **Configurable 7-Segment Digital Fonts:** 120px (sprite) and 28px (direct) seven-segment fonts with background "ghost digit" rendering (888 backdrop effect) and fully user-configurable integer and decimal digit boundaries for all telemetry counters.
- **Full Sensor Suite Integration:** Precise fuel level monitoring (20-point piecewise linear calibration table + EMA filtering), engine coolant thermistor telemetry (Steinhart-Hart equation), battery voltage divider monitoring, 3-axis I2C digital compass heading, trip fuel economy tracking, and **trip average speed** display.
- **FreeRTOS Dual-Core Multitasking:** Strict core isolation — the continuous ~1.1KB/s GNSS UART stream is quarantined in its own Core 0 task (bulk ring-buffer read, bounded per tick, sitting below the WiFi stack), while the vehicle sensor suite and UI rendering run on Core 1, so a GPS stream backlog can never stall the dashboard or freeze any real-time value.
- **Embedded Web Management Portal:** Embedded single-page Web application accessible over SoftAP or local WiFi network featuring grouped card-based configuration UI with live search, real-time performance telemetry panel (FPS, CPU frequency/temp, RAM, flash storage), interactive sliders, color pickers, NVS backup/restore, web serial terminal stream, cloud OTA pull, and HTTP file upload OTA firmware updating.
- **Hysteresis-Based Dynamic CPU Scaling:** Three-state frequency governor (240/160/80 MHz) with hysteresis deadbands prevents oscillation, and thermal throttling automatically caps frequency at configurable warning/critical temperature thresholds.

---

## System Architecture & Task Allocation

The system leverages the ESP32's Xtensa dual-core processor via FreeRTOS tasks to guarantee deterministic sensor sampling without visual stuttering or UI delays.

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                            ESP32 WROOM-32 DUAL-CORE                             │
├────────────────────────────────────────┼────────────────────────────────────────┤
│    CORE 0  (GNSS Stream & Network)     │  CORE 1  (Vehicle Sensors & Display)   │
├────────────────────────────────────────┼────────────────────────────────────────┤
│┌──────────────────────────────────────┐│┌──────────────────────────────────────┐│
││ Task: GpsTaskCore0 (Prio 2, 10KB)    │││ Task: SensorTaskCore1 (Prio 2, 10KB) ││
││                                      │││                                      ││
││• UART2 @ 115200 bulk ring drain      │││• Hall Sensor GPIO33 ISR              ││
││  (readBytes() batch, 1024 B/tick cap)│││• QMC5883L Magnetometer               ││
││• TinyGPS++ NMEA + UBX NAV-PVT parse  │││  (I2C @ 200 Hz)                      ││
││• Speed Fusion & Odometer calc        │││• ADC: Fuel (32), Temp (36), Bat (35) ││
││• GNSS Epoch Time Sync -> settimeofday│││• Fuel Economy & Accel Timer          ││
││• Safe Thread Sync via g_stateMutex   │││• Trip Average Speed                  ││
││                                      │││• Safe Thread Sync via g_stateMutex   ││
││──────────────────────────────────────│││──────────────────────────────────────││
││ Task: WebTaskCore0 (Prio 1, 12KB)    │││ Main Loop (Priority 1)               ││
││                                      │││                                      ││
││• SoftAP (Dashboard_Config) & STA     │││• Dirty-Rendering Frame Pipeline      ││
││• WebServer HTTP Handlers             │││• LovyanGFX SPI Driver (60 MHz)       ││
││• ArduinoOTA Firmware Listener        │││• Sprite-Based Speed (120px VLW)      ││
││• Serial Log Streaming API            │││• Hysteresis CPU Scaling (80/160/240) ││
│└──────────────────────────────────────┘││• Auto Night-Mode Backlight PWM       ││
│                                        ││• Ignition Deep Sleep State Machine   ││
│                                        ││• Telemetry Logging to 4KB Ring       ││
│                                        │└──────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────────┘
```

> [!NOTE]
> The GNSS task sits on Core 0 **below** the WiFi/HTTPD/LWIP stack on purpose: the module streams
> ~1.1KB/s continuously and the drain is I/O-gated at the byte arrival rate, so it can never
> out-run the stream. GPS is 1Hz-tolerant data, so if the network preempts the drain, only GPS
> values lag — the vehicle sensors and the screen are never touched.

> [!NOTE]
> The **weather fetch task** (Core 0, spawned by `startWeatherFetch()` on Core 1) polls the Open-Meteo
> API on a configurable interval (`WEATHER_REFRESH_MIN`) and stores the result in a dedicated
> `g_weatherData` struct. A 30 s hung-fetch guard kills stuck tasks; a config save triggers an
> instant refetch with a 2 s retry backoff.

---

## Hardware Component Specifications

| Component | Part / Model | Protocol / Signal | Specifications |
| :--- | :--- | :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 | Xtensa 32-bit LX6 | Dual-core 240 MHz, 520 KB SRAM, 4 MB SPI Flash, RTC IO |
| **Display Panel** | ILI9488 TFT LCD (4.0") | SPI (16-bit RGB565) | 480×320 pixels, 60 MHz SPI bus speed, hardware CS/DC/RST |
| **Display Backlight** | LED Backlight Channel | LEDC PWM (Channel 0) | 1 kHz hardware PWM, 256 brightness levels, logarithmic fading |
| **GNSS Module** | BZGNSS P25 Pro (u-blox M10) | UART2 (RX=25, TX=26) | 115200 baud (configurable), NMEA 0183 (forced at boot via UBX), 10 Hz update rate, multi-constellation (GPS/GLONASS/BDS/Galileo), UTC epoch time synchronization |
| **Digital Compass** | QMC5883L (built into BZGNSS P25 Pro) | I2C (SDA=21, SCL=22) | 3-axis magnetometer, 0x0D address, 200 Hz continuous mode, configurable declination offset |
| **Wheel Speed Sensor** | Hall Effect Interrupt | GPIO33 (Input Pullup) | Hardware Falling-Edge ISR, microsecond interval timing |
| **Fuel Level Sensor** | Capacitive / Resistive Sender | GPIO32 (ADC1_CH4) | Analog 0–3.3V, 20-point touch table, EMA smoothing filter |
| **Engine Temp Sensor** | NTC Thermistor (10k/100k) | GPIO36 (ADC1_CH0) | Analog 0–3.3V, Steinhart-Hart equation, voltage divider balance |
| **Battery Voltage** | Voltage Divider (5.7:1) | GPIO35 (ADC1_CH7) | Analog 0–3.3V, range 0–18.8V DC, sampled every 500 ms |
| **Power / Ignition** | Ignition Key Sense Line | GPIO34 (RTC_GPIO4) | High=Ignition ON, Low=Power Lost → Animated Deep Sleep |

---

## Hardware Pinout Matrix

| ESP32 Pin | Function Name | Peripheral Type | Signal Direction | Hardware Configuration & Notes |
| :---: | :--- | :--- | :---: | :--- |
| **GPIO4** | `POWER_SENSE_PIN` | Power Sense | Input (No Pull) | Ignition sense line; triggers EXT0 RTC wake up from deep sleep |
| **GPIO5** | `CS_DISPLAY` | SPI Chip Select | Output | Hardware SPI CS for ILI9488 Display |
| **GPIO12** | `BL_DISPLAY` | Backlight PWM | Output | Attached to ESP32 LEDC Channel 0 (1 kHz PWM) |
| **GPIO14** | `SPI_RST` | Display Reset | Output | Active-Low hardware reset line for ILI9488 |
| **GPIO18** | `SPI_CLK` | SPI Clock | Output | Hardware SPI SCK pin (60 MHz) |
| **GPIO21** | `COMPASS_SDA` | I2C Data | Bi-directional | QMC5883L I2C SDA — wired to the GNSS module's SDA pin (built-in compass) (Requires external/internal 4.7k pullups) |
| **GPIO22** | `COMPASS_SCL` | I2C Clock | Output | QMC5883L I2C SCL — wired to the GNSS module's SCL pin (built-in compass) |
| **GPIO23** | `SPI_MOSI` | SPI Master Out | Output | Hardware SPI MOSI pin for LCD data command stream |
| **GPIO25** | `RXD2` | GPS Serial RX | Input | Connected to GNSS Module TX pin (UART2) |
| **GPIO26** | `TXD2` | GPS Serial TX | Output | Connected to GNSS Module RX pin (UART2) |
| **GPIO27** | `SPI_DC` | Data / Command | Output | High = Data, Low = Command for ILI9488 controller |
| **GPIO32** | `FUEL_TOUCH_PIN` | Fuel ADC | Input | Dedicated ADC1 Channel 4 pin for fuel level reading |
| **GPIO33** | `HALL_SENSOR_PIN` | Hall Interrupt | Input (Pullup) | Falling-edge hardware interrupt for wheel magnet pulses |
| **GPIO34** | `LIGHT_SENSOR_PIN` | Ambient Light | Input (No Pull) | LDR ambient light sensor for auto-brightness (calibrated via `/api/ambient/cal-dark` / `cal-bright`) |
| **GPIO35** | `BATTERY_SENSE_PIN`| Battery ADC | Input (No Pull) | Connected to 5.7:1 precision resistor divider node |
| **GPIO36** | `TEMP_SENSE_PIN` | Engine Temp ADC | Input (No Pull) | Connected to NTC thermistor / balance resistor divider node |

> [!IMPORTANT]
> GPIO32 is dedicated to the fuel ADC input to avoid pin-sharing conflicts with the GPIO33 Hall interrupt hardware line.

> [!NOTE]
> The current code carries temporary isolation-test pins for `RXD2` (GPIO16), `TXD2` (GPIO17), `COMPASS_SDA` (GPIO13), and `COMPASS_SCL` (GPIO15), as flagged in `dashboard.h`. The matrix above reflects the board's intended wiring (GPIO21/22 for compass I²C, GPIO25/26 for GNSS serial).

---

## Software Architecture & Mathematical Models

### 1. Dual-Source Speed Fusion & Odometer Engine

Dashboard++ features a dual-source speed calculation engine that combines low-latency wheel rotation timing with absolute satellite GPS telemetry.

#### Hall Sensor Calculation
The speed calculated from Hall pulses is derived from microsecond timing between consecutive interrupts:
$$V_{\text{hall}} = \frac{K_{\text{wheel}}}{\Delta t_{\text{pulse}}} \quad [\text{km/h}]$$
where $K_{\text{wheel}}$ is the wheel speed scaling factor computed from the wheel circumference $C_{\text{mm}}$:
$$K_{\text{wheel}} = \left(\frac{C_{\text{mm}}}{10^6\text{ km}}\right) \times \left(3.6 \times 10^9\text{ }\mu\text{s/h}\right) = 3600 \times C_{\text{mm}}$$

#### Dual-Source Fusion Logic
Speed fusion evaluates satellite quality $N_{\text{sat}}$ against configured bounds $N_{\text{min}}$ (`MIN_SATELLITES`, default=5) and $N_{\text{opt}}$ (`OPTIMAL_SATELLITES`, default=8):

$$\text{SpeedSourceMode} = \begin{cases} 
\text{GPS (1)}, & \text{if } N_{\text{sat}} \ge N_{\text{opt}} \text{ and } V_{\text{gps}} \text{ is valid} \\
\text{G+H Fusion (2)}, & \text{if } N_{\text{min}} \le N_{\text{sat}} < N_{\text{opt}} \text{ and } |V_{\text{gps}} - V_{\text{hall}}| \le \Delta V_{\text{max}} \\
\text{Hall-Only (0)}, & \text{if } N_{\text{sat}} < N_{\text{min}} \text{ or } |V_{\text{gps}} - V_{\text{hall}}| > \Delta V_{\text{max}}
\end{cases}$$

When in **G+H Fusion mode**, the output speed $V_{\text{fused}}$ uses linear confidence interpolation:
$$W_{\text{gps}} = \frac{N_{\text{sat}} - N_{\text{min}} + 1}{N_{\text{opt}} - N_{\text{min}} + 1}$$
$$V_{\text{fused}} = (W_{\text{gps}} \cdot V_{\text{gps}}) + ((1 - W_{\text{gps}}) \cdot V_{\text{hall}})$$

#### Odometer Persistence Strategy
To protect the ESP32 NVS Flash memory from wear, distance accumulation runs continuously in RAM. The odometer writes to non-volatile storage **only after accumulating a full 1.0 km increment**:
$$\Delta D_{\text{ram}} \ge 1.0\text{ km} \implies \text{Preferences.putDouble("odo", } D_{\text{total}}\text{)}$$

---

### 2. Anti-Aliased GFX Engine & 7-Segment Fonts

Graphics rendering is built on `LovyanGFX` with specialized antialiasing routines and a PROGMEM-mapped VLW font system (compiled into flash — no runtime filesystem lookups, zero DRAM per glyph).

#### Alpha Blending Math
Sub-pixel anti-aliased primitives draw edges with fractional coverage $\alpha \in [0, 1]$. Color blending uses fast integer arithmetic:
$$R_{\text{out}} = \lfloor (R_{\text{bg}} \cdot (256 - a) + R_{\text{fg}} \cdot a) \gg 8 \rfloor$$
$$G_{\text{out}} = \lfloor (G_{\text{bg}} \cdot (256 - a) + G_{\text{fg}} \cdot a) \gg 8 \rfloor$$
$$B_{\text{out}} = \lfloor (B_{\text{bg}} \cdot (256 - a) + B_{\text{fg}} \cdot a) \gg 8 \rfloor$$

#### Sprite-Based Speed Rendering
The main speed display is rendered to an off-screen `LGFX_Sprite` using the VLW 120px 7-segment digital font. Ghost digits (888 backdrop) and active digits are drawn once per frame into the sprite, then pushed to the display in a single DMA transfer — drastically reducing SPI bus transactions compared to per-digit draw calls.

#### VLW Font System
All typography is compiled into flash as PROGMEM byte arrays (`include/*.h`, generated from the `.vlw` sources in `data/Fonts/` by `scripts/vlw_to_header.py`):
- `DS-DIGIT_120px.vlw` → `DS_DIGIT_120px_vlw.h` — Main speed readout (sprite-rendered)
- `DS-DIGIT_28px.vlw` → `DS_DIGIT_28px_vlw.h` — Secondary digits (time, odometer, satellite, battery, fuel economy, average speed)
- `Conthrax_SemiBold_28px.vlw` → `Conthrax_SemiBold_28px_vlw.h` — Section headers and badges
- `Conthrax_SemiBold_16px.vlw` → `Conthrax_SemiBold_16px_vlw.h` — Unit labels and sidebar text
- `Conthrax_SemiBold_10px.vlw` → `Conthrax_SemiBold_10px_vlw.h` — Micro labels (AVG badge, IST badge)

Legacy compiled-in GFX font bitmaps (`Conthrax_SemiBold4pt7b`, `Conthrax_SemiBold7pt7b`) are retained as fallbacks for the badge/label sizes.

---

### 3. Piecewise Linear Fuel Calibration & Filtering

Fuel tank geometries are non-linear. Dashboard++ converts raw ADC inputs into precise volume measurements using an Exponential Moving Average (EMA) and a configurable 20-point touch table.

#### Exponential Moving Average (EMA)
Raw ADC readings $S_t$ from `FUEL_TOUCH_PIN` (GPIO32) are filtered to prevent fuel sloshing fluctuations:
$$\bar{S}_t = (\alpha \cdot S_t) + ((1 - \alpha) \cdot \bar{S}_{t-1})$$
where $\alpha =$ `FUEL_FILTER_ALPHA` (default=0.08).

#### Piecewise Linear Interpolation
Given $N$ touch table points $T[0 \dots N-1]$ mapped to liter indices $0 \dots N-1$:
Find segment $i$ such that $T[i] \ge \bar{S}_t \ge T[i+1]$:
$$L_{\text{fuel}} = i + \left(\frac{\bar{S}_t - T[i]}{T[i+1] - T[i]}\right)$$
$$\text{FuelPercentage} = \text{constrain}\left(\left\lfloor \frac{L_{\text{fuel}}}{N - 1} \times 100 \right\rfloor, 0, 100\right)$$

---

### 4. Steinhart-Hart Coolant Temperature Math

Engine temperature is measured via an NTC thermistor connected in a resistor divider configuration with a balance resistor $R_{\text{balance}}$ (default=10,000 $\Omega$).

#### Voltage Divider Resistance Calculation
$$V_{\text{out}} = \text{ADC}_{\text{raw}} \times \left(\frac{3.3}{4095}\right)$$
$$R_{\text{ntc}} = R_{\text{balance}} \times \left(\frac{V_{\text{out}}}{3.3 - V_{\text{out}}}\right)$$

#### Steinhart-Hart B-Parameter Equation
$$T_{\text{Kelvin}} = \left( \frac{1}{\beta} \cdot \ln\left(\frac{R_{\text{ntc}}}{R_{\text{room}}}\right) + \frac{1}{T_{\text{room}}} \right)^{-1}$$
$$T_{\text{Celsius}} = T_{\text{Kelvin}} - 273.15$$
where $R_{\text{room}} =$ `NTC_R_ROOM` (10,000 $\Omega$), $T_{\text{room}} = 298.15\text{ K}$ ($25^\circ\text{C}$), and $\beta =$ `NTC_BETA` (default=3950).

---

### 5. Fuel Economy & Performance Drag Timer

#### Instantaneous & Trip Fuel Economy
- **Trip Average (KM/L):** $KM/L_{\text{avg}} = \frac{D_{\text{trip}}}{C_{\text{consumed}}}$, auto-resets when refuel is detected ($C_{\text{consumed}} < -0.5\text{ L}$).
- **Instantaneous (KM/L):** Calculated across a 3-second sliding window:
  $$KM/L_{\text{inst}} = 0.4 \cdot \left(\frac{\Delta D_{3\text{s}}}{\Delta C_{3\text{s}}}\right) + 0.6 \cdot KM/L_{\text{inst, prev}}$$
- **Trip Average Speed (KM/H):** $V_{\text{avg}} = \frac{D_{\text{trip}}}{t_{\text{elapsed}}}$, displayed in the bottom row alongside fuel economy readouts.

#### Acceleration Performance Timer
Measures time taken to accelerate between configured speed thresholds (`ACCEL_START_SPEED` to `ACCEL_TARGET_SPEED`, e.g., 0–50 km/h or 0–100 km/h) using a 3-state state machine with configurable timeout (`ACCEL_MAX_TIME`, default=30s):

```
  ┌─────────┐    Speed >= StartSpeed     ┌─────────┐
  │  READY  │ ─────────────────────────> │ RUNNING │
  └─────────┘                            └─────────┘
       ▲                                      │
       │ Speed < Start (Reset)                │ Speed >= TargetSpeed OR Timeout
       └──────────────────────────────────────┼───────────────────────┐
                                               ▼                       │
                                         ┌──────────┐                  │
                                         │ FINISHED │ <────────────────┘
                                         └──────────┘
```

---

### 6. Power Management & Dynamic CPU Scaling

To balance processing performance, power consumption, and thermal stability, Dashboard++ includes dynamic CPU management and ignition sensing.

#### Hysteresis Dynamic Scaling Matrix

The governor uses a 3-state machine with hysteresis deadbands to prevent frequency oscillation:

```
                      ┌──────────────────────────────┐
                      │ Read Current Measured FPS &  │
                      │ Die Temp (temperatureRead()) │
                      └──────────────┬───────────────┘
                                     │
                     Is ENABLE_DYNAMIC_CPU true?
                     ├─── NO  ──> Set MANUAL_CPU_FREQ (80 / 160 / 240 MHz)
                     └─── YES ──> Check current state:
                                    ├── State = 240 MHz:
                                    │    └── FPS > 85% Target → 160 MHz
                                    │    └── else → stay at 240 MHz
                                    ├── State = 160 MHz:
                                    │    ├── FPS < 65% Target → 240 MHz
                                    │    ├── FPS > 95% Target → 80 MHz
                                    │    └── else → stay at 160 MHz
                                    └── State = 80 MHz:
                                         ├── FPS < 45% Target → 240 MHz
                                         ├── FPS < 70% Target → 160 MHz
                                         └── else → stay at 80 MHz
                                    │
                                    └── Apply Thermal Throttling:
                                          ├── Temp >= CRIT (70°C): Force 80 MHz
                                          └── Temp >= WARN (60°C): Cap at 160 MHz
```

#### Ignition Loss & Deep Sleep Transition
When `ENABLE_POWER_SENSE` is enabled and `POWER_SENSE_PIN` (GPIO34) drops LOW:
1. Plays goodbye screen animation (`showGoodbyeScreen(true)`).
2. Fades display backlight down to 0% via LEDC PWM.
3. Configures RTC EXT0 wake-up trigger on GPIO34 (High level).
4. Invokes `esp_deep_sleep_start()`.

---

## Embedded Web UI & REST API Reference

Dashboard++ embeds a single-page management portal directly into flash memory (`index_html`).

### WiFi Network Connectivity
- **SoftAP Mode:** Emits AP SSID `Dashboard_Config` (Default IP: `192.168.4.1`).
- **Multi-SSID Client Mode:** Can store up to 4 fallback WiFi network profiles (`WIFI_SSID_1` through `WIFI_SSID_4`). Automatically attempts connection on boot.

### Web UI Features
The management portal features a modern grouped card-based layout:
- **Collapsible sections** with smooth accordion animations (non-JS fallback)
- **Live search bar** to filter configuration parameters across all sections
- **Autosave** triggered 2 seconds after any input change
- **Color pickers** with inline preview for all UI color values
- **Slider + number inputs** with mouse-wheel scroll protection for all range parameters
- **XY offset controls** with linked sliders for UI element positioning
- **Real-time performance panel** displaying FPS, CPU frequency/temperature, RAM usage, flash storage utilization, and live serial output monitor
- **Weather Widget group** with city, latitude/longitude, refresh interval, locale, and a day/night icon
- **Compass calibration** (start / cancel / live status) and **ambient light calibration** (dark / bright reference points)
- **Cloud OTA pull** controls (enable, URL, interval) with live status polling
- **OTA firmware upload** via file picker
- **NVS backup/restore** (export/import JSON)

### REST API Endpoints

| Endpoint | Method | Description | Request Payload / Params | Content-Type |
| :--- | :---: | :--- | :--- | :--- |
| `/` | `GET` | Serves pre-gzipped single-page Web UI | None | `text/html` |
| `/debug` | `GET` | Dump first bytes of the pre-gzipped UI buffer (build sanity check) | None | `text/plain` |
| `/api/config` | `GET` | Exports complete NVS configuration | None | `application/json` |
| `/api/config` | `POST` | Updates NVS parameters and applies changes | Config JSON object | `application/json` |
| `/api/time` | `POST` | Syncs system clock from browser | `?epoch=1700000000` | `text/plain` |
| `/api/odo` | `GET` | Reads odometer distance in km | None | `application/json` |
| `/api/odo` | `POST` | Sets odometer distance | `{"km": 123.45}` | `application/json` |
| `/api/reboot` | `POST` | Triggers graceful device restart | None | `text/plain` |
| `/api/sleep` | `POST` | Triggers immediate deep sleep | None | `text/plain` |
| `/api/reset` | `POST` | Performs factory reset (clears NVS) | None | `text/plain` |
| `/api/ambient` | `GET` | Reads raw ambient light sensor value | None | `application/json` |
| `/api/ambient/cal-dark` | `POST` | Sets dark-reference ambient light value (auto-brightness floor) | None | `text/plain` |
| `/api/ambient/cal-bright` | `POST` | Sets bright-reference ambient light value (auto-brightness ceiling) | None | `text/plain` |
| `/api/compass/cal-start` | `POST` | Starts a ~30 s two-sided compass calibration | None | `text/plain` |
| `/api/compass/cal-cancel` | `POST` | Cancels an in-progress compass calibration | None | `text/plain` |
| `/api/compass/cal-status` | `GET` | Live calibration state (min/max per axis, offsets, result) | None | `application/json` |
| `/api/ota` | `POST` | Over-The-Air firmware binary upload | Binary `.bin` payload | `multipart/form-data` |
| `/api/ota/pull` | `POST` | Triggers cloud OTA pull (checks `OTA_PULL_URL`) | None | `text/plain` |
| `/api/ota/check` | `GET` | Reports cloud OTA pull state (enabled, URL, version, status) | None | `application/json` |
| `/api/serial` | `GET` | Streams internal 4 KB ring buffer logs | None | `text/plain` |
| `/api/perf` | `GET` | Live telemetry (CPU, Heap, FPS, WiFi, partitions) | None | `application/json` |
| `/api/health` | `GET` | Quick heap / mem-saver / uptime health probe | None | `application/json` |

---

## NVS Configuration Parameter Reference

Dashboard++ uses a generic 3-mode macro system (`processConfig()`) to load, serialize, and deserialize over 60 configuration variables from ESP32 NVS flash storage.

### Key Configuration Categories

#### System & Performance
- `TARGET_FPS` (default=60): Desired display refresh rate (up to ~500 FPS supported, hardware-limited).
- `SPI_BUS_SPEED` (default=60000000): SPI bus frequency in Hz.
- `ENABLE_DYNAMIC_CPU` (default=false): Toggles automatic CPU frequency scaling (hysteresis-based).
- `MANUAL_CPU_FREQ` (default=240): Fixed CPU clock frequency (80, 160, or 240 MHz).
- `ENABLE_CPU_THROTTLE` (default=true): Enables thermal frequency capping.
- `CPU_THROTTLE_TEMP_WARN` (default=60): Warning temperature threshold in °C.
- `CPU_THROTTLE_TEMP_CRIT` (default=70): Critical temperature threshold in °C.

#### Display & Visual Design
- `DISPLAY_ROTATION` (default=3): Screen rotation (0, 1, 2, 3).
- `BACKLIGHT_BRIGHTNESS` (default=100): Backlight duty cycle (0–100%).
- `ENABLE_ANTIALIASING` (default=true): Anti-aliased line rendering toggle.
- `AA_SHARPNESS` (default=1.0): Anti-aliasing gamma correction factor.
- `GHOST_COLOR_STR` (default="#212021"): Hex color code for inactive 7-segment digit background.
- `COLOR_TEMP_NORM`, `COLOR_TEMP_WARN`, `COLOR_TEMP_CRIT`: Hex color strings for engine temperature gradient bar.
- `COLOR_FUEL_NORM`, `COLOR_FUEL_WARN`, `COLOR_FUEL_CRIT`: Hex color strings for fuel status bar.

#### Sensors & Vehicle Calibration
- `WHEEL_CIRCUMFERENCE_MM` (default=1650.0): Tire rolling circumference in millimeters.
- `FUEL_FILTER_ALPHA` (default=0.08): EMA filter coefficient for raw fuel readings.
- `FUEL_TOUCH_POINTS` (default=8): Number of valid entries in the calibration touch table.
- `NTC_R_BALANCE` (default=10000.0): Balance resistor value in ohms for NTC divider.
- `NTC_BETA` (default=3950.0): Thermistor Beta coefficient.
- `GPS_BAUD` (default=115200): UART baud rate for the GNSS module. On boot the module is forced into NMEA output at this baud with a 10 Hz update rate via u-blox UBX commands (compatible with BZGNSS P25 Pro / M10 receivers).
- `COMPASS_DECLINATION_DEG` (default=0.0): Magnetic declination offset applied to the compass heading.
- `REFRESH_COMPASS_MS` (default=200): Throttle for the compass heading widget redraw.
- `HEADING_DIGITS` (default=3): Integer digit count for the compass heading readout.
- `MIN_SATELLITES` (default=5): Minimum GPS satellite lock requirement.
- `OPTIMAL_SATELLITES` (default=8): Satellite count threshold for full GPS speed reliance.
- `MAX_SPEED_DELTA_KMH` (default=5.0): Maximum allowable difference between GPS and Hall speed before falling back.
- `ACCEL_MAX_TIME` (default=30.0): Acceleration timer maximum duration in seconds before auto-finish.

#### Weather Widget (Open-Meteo)
- `SHOW_ELEMENT_WEATHER` (default=true): Toggle the weather widget on/off.
- `WEATHER_CITY` (default=""): City label shown in the widget (GPS-geocoded when empty).
- `WEATHER_LAT` (default=0.0) and `WEATHER_LON` (default=0.0): Coordinates for the Open-Meteo forecast request.
- `WEATHER_REFRESH_MIN` (default=15): Refresh interval in minutes.
- `WEATHER_LOCALE` (default="en"): ISO locale code for weather-condition naming.

#### WiFi & Cloud OTA
- `WIFI_RETRY_MODE` (default=1): Search policy — `0` = one cycle, `1` = fixed-time (`WIFI_RETRY_SECONDS`), `2` = search forever. Same policy governs reconnects after a lost link.
- `WIFI_RETRY_SECONDS` (default=300): Elapsed-search budget for policy `1` (seconds).
- `OTA_PULL_ENABLED` (default=false): Toggle automatic cloud pull.
- `OTA_PULL_URL` (default=""): HTTPS URL of the target firmware binary.
- `OTA_PULL_INTERVAL_HOURS` (default=24): How often the cloud pull checks for an update.
- `OTA_CURRENT_VERSION` (default="1.2.6"): Version string compared against the cloud manifest.

#### Ambient Light (Auto-Brightness)
- `LIGHT_SENSOR_DARK_VAL`: Dark-reference ambient light value (calibrated via `/api/ambient/cal-dark`).
- `LIGHT_SENSOR_BRIGHT_VAL`: Bright-reference ambient light value (calibrated via `/api/ambient/cal-bright`).

#### Digit Boundaries (Configurable 7-Segment Formatting)
- `SPEED_DIGITS`, `SAT_DIGITS`, `TMR_INT_DIGITS`, `TMR_DEC_DIGITS`, `BAT_INT_DIGITS`, `BAT_DEC_DIGITS`, `INST_INT_DIGITS`, `INST_DEC_DIGITS`, `AVG_INT_DIGITS`, `AVG_DEC_DIGITS`, `AVG_SPEED_INT_DIGITS`, `AVG_SPEED_DEC_DIGITS`, `FUEL_INT_DIGITS`, `FUEL_DEC_DIGITS`, `ODO_INT_DIGITS`, `ODO_DEC_DIGITS`: Configurable integer and decimal digit limits for all UI numerical readouts.

#### UI Layout Offset Coordinates
- `BIG_CENTER_X`, `BIG_CENTER_Y`: Screen anchor origin point.
- `OFFSET_BIG_TIME_X/Y`, `OFFSET_BIG_DATE_X/Y`, `OFFSET_BIG_SPEED_NUM_X/Y`, `OFFSET_BIG_SPEED_UNIT_X/Y`, `OFFSET_BIG_ODO_X/Y`, `OFFSET_BIG_SAT_X/Y`, `OFFSET_BIG_TMR_X/Y`, `OFFSET_BIG_BAT_X/Y`, `OFFSET_INST_KML_X/Y`, `OFFSET_AVG_KML_X/Y`, `OFFSET_AVG_SPEED_X/Y`, `OFFSET_FUEL_LTRS_X/Y`, `OFFSET_COMPASS_X/Y`, `SIDEBAR_LEFT_X/Y`, `SIDEBAR_RIGHT_X/Y`: Fine-grained pixel coordinate offsets for every UI component.

---

## Codebase Architecture & File Map

```
Dashboard++ for ESP32/
├── platformio.ini         # PlatformIO project configuration & dependencies
├── partitions.csv         # Custom flash partition table (OTA + LittleFS)
├── dashboard_backup.json  # Reference JSON configuration backup template
├── data/
│   └── Fonts/             # VLW font sources (compiled to PROGMEM by scripts/vlw_to_header.py)
│       ├── DS-DIGIT_120px.vlw         # 120px 7-segment font (speed sprite)
│       ├── DS-DIGIT_28px.vlw          # 28px 7-segment font (time/odo/telemetry)
│       ├── Conthrax_SemiBold_28px.vlw # 28px header font
│       ├── Conthrax_SemiBold_16px.vlw # 16px label font
│       └── Conthrax_SemiBold_10px.vlw # 10px micro label font
├── scripts/
│   ├── gzip_webui.py      # Pre-gzips webui.html -> webui_html_gz.h (~93 KB raw -> ~20 KB)
│   └── vlw_to_header.py   # Compiles .vlw font files into PROGMEM C headers
├── include/
│   ├── Conthrax_SemiBold7pt7b.h  # GFXfont fallback (Small badge size)
│   ├── Conthrax_SemiBold4pt7b.h  # GFXfont fallback (Micro label size)
│   ├── DS_DIGIT_120px_vlw.h      # 120px 7-segment VLW font (PROGMEM)
│   ├── DS_DIGIT_28px_vlw.h       # 28px 7-segment VLW font (PROGMEM)
│   ├── Conthrax_SemiBold_28px_vlw.h  # 28px VLW font (PROGMEM)
│   ├── Conthrax_SemiBold_16px_vlw.h  # 16px VLW font (PROGMEM)
│   └── Conthrax_SemiBold_10px_vlw.h  # 10px VLW font (PROGMEM)
├── lib/
│   ├── ArduinoJson/       # Optimized embedded JSON parser/serializer library
│   └── TinyGPSPlus/       # NMEA 0183 GPS stream parser library
└── src/
    ├── dashboard.h        # Central global header, structure definitions, API declarations
    ├── main.cpp           # System setup(), dual FreeRTOS task spawns, main display loop
    ├── config.cpp         # NVS parameter storage, JSON serialization/deserialization engine
    ├── gfx.cpp            # LovyanGFX display device class, PROGMEM VLW font loader, AA primitives, icons
    ├── sensors.cpp        # Core 0 GPS task (bulk UART drain, TinyGPS++/UBX parser, speed fusion, odo, time-sync) + Core 1 sensor task (Hall ISR, QMC5883L compass, ADC sensors, snapshot)
    ├── ui.cpp             # Dirty-rendering dashboard visual layout engine
    ├── web.cpp            # SoftAP/STA WiFi manager, REST API handlers, cloud OTA pull, embedded Web UI
    └── webui.html         # Single-page Web UI source (gzipped at build time by scripts/gzip_webui.py)
```

---

## Build, Installation & Flashing Guide

### Prerequisites
1. Install [PlatformIO Core](https://platformio.org/install/cli) or VS Code with the PlatformIO extension.
2. Install Python 3.8+ and USB-to-UART bridge drivers (CP210x or CH340).

### Partition Table
The project uses a custom `partitions.csv` with:
- Two OTA app slots (0x1D0000 each)
- A 320 KB LittleFS partition (0x50000), retained only for `/api/health` byte reporting (fonts are now PROGMEM)

### Compiling & Flashing via USB

```bash
# Clone the repository
git clone https://github.com/alefinot/Dashboard-for-ESP32.git
cd Dashboard-for-ESP32

# Compile project source code
platformio run

# Flash firmware binary to ESP32 over serial USB
platformio run --target upload

# Open serial device monitor (115200 baud)
platformio device monitor
```

> [!NOTE]
> Fonts are compiled into flash as PROGMEM arrays at build time (`scripts/vlw_to_header.py`), so no `uploadfs` step is needed for text. The LittleFS partition remains in `partitions.csv` (used only for `/api/health` byte reporting).

### Over-The-Air (OTA) Updates

#### Via PlatformIO CLI
Ensure your host machine is connected to the `Dashboard_Config` WiFi network or local network:

```bash
platformio run -t upload --upload-port 192.168.4.1
```

#### Via Web Management Portal
1. Open a browser and navigate to `http://192.168.4.1` (or your assigned STA IP).
2. Open the **System Actions** section.
3. Click **Choose File**, select the `.pio/build/esp32dev/firmware.bin` file, and click **Upload**.

---

## Simulation & Demo Mode

Dashboard++ includes an integrated simulation engine for bench testing UI rendering without requiring connected physical hardware.

To enable Demo Mode:
1. Open the Web UI at `http://192.168.4.1`.
2. Expand the **System & Modes** section.
3. Enable **Demo Mode**.
4. Click **Save Settings** (or wait 2 seconds for autosave).

In Demo Mode:
- Speed oscillates synthetically between 10 km/h and 110 km/h using sinusoidal formulas.
- Engine temperature, fuel level, battery voltage, compass heading, satellite count, instant/avg KM/L, and average speed simulate active riding telemetry.
- Speed source indicator badge automatically cycles between `HAL`, `GPS`, and `G+H` every 2 seconds.

---

## Changelog

### V1.2.7 — Fuel smoothing alpha + web UI reorganization
- **Fuel smoothing alpha** — fuel-level EMA smoothing alpha is now configurable live in the Fuel Sensor card (tune it without a reflash).
- **Web UI settings reorganization** — settings regrouped into the five card groups (System & General, WiFi and Connectivity, Display & Colors, Sensors Tuning, UI Layout); time, weather, auto-brightness, and fuel-alpha controls each moved to their correct home.
- **Factory-default seeding** — new `CFG_VER 5` factory-default seeding pulls initial values from `dashboard_backup.json` on first boot.
- **Configurable WiFi search policy** — new `WIFI_RETRY_MODE` (one cycle / fixed time / search forever) with auto-reconnect on lost link; the old "AUTO DISABLE WIFI" setting is removed (STA is always on).
- **Web UI cleanup** — removed the dead `buildPerfPanel` JS from the pre-gzipped webui.

### V1.2.6 — Dynamic weather widget layout
- **Weather widget layout** — temp/humidity/wind/sunset stats render with an equal-spaced layout, plus a quadratic city-name fade and an ASCII-safe degree ring.
- **Instant weather refetch** — saving a weather config triggers an instant refetch with a 2 s retry backoff.
- **Hung-fetch guard** — a 30 s guard kills stuck weather-fetch tasks.
- **Fixed-width source badge** — the speed-source badge (HAL / GPS / G+H) is now fixed-width so the layout doesn't shift.
- **Satellite-count badge** — satellite count renders as an icon + Conthrax 10px badge.
- **Budget optimization** — hidden UI elements now skip the frame budget calculation entirely.

### V1.2.5 — PROGMEM fonts + weather geocoding
- **PROGMEM VLW fonts** — all VLW fonts (10/16/28px) streamed from PROGMEM headers (zero DRAM per glyph), generated from the `.vlw` sources by `scripts/vlw_to_header.py`.
- **Ghost-digit time/date** — fixed-slot ghost-digit rendering for the time/date row, with a configurable `SH_GHOST` alpha.
- **NTC temp EMA smoothing** — smooths the sidebar temperature and kills the flicker.
- **Satellite GPS icon** — a satellite-count GPS icon.
- **Configurable weather city language** — new `WEATHER_LOCALE` param.
- **Factory reset** — now preserves WiFi credentials across resets; the hardcoded WiFi password default is removed.
- **GPS-geocoded weather city** — the weather city is geocoded from the GPS position, with a configurable fetch interval.
- **Day/night weather icon** — real sunrise/sunset-based day/night icon.
- **Dropped** — web config PIN auth and legacy pt7b font fallbacks.

### V1.2.1 — Low-RAM overhaul
- **Zero-DRAM 120px font** — the 120px speed digit font is now a PROGMEM header (no DRAM).
- **Pre-gzipped webui** — the webui HTML is pre-gzipped, slashing the config-page transfer by ~75 KB of flash.
- **Fixed char buffers** — all config `String`s converted to fixed char buffers (no per-request heap allocation).
- **8-bit grayscale speed sprite** — the speed sprite is now true 8-bit grayscale, fixing the AA snow-edge artifacts.
- **Web watchdog auto re-arm** — the web watchdog auto re-arms 3 min after a boot-storm disarm.

### V1.2.0 — Cloud OTA pull hardening
- **Resumable TLS downloads** — OTA pulls now resume on TLS failures.
- **Static 32 KB pull task** — the OTA pull runs in a static 32 KB task.
- **Flash-safe reboot** — the streaming bar is capped so only verified flashes reboot; the web loop yields the radio during flash.
- **Manifest phase scoping** — the manifest phase is scoped to compact heap before the handshake; a 1-min recheck throttle.
- **VLW120 font alloc crash-guard** — the 120px font allocation is crash-guarded (mem-saver safe).

### V1.1.6 — Open-Meteo weather widget + task-level CPU isolation
- **Open-Meteo weather widget** — live weather widget (city, lat/lon, refresh interval, locale, day/night icon) fetched by a dedicated Core-0 task.
- **GNSS task quarantine** — GPS parsing quarantined into its own Core-0 task (below the WiFi stack), with bulk `readBytes()` ring drain and a 1024 B/tick cap (the per-byte read cost was ~920 µs).
- **Sensor task** — moved back to Core 1 beside the display.
- **Frame-budget deferral** — frame-budget deferral so the display never blocks on network I/O.

---

## License & Credits

Designed and engineered by **alefinot**.

- Built using [LovyanGFX](https://github.com/lovyan03/LovyanGFX) for high-speed SPI display driving.
- GPS parsing powered by [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus).
- Configuration engine powered by [ArduinoJson](https://arduinojson.org/).

*Copyright © alefinot — Dashboard++ for ESP32*
