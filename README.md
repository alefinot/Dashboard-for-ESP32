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
| **GPIO0** | BOOT | Factory Reset | Input (Pullup) | `pinMode(0, INPUT_PULLUP)`; **hold BOOT 4 s, release, hold 4 s again** within the first 30 s after boot to wipe the config — the reliable path. The release in the middle is required because GPIO0 is shared with the serial adapter's DTR line through the auto-reset transistor: a PC that holds DTR asserted keeps GPIO0 low forever, and the old single 8 s hold wiped the config on every boot with nobody touching the board. The serial fallback (send `RESET` within the first 2 s of boot) is matched against a rolling 32-character window, so boot-log echo can no longer fill the capture and hide the command (issue #39) |
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

#### UBX Frame Parser (receive-only)

The module streams UBX and NMEA on the same UART, so `src/sensors.cpp` runs a byte-at-a-time state machine (`ubxParseByte`) alongside TinyGPS++: it locks onto `0xB5 0x62`, reads class/id/length, seeds both Fletcher-8 accumulators by stepping through each header byte individually, and hands a checksum-valid NAV-PVT to the NMEA side. It never transmits anything (see the receive-only rule).

- **Length is honoured, including zero.** A zero-length payload goes straight to the checksum states — entering the payload state with `len = 0` used to consume `CK_A`/`CK_B` as payload, corrupt the accumulators and raise a spurious `CKFAIL`, leaving the parser to resync by luck on the next `0xB5` (issue #33).
- **Oversize frames are dropped at the header** (`len > 92`) rather than overflowing the payload buffer, and a bad checksum drops exactly one frame before resyncing.
- **Counters, not silence:** `ubxSyncSeen`, `ubxCkFail`, `ubxOversize` are reported in the GPS debug telemetry so a framing problem is visible instead of looking like lost satellites.
- **Checked offline:** `python scripts/verify_ubx_parser.py` runs a host replica of the state machine against hand-built frames — NAV-PVT (92 B), ACK (10 B), zero-length, poisoned checksum, garbage/false-sync, oversize, back-to-back frames — and asserts that the old zero-length path still fails, so the regression cannot come back unnoticed.

---

### 2. Anti-Aliased GFX Engine & 7-Segment Fonts

Graphics rendering is built on `LovyanGFX` with specialized antialiasing routines and a PROGMEM-mapped VLW font system (compiled into flash — no runtime filesystem lookups, zero DRAM per glyph).

#### Text Bounds Contract (`getTextBounds`)

`LGFX_ST7789_4::getTextBounds()` (declared in `src/dashboard.h`, defined in `src/gfx.cpp`) reports **advance-based** bounds, not ink bounds: `w` is `textWidth()` (the advance the string consumes), `h` is the font line height, `y1` is `-baseline`, and **`x1` is always `0` by contract** (issue #44). Every layout path — 7-segment digit cells, the weather/accel badges, the centred splash and signature strings — places text on advances, and the `- x1` terms those expressions carry are the documented slot where a real per-glyph left bearing would flow through, not a live correction. Code must not treat `x1` as a measured bearing; if ink bounds are ever needed, a separate helper gets added rather than these semantics changing. The `digitXOff[]` / `ds15_digitXOff[]` tables fed from it are therefore advance-only offsets by construction.

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
- **Fuel sender** — the live sender resistance (Ω) is shown next to a fault state (`open` / `shorted` / `out of range` / `no sender`). Two ways to build the table (`fuelCalOhms`): **Fill from empty/full ohm** pre-fills a linear ramp from `FUEL_OHM_EMPTY` to `FUEL_OHM_FULL` (a good start, no driving needed), or drive the tank through its levels and **Capture** the live reading into each slot — slot `0` = empty, slot `N-1` = full. Changing the point count regenerates the ramp.
- Calibrating against a live reading means no raw-ADC arithmetic in the browser: the offset is derived on the device from the value it is currently seeing.

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
- **Multi-SSID Client Mode:** Stores up to 5 networks. They are tried in slot order, and the search **never expires** — a failed cycle is simply followed by another one, forever (there is no search-policy parameter to set). The order is **automatic**: when a network joins, it is moved to slot 0, so the network this unit actually reached last is the first one tried after a reboot or a dropout — no hand-ordered “primary / fallback 1…4”. Credentials are only ever removed by an explicit **Forget**; being out of range is the normal state for a vehicle and never deletes a network by itself.
- **Phone-style network picker (Web UI → Network → Wifi):** press **Scan Networks**, tap the network, type the passphrase once. Open networks join without a password, a network that is already stored reconnects without asking for it again, and a hidden network is added by name. The scan is a cached list of the 30 strongest SSIDs (channels 1–13); it is never refreshed automatically, because a scan moves the radio off its working channel — it runs when asked and after a failed search cycle. With all five slots full the device refuses to guess: the UI asks which stored network may be replaced.

> [!IMPORTANT]
> **Access model (deliberate).** The config portal and the REST API have **no login** — access control is the Wi-Fi itself. `AP_PASSWORD` must be empty or 8–63 characters; a shorter value is rejected and the default (`12345678`) is restored, so the hotspot can never come up with a password the ESP32 silently refused. **Change that default before using the dashboard on a public network**: anyone who joins the hotspot (or reaches the dashboard's LAN IP) can change settings and flash firmware.

### Web UI Features
The management portal features a modern grouped card-based layout:
- **Collapsible sections** with smooth accordion animations (non-JS fallback)
- **Live search bar** to filter configuration parameters across all sections
- **Autosave** triggered 2 seconds after any input change
- **Visible checkboxes** (UI Layout → Screen Elements) control only what is drawn on the display; sensors, GPS, odometer and every calculation keep running whether the element is visible or not, so hiding a readout never stops the data behind it
- **Advanced Mode** switch in the header reveals the technical parameters, so the common setup path stays short (see `ADV_MODE`)
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
| `/api/config` | `POST` | Updates NVS parameters and applies changes; on a save where N values could not be written the answer carries `nvsErrors: N`, `nvsFailedKeys: "KEY,…"` and `nvsAvailable` (free NVS entries) | Config JSON object | `application/json` |
| `/api/wifi/scan` | `GET` | Cached Wi-Fi scan plus the stored networks: `scanning`, `ageMs`, `count`, `max: 30`, `nets[]` (`ssid`, `rssi`, `ch`, `auth`, `authName`, `saved`) and `saved[]` (`slot`, `ssid`, `connected`). Reading it never touches the radio | None | `application/json` |
| `/api/wifi/scan` | `POST` | Queues a background scan of channels 1–13 (the radio leaves its channel for a few seconds, which is why the firmware never scans on its own); poll `GET` for the result | None | `application/json` (`{"status":"requested"}` / `{"status":"scanning"}`) |
| `/api/wifi/join` | `POST` | Stores one network and searches for it immediately. An SSID already stored keeps its slot (with no `pass` field the stored passphrase is reused, not blanked); otherwise the first free slot is used. With all five taken the answer is `status:"full"` plus the stored list, and re-sending with `"slot":n` names the network to displace | `{"ssid":"…","pass":"…"}` — `pass` omitted = reuse stored / open network; `"slot":n` = overwrite that slot | `application/json` (`status`, `slot`, `ssid`, `nvsOk`, `overwrite`) |
| `/api/wifi/forget` | `POST` | Removes one stored network from RAM and NVS and restarts the search. Credentials are never dropped by the device on its own | `{"slot":n}` (0–4) | `application/json` (`status`, `slot`, `ssid`, `nvsOk`) |
| `/api/time` | `POST` | Syncs system clock from browser | JSON body `{"timestamp":1700000000}` (integer epoch s; must fall in 2020–2100) | `application/json` (`{"status":"ok"}` / `400` with reason when out of range) |
| `/api/odo` | `GET` | Reads odometer distance in km | None | `application/json` |
| `/api/odo` | `POST` | Sets odometer distance | `{"km": 123.45}` | `application/json` |
| `/api/trip/reset` | `POST` | Zeros the trip stats (same reset as the GPIO25 button); the odometer and session max speed are untouched | None | `application/json` |
| `/api/reboot` | `POST` | Triggers graceful device restart | None | `text/plain` |
| `/api/sleep` | `POST` | Triggers immediate deep sleep | None | `text/plain` |
| `/api/reset` | `POST` | Factory reset — clears the `cfg` namespace; WiFi credentials and the odometer survive | None | `text/plain` |
| `/api/ambient` | `GET` | Reads raw ambient light sensor value | None | `application/json` |
| `/api/sensors` | `GET` | Reads calibrated battery voltage (`v`) and coolant temperature (`t`) | None | `application/json` |
| `/api/fuel` | `GET` | Reads the fuel input: `raw` averaged ADC code, `liters`, `pct`, `ohm` sender resistance, `st` input state (0 = disabled, 1 = ok, 2 = open circuit, 3 = shorted) | None | `application/json` |
| `/api/ambient/cal-dark` | `POST` | Sets dark-reference ambient light value (auto-brightness floor) | None | `application/json` (`{"status":"ok"\|"saved-ram-only","value":N}` — `saved-ram-only` means the value is applied but the NVS write failed) |
| `/api/ambient/cal-bright` | `POST` | Sets bright-reference ambient light value (auto-brightness ceiling) | None | `application/json` (same shape as `cal-dark`) |
| `/api/ota` | `POST` | Over-The-Air firmware binary upload | Binary `.bin` payload | `multipart/form-data` |
| `/api/ota/pull` | `POST` | Triggers cloud OTA pull (checks `OTA_PULL_URL`) | None | `text/plain` |
| `/api/ota/check` | `GET` | Reports cloud OTA pull state (`enabled`, `url`, `current_version`, `build_version`, `version_override`, `previous_version`, `status`) | None | `application/json` |
| `/api/serial` | `GET` | Streams internal 4 KB ring buffer logs | None | `text/plain` |
| `/api/perf` | `GET` | Live telemetry (CPU, Heap, task stack headroom, FPS, WiFi, partitions) | None | `application/json` |
| `/api/health` | `GET` | Health probe: heap, FPS, partition layout and the NVS entry budget (`nvs_bytes`, `nvs_entries_used` / `_available` / `_total`) | None | `application/json` |
| `/api/boot` | `GET` | Boot/reboot forensics (reset reason, storm, last-reboot tag, heap watermark). `reset_reason` covers the whole core enum — `POWERON`, `EXT`, `SW (clean restart)`, `PANIC (crash/abort)`, `INT_WDT`, `TASK_WDT`, `WDT (other)`, `DEEP_SLEEP_WAKE`, `BROWNOUT`, `SDIO`, `USB`, `JTAG`, `EFUSE_ERR`, `PWR_GLITCH`, `CPU_LOCKUP`, `UNKNOWN`; anything unmapped prints `RST_<n>` (issue #42) | None | `application/json` |

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

### NVS Access, Locking & Write Errors (issue #32)

ESP-IDF's NVS layer has its own internal locking, so a torn write is unlikely — what is *not* protected is the session. Two tasks opening and closing the same namespace independently can still collide: one task's `end()` closes the handle the other is still using, which comes back as `ESP_ERR_NVS_*` from `putInt()`/`putString()` and shows up as a setting that quietly reverted after a reboot. The old code took `prefsMux` at some call sites and not at others, so the protection depended on which file the code happened to live in.

- Every namespace open now goes through **`NvsSession`** (`src/dashboard.h`): an RAII wrapper that holds `prefsMux` for the whole `begin()`/`end()` pair, logs when a namespace cannot be opened, and closes on scope exit so an early return cannot leak the handle. It skips the lock while `prefsMux` is still `NULL` (early boot, single-threaded). The long-lived `preferences` object for the `dashboard` namespace keeps its explicit `prefsMux` pairs.
- **Write results are checked.** `nvsWriteFailed(key, esp_err_t)` logs the failing key with `esp_err_to_name()`; the `CFG_*` save macros count the failures and a save ends with `Config save: N parameter(s) failed to reach NVS … they will revert on reboot` instead of pretending nothing happened. The same check covers the fuel-table writes, the factory-reset credential restore, the bootinfo writes and the ambient calibrations.
- **A failed read is not a default.** Where an unreadable NVS would previously have produced "no value" and triggered a factory seed, the seed is now skipped with a log line — a flash that cannot be read must never be answered by overwriting it.
- `prefsMux` is a plain (non-recursive) mutex: nothing inside a locked NVS region may open NVS itself.

### Key Configuration Categories

#### System & Performance
- `TARGET_FPS` (default=60): Desired display refresh rate, **5–120 FPS** (band-checked on load and in the Web UI). It sets the derived `DISPLAY_REFRESH_MS` frame budget (120 → 8 ms, 60 → 16 ms, 5 → 200 ms); values above what the SPI panel can carry simply drop the FPS counter rather than saturating the display core (issue #36).
- `SPI_BUS_SPEED` (default=60000000): SPI bus frequency in Hz. Accepted range **1000000–80000000** (1–80 MHz; WebUI shows MHz). Out-of-range values are clamped on load/save — a `0` or a negative (wrapped) value would otherwise feed LovyanGFX a degenerate clock divider and blank the panel. The read clock is derived as `SPI_BUS_SPEED × 8/5`, computed in 64-bit and clamped to the same band.
- `ENABLE_DYNAMIC_CPU` (default=false): Toggles automatic CPU frequency scaling (hysteresis-based).
- `MANUAL_CPU_FREQ` (default=240): Fixed CPU clock frequency (80, 160, or 240 MHz) used when `ENABLE_DYNAMIC_CPU` is off. The value is snapped to the nearest legal step, and every `setCpuFrequencyMhz()` call now checks its result — a refused switch logs `CPU: refused to switch to …MHz` instead of claiming a frequency the chip never switched to (issue #34).
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
- `WEATHER_LAT` (default=0.0) and `WEATHER_LON` (default=0.0): Fallback coordinates for the Open-Meteo forecast request. While the unit has a GPS fix it uses the **live position** instead and these two are ignored; they are what the widget uses when there is no fix (no antenna, parked underground, GNSS off). With no fix **and** no coordinates set, no weather request is made at all. `WEATHER_CITY` is only the label, never the source of the coordinates.
- `WEATHER_REFRESH_MIN` (default=15): Refresh interval in minutes.
- `WEATHER_LOCALE` (default="it"): ISO locale code for the **city name** — the language the widget spells it in. With a live GPS fix the reverse-geocode (BigDataCloud) returns the name in this language. With no fix, the typed `WEATHER_CITY` is resolved on Open-Meteo's geocoding service and re-spelled in this language when that place has a name in it (`Milan` → `Mailand` for `de`, `München` → `Monachium` for `pl`); a name the service does not know (`Casa di NONNA`) or a match more than 150 km from the configured position is displayed exactly as typed, and the lookup is cached per name+locale so it costs one request per change, not per refresh. The service matches names *in the requested language*, so an exonym it doesn't index in that language (typing `Milano` while set to `de`) leaves the label untouched rather than swapping in a same-named town. Localization also needs coordinates to anchor on: with no fix and no `WEATHER_LAT`/`WEATHER_LON`, the typed name is shown as written. The localized spelling never overwrites the stored `WEATHER_CITY`. Only the Latin-script codes (`it en fr de es pt nl pl tr`) are localized on the panel — the Conthrax font subset has no verified glyphs for Cyrillic/Greek/CJK/RTL, so those keep the typed spelling (the WebUI has no such limit). The forecast itself is not translated. Saving a new value re-fetches the weather straight away, so the name changes without a reboot.

#### Time, Date & Daylight Saving
- `NTP_ENABLED` (default=true): Sync the system clock from NTP once the WiFi station is up. `configTime()` is armed and then polled **once per web-loop iteration** over a 5 s window — the old `delay(500)` retry loop ran inside the web task and left the config page and API unreachable for up to 5 s after every Wi-Fi join (issue #43). A missed sync is logged; a GPS fix or `POST /api/time` can still set the clock.
- `NTP_SERVER` (default="pool.ntp.org"): NTP host used for that sync.
- `TZ_OFFSET_HOURS` (default=1): Zone offset in whole hours — **the standard (winter) offset**, range −14…14. With DST on, this is the offset the clock uses outside the DST period; the rule below adds the extra hour. New York is `-5`, Paris is `+1`, Athens is `+2`.
- `TZ_DST_ENABLED` (default=true): Master switch for daylight saving. When off the clock keeps `TZ_OFFSET_HOURS` all year.
- `TZ_DST_RULE` (default=1): Which calendar `TZ_DST_ENABLED` applies (issue #22). `0` = none, `1` = **EU/EEA** (last Sunday of March and October, both at 01:00 UTC), `2` = **US/Canada** (second Sunday of March, first Sunday of November, both at 02:00 local wall clock — converted to UTC through `TZ_OFFSET_HOURS`, so a GMT−6 zone springs forward at 08:00 UTC and a GMT−5 zone at 07:00 UTC). The rules are integer arithmetic on the UTC date (no zone database); `scripts/verify_dst_rules.py` checks them against the published 2024–2026 transition instants. Before this parameter the DST half was hard-wired to the EU calendar, so a US zone was wrong for roughly three weeks a year.
#### WiFi & Cloud OTA
- `AP_PASSWORD` (default="12345678"): Password of the `Dashboard_Config` SoftAP (the configuration portal). Set in the WebUI (System & General → Wifi → Device Config AP). Must be **empty** (open network, not recommended) or **8–63 characters**: the Wi-Fi stack rejects 1–7 character passphrases, so a shorter value is refused at load/save time and the shipped default is restored instead (logged as `Config: AP_PASSWORD too short`). The WebUI warns while the default password is still in use.
- **Not parameters any more:** the search policy is fixed to *search forever* and TX power is fixed at `WIFI_POWER_19_5dBm` (19.5 dBm — the maximum the classic ESP32 accepts). `WIFI_RETRY_MODE`, `WIFI_RETRY_SECONDS` and `WIFI_TX_POWER_DBM` are gone from the parameter table, the WebUI and the backup template; a backup that still carries the old `WIFI_RETRY_M` / `WIFI_RETRY_S` / `WIFI_TXP` keys restores without them.
- `WIFI_ATTEMPT_SECONDS` (default=12, range 5–60): How long one network gets per try — the window covers the driver’s scan plus association, 4-way handshake and DHCP. A dead network is dropped earlier as soon as the driver reports its `STA_DISCONNECTED` reason (`NO_AP_FOUND`, wrong password, auth timeout), so this window only decides how long a *silent* network (in range but not answering, or a stalled DHCP lease) is waited for before the next slot is tried.
- `OTA_PULL_ENABLED` (default=false): Toggle automatic cloud pull (checks once per boot while enabled).
- `OTA_PULL_URL` (default=""): HTTPS URL of the update **manifest** — either JSON with `version` + `firmware_url`, or the GitHub `releases/latest` API URL (first `.bin` asset is used). The matching signature is fetched from `<firmware_url>.sig`.
- `VERSION_OVERRIDE` (default=""): Optional version **label**. When set, the unit reports this string instead of its compiled-in build version (`FW_VERSION` in `src/config.cpp`) — for display and as the reference version of the cloud OTA check, which is how you test an update pull against an arbitrary version. It never changes the firmware image, and nothing fetched from the network ever writes it (the previous behaviour of storing the manifest's version is why a unit could report a version it never ran). The real build version is always exposed read-only as `build_version` (`GET /api/config`) and shown in the WebUI (System → Firmware Update). NVS key `VER_OVR`.

#### Ambient Light (Auto-Brightness)
- `LIGHT_SENSOR_DARK_VAL`: Dark-reference ambient light value (calibrated via `/api/ambient/cal-dark`).
- `LIGHT_SENSOR_BRIGHT_VAL`: Bright-reference ambient light value (calibrated via `/api/ambient/cal-bright`).

#### Digit Boundaries (Configurable 7-Segment Formatting)
- `SPEED_DIGITS`, `SAT_DIGITS`, `TMR_INT_DIGITS`, `TMR_DEC_DIGITS`, `BAT_INT_DIGITS`, `BAT_DEC_DIGITS`, `INST_INT_DIGITS`, `INST_DEC_DIGITS`, `AVG_INT_DIGITS`, `AVG_DEC_DIGITS`, `AVG_SPEED_INT_DIGITS`, `AVG_SPEED_DEC_DIGITS`, `MAX_SPEED_INT_DIGITS`, `MAX_SPEED_DEC_DIGITS`, `FUEL_INT_DIGITS`, `FUEL_DEC_DIGITS`, `ODO_INT_DIGITS`, `ODO_DEC_DIGITS`: Configurable integer and decimal digit limits for all UI numerical readouts. These values size fixed 16-slot cell arrays in the display renderer, so they are range-checked on every load and save (issue #6): integer counts `1..14` (1-4 for `SPEED_DIGITS`, 1-3 for `SAT_DIGITS`), decimal counts `0..4`, and per readout `int + dec + 1 <= 16` (+1 is the decimal-point slot). Out-of-range values are clamped on load (boot, `POST /api/config`, config restore) and logged; the renderer keeps a hard bounds guard as a second line of defence. A readout also caps its *value* to what its configured digits can print (e.g. `FUEL_INT_DIGITS=1` + `FUEL_DEC_DIGITS=1` shows at most `9.5`; raise the int count to `2` in the WebUI — "Fuel & Battery" card → Fuel LTRS → Integer Digits — for two-digit litres), so a value with more digits than the cells never draws outside its cell array (issue #7). `SAT_DIGITS` (1–3) sizes the satellite readout the same way since issue #41: the count is capped to what N digits can print and the erase box is measured from N × `'8'` instead of a hardcoded two-digit string — before that the parameter was stored, exported, adjustable in the WebUI and documented, but no code ever read it. Separately, every digit loop resolves its character through `digitIndex()` in `src/ui.cpp` before touching the per-glyph metric tables (`digitWidth[10]`, `ds15_digitXOff[10]`, …): a character outside `'0'..'9'` returns `-1`, the cell is skipped and keeps whatever the ghost/erase pass already painted, instead of indexing a 10-entry table with `-3` (issue #31).

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
- An **80 KB NVS partition** (`0x14000`, 20 pages) — 4× the 20 KB shipped until 1.4.0, see the changelog entry on NVS exhaustion
- Two OTA app slots (0x1D0000 each)
- A 256 KB LittleFS partition (0x40000), retained only for `/api/health` byte reporting (fonts are now PROGMEM)

Layout (4 MB flash, ends at exactly `0x400000`):

| Partition | Offset | Size |
|---|---|---|
| `nvs` | `0x9000` | `0x14000` (80 KB) |
| `otadata` | `0x1D000` | `0x2000` |
| `app0` | `0x20000` | `0x1D0000` |
| `app1` | `0x1F0000` | `0x1D0000` |
| `spiffs` | `0x3C0000` | `0x40000` (256 KB) |

> [!WARNING]
> A partition-table change is **not** delivered by an OTA update — OTA replaces the app slot, never the table. A unit that updates over the air keeps whatever table it was last flashed with, and the boot log line `NVS 'nvs' <bytes> bytes: …` is how you tell which one it is. Moving a board onto the new table needs a wired re-flash (`pio run -t erase` then `pio run -t upload`), and **the erase wipes NVS**: WiFi credentials, odometer and calibration have to be entered again.

#### Settings storage budget

Web UI → Performance panel → **Memory & Storage** shows `Settings storage (NVS …): 420 of 2520 entries used, 1974 available`, served by `/api/health`. The figures are entry **slots**, not bytes, because slots are what NVS actually runs out of — a partition can report hundreds of free slots and still refuse a write, since its tidier needs one spare page to move live data into. `available` already subtracts that reserve, so it is the number to watch; it turns amber below 15 % of the total and red below 3 %, where saves start failing. ESP-IDF garbage-collects NVS automatically inside its own write path — there is no public compaction call in arduino-esp32 3.3.x — so this readout is deliberately observation only.

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
{ "version": "1.4.0",
  "firmware_url": "https://github.com/alefinot/Dashboard-for-ESP32/releases/download/v1.4.0/firmware.bin" }
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
python scripts/ota_sign.py sign   1.4.0 .pio/build/esp32dev/firmware.bin
python scripts/ota_sign.py verify 1.4.0 .pio/build/esp32dev/firmware.bin
```

`sign` writes `firmware.bin.sig` next to the binary — a DER ECDSA P-256 signature
over `sha256(firmware.bin) || 0x0A || version`. Upload it as a **sibling release
asset**; a release without the `.sig` will not install on any device. The signed
version is the plain number (`1.4.0`) — the release tag is `v1.4.0`, and a `v`
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

### V1.4.0 — Input validation sweep, NVS discipline, receive-only GNSS, marine fuel sender and a trip-reset button

The issue-sweep release: 35 reported defects closed since V1.3.9, plus three user-facing features. No pin changes.

**New features**
- **Marine resistive fuel sender (#18)** — the fuel input is no longer demo-only. GPIO32 (ADC1_CH4) reads a resistive tank sender through the calibration table; `FUEL_OHMS_MIN`/`FUEL_OHMS_MAX` describe the sender's empty/full resistance, the ADC is characterised once at boot (`Fuel: raw … at E/F ohms`) and refuel detection works outside simulation.
- **Trip-reset button on GPIO25 (#17)** — debounced active-low input with internal pull-up resets trip distance, trip time, average speed and session max. The automatic refuel trip-reset is gone: a trip is reset by the button or the Web UI only.
- **DST rule selector (#22)** — `DST_RULE` picks `EU`, `US` or `none`, so zones whose summer offset is not simply base+1 h stop drifting an hour twice a year. `TZ_OFFSET_HOURS` is documented as the winter/base offset and is now bounded (#35).

**Config input validation**
- **Every numeric parameter has a band (#21, #20, #13, #34, #35, #36)** — 131 parameters carry an accepted range checked on read, on write and after an NVS load; out-of-band input keeps the last-good value and logs it. Includes the panel SPI clock (#13 — a 0 or oversized value blanked the ILI9488), backlight clamping through one shared helper (#20), `setCpuFreqMHz()` return checking (#34), `TZ_OFFSET_HOURS` bounds that previously overflowed the local-time maths (#35), and `TARGET_FPS` bounds that also retire a dead branch (#36). `scripts/verify_config_ranges.py` fails when a parameter is missing from the table.
- **Digit counts really are boundaries (#6, #7, #41)** — counts are clamped to the cell arrays that draw them (14 cells max), values are capped to what N integer digits can print, and an over-wide string drops leading characters instead of indexing a cell array at `-1`. `SAT_DIGITS` (1–3) is wired to the satellite readout (#41): value capped, erase box measured from the configured width.
- **WebUI limits are generated, not hand-maintained** — `scripts/make_webui_limits.py` regenerates `FIRMWARE_LIMITS` from `config.cpp` so a web input can never accept a value the firmware misuses.

**Weather city language**
- **City name follows `WEATHER_LOCALE` with no GPS fix** — with the GNSS dark the typed `WEATHER_CITY` is looked up on Open-Meteo's geocoding service (`geocoding-api.open-meteo.com`, keyless, plain HTTP) and the widget shows that place's name in the configured language (`Milan` → **Mailand** in German, `München` → **Monachium** in Polish). Candidates more than 150 km from the configured position are discarded — an un-hinted `Milano` search first hits Milano, Texas, 8,694 km away — a name the service does not know is kept exactly as typed, the lookup needs coordinates to anchor on, and the answer is cached per name+locale, so it is one extra request per change rather than per refresh. `WEATHER_CITY` itself is never rewritten. Spellings the panel font cannot draw (non-Latin locales) keep the typed name, and localized names are now cut on a character boundary instead of through a multi-byte sequence (`copyFixed` is shared with `ui.cpp`).

**Concurrency and shared state**
- **GPS read through a snapshot, not cross-core TinyGPS++ (#9)** — display and odometer paths read the mutex-guarded `g_gpsFix` copy; the TinyGPS++ objects belong to `gpsTask` alone. A weather fetch used to steal the odometer's pending fix mid-ride.
- **Odometer reads locked as well as writes (#14)** — the 8-byte `totalDistanceKm` is copied under `odoMux`, so telemetry can no longer show a torn value.
- **Weather text under `g_stateMutex` (#8)** — the display task copies the fixed-size weather fields under the lock instead of racing the fetch task (the `String` members are gone too).
- **`POST /api/config` no longer reconfigures live from the web task (#10)** — bus and CPU changes raised by a save are applied by the display loop, which owns the panel.
- **Task creation checked (#25)** — a `sensorTask`/`gpsTask`/`webServerTask` that cannot start is logged and retried with a smaller footprint instead of the dashboard silently showing zeros.
- **NTP no longer blocks the web server (#43)** — the post-Wi-Fi wait that froze every endpoint for up to 5 s is now a non-blocking settle poll.

**GNSS**
- **The link is strictly receive-only** — `ubxSend()`, the recovery-command path and the boot reconfiguration are removed. Nothing is ever written to the GNSS UART; the module is left exactly as configured and the dashboard only parses what it receives (the #11/#26/#39 recovery machinery goes with it).
- **UBX parser cannot desync (#33)** — a zero-length payload frame advanced the state machine wrongly and cost frame sync until the next NMEA line; the parser now consumes exactly one frame. `scripts/verify_ubx_parser.py` replays the 11 malformed cases, including the two that hung the parser.
- **Fix validity read correctly (#12)** — NAV-PVT fix state comes from the fix-validity flag, not the time-validity bit.
- **Boot RESET capture bounded (#39)** — the pre-reset serial capture buffer can no longer saturate before the command arrives.

**Power and uptime**
- **Power-sense blips don't park the GPS task (#26)** — `POWER_SENSE` LOW is debounced, and a real power loss sleeps 10 s instead of halting forever.
- **Stacks measured and sized (#27)** — `gpsTask`'s 1 KB buffer inside its 4 KB stack plus the snapshot path came within ~100 bytes of overflow; high-water marks are reported (`TASKS: gps high-water … bytes`) and re-sized on a half stack.
- **Overlay redraw actually gated (#15)** — the GPS debug overlay compared values that could never match and repainted every 500 ms; caching restores the intended cadence.

**Sensors and rendering**
- **Hall ISR no longer busy-waits (#30)** — the 1 ms width-qualification loop ran with interrupts disabled on the core driving the display; edges are stamped in the ISR and width-filtered on the task.
- **Fuel gauge cannot freeze on a bad table (#19, #4)** — out-of-order (E > F) calibration rows are repaired on load, and a reading outside the table clamps instead of sticking at a stale value.
- **No-data fuel readout (#16)** — a trip with no driving shows `0`, not the placeholder `99.9 km/L`.
- **Temperature marker cleanup (#29)** — the green `TEMP_WARN_GRN` marker (never stored, exported or exposed) is removed; the gauge fades blue → amber → red across the two remaining thresholds.
- **Digit renderers validate their input (#31)** — every digit loop resolves its character through `digitIndex()` before touching the 10-entry per-glyph metric tables; a non-digit skips the cell instead of reading out of bounds.
- **Text bounds contract documented (#44)** — `getTextBounds()` is advance-based by definition (`x1` always 0, `y1 = -baseline`); the `- x1` terms in the layout code are a documented slot, not a live offset.

**WiFi connectivity**
- **The search really walks the saved networks** — since the arduino-esp32 3.3.12 / ESP-IDF 5.5.5 core migration (V1.3.6), `esp_wifi_set_config()` returns `ESP_ERR_WIFI_STATE` while a connect attempt is in flight (`esp_wifi.h`: "ESP_ERR_WIFI_STATE: WiFi still connecting when invoke esp_wifi_set_config"), and the core auto-reconnects on its own by default (`STAClass::_autoReconnect = true`, `WIFI_REASON_NO_AP_FOUND` counts as reconnectable) — so a failed SSID leaves an attempt armed on that SSID forever. Every `WiFi.begin()` for the next saved network was refused: the STA config never changed, `WiFi.status()` never reached `WL_DISCONNECTED`, and the unit stayed pinned to the first network it tried (typically the primary), never reaching `WIFI_SSID_1…4`; a lost link was never re-searched either. The search loop now owns reconnects (`WiFi.setAutoReconnect(false)` plus a `WiFi.onEvent` handler), waits for the driver to report an attempt over before installing the next SSID, gives up on a dead network as soon as its `STA_DISCONNECTED` reason arrives instead of waiting out the 5 s window, and always restarts from slot 0 — which since the network picker landed below is *the network that connected last*, not a hand-picked primary. Verified by build and code review; the ESP-IDF rule is confirmed against the 5.5.5 headers and espressif/esp-idf#17484.
- **The search never expires — and there is nothing left to configure** — `WIFI_RETRY_MODE` and `WIFI_RETRY_SECONDS` are removed as parameters (NVS keys, WebUI controls and backup-template entries all gone): a cycle through the saved networks that ends without a join is followed by the next cycle after a 2 s backoff, indefinitely, and `STA_GIVEUP` is only ever reached when *nothing* is configured to search for. The old “stop after one cycle / search for a fixed time” options were the reason a unit could sit offline for hours after its router rebooted. The heartbeat line gains `sta=<phase>/<net>/<reason>` and `STA link lost (reason=…)` names the driver’s `WIFI_REASON_*` code, so a unit that will not join is diagnosable without reproducing it.
- **TX power was being set 16× lower than the setting claimed — now fixed at the module maximum** — `WIFI_TX_POWER_DBM` (−1…20, default 20) was passed straight into the `wifi_power_t` enum: that enum is **0.25 dBm steps**, not dBm (`WIFI_POWER_5dBm = 20`, `WIFI_POWER_8_5dBm = 34`, `WIFI_POWER_19_5dBm = 78`), so “20 dBm” selected raw value 20 = **5 dBm**, and the log agreed with the setting instead of the radio. The unit has been transmitting at ~3 mW, which also weakens probe and ACK power — a direct candidate for the `WIFI_REASON_NO_AP_FOUND` results on networks that are plainly in range. `WiFi.setTxPower(WIFI_POWER_19_5dBm)` is now compiled in (raw 78: `esp_wifi_set_max_tx_power()` accepts 8–78 on the classic ESP32; ESP32-WROOM-32 datasheet Table 17 quotes 19.5 dBm typ for 802.11b, 18.0 dBm for HT20 MCS0 — the PHY backs the PA off by rate itself), logged truthfully as `WiFi TX power: 19.5 dBm (raw 78, module maximum)`, and the slider is gone.
- **Networks added after boot are found without a reboot** — the search list was built once when the web task started, so a second or third SSID saved through the Web UI stayed invisible to the search until the next reboot (the config globals are pointed at, so editing an *existing* slot did take effect — only the list length was frozen). The list is now rebuilt at every search-cycle boundary, and boot logs the credentials NVS actually returned: `WiFi credentials from NVS: [0]=Home/pw set [1]=Cafe/pw set [2]=(no SSID)/pw empty …`.
- **Wi-Fi setup is now the phone flow: scan, tap, type the password once** — the Network panel no longer has ten SSID/password boxes labelled Primary and Fallback 1–4; it has **Scan Networks**, a signal-strength list of what the radio can hear, and **Forget**. `GET/POST /api/wifi/scan` serve a static 30-entry scan cache (no per-request heap, AGENTS §14) that is refreshed on request and after a failed search cycle; `POST /api/wifi/join` and `POST /api/wifi/forget` write the same five `WIFI_SSID`/`WIFI_PWD`…`WIFI_S4`/`WIFI_P4` slots the firmware has always used, so nothing about storage or the Android consumer changed. A tap on a stored network reconnects with its stored passphrase instead of blanking it, and a fifth network when all slots are full returns `status:"full"` and makes the user name which one may go — credentials are never silently displaced. Hidden networks keep an explicit add-by-name row, since a scan cannot see them — and that SSID box now carries an explicit `type="text"`, because an `<input>` with no `type` attribute never matches the `input[type="text"]` rule and rendered unstyled next to the styled password box.
- **The network that worked is tried first** — when a join succeeds, that SSID/password pair is rotated to slot 0 in RAM and NVS (`WiFi: <ssid> is now the first network tried`), and a lost link restarts at slot 0. “Primary” is therefore whatever this vehicle can actually reach, decided by the device instead of by whoever filled the form last. Joins are a handful per day, so the rewrite stays inside the NVS wear rule (AGENTS §12), and pairs that already match are not rewritten.
- **Longer, better-targeted connect attempts** — a per-network window of `WIFI_ATTEMPT_SECONDS` (default 12 s, was a hard 5 s) now covers the driver scan, association, handshake *and* DHCP, which is what a cold scan plus a slow lease actually needs; the attempt is still abandoned early when the driver reports a definite reason. The channel from the last scan of that SSID (if younger than 30 s) is passed to `WiFi.begin()`, so the driver starts on the right channel instead of sweeping 1–13 first — roughly 1 s instead of ~4 s for a join. When a whole cycle fails, one log line names what the radio could see (`WiFi scan (12s ago): D-Link627F3B -48 dBm ch6 …`), which settles “is it out of range or is it the password?” without a repro.

**OTA and NVS robustness**
- **One NVS session, one lock (#32)** — an RAII `NvsSession` holds `prefsMux` across the whole `begin()/end()` pair at all 13 call sites; every failed write is counted and reported (`Config save: 3 parameter(s) failed to reach NVS … they will revert on reboot`), the ambient-calibration endpoints answer `saved-ram-only`, and a boot that cannot read NVS no longer seeds factory defaults over an unreadable namespace.
- **That write reporting was itself inverted, and is now right** — `Preferences::put*()` returns the number of bytes written (`0` on failure, `1` for numerics, `strlen(value)` for a string), not an `esp_err_t`, and the #32 helper compared that count against `ESP_OK`. A genuine failure (0) therefore read as success — the silent loss #32 was raised to stop — while every successful write logged a bogus failure whose "error name" was the value's length (`NVS: write of WIFI_S1 failed (0xc)` for a 12-character SSID). The helper now takes the byte count (with a separate overload for the `bool`-returning `clear()`/`remove()`, plus an empty-string-aware variant so clearing an SSID is not a false alarm), failures log once with the key name, and a save answers `nvsErrors: N` / `nvsFailedKeys` / `nvsAvailable` so the Web UI names the keys that did not stick instead of showing "Configuration saved successfully".
- **The factory-default seed no longer erases the stored SSIDs** — `FACTORY_DEFAULT_JSON` carried all five SSIDs as empty strings, so any boot that found `CFG_VER < 5` cleared every saved network name in RAM and NVS (passwords were already left out). The `CFG_VER` stamp was written unchecked, so one lost stamp repeated that wipe on every boot: credentials had to be retyped each session and looked unsaved. WiFi credentials are now excluded from the seed — matching a Factory Reset, which deliberately puts them back — and the stamp is checked and logged when it cannot be stored.
- **The NVS budget is visible in the Web UI, not only the log** — boot logs `NVS 'nvs' N bytes: N entries used, N available, N free, N total, N namespace(s)` (and again after a save that failed), plus `WiFi credentials from NVS: [0]=Home/pw set [1]=(no SSID) …`, and `/api/health` now carries `nvs_bytes` / `nvs_entries_used` / `nvs_entries_available` / `nvs_entries_total` so the Performance panel → Memory & Storage shows a permanent `Settings storage (NVS 80 KB): 420 of 2520 entries used, 1974 available` line (amber under 15 % available, red under 3 %). The numbers are entry slots rather than bytes because slots are what runs out, and the read is throttled to one page-walk per 5 s. ESP-IDF tidies NVS inside its own write path and arduino-esp32 3.3.x exposes no public compaction call, so this is deliberately observation only.
- **The real cause of the lost WiFi saves: the NVS partition was full** — with `nvs` at 20 KB (5 pages, one always reserved for garbage collection) a unit reported `NVS has 1 free entries`, and the two writes that could not fit were `WIFI_SSID`/`WIFI_PWD` — **slot 0, the primary network**, which is why it looked hardcoded while a secondary slot saved fine when room happened to be free. Two changes, one layout and one behavioural:
  - **`nvs` is now 80 KB** (20 pages) in `partitions.csv`, taken from LittleFS (320 → 256 KB), which has held nothing since fonts moved into the binary. Offsets shift, so this only reaches a board through a wired re-flash — and OTA-updated boards keep the old table until they get one.
  - **A save now writes only what changed.** Every `CFG_*` parameter, the WiFi passwords and the fuel ramp are read back first and skipped when the value already matches. The old behaviour rewrote all ~200 parameters on every autosave; re-storing a string at a different length strands its old 16-byte items, and those can only be erased once a whole page holds nothing live — with ~200 keys spread over four usable pages that never happened, so stale items grew until the partition was full. This is also the wear rule (AGENTS §12) applied to the save path rather than only the odometer.
- **The BOOT-hold recovery gesture could be fired by a PC, and it took the odometer with it** — the gesture watched GPIO0 for 8 continuous low seconds, but GPIO0 is driven by the USB-serial adapter's DTR line through the auto-reset transistor, so any host holding DTR asserted (a serial monitor left open across a boot is enough) wiped the configuration eight seconds after every power-on, silently, with nobody near the board. Two resets were reproduced on the bench before the cause was found, and the same capture with DTR de-asserted boots clean. It is now a two-stage gesture — hold 4 s, **release**, hold 4 s again, all inside the first 30 s — which a stuck-low line can never satisfy, and every abort path logs its reason (`Factory reset aborted: BOOT was never released …`). A factory reset also no longer clears the `dashboard` namespace: recovering from a forgotten config PIN is no reason to erase the vehicle's lifetime mileage, and unlike the settings, an odometer cannot be re-entered from memory. Applies to all three reset paths (BOOT gesture, serial `RESET`, `POST /api/reset`).
- **Progress bar cannot divide by zero (#38)** — a zero or negative total in `updateOTAProgress()` raised an integer-divide panic in the ArduinoOTA callback mid-update.
- **Boot provenance complete (#42)** — every `esp_reset_reason_t` maps to a label, unmapped ones print their numeric id instead of `UNKNOWN`.
- **`/api/time` range checked (#40)** — only 2020–2100 epochs are accepted, so a typo'd timestamp cannot poison the NTP baseline.

**Size and tooling** — `RAM 22.2 %` (72,884 B of 327,680 B), `Flash 90.3 %` (1,715,375 B of the 1,900,544 B OTA slot, ~185 KB headroom). New checks: `verify_config_ranges.py`, `verify_ubx_parser.py`, `verify_fuel_ohms.py`, `verify_dst_rules.py`, and `make_webui_limits.py` for the WebUI `FIRMWARE_LIMITS` table.

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
