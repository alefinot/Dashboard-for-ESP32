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
- **Full Sensor Suite Integration:** Precise fuel level monitoring (20-point piecewise linear calibration table + EMA filtering), engine coolant thermistor telemetry (Steinhart-Hart equation), battery voltage divider monitoring, trip fuel economy tracking, **trip average speed**, and a **session max speed (Vmax)** readout display.
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
││  (readBytes() batch, 1024 B/tick cap)│││• ADC: Fuel (32), Temp (36), Bat (35) ││
││• TinyGPS++ NMEA + UBX NAV-PVT parse  │││• Fuel Economy & Accel Timer          ││
││• Speed Fusion & Odometer calc        │││• Trip Average Speed                  ││
││• GNSS Epoch Time Sync -> settimeofday│││• Safe Thread Sync via g_stateMutex   ││
││• Safe Thread Sync via g_stateMutex   │││                                      ││
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
> `g_weatherData` struct. A 30 s guard asks a stuck fetch to abort (the fetch polls
> `weatherAbort` at its checkpoints and HTTP calls carry 8 s connect/read timeouts) so it unwinds
> and deletes itself rather than being killed with `vTaskDelete()`; while an aborted fetch is still
> alive, `startWeatherFetch()` refuses to stack a second one. A config save triggers an
> instant refetch with a 2 s retry backoff.

> [!NOTE]
> Every FreeRTOS task is started through `startTask()` in `src/main.cpp`, which checks the
> `xTaskCreatePinnedToCore()` return value, retries once on half the requested stack (a reduced
> stack usually fits and beats a missing subsystem) and, if that fails too, records the task in a
> `failedTasksMask` and logs it with the free heap. A task that cannot start is therefore named in
> the boot log instead of silently missing — the failure that used to show up only as a frozen
> heartbeat, no Web UI or dead sensors.

---

## Hardware Component Specifications

| Component | Part / Model | Protocol / Signal | Specifications |
| :--- | :--- | :--- | :--- |
| **Microcontroller** | ESP32-WROOM-32 | Xtensa 32-bit LX6 | Dual-core 240 MHz, 520 KB SRAM, 4 MB SPI Flash, RTC IO |
| **Display Panel** | ILI9488 TFT LCD (4.0") | SPI3 (16-bit RGB565) — SCLK GPIO18, MOSI GPIO23, CS GPIO5, DC GPIO27, RST GPIO14 (no MISO) | 480×320 pixels, 60 MHz SPI bus speed (clamped to 1–80 MHz), hardware CS/DC/RST |
| **Display Backlight** | LED Backlight (`BL_DISPLAY`, GPIO12) | LEDC PWM — pin-based `ledcAttach(BL_DISPLAY, 1000, 8)` | 1 kHz hardware PWM, 8-bit resolution (256 brightness levels), logarithmic fading; the LEDC channel is assigned by the arduino-esp32 3.x wrapper |
| **GNSS Module** | BZGNSS P25 Pro (u-blox M10) | UART2 (RX=GPIO16, TX=GPIO17) | 115200 baud (configurable), UBX NAV-PVT / NMEA 0183 stream (module is preconfigured — the firmware is receive-only), 10 Hz update rate, multi-constellation (GPS/GLONASS/BDS/Galileo), UTC epoch time synchronization |
| **Wheel Speed Sensor** | Hall Effect Interrupt | GPIO33 (Input Pullup) | Any-edge ISR stamps the edges; interval/filter logic runs on the sensor task, microsecond timing |
| **Fuel Level Sensor** | Marine Resistive Sender (capacitive touch removed in v1.3.6) | GPIO32 (ADC1_CH4) | Voltage divider behind an excitation resistor, ohm-domain calibration table (up to 20 points), oversampling + EMA smoothing, open/short detection |
| **Engine Temp Sensor** | NTC Thermistor (10k/100k) | GPIO36 (ADC1_CH0) | Analog 0–3.3V, Steinhart-Hart equation, voltage divider balance |
| **Battery Voltage** | Voltage Divider (5.7:1) | GPIO35 (ADC1_CH7) | Analog 0–3.3V, range 0–18.8V DC, sampled every 500 ms |
| **Power / Ignition** | Ignition Key Sense Line | GPIO4 | High=Ignition ON, Low=Power Lost → Animated Deep Sleep |

---

## Hardware Pinout Matrix

| ESP32 Pin | Function Name | Peripheral Type | Signal Direction | Hardware Configuration & Notes |
| :---: | :--- | :--- | :---: | :--- |
| **GPIO0** | BOOT | Factory Reset | Input (Pullup) | `pinMode(0, INPUT_PULLUP)`; hold BOOT for 8 s within the first 30 s after boot to wipe the config |
| **GPIO1** | Console TX | Debug Console | Output | `Serial.setPins(1, 3)` pins the console explicitly (arduino-esp32 3.x moved the UART1 default to GPIO26/27) |
| **GPIO3** | Console RX | Debug Console | Input | Same `Serial.setPins(1, 3)` call |
| **GPIO4** | `POWER_SENSE_PIN` | Power Sense | Input (No Pull) | Ignition sense line; triggers EXT0 RTC wake up from deep sleep |
| **GPIO5** | `CS_DISPLAY` | SPI Chip Select | Output | Hardware SPI CS for ILI9488 Display |
| **GPIO12** | `BL_DISPLAY` | Backlight PWM | Output | Pin-based `ledcAttach(BL_DISPLAY, 1000, 8)` — 1 kHz PWM, 8-bit resolution |
| **GPIO14** | `SPI_RST` | Display Reset | Output | Active-Low hardware reset line for ILI9488 |
| **GPIO16** | `GNSS_UART2_RX_PIN` | GPS Serial RX (UART2) | Input | Connected to the GNSS module TX pin. Receive-only: the firmware never transmits to the module |
| **GPIO17** | `GNSS_UART2_TX_PIN` | GPS Serial TX (UART2) | Output | Connected to the GNSS module RX pin; configured but idle - no UBX command is ever sent |
| **GPIO18** | `cfg.pin_sclk` | SPI Clock | Output | Hardcoded in `gfx.cpp` (SPI3_HOST), 60 MHz default |
| **GPIO23** | `cfg.pin_mosi` | SPI Master Out | Output | Hardcoded in `gfx.cpp`, LCD data/command stream |
| **GPIO25** | `TRIP_RESET_PIN` | Trip Reset Button | Input (Pullup) | Momentary push button to GND; hold `TRIP_RESET_HOLD_MS` to zero the trip stats (issue #17). Internal ~45 kΩ pull-up, no external parts |
| **GPIO27** | `SPI_DC` | Data / Command | Output | High = Data, Low = Command for ILI9488 controller |
| **GPIO32** | `FUEL_TOUCH_PIN` | Fuel sender input | Input | Dedicated ADC1 Channel 4 pin: 3V3 → `FUEL_EXC_RES_OHM` → GPIO32 → sender → tank ground (capacitive touch removed in v1.3.6) |
| **GPIO33** | `HALL_SENSOR_PIN` | Hall Interrupt | Input (Pullup) | Any-edge (`CHANGE`) hardware interrupt: the ISR timestamps both transitions, the filter chain validates them on the sensor task |
| **GPIO34** | `LIGHT_SENSOR_PIN` | Ambient Light | Input (No Pull) | LDR ambient light sensor for auto-brightness (calibrated via `/api/ambient/cal-dark` / `cal-bright`) |
| **GPIO35** | `BATTERY_SENSE_PIN` | Battery ADC | Input (No Pull) | Connected to 5.7:1 precision resistor divider node |
| **GPIO36** | `TEMP_SENSE_PIN` | Engine Temp ADC | Input (No Pull) | Connected to NTC thermistor / balance resistor divider node |

> [!IMPORTANT]
> GPIO32 is dedicated to the fuel ADC input to avoid pin-sharing conflicts with the GPIO33 Hall interrupt hardware line.

> [!NOTE]
> This matrix matches the code (`src/dashboard.h` pin defines and the hardcoded display SPI pins in `src/gfx.cpp`), which is the source of truth. The GNSS serial pair moved from GPIO25/26 to **GPIO16/17** during the pin-swap test and stayed there, which is what freed **GPIO25** for the trip-reset button; **GPIO26** stays free as the spare for a second button. Strapping pins (`GPIO0`, `GPIO2`, `GPIO5` = `CS_DISPLAY`, MTDI/`GPIO12`, MTDO/`GPIO15`) are deliberately not used for buttons — a button to GND can hold them the wrong way during reset and block boot. The display SPI bus has no MISO line (`cfg.pin_miso = -1`), and GPIO21/22 (former compass I²C) are unused.

---

## Software Architecture & Mathematical Models

### 1. Dual-Source Speed Fusion & Odometer Engine

Dashboard++ features a dual-source speed calculation engine that combines low-latency wheel rotation timing with absolute satellite GPS telemetry.

#### Hall Sensor Calculation & 5-Layer Defense Pipeline
Speed is derived from microsecond timing between consecutive interrupt pulses on GPIO33, protected by an end-to-end 5-layer defense pipeline that eliminates spark ignition EMI glitches, prevents baseline collapse during deceleration, and bounds rate-of-change to physical limits:

1. **Layer 1: Measured Pulse-Width Qualification (Glitch Filter):** Real wheel magnets hold GPIO33 LOW for hundreds of microseconds (one revolution at 200 km/h is still 29.7 ms long). High-frequency ignition coil ringing collapses back to HIGH within <10 µs. The GPIO ISR used to prove the width by busy-waiting `HALL_PULSE_MIN_US` and re-sampling `REG_READ(GPIO_IN1_REG)` **inside** the interrupt, which masked interrupts on that core for up to 1 ms per glitch (~86 bytes of GNSS RX at 115200 baud). Now the interrupt is `CHANGE` (both edges), the ISR only stamps the transition time into a 64-slot ring, and the qualification is *measured*: `rise − fall ≥ HALL_PULSE_MIN_US` (default 150 µs, WebUI-tunable, clamped to 10…1000 µs). A glitch is dropped before any interval logic runs, exactly as before, and the ISR stays at roughly a microsecond. Ring overflow drops the newest edge and is counted as `drop=` in the `HALL:` telemetry line; the accept latency is reported as `lag=`.
2. **Layer 2: Anti-Collapse Timing Baseline (Integer Guard) + Period Guard:** 100% integer timestamping and period guard with zero floating-point math, in the validation step that runs on the sensor task (`processHallEdges()`). When actively rolling, candidate intervals faster than $2\times$ previous speed ($gap \times 2 < last$) are dropped without updating `lastHallPulseTimeUs` or the stable baseline `hallStableIntervalUs`. This prevents spark bursts during braking from collapsing the guard baseline and producing >200 km/h spikes. The complementary upper bound is `HALL_PERIOD_GUARD`: an interval more than $N\times$ the last accepted one is dropped too (a wheel cannot slow $N$-fold in a single revolution; a genuine stop is handled by Layer 3 instead). $N = 1$ disables it.
3. **Layer 3: Standstill Detection, Buffer Purge & Deceleration Decay:** If time since the last pulse exceeds `STANDSTILL_TIMEOUT_US` (1.5 s), the wheel was stopped. The first pulse synchronizes the timing baseline, holds its odometer credit until a confirming pulse follows, and completely purges the history ring buffer. During deceleration, if elapsed time $dt$ since the last pulse exceeds $1.5\times$ the rotation period (plus the recorded accept latency `lag`, so deferred validation cannot make an on-time pulse look overdue), dynamic decay constrains speed smoothly to 0 km/h without mid-revolution diving.
   - **Layer 3b: Confirmed-Roll Speed Gate:** the purge in Layer 3 also disarms Layer 2 (which is conditioned on the rolling state) and leaves the median window with a single sample, so before this gate *one* accepted inter-edge gap became the displayed speed — two ignition-EMI edges 12 ms…1.5 s apart on a parked machine reported a plausible reading (100 ms → 59 km/h on a 1650 mm wheel). The speed output now stays at 0 until `HALL_CONFIRM_REVS` (3) consecutive accepted intervals agree pairwise within $2\times$ — the same physical bound Layer 2 uses while rolling. Cost: a genuine roll start is displayed two revolutions later (≈0.6 s at 12 km/h, ≈0.2 s at 30 km/h). Distance credits are unaffected (the odometer is guarded by the pending-anchor logic). Below ~4 km/h the 1.5 s standstill timeout already reports 0, so the usable low-speed floor does not change.
4. **Layer 4: Physical Slew-Rate Limiter (Max Accel / Decel):** Terrestrial motorcycles cannot exceed physical acceleration limits (~1.7g / 60 km/h/s) or emergency braking (~2.3g / 80 km/h/s). Clamping $\Delta V$ between update ticks (~20 ms) mathematically prevents instantaneous multi-hundred km/h jumps on the speedometer.
5. **Layer 5: Sensor Fusion Outlier Rejection:** In Sensor Fusion mode (2), if Hall speed reads $> V_{\text{gps}} + 30\text{ km/h}$ while GPS has a solid satellite lock, the Hall reading is rejected as an anomalous spike and the dashboard displays GPS speed.

#### Speed Source & Fusion Logic
The displayed speed source is selected by `SPEED_SOURCE_MODE`: `0`=Hall only, `1`=GPS only, `2`=Sensor Fusion (default).

In **Sensor Fusion mode (2)**, the engine dynamically balances both sensors:
- **Rock-Solid Standstill:** When $V_{\text{hall}} = 0$ and $V_{\text{gps}} < \text{GPS\_START\_KMH}$ (3.0 km/h), speed is forced to solid `0.0 km/h`, eliminating GPS drift at traffic lights.
- **Immediate Response:** As soon as wheel pulses arrive ($V_{\text{hall}} > 0$), speed is displayed immediately without clamping.
- **Dynamic Throttle Tracking:** When $|V_{\text{gps}} - V_{\text{hall}}| > \text{MAX\_SPEED\_DELTA\_KMH}$ (such as normal GPS lag during rapid acceleration), Hall is trusted directly so throttle response is never suppressed.
- **Dual Failsafe Redundancy:** If the Hall sensor fails or disconnects at speed, the system automatically falls back to valid GPS ($V_{\text{gps}} \ge 3.0\text{ km/h}$). If GPS signal is lost in a tunnel, Hall sensor carries 100% of the speed. Speed never drops to 0 while moving.
- **Dynamic Confidence Blending:** When both sensors are healthy and consistent ($\Delta V \le \text{MAX\_SPEED\_DELTA\_KMH}$):
$$C_{\text{sat}} = \text{constrain}\left(\frac{N_{\text{sat}} - N_{\text{min}} + 1}{N_{\text{opt}} - N_{\text{min}} + 1}, 0.0, 1.0\right)$$
$$C_{\delta} = \text{constrain}\left(1.0 - \frac{\Delta V - \Delta V_{\text{min}}}{\Delta V_{\text{max}} - \Delta V_{\text{min}}}, 0.0, 1.0\right)$$
$$W_{\text{gps}} = C_{\text{sat}} \cdot C_{\delta} \cdot 0.5$$
$$V_{\text{fused}} = (W_{\text{gps}} \cdot V_{\text{gps}}) + ((1 - W_{\text{gps}}) \cdot V_{\text{hall}})$$
Capping $W_{\text{gps}}$ at 0.5 ensures Hall sensor's zero-latency dynamic throttle/brake response is always preserved, while GPS anchors long-term tire wear accuracy.

#### Odometer Persistence Strategy
To protect the ESP32 NVS Flash memory from wear, distance accumulation runs continuously in RAM. The odometer writes to non-volatile storage **only after accumulating a full 1.0 km increment**:
$$\Delta D_{\text{ram}} \ge 1.0\text{ km} \implies \text{Preferences.putDouble("odo", } D_{\text{total}}\text{)}$$

**Sharing the odometer across cores.** The odometer is a 64-bit `double`, and a 64-bit load/store on ESP32 is two 32-bit bus cycles: a read that lands between the two halves of a write returns a mixture of two different distances. The totals therefore live file-local in `src/sensors.cpp` and every other task reaches them through the locked accessors `odoGet()`, `odoSet()`, `odoAdd()` (read-modify-write in one critical section), `odoLastSaved()` and `odoMarkSaved()`, guarded by a dedicated spinlock (`odoMux`) — deliberately *not* `g_stateMutex`, so the display never waits behind sensor or NVS work and the critical sections stay a few instructions long. The accessors never write flash; the 1 km wear discipline above is unchanged.

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

### 3. Marine Resistive Fuel Sender: Divider, Ohm-Domain Table & Filtering

Fuel level is read from a standard marine resistive sender wired as a voltage
divider on `FUEL_TOUCH_PIN` (GPIO32). The capacitive pad this input used to carry
is gone; the pin is now a plain ADC input (issue #18):

```
3V3 ─── FUEL_EXC_RES_OHM ─── GPIO32 (ADC1_CH4) ─── sender ─── tank ground
```

#### From ADC code to ohms
`FUEL_OVERSAMPLE` conversions are averaged into a raw code $\bar{S}$ (software
oversampling — the core exposes no hardware averaging for this pin), turned into
a voltage with `FUEL_ADC_VREF`, and the sender resistance is recovered from the
divider:

$$V = V_{\text{ref}} \cdot \frac{R_{\text{sender}}}{R_{\text{sender}} + R_{\text{exc}}} \qquad\Rightarrow\qquad R_{\text{sender}} = R_{\text{exc}} \cdot \frac{V}{V_{\text{ref}} - V}$$

The calibration table (`fuelCalOhms`) stores **ohms, not ADC codes**. That is
what makes any sender standard work on the same firmware — SAE **10–180 Ω**
(low = empty), European/VDO **240–33 Ω** (high = empty), GM **0–90 Ω** — and it
means a captured table still means the same thing after the excitation resistor
(or the sender) is swapped. `FUEL_OHM_EMPTY` / `FUEL_OHM_FULL` describe the
sender: they size the exciter, define the fault band, and pre-fill the table
ramp; they do not replace the table.

#### EMA + piecewise interpolation
Smoothing stays in the ADC-code domain (linear, and the same domain the table was
captured against), then the ohm value walks the table:
$$\bar{S}_t = (\alpha \cdot S_t) + ((1 - \alpha) \cdot \bar{S}_{t-1}) \qquad \alpha = \texttt{FUEL\_FILTER\_ALPHA}\ (default{=}0.08)$$
Given $N$ table points $T[0 \dots N-1]$ mapped to tank slots $0 \dots N-1$, find
segment $i$ containing $R_{\text{sender}}$:
$$L_{\text{fuel}} = i + \left(\frac{R_{\text{sender}} - T[i]}{T[i+1] - T[i]}\right)$$
$$\text{FuelPercentage} = \text{constrain}\left(\left\lfloor \frac{L_{\text{fuel}}}{N - 1} \times 100 \right\rfloor, 0, 100\right)$$

The traversal direction is taken from the table's two ends, so a rising (SAE) and
a falling (European) sender are both handled without a mode switch.

#### Sender fault detection
The failure that makes a self-built sender useless is an open line that still
looks like a plausible reading, so the input is classified every sample and shown
in the Web UI instead of being turned into a tank level:

| Condition | Reported as |
| :--- | :--- |
| `FUEL_INPUT_ENABLED` off | **no input** — gauge pinned at 0, a floating pin never invents a level |
| pin within 0.5 % of the rail, or $R > 3 \times$ the entered full/empty maximum | **open circuit** — sender unplugged, wire broken, arm off the pivot |
| $R$ below `min(EMPTY, FULL)` by more than `max(5 Ω, 25 %)` | **shorted input** — signal wire on ground |

Demo mode drives the same pipeline: the simulated level is converted back through
the calibration table into a divider code and read forward again, so the table,
divider math and fault band are exercised with no hardware fitted (rule 20).

#### Excitation resistor sizing
$R_{\text{exc}}$ is a trade: the geometric mean of the sender range spreads the
span over the most volts, but pushes more current through the sender element. The
Web UI suggests the best E12 value for the entered range under a **15 mA** sender
current budget and keeps the top of the span below 2.6 V, because a reading
pinned near the rail is indistinguishable from an open circuit. For the shipped
10–180 Ω default that suggestion is 220 Ω — the same value the firmware ships
with — giving a 1.34 V span at 14.3 mA.

Check the whole chain on a host with `python scripts/verify_fuel_ohms.py`
(divider round-trip, both conventions, resolution per tank slot, demo round-trip,
fault band, exciter suggestion).

---

### 4. Steinhart-Hart Coolant Temperature Math

Engine temperature is measured via an NTC thermistor connected in a resistor divider configuration with a balance resistor $R_{\text{balance}}$ (default=10,000 $\Omega$).

#### Voltage Divider Resistance Calculation
$$V_{\text{out}} = \text{ADC}_{\text{raw}} \times \left(\frac{3.3}{4095}\right)$$
$$R_{\text{ntc}} = R_{\text{balance}} \times \left(\frac{V_{\text{out}}}{3.3 - V_{\text{out}}}\right)$$

#### Steinhart-Hart B-Parameter Equation
$$T_{\text{Kelvin}} = \left( \frac{1}{\beta} \cdot \ln\left(\frac{R_{\text{ntc}}}{R_{\text{room}}}\right) + \frac{1}{T_{\text{room}}} \right)^{-1}$$
$$T_{\text{Celsius}} = T_{\text{Kelvin}} - 273.15$$
where $R_{\text{room}} =$ `NTC_R25` (default=10,000 $\Omega$), $T_{\text{room}} = 298.15\text{ K}$ ($25^\circ\text{C}$), and $\beta =$ `NTC_BETA` (default=3950). A fixed `NTC_TEMP_OFFSET` (°C, default=0.0) is added to the result to correct a systematic reading bias.

#### WebUI Sensor Calibration
The **Sensors Tuning** WebUI card calibrates the two analog sensors against a reference, using the live `/api/sensors` reading (no raw-ADC conversion needed):
- **Engine Temperature** — the live reading is shown; type a reference temperature and press **Apply** to set `NTC_TEMP_OFFSET`. `NTC_R25`, `NTC_R_BALANCE` and `NTC_BETA` are under **Advanced**.
- **Battery Voltage** — the live reading is shown; type a reference voltage (e.g. a multimeter) and press **Apply** to set `BATTERY_OFFSET`. `BATTERY_SCALE` (divider ratio) is under **Advanced**.

---

### 5. Fuel Economy & Performance Drag Timer

#### Instantaneous & Trip Fuel Economy
- **Trip Average (KM/L):** $KM/L_{\text{avg}} = \frac{D_{\text{trip}}}{C_{\text{consumed}}}$. The trip is zeroed only by an explicit trip reset — the physical button on GPIO25, `POST /api/trip/reset`, or a factory reset — never by a detected refuel (issue #17). A fuel-level rise still re-anchors the consumption baseline so the average stays sane, but it leaves the trip distance and the averages alone.
- **Instantaneous (KM/L):** Calculated across a 3-second sliding window:
  $$KM/L_{\text{inst}} = 0.4 \cdot \left(\frac{\Delta D_{3\text{s}}}{\Delta C_{3\text{s}}}\right) + 0.6 \cdot KM/L_{\text{inst, prev}}$$
- **Trip Average Speed (KM/H):** $V_{\text{avg}} = \frac{D_{\text{trip}}}{t_{\text{elapsed}}}$, displayed in the bottom row alongside fuel economy readouts.
- **Session Max Speed (Vmax):** The highest fused/filtered speed reached this power-on session; held on screen until the next reboot (RAM-only, no NVS persistence, no manual reset). Rendered as the **MAX** icon in the bottom row — same 28px 7-segment structure as the Instant KM/L readout — in MPH under imperial. Controlled by `SHOW_ELEMENT_MAX_SPEED`, `OFFSET_MAX_SPEED_X/Y`, `ALIGN_MAX_SPEED`, `MAX_SPEED_INT_DIGITS`, `MAX_SPEED_DEC_DIGITS`, and the `REFRESH_MAX_SPEED_MS` redraw throttle.

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
When `ENABLE_POWER_SENSE` is enabled and `POWER_SENSE_PIN` (GPIO4) stays **continuously LOW for `POWER_SENSE_OFF_MS`** (default 10 s, 500–120000 ms):
1. Plays goodbye screen animation (`showGoodbyeScreen(true)`).
2. Fades display backlight down to 0% via LEDC PWM.
3. Configures RTC EXT0 wake-up trigger on GPIO4 (High level).
4. Invokes `esp_deep_sleep_start()`.

The line is debounced in software (`powerSenseOffConfirmed()` in `src/sensors.cpp`): the timer restarts on every HIGH, so ignition bounce, a load-dump dip or a flaky ground of a few milliseconds up to several seconds cannot stall the GPS/odometer task or put the unit to sleep (issue #26). A single LOW sample used to be enough. Side effect: key-off costs `POWER_SENSE_OFF_MS` of live current draw before the goodbye screen and sleep; wake behaviour is unchanged (EXT0 on GPIO4 going HIGH).

---

## Embedded Web UI & REST API Reference

Dashboard++ embeds a single-page management portal directly into flash memory (`index_html`).

### WiFi Network Connectivity
- **SoftAP Mode:** Emits AP SSID `Dashboard_Config` (Default IP: `192.168.4.1`). Always up alongside the client connection (`WIFI_AP_STA`), secured by `AP_PASSWORD` (empty or 8–63 characters).
- **Multi-SSID Client Mode:** Can store up to 4 fallback WiFi network profiles (`WIFI_SSID_1` through `WIFI_SSID_4`). Automatically attempts connection on boot.

> [!IMPORTANT]
> **Access model (deliberate).** The config portal and the REST API have **no login** — access control is the Wi-Fi itself. `AP_PASSWORD` must be empty or 8–63 characters; a shorter value is rejected and the default (`12345678`) is restored, so the hotspot can never come up with a password the ESP32 silently refused. **Change that default before using the dashboard on a public network**: anyone who joins the hotspot (or reaches the dashboard's LAN IP) can change settings and flash firmware.

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
- **Ambient light calibration** (dark / bright reference points)
- **Cloud OTA pull** controls (enable, URL) with live status polling
- **OTA firmware upload** via file picker
- **NVS backup/restore** (export/import JSON)

### REST API Endpoints

| Endpoint | Method | Description | Request Payload / Params | Content-Type |
| :--- | :---: | :--- | :--- | :--- |
| `/` | `GET` | Serves pre-gzipped single-page Web UI | None | `text/html` |
| `/debug` | `GET` | Dump first bytes of the pre-gzipped UI buffer (build sanity check) | None | `text/plain` |
| `/api/config` | `GET` | Exports complete NVS configuration plus read-only `build_version` | None | `application/json` |
| `/api/config` | `POST` | Updates NVS parameters and applies changes | Config JSON object | `application/json` |
| `/api/time` | `POST` | Syncs system clock from browser | `?epoch=1700000000` | `text/plain` |
| `/api/odo` | `GET` | Reads odometer distance in km | None | `application/json` |
| `/api/odo` | `POST` | Sets odometer distance | `{"km": 123.45}` | `application/json` |
| `/api/trip/reset` | `POST` | Zeros the trip stats (same reset as the GPIO25 button); the odometer and session max speed are untouched | None | `application/json` |
| `/api/reboot` | `POST` | Triggers graceful device restart | None | `text/plain` |
| `/api/sleep` | `POST` | Triggers immediate deep sleep | None | `text/plain` |
| `/api/reset` | `POST` | Performs factory reset (clears NVS) | None | `text/plain` |
| `/api/ambient` | `GET` | Reads raw ambient light sensor value | None | `application/json` |
| `/api/sensors` | `GET` | Reads calibrated battery voltage (`v`) and coolant temperature (`t`) | None | `application/json` |
| `/api/fuel` | `GET` | Reads the fuel input: `raw` averaged ADC code, `liters`, `pct`, `ohm` sender resistance, `st` input state (0 = disabled, 1 = ok, 2 = open circuit, 3 = shorted) | None | `application/json` |
| `/api/ambient/cal-dark` | `POST` | Sets dark-reference ambient light value (auto-brightness floor) | None | `text/plain` |
| `/api/ambient/cal-bright` | `POST` | Sets bright-reference ambient light value (auto-brightness ceiling) | None | `text/plain` |
| `/api/ota` | `POST` | Over-The-Air firmware binary upload | Binary `.bin` payload | `multipart/form-data` |
| `/api/ota/pull` | `POST` | Triggers cloud OTA pull (checks `OTA_PULL_URL`) | None | `text/plain` |
| `/api/ota/check` | `GET` | Reports cloud OTA pull state (`enabled`, `url`, `current_version`, `build_version`, `version_override`, `previous_version`, `status`) | None | `application/json` |
| `/api/serial` | `GET` | Streams internal 4 KB ring buffer logs | None | `text/plain` |
| `/api/perf` | `GET` | Live telemetry (CPU, Heap, task stack headroom, FPS, WiFi, partitions) | None | `application/json` |
| `/api/health` | `GET` | Quick heap / mem-saver / uptime health probe | None | `application/json` |
| `/api/boot` | `GET` | Boot/reboot forensics (reset reason, storm, last-reboot tag, heap watermark) | None | `application/json` |

---

## NVS Configuration Parameter Reference

Dashboard++ uses a generic 3-mode macro system (`processConfig()`) to load, serialize, and deserialize over 60 configuration variables from ESP32 NVS flash storage.

### Numeric Range Validation (issue #21)

Every numeric parameter carries an explicit **known-good band** in its `CFG_INT` / `CFG_UINT` / `CFG_FLT` declaration in `src/config.cpp` — that table is the single source of truth. The band is enforced on **all three write paths** (boot load, `POST /api/config`, and config restore): an out-of-range value is **clamped to the nearest end of the band, logged by name, and the clamped value is what gets written to NVS** — a bad value never reaches the running system, and a restore is never rejected outright because one field is off.

- **Non-finite floats** (`nan`, `inf`) fall back to the parameter's default rather than poisoning a calculation.
- **Cross-field rules** are repaired after the per-field clamps, in one place (`sanitizeConfigPairs()`): `TEMP_BAR_MIN < TEMP_BAR_MAX`, `TEMP_WARN_YEL <= TEMP_WARN_RED`, `FUEL_WARN_RED <= FUEL_WARN_YEL` (fuel is mirrored — it turns red *below* its marker), `LIGHT_SENSOR_DARK_VAL < LIGHT_SENSOR_BRIGHT_VAL`, `MIN_SATELLITES <= OPTIMAL_SATELLITES`, `CPU_THROTTLE_TEMP_WARN <= CPU_THROTTLE_TEMP_CRIT`.
- **Enumerated parameters** are snapped to the nearest legal step: `GPS_BAUD` to a standard u-blox rate (1200…921600), `MANUAL_CPU_FREQ` to 80 / 160 / 240 MHz. `DISPLAY_ROTATION` is masked to 0–3, `WHEEL_CIRCUMFERENCE_MM` has always been floored at 1 (it is a divisor).
- **Fuel calibration table** (`fuelCalOhms`) cells are clamped to the sender band 0–100000 Ω on load and on restore. The table is in **ohms** since issue #18 (it used to be raw ADC codes under the name `touchTable`); old `touchTable` values in a backup are ignored rather than re-injected as ohms. The table is also required to be **monotonic in one direction** (the direction its first and last entries already imply): out-of-order points are sorted by `repairFuelTable()` on boot load and on save, written back to NVS, and named in the log, because a gap in the ramp would otherwise freeze the needle (issue #19). The gauge walk in `src/sensors.cpp` carries a second layer — a reading that matches no segment parks on the nearer end and logs once every 5 s instead of leaving the last value in place.
- The **WebUI mirrors the same bands** so a bad entry is caught before it is posted: numeric inputs get `min`/`max` from a `FIRMWARE_LIMITS` table generated out of `config.cpp` (`python scripts/make_webui_limits.py` regenerates it after any band change), an out-of-range entry is corrected in the box and listed in the save message, and an emptied numeric box falls back to its default instead of posting a blank (which the device used to read as `0`).

Bands are also checked offline by `python scripts/verify_config_ranges.py`: every numeric parameter must have a band, no shipped default may fall outside its own band, and the cross-field rules must hold for the factory values.

### Key Configuration Categories

#### System & Performance
- `TARGET_FPS` (default=60): Desired display refresh rate (up to ~500 FPS supported, hardware-limited).
- `SPI_BUS_SPEED` (default=60000000): SPI bus frequency in Hz. Accepted range **1000000–80000000** (1–80 MHz; WebUI shows MHz). Out-of-range values are clamped on load/save — a `0` or a negative (wrapped) value would otherwise feed LovyanGFX a degenerate clock divider and blank the panel. The read clock is derived as `SPI_BUS_SPEED × 8/5`, computed in 64-bit and clamped to the same band.
- `ENABLE_DYNAMIC_CPU` (default=false): Toggles automatic CPU frequency scaling (hysteresis-based).
- `MANUAL_CPU_FREQ` (default=240): Fixed CPU clock frequency (80, 160, or 240 MHz).
- `ENABLE_CPU_THROTTLE` (default=true): Enables thermal frequency capping.
- `CPU_THROTTLE_TEMP_WARN` (default=60): Warning temperature threshold in °C.
- `CPU_THROTTLE_TEMP_CRIT` (default=70): Critical temperature threshold in °C.

#### Web UI
- `POWER_SENSE_OFF_MS` (default=10000): How long `POWER_SENSE_PIN` (GPIO4) must stay continuously LOW before the unit plays the goodbye screen and deep-sleeps (500–120000 ms). Any HIGH restarts the window, so ignition bounce and supply dips cannot stall the GPS task or sleep the dash (issue #26). Only used when `ENABLE_POWER_SENSE` is on.
- `ADV_MODE` (default=false): Global “Advanced Mode” toggle in the Web UI. When enabled, advanced and technical settings (CPU, GPS, Polling Rates, technical display options, advanced WiFi, and sensor-calibration internals) are shown as normal rows in their groups; when disabled they are hidden.

#### Display & Visual Design
- `DISPLAY_ROTATION` (default=3): Screen rotation (0, 1, 2, 3).
- `BACKLIGHT_BRIGHTNESS` (default=100): Backlight duty cycle (**0–100%**, clamped on load/save). The percent is turned into the 8-bit LEDC duty in one place (`backlightDuty()` / `applyBacklight()` in `src/main.cpp`), so no path — WebUI slider, night mode, boot splash, sleep/wake fade or auto-brightness — can put an out-of-range duty on the panel (issue #20).
- `NIGHT_BACKLIGHT` (default=29): Night-mode backlight duty cycle (0–100%), applied while night mode is active.
- `ENABLE_ANTIALIASING` (default=true): Anti-aliased line rendering toggle.
- `AA_SHARPNESS` (default=1.0): Anti-aliasing gamma correction factor.
- `GHOST_COLOR_STR` (default="#212021"): Hex color code for inactive 7-segment digit background.
- `COLOR_TEMP_NORM`, `COLOR_TEMP_WARN`, `COLOR_TEMP_CRIT`: Hex color strings for engine temperature gradient bar.
- `TEMP_BAR_MIN` / `TEMP_BAR_MAX` (default=10 / 110): the two ends of the engine-temp sidebar bar in °C.
- `TEMP_WARN_YEL` (default=45): °C where the bar leaves plain `COLOR_TEMP_NORM` and starts fading toward `COLOR_TEMP_WARN`.
- `TEMP_WARN_RED` (default=90): °C where that fade reaches amber and the amber→`COLOR_TEMP_CRIT` fade begins; full red at `TEMP_BAR_MAX`. Always kept at or above `TEMP_WARN_YEL`. The ramp is **light blue → amber → red**: a third marker (`TEMP_WARN_GRN`) existed in the code but was never stored in NVS, never in a backup and never in the Web UI, so it has been removed (issue #29).
- `COLOR_FUEL_NORM`, `COLOR_FUEL_WARN`, `COLOR_FUEL_CRIT`: Hex color strings for fuel status bar.

#### Sensors & Vehicle Calibration
- `WHEEL_CIRCUMFERENCE_MM` (default=1650.0): Tire rolling circumference in millimeters.
- `FUEL_FILTER_ALPHA` (default=0.08): EMA filter coefficient applied to the raw fuel ADC code.
- `FUEL_INPUT_ENABLED` (default=false): Turns the resistive fuel sender input on. Leave it off until a sender is actually wired to GPIO32 — with no sender the pin floats and the gauge reports "no input" and stays at 0 (issue #18).
- `FUEL_OHM_EMPTY` / `FUEL_OHM_FULL` (defaults=10.0 / 180.0): Sender resistance at empty and full, in ohms. Any standard works — 10–180 Ω (SAE), 240–33 Ω (European/VDO), 0–90 Ω (GM). Used to size the excitation resistor, define the fault band and pre-fill the calibration ramp.
- `FUEL_EXC_RES_OHM` (default=220): Resistor from 3V3 to GPIO32 that the sender forms a divider against. The Web UI suggests the best E12 value for the entered range (15 mA sender current budget, top of span below 2.6 V).
- `FUEL_ADC_VREF` (default=3.30): ADC reference / actual rail voltage in volts, used to turn the code into a divider voltage. Measuring the real 3V3 rail and entering it here removes a rail-error term (the calibration table cancels it at the points that were captured).
- `FUEL_OVERSAMPLE` (default=16): ADC conversions averaged into one sample (1–64). The ESP32 converter only delivers ~9–10 usable bits; oversampling is what buys the resolution back for a sender whose useful span can be a few hundred codes.
- `FUEL_TOUCH_POINTS` (default=8): Number of valid entries in the fuel calibration table (`fuelCalOhms`): index 0 = empty, index `N-1` = full, so the number of tank slots is one per litre for the shipped 1 L-per-step default. Changing this regenerates the table as a linear ramp between `FUEL_OHM_EMPTY` and `FUEL_OHM_FULL`.
- `TRIP_RESET_HOLD_MS` (default=1500): How long the physical trip-reset button on GPIO25 must be held to zero the trip stats (200–10000 ms). Shorter holds are ignored so a knock on the dash cannot wipe a trip. Refuelling no longer resets anything by itself (issue #17).
- `BATTERY_SCALE` (default=5.7): Battery divider ratio `(R1+R2)/R2`. Leave at `5.7` for the stock wiring; set to `4.7` for a 4.7 kΩ / 1 kΩ divider.
- `BATTERY_OFFSET` (default=0.2): Fixed voltage offset added to the divided battery reading.
- `NTC_R_BALANCE` (default=10000.0): Balance resistor value in ohms for NTC divider.
- `NTC_R25` (default=10000.0): Thermistor resistance at 25 °C (R25) in ohms.
- `NTC_BETA` (default=3950.0): Thermistor Beta coefficient.
- `NTC_TEMP_OFFSET` (default=0.0): Fixed temperature offset (°C) added to the Steinhart result.
- `GPS_BAUD` (default=115200): UART baud rate for the GNSS module. On boot the ESP listens passively across the common baud rates and stores the first rate at which the module's own traffic is seen. The module is **never** configured or reset by the firmware — no UBX command is ever transmitted; use u-center2 to change the module itself.
- `MIN_SATELLITES` (default=5): Minimum GPS satellite lock requirement.
- `OPTIMAL_SATELLITES` (default=8): Satellite count threshold for full GPS speed reliance.
- `MAX_SPEED_DELTA_KMH` (default=5.0): Maximum allowable difference between GPS and Hall speed before falling back.
- `SPEED_SOURCE_MODE` (default=2): Speed source — `0`=Hall only, `1`=GPS only, `2`=Sensor Fusion.
- `HALL_MEDIAN_SAMPLES` (default=3): Speed window size W (accepted pulses averaged); 1 = raw single interval.
- `HALL_PERIOD_GUARD` (default=8): Reject an interval longer than N× the last accepted one (the lower bound is fixed at 1/2 by Layer 2); `1` = off.
- `HALL_PULSE_MIN_US` (default=150): Layer 1 pulse-width qualification window in microseconds — a pulse whose measured `rise − fall` width is below this is dropped as EMI. Clamped to 10…1000 µs. Raise it when ignition EMI gets through; a genuine magnet pass is hundreds of µs wide, so 150 µs costs nothing. Measured from the stamped edges since 1.3.x (issue #30) — it no longer costs interrupt time.
- `ACCEL_MAX_TIME` (default=30.0): Acceleration timer maximum duration in seconds before auto-finish.

#### Weather Widget (Open-Meteo)
- `SHOW_ELEMENT_WEATHER` (default=true): Toggle the weather widget on/off.
- `WEATHER_CITY` (default=""): City label shown in the widget (GPS-geocoded when empty).
- `WEATHER_LAT` (default=0.0) and `WEATHER_LON` (default=0.0): Coordinates for the Open-Meteo forecast request.
- `WEATHER_REFRESH_MIN` (default=15): Refresh interval in minutes.
- `WEATHER_LOCALE` (default="en"): ISO locale code for weather-condition naming.

#### Time, Date & Daylight Saving
- `NTP_ENABLED` (default=true): Sync the system clock from NTP once the WiFi station is up.
- `NTP_SERVER` (default="pool.ntp.org"): NTP host used for that sync.
- `TZ_OFFSET_HOURS` (default=1): Zone offset in whole hours — **the standard (winter) offset**, range −14…14. With DST on, this is the offset the clock uses outside the DST period; the rule below adds the extra hour. New York is `-5`, Paris is `+1`, Athens is `+2`.
- `TZ_DST_ENABLED` (default=true): Master switch for daylight saving. When off the clock keeps `TZ_OFFSET_HOURS` all year.
- `TZ_DST_RULE` (default=1): Which calendar `TZ_DST_ENABLED` applies (issue #22). `0` = none, `1` = **EU/EEA** (last Sunday of March and October, both at 01:00 UTC), `2` = **US/Canada** (second Sunday of March, first Sunday of November, both at 02:00 local wall clock — converted to UTC through `TZ_OFFSET_HOURS`, so a GMT−6 zone springs forward at 08:00 UTC and a GMT−5 zone at 07:00 UTC). The rules are integer arithmetic on the UTC date (no zone database); `scripts/verify_dst_rules.py` checks them against the published 2024–2026 transition instants. Before this parameter the DST half was hard-wired to the EU calendar, so a US zone was wrong for roughly three weeks a year.
#### WiFi & Cloud OTA
- `AP_PASSWORD` (default="12345678"): Password of the `Dashboard_Config` SoftAP (the configuration portal). Set in the WebUI (System & General → Wifi → Device Config AP). Must be **empty** (open network, not recommended) or **8–63 characters**: the Wi-Fi stack rejects 1–7 character passphrases, so a shorter value is refused at load/save time and the shipped default is restored instead (logged as `Config: AP_PASSWORD too short`). The WebUI warns while the default password is still in use.
- `WIFI_RETRY_MODE` (default=1): Search policy — `0` = one cycle, `1` = fixed-time (`WIFI_RETRY_SECONDS`), `2` = search forever. Same policy governs reconnects after a lost link.
- `WIFI_RETRY_SECONDS` (default=300): Elapsed-search budget for policy `1` (seconds).
- `OTA_PULL_ENABLED` (default=false): Toggle automatic cloud pull (checks once per boot while enabled).
- `OTA_PULL_URL` (default=""): HTTPS URL of the update **manifest** — either JSON with `version` + `firmware_url`, or the GitHub `releases/latest` API URL (first `.bin` asset is used). The matching signature is fetched from `<firmware_url>.sig`.
- `VERSION_OVERRIDE` (default=""): Optional version **label**. When set, the unit reports this string instead of its compiled-in build version (`FW_VERSION` in `src/config.cpp`) — for display and as the reference version of the cloud OTA check, which is how you test an update pull against an arbitrary version. It never changes the firmware image, and nothing fetched from the network ever writes it (the previous behaviour of storing the manifest's version is why a unit could report a version it never ran). The real build version is always exposed read-only as `build_version` (`GET /api/config`) and shown in the WebUI (System → Firmware Update). NVS key `VER_OVR`.

#### Ambient Light (Auto-Brightness)
- `LIGHT_SENSOR_DARK_VAL`: Dark-reference ambient light value (calibrated via `/api/ambient/cal-dark`).
- `LIGHT_SENSOR_BRIGHT_VAL`: Bright-reference ambient light value (calibrated via `/api/ambient/cal-bright`).

#### Digit Boundaries (Configurable 7-Segment Formatting)
- `SPEED_DIGITS`, `SAT_DIGITS`, `TMR_INT_DIGITS`, `TMR_DEC_DIGITS`, `BAT_INT_DIGITS`, `BAT_DEC_DIGITS`, `INST_INT_DIGITS`, `INST_DEC_DIGITS`, `AVG_INT_DIGITS`, `AVG_DEC_DIGITS`, `AVG_SPEED_INT_DIGITS`, `AVG_SPEED_DEC_DIGITS`, `MAX_SPEED_INT_DIGITS`, `MAX_SPEED_DEC_DIGITS`, `FUEL_INT_DIGITS`, `FUEL_DEC_DIGITS`, `ODO_INT_DIGITS`, `ODO_DEC_DIGITS`: Configurable integer and decimal digit limits for all UI numerical readouts. These values size fixed 16-slot cell arrays in the display renderer, so they are range-checked on every load and save (issue #6): integer counts `1..14` (1-4 for `SPEED_DIGITS`, 1-3 for `SAT_DIGITS`), decimal counts `0..4`, and per readout `int + dec + 1 <= 16` (+1 is the decimal-point slot). Out-of-range values are clamped on load (boot, `POST /api/config`, config restore) and logged; the renderer keeps a hard bounds guard as a second line of defence. A readout also caps its *value* to what its configured digits can print (e.g. `FUEL_INT_DIGITS=1` + `FUEL_DEC_DIGITS=1` shows at most `9.5`; raise the int count to `2` in the WebUI — "Fuel & Battery" card → Fuel LTRS → Integer Digits — for two-digit litres), so a value with more digits than the cells never draws outside its cell array (issue #7).

#### UI Layout Offset Coordinates
- `BIG_CENTER_X`, `BIG_CENTER_Y`: Screen anchor origin point.
- `OFFSET_BIG_TIME_X/Y`, `OFFSET_BIG_DATE_X/Y`, `OFFSET_BIG_SPEED_NUM_X/Y`, `OFFSET_BIG_SPEED_UNIT_X/Y`, `OFFSET_BIG_ODO_X/Y`, `OFFSET_BIG_SAT_X/Y`, `OFFSET_BIG_TMR_X/Y`, `OFFSET_BIG_BAT_X/Y`, `OFFSET_INST_KML_X/Y`, `OFFSET_AVG_KML_X/Y`, `OFFSET_AVG_SPEED_X/Y`, `OFFSET_MAX_SPEED_X/Y`, `OFFSET_FUEL_LTRS_X/Y`, `SIDEBAR_LEFT_X/Y`, `SIDEBAR_RIGHT_X/Y`: Fine-grained pixel coordinate offsets for every UI component.

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
    ├── sensors.cpp        # Core 0 GPS task (bulk UART drain, TinyGPS++/UBX parser, speed fusion, odo, time-sync) + Core 1 sensor task (Hall ISR, ADC sensors, snapshot)
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

The upload path is a local developer path: it is **not** signature-checked, but it
is bounded by the real OTA partition size, and an upload whose byte count is
inconsistent or whose ESP image header does not look like firmware (magic `0xE9`,
plausible segment count, at least 4 KB) is discarded instead of activated.

#### Cloud OTA Pull & Firmware Signing

The cloud pull (Web UI → **System & Modes** → *Cloud OTA Pull*) reads a small JSON
manifest from `OTA_PULL_URL`, then downloads and flashes the image it points at:

```json
{ "version": "1.3.9",
  "firmware_url": "https://github.com/alefinot/Dashboard-for-ESP32/releases/download/v1.3.9/firmware.bin" }
```

`OTA_PULL_URL` may also point straight at the GitHub API
(`https://api.github.com/repos/alefinot/Dashboard-for-ESP32/releases/latest`), in
which case the first `.bin` asset of that release is used — keep **one** `.bin`
asset per release so the wrong file can never be picked.

Every pulled image is verified before it is allowed to boot:

| Guard | What happens when it fails |
|---|---|
| TLS certificate checked against the CA bundle compiled into the core | connection refused, mbedTLS reason logged |
| Manifest version differs from the compiled-in `FW_VERSION` | "already up-to-date", nothing downloaded |
| Sibling asset `<firmware_url>.sig` exists and is readable | update refused (a missing signature is never a pass) |
| ECDSA P-256 / SHA-256 signature valid over `sha256(firmware.bin)` + version | slot discarded, no reboot, reason shown in the Web UI |
| Every byte hashed exactly once | byte-range resume is switched off while signing is enforced; the image restarts from byte 0 |

`Update.end(true)` — the call that makes the new slot bootable — runs **only
after** the signature verifies. The public key is compiled into the firmware
(`include/ota_pubkey.h`); a firmware image built without it refuses every pull.

**Signing a release (maintainer side)**

The private key never enters the repository (`keys/` is gitignored); only its
public half is committed, generated into `include/ota_pubkey.h` (generated file —
regenerate with the script, never hand-edit):

```bash
python scripts/ota_sign.py keygen                                  # once, on the signing machine
python scripts/ota_sign.py pubkey                                  # refresh include/ota_pubkey.h
python scripts/ota_sign.py sign   1.3.9 .pio/build/esp32dev/firmware.bin
python scripts/ota_sign.py verify 1.3.9 .pio/build/esp32dev/firmware.bin
```

`sign` writes `firmware.bin.sig` next to the binary — a DER ECDSA P-256 signature
over `sha256(firmware.bin) || 0x0A || version`. Upload it as a **sibling release
asset**; a release without the `.sig` will not install on any device. The signed
version is the plain number (`1.3.9`) — the release tag is `v1.3.9`, and a `v`
prefix in a manifest version is stripped before the version compare.

Keep `keys/ota_sign_key.pem` backed up offline and private: lose it and no future
signed release can be built; leak it and anyone can ship firmware to every device.

> [!NOTE]
> **Bench builds only.** Adding `-DOTA_ALLOW_UNSIGNED` to `platformio.ini` builds
> the pull path without the signature check (the Web UI logs a warning on every
> pull). Never publish a release built with that flag. USB serial flashing and
> ArduinoOTA (`espota`) stay unsigned by design — they are local developer paths.

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
- Engine temperature, fuel level, battery voltage, satellite count, instant/avg KM/L, and average speed simulate active riding telemetry.
- Speed source indicator badge automatically cycles between `HAL`, `GPS`, and `G+H` every 2 seconds.

---

## Changelog

### V1.3.9 — Signed OTA firmware, truthful version identity, hotspot password guard
- **Firmware signing (cloud OTA pull)** — every pulled image is verified against an ECDSA P-256 / SHA-256 signature (`firmware.bin.sig`, a sibling release asset) before `Update.end()` marks the new slot bootable. The public key is compiled in (`include/ota_pubkey.h`, generated by `scripts/ota_sign.py`); the private key stays on the signing machine (`keys/`, gitignored). Missing signature, wrong version or a tampered binary = update refused, current firmware untouched. See *Cloud OTA Pull & Firmware Signing*.
- **TLS is actually verified** — the pull used `setInsecure()`, which accepted any certificate. HTTPS pulls now validate the server against the CA bundle built into the core, and a rejected certificate is logged with the mbedTLS reason instead of looking like a dead server.
- **No resume while signing** — a signature covers the whole image, so a byte-range resume would skip bytes the hash never saw. A stalled signed pull discards the partial slot and restarts from byte 0.
- **One flash session at a time (#24)** — the 15-minute pull-overrun guard used to clear the "busy" latch while the first session still held the OTA slot open, letting a second pull write into the same slot. `Update.begin/abort/end` are now behind an interlock shared by the pull task and the Web UI upload; a reset with a session still open locks OTA until reboot.
- **Web OTA upload hardening (#28)** — uploads are bounded by the real OTA partition size, every `Update.write()` is checked, the session aborts on the first error, and a file that is too small or does not start with a plausible ESP image header is discarded instead of being activated and boot-looping the unit.
- **Version identity is the build, not NVS** — `FW_VERSION` in `src/config.cpp` is the compiled-in truth; the WebUI field is now a display/pull-test label (`VERSION_OVERRIDE`). The manifest's claimed version is no longer written to NVS, so a device can no longer report "up to date" with a firmware it never ran.
- **Boot provenance** — the `bootinfo` namespace records what was running before: `BOOTINFO: firmware v1.3.9 running (previous: 1.3.8)` after an update, plus `build_version` / `version_override` / `previous_version` in `/api/ota/check`.
- **Hotspot password guard (#45)** — an `AP_PASSWORD` of 1–7 characters was rejected by the ESP32 radio, so the config hotspot silently never came up (and the old password stayed in NVS). Short values are now refused with a log line and the default is restored; `WiFi.softAP()` failures are checked and logged; the WebUI warns while the default password is in use.
- Flash usage grows from ~84.7 % to ~88.6 % of the OTA slot (mbedTLS ECDSA/SHA-256 verification code); RAM is unchanged.

### V1.3.8 — Odometer accuracy: fusion double-count fix, GPS anchor fix, persistence and sensor hardening
- **Fusion double-count fixed** — in Sensor Fusion mode the distance source is chosen on the **wheel rolling state**, not on whether one 20 ms tick happened to catch a Hall pulse. Previously most 1 Hz GPS-fix ticks contained no pulse and ran the GPS distance branch *on top of* the Hall distance already counted in the other ticks, so the odometer grew up to ~2× at speed.
- **Stale GPS anchor fixed (the stop → big-jump symptom)** — the GPS distance branch consumed TinyGPS++'s one-shot `updated` flag before the anchor update could run, so while accumulating the anchor stayed pinned at the last stop: every fix re-added `distance(current → stale anchor)` (growing bursts) until the span passed the 500 m guard and the odometer froze mid-ride, releasing only with a jump after a stop. Fixes are now captured once per fix and the anchor advances on every fresh valid fix.
- **Odometer persistence hardened** — all writes to the shared `preferences` object (gpsTask 1 km saves, WebUI `POST /api/odo`, sleep/reboot save) are serialized by a dedicated `prefsMux` mutex; the save marker (`lastSavedOdo`) only advances when the NVS write actually succeeds.
- **GPS fix snapshot** — `gpsTask` publishes every fix once to a mutex-guarded snapshot (`g_gpsFix`); the odometer, weather fetch and debug telemetry all read that copy. Cross-task reads can no longer steal the odometer's pending fix (weather fetch used to) or tear 64-bit coordinates mid-write.
- **Standstill phantom-pulse guard** — the first Hall edge after a stop is credited only when a confirming edge arrives within the standstill window (one real revolution later). Parked ignition-EMI blips no longer creep phantom kilometers onto the odometer; real roll starts count exactly as before.
- **New diagnostics (log-only, nothing is auto-corrected)** — `ODO: <n> m fix jump rejected by 500 m guard` (rate-limited to 1/min) surfaces uncounted GPS teleport stretches, and `ODO drift: hall X km vs gps Y km (±N %)` warns when Hall and GPS disagree by more than 5 % over any 60 s window (wrong wheel circumference, tire slip or missed Hall edges).

**V1.3.8 re-release (same version string) — Hall speed stability with the engine running:**

- **Standstill EMI could still move the speedometer** — the Layer 3 purge wipes every trace of a stopped wheel, and that also disarms Layer 2 (which is conditioned on the rolling state) while the median window is left holding a single sample. Two ignition-EMI edges 12 ms…1.5 s apart therefore produced a *plausible* speed on a parked machine — 100 ms → 59 km/h, 300 ms → 20 km/h on a 1650 mm wheel — and the slew limiter then ramped up and back down through that bogus target, which is the "big number oscillates while stopped" symptom. With no hardware filtering between the sensor and GPIO33 this was reachable from ordinary ignition noise.
- **Layer 3b: confirmed-roll speed gate** — the Hall speed output now stays at 0 after a standstill purge until **3 consecutive accepted intervals agree pairwise within 2×**, the same physical bound Layer 2 already applies while rolling. A random EMI pair needs three mutually consistent intervals to get through, and one consistent pair is no longer enough. Cost: a genuine roll start is displayed two revolutions later (≈0.6 s at 12 km/h, ≈0.2 s at 30 km/h). Odometer credits are untouched, and the usable low-speed floor is unchanged — the 1.5 s standstill timeout already reports 0 below ~4 km/h.
- **Layer 1 window is now tunable and wider** — new `HALL_PULSE_MIN_US` (default **150 µs**, was hard-coded 25 µs, clamped to 10…1000 µs in the ISR). A real magnet pass is hundreds of µs wide — one revolution at 200 km/h is still 29.7 ms — while ignition transients ring and collapse in <10 µs, so 150 µs rejects them with an order of magnitude of physical margin. This is the first line of defence for an unfiltered sensor at the pin, and it is now a WebUI knob instead of a recompile.
- **`HALL_PERIOD_GUARD` is finally live** — the parameter, the NVS key and the WebUI control have existed since 1.3.2 but no code ever read them. It now rejects intervals longer than N× the last accepted one (the upper half of the behaviour the docs always described; the lower half remains Layer 2's fixed 1/2 bound). `1` = off.
- **Hall filter-chain counters + `HALL:` telemetry** — per-layer counters (edges, L1 rejects, debounce rejects, L2 rejects, guard rejects, purges, speed-path holds, accepted) with the resulting Hall and displayed speed are logged at 1 Hz for the first 5 minutes after boot and once a minute afterwards, so the whole chain is finally observable over serial.
- **NVS odometer save storm fixed** — the save marker advances only on a successful write, which left a failing `putDouble()` retrying every 20 ms odometer tick; flash writes suspend flash-resident code on both cores, so that stalls the dashboard. Retries are now floored at 30 s and the failure is logged (`ODO: NVS save failed`).
- **Re-release note** — the firmware version string stays `1.3.8`, so a cloud OTA pull manifest cannot distinguish this build from the original 1.3.8; flash it over USB or from the Web UI upload instead. The GitHub release assets were replaced with the re-release binaries.

### V1.3.7 — Hall 5-layer defense pipeline, anti-collapse baseline, slew-rate limiter, pure integer ISR, sensor fusion overhaul
- **Layer 1: ISR pulse-width qualification** — on `FALLING` interrupt, delays 25 µs and directly samples the GPIO33 input register; spark ignition EMI ringing (<10 µs) is dropped immediately before any timestamping or interval logic runs, while genuine wheel magnet pulses (>140 µs) pass through cleanly.
- **Layer 2: Anti-collapse timing baseline** — locks reference interval `hallStableIntervalUs` to verified wheel rotation; unverified spark bursts during deceleration cannot collapse the guard baseline or trigger false >200 km/h readings.
- **Layer 3: Hall standstill deadlock fix & history purge** — resolved the critical bug where starting from a stop rejected pulses and permanently deadlocked the Hall sensor at 0 km/h; implemented a two-stage state machine (`STANDSTILL_TIMEOUT_US = 1.5s`) that cleanly re-syncs timing and purges the history ring buffer upon stopping so no stale cruising speeds linger.
- **Layer 4: Physical slew-rate limiter** — caps speed rate-of-change to physical vehicle dynamics (max 60 km/h/s acceleration, 80 km/h/s braking); mathematically prevents multi-hundred km/h instantaneous speedometer jumps.
- **Layer 5: Sensor fusion outlier rejection** — in Dual Sensor Fusion mode, cross-checks Hall speed against GPS: if Hall exceeds GPS by >30 km/h with a solid fix, Hall reading is rejected as an EMI spike and GPS speed is displayed.
- **Pure integer ISR & hardware debounce** — 100% integer timestamping and period guard in the ISR with zero floating-point math, protecting hardware FPU registers from interrupt corruption; 12 ms ISR debounce (`DEBOUNCE_US = 12000UL`, ~495 km/h) rejects contact bounce.
- **Overdue deceleration decay** — dynamic braking decay engages only when a pulse is truly overdue ($dt > 1.5 \times \text{period}$), ensuring the speedometer glides smoothly to 0 km/h when stopping without oscillating or diving mid-rotation during active driving.
- **Responsive median filter ($W=3$)** — lightweight 3-sample median filter mathematically rejects isolated ignition EMI blips with only a single wheel revolution of lag, tracking brisk throttle roll-on in real time.
- **Dual sensor fusion overhaul** — holds a rock-solid 0.0 km/h at traffic lights (killing stationary GPS drift), displays immediately upon rolling, dynamically blends Hall and GPS confidence when cruising, trusts Hall during rapid throttle acceleration, and seamlessly falls back to GPS if the Hall sensor is disconnected while driving.
- **Speed Source selector & independent modes** — **Hall only** (0), **GPS only** (1), **Sensor Fusion** (2, default). Hall-only mode now correctly records session `maxSpeed` and odometer distance.

### V1.3.6 — arduino-esp32 3.3.12 (ESP-IDF 5.5.5) core migration
- **Core migration** — firmware now builds on arduino-esp32 3.3.12 (ESP-IDF v5.5.5) via the pioarduino community PlatformIO platform, pinned to exact release tag `55.03.312` in `platformio.ini`; the official SCons platform tops out at 2.0.17 (espressif/arduino-esp32#8606, platformio/platform-espressif32#1225).
- **3.x API changes** — LEDC backlight switched to the pin-based `ledcAttach`/`ledcWrite` API (same GPIO12, same 1 kHz 8-bit PWM), and the console is explicitly pinned to UART1 on GPIO1/GPIO3 (`Serial.setPins`) because 3.x moved the default console to GPIO26/27.
- **Capacitive fuel touch sensor removed** — the legacy RTC touch-pad driver cannot coexist with the new-generation touch driver of arduino-esp32 3.x (IDF 5.5 aborts on mixing), so the capacitive fuel sensor is gone. The fuel gauge pipeline (EMA filter, 20-point calibration table, ascending/resistive branch) is unchanged and now reads a neutral 0 — the gauge shows empty until a resistive fuel sender is wired to the reserved GPIO32 and the table re-fitted.
- **Build environment** — first build downloads the pinned platform release (full rebuild + xtensa 13.2 toolchain); pins, `partitions.csv`, NVS layout and library pins are unchanged.

### V1.3.5 — Compass/heading feature removed
- **Compass feature removed** — the dashboard is now screwed to the vehicle and cannot be rotated for auto-calibration, so the magnetometer compass is gone for good: QMC5883L driver, tilt-compensated heading math, heading-tape HUD readout, WebUI calibration workbench, the five `/api/compass/*` endpoints, and all related NVS parameters (`OFFSET_COMPASS_X/Y`, `SHOW_ELEMENT_COMPASS`, `COMPASS_DECLINATION_DEG`, `HEADING_DIGITS`, `COMPASS_CAL_*`, `COMPASS_TILT_COMP`).
- **Freed pins** — GPIO21/22 (former compass I²C bus) are now free.
- **Mobile WebUI fixes** — the Time Zone select no longer overflows the System & General card on narrow screens (it was sized to its longest option and the details box clipped its right edge), and the Advanced Mode toggle no longer overlaps the page title on phones (it now flows below the IP banner, centered).
- **WebUI source cleanup** — removed the browser "saved page" artifact (the `saved from` URL header) and the hardcoded `192.168.1.136` initial state from `webui.html`; the IP banner now starts from the always-true AP address and the live `/api/perf` value remains the source of truth.

### V1.3.4 — Refuel fix, mDNS rebind on reconnect, OTA check auto-retry
- **Refuel trip reset** — the automatic refuel reset (fuel rise ≥ threshold) now also clears `tripDistanceKm`, so the post-refuel average KM/L is no longer diluted against the pre-refuel trip distance.
- **mDNS rebind on reconnect** — the STA finalize latch is cleared on link loss, so a reconnect re-binds mDNS to the new DHCP IP (`dashboard-pp.local` no longer advertises a stale address) and re-arms the weather fetch; NTP stays one-shot.
- **OTA check auto-retry** — a throttled cloud update check now waits out its 1-minute limit and retries by itself, showing a live countdown in the WebUI instead of a frozen "waiting" message; the first check after boot always runs.
- **Configurable AP password** — new `AP_PASSWORD` param (default `12345678`) makes the `Dashboard_Config` SoftAP password user-settable in the WebUI (System & General → Wifi).
- **Thread-safe clock** — the shared local-time computation switched to `gmtime_r` (it is called from two cores).
- **Cleanup** — removed the dead 10px Conthrax VLW font (~10 KB flash; its path shimmed to the 4pt7b font) and a duplicate `otaMemReleased` extern.

### V1.3.3 — Crash forensics, safe-mode loop breaker, bad_alloc guards
- **Boot/reboot forensics** — new `/api/boot` endpoint exposes the reset reason, fast-reboot-storm state, last-reboot tag + heap, and the min-free-heap watermark since boot; every `ESP.restart()` site is now tagged so a crash vs. a clean reboot is diagnosable (a hard crash reads `PANIC`, a clean reboot reads `SW`).
- **Safe-mode loop breaker** — the heap-critical auto-reboot is gone: when memory-saver can't hold heap above ~16KB the device stays UP in its most-frugal state (mem-saver on, weather/TLS suppressed) instead of rebooting; only the OOM floor (`<8KB`) reboots, and never during a fast-reboot storm.
- **bad_alloc guards** — the 120px VLW reload and the speed-sprite build now pre-check the largest free heap block before allocating, so a fragmented heap degrades to the existing fallback instead of tripping the uncaught `new → bad_alloc → abort` crash.

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
