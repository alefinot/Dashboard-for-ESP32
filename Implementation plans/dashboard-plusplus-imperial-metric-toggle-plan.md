# Dashboard++ — Metric ⇄ Imperial Unit Toggle
## Implementation Plan for AI Coding Agent

> **Status: implemented (2025).** Web UI toggle under System & General → "Use Imperial Units";
> `collectConfig()` converts unit fields to metric before every POST; the firmware
> weather widget now converts temp/wind at draw time; fuel label is "G" (not "GAL").
> `src/webui_html_gz.h` is regenerated from `src/webui.html` via the pre-build hook.

**Target repo:** `alefinot/Dashboard-for-ESP32`
**Feature:** A single Web UI setting that switches the entire system — TFT dashboard and Web UI — between Metric and Imperial units, persisted in NVS.

---

## 0. How to use this document

This plan is written to be handed to an AI coding agent (Claude Code, Cursor, etc.) working directly against the repo checkout. It is architecture-accurate (based on the project's README/technical doc — the `processConfig()` NVS macro system, dirty-rendering pipeline, embedded `index_html` Web UI) but does **not** assume exact line numbers or internal symbol names, since those weren't available to verify in this session. **Section 3 is a mandatory first step**: the agent must grep the real source and confirm exact identifiers before writing any conversion code, then substitute the real names into the patterns given in Sections 5–8.

Do not skip Section 3. Guessing at call sites and shipping blind edits risks silently breaking the dirty-rendering cache or the NVS config round-trip.

---

## 1. Objective

Add one persisted setting — call it **`UNITS_IMPERIAL`** (bool, default `false`) — that:

- Is stored in NVS exactly like every other config parameter (round-trips through `/api/config` GET/POST automatically).
- Appears as a toggle in the embedded Web UI.
- When flipped, re-renders **every unit-bearing readout** on both the physical TFT dashboard and the Web UI in the chosen system, without requiring a reboot (consistent with how other live parameters already apply).
- Requires **zero changes** to sensor fusion math, calibration tables, or how values are stored in NVS/RAM.

---

## 2. Architectural principle: single canonical unit system internally

Every telemetry value in the firmware (Hall/GPS speed fusion, odometer accumulation, Steinhart-Hart coolant temp, fuel economy, wheel circumference) stays in metric — km/h, km, °C, L, km/L, mm — exactly as it is today. **Nothing about the math changes.**

A new conversion layer sits only at the two places a human actually reads a number:

1. The TFT rendering calls in `ui.cpp` (right before a value is handed to the digit/sprite renderer).
2. The Web UI's display/serialize functions in the embedded `index_html` JS (right before a value is painted into the DOM, and — for Phase 2 — right before an imperial-typed input is POSTed back).

This means: no dual-storage, no drift risk, no touching `sensors.cpp`'s physics, and Demo Mode gets the feature for free (see §12).

---

## 3. Pre-flight discovery checklist (do this first, before writing any code)

Open the actual checkout and locate the following. Note the exact file + line for each — you'll need them in Sections 5–9.

**In `src/dashboard.h`:**
- [ ] How existing boolean config parameters are declared (e.g. `ENABLE_DYNAMIC_CPU`, `ENABLE_ANTIALIASING`, `ENABLE_CPU_THROTTLE`). Confirm whether they're plain `extern bool` globals or members of a config struct.
- [ ] The struct/field holding live sensor state consumed by `ui.cpp` (the thing `speedKmh`, `odoKm`, coolant temp, etc. live on — likely something like `DashboardState` or `g_state`, protected by `g_stateMutex` per the README).

**In `src/config.cpp`:**
- [ ] The `processConfig()` macro system: find one existing bool entry end-to-end (its NVS load line, its NVS save line, its JSON-export line, its JSON-import line). This is the exact 4-point pattern `UNITS_IMPERIAL` must replicate.
- [ ] Confirm how a missing JSON key on import is handled (should fall back to compile-time default) — this determines old-backup compatibility (§11).

**In `src/ui.cpp`:**
- [ ] Every call site that renders: main speed, odometer, trip average speed, instantaneous fuel economy, average fuel economy, coolant temperature, and (if it exists) a numeric fuel-remaining-in-liters readout.
- [ ] Every place a unit-label string (`"KM/H"`, `"KM"`, `"L"`, `"KM/L"`, a `°C` glyph, etc.) is drawn — search for these literals directly.
- [ ] The dirty-rendering "last drawn value" cache variables for each of the above elements (needed for §6.3).
- [ ] Confirm whether unit-label text is drawn every frame or only once at layout build time — this changes how §6.3's invalidation must work.
- [ ] Confirm whether coolant temperature has an actual numeric digit readout, or is **bar/gauge-only** (the README only documents `COLOR_TEMP_NORM/WARN/CRIT` — gradient bar colors — with no `TEMP_INT_DIGITS`-style digit-boundary parameter, which suggests it may be a bar with no on-screen number). This materially changes the §6 approach for that one element.

**In `src/web.cpp` (embedded `index_html`):**
- [ ] The card/section markup pattern used by an existing settings group (e.g. "System & Modes", where Demo Mode's toggle lives).
- [ ] The JS function that serializes form fields into the `/api/config` POST payload, and the one that populates fields from the `/api/config` GET response (autosave fires 2s after input change, per the README — find that debounce function too).
- [ ] Whether existing booleans use `<input type="checkbox">` or a custom switch component, so the new control matches the existing visual idiom.
- [ ] The fields that will matter for Phase 2: `WHEEL_CIRCUMFERENCE_MM`, `ACCEL_START_SPEED`, `ACCEL_TARGET_SPEED`, `MAX_SPEED_DELTA_KMH`.

---

## 4. New NVS parameter

| Property | Value |
|---|---|
| Name | `UNITS_IMPERIAL` |
| Type | `bool` |
| Default | `false` (Metric) |
| Storage | NVS via `processConfig()`, identical pattern to other booleans |

Use `UNITS_IMPERIAL` as both the NVS key **and** the global variable name, matching the repo's existing convention where config keys and their in-memory globals share a name (e.g. `ENABLE_DYNAMIC_CPU`, `MANUAL_CPU_FREQ`).

---

## 5. New conversion utility module — `src/units.h`

Pure, header-only, no dependencies. This part is unit-agnostic to the rest of the codebase and can be written verbatim:

```cpp
// src/units.h
#pragma once

// ============================================================
//  Metric -> Imperial (forward)
// ============================================================
inline double kmhToMph(double kmh)      { return kmh * 0.621371; }
inline double kmToMi(double km)         { return km  * 0.621371; }
inline double cToF(double c)            { return c * 9.0 / 5.0 + 32.0; }
inline double litersToGal(double l)     { return l * 0.264172; }       // US gallon
inline double kmplToMpg(double kmpl)    { return kmpl * 2.352145833; } // US MPG
inline double mmToIn(double mm)         { return mm / 25.4; }

// ============================================================
//  Imperial -> Metric (inverse — needed for Phase 2 input fields)
// ============================================================
inline double mphToKmh(double mph)      { return mph / 0.621371; }
inline double miToKm(double mi)         { return mi  / 0.621371; }
inline double fToC(double f)            { return (f - 32.0) * 5.0 / 9.0; }
inline double galToLiters(double g)     { return g / 0.264172; }
inline double mpgToKmpl(double mpg)     { return mpg / 2.352145833; }
inline double inToMm(double in)         { return in * 25.4; }

// ============================================================
//  Unit label helpers — return the correct label for current mode
// ============================================================
inline const char* speedUnitLabel(bool imperial)    { return imperial ? "MPH"  : "KM/H"; }
inline const char* distanceUnitLabel(bool imperial) { return imperial ? "MI"   : "KM";   }
inline const char* tempUnitLabel(bool imperial)     { return imperial ? "\xB0""F" : "\xB0""C"; }
inline const char* volumeUnitLabel(bool imperial)   { return imperial ? "GAL"  : "L";    }
inline const char* economyUnitLabel(bool imperial)  { return imperial ? "MPG"  : "KM/L"; }

// ============================================================
//  Render-time convenience wrappers — one call site, no branching
//  scattered through ui.cpp
// ============================================================
inline double dispSpeed(double kmh, bool imp)    { return imp ? kmhToMph(kmh)   : kmh;  }
inline double dispDist(double km, bool imp)      { return imp ? kmToMi(km)      : km;   }
inline double dispTemp(double c, bool imp)       { return imp ? cToF(c)         : c;    }
inline double dispVolume(double l, bool imp)     { return imp ? litersToGal(l)  : l;    }
inline double dispEconomy(double kmpl, bool imp) { return imp ? kmplToMpg(kmpl) : kmpl; }
```

**Verify the `\xB0` (°) byte renders correctly against the project's VLW font glyph set** — if the existing Celsius label already draws a degree glyph successfully today, the same glyph should work unchanged for Fahrenheit (only the trailing letter differs).

Add `#include "units.h"` to `dashboard.h` (or directly in `ui.cpp`/`web.cpp` where used).

---

## 6. Firmware Display Layer (`ui.cpp`, possibly `gfx.cpp`)

### 6.1 Elements requiring conversion

| Telemetry | Canonical unit | Metric label | Imperial label | Conversion fn |
|---|---|---|---|---|
| Main speed (120px sprite) | km/h | KM/H | MPH | `dispSpeed` |
| Odometer | km | KM | MI | `dispDist` |
| Trip average speed | km/h | KM/H | MPH | `dispSpeed` |
| Instantaneous fuel economy | km/L | KM/L | MPG | `dispEconomy` |
| Trip average fuel economy | km/L | KM/L | MPG | `dispEconomy` |
| Coolant temperature | °C | °C | °F | `dispTemp` |
| Fuel remaining (only if a numeric liters readout exists — confirm in §3) | L | L | GAL | `dispVolume` |

**Explicitly NOT converted** (unit-agnostic or has no imperial equivalent):
- Battery voltage (Volts)
- Acceleration timer (seconds)
- Satellite count (dimensionless)
- Compass heading (degrees)
- Wheel circumference calibration input, acceleration thresholds, speed-delta threshold — these are Phase 2 (§9), not part of core telemetry rendering.

### 6.2 Refactor pattern

Apply this pattern at every call site identified in §3.3. This is illustrative — substitute the real function/variable names found during discovery:

```cpp
// BEFORE (illustrative — confirm exact call in ui.cpp)
drawSevenSegValue(spriteSpeed, currentState.speedKmh, cfg.SPEED_DIGITS, 0);
drawLabel(lblSpeedUnit, "KM/H");

// AFTER
double dSpeed = dispSpeed(currentState.speedKmh, UNITS_IMPERIAL);
drawSevenSegValue(spriteSpeed, dSpeed, cfg.SPEED_DIGITS, 0);
drawLabel(lblSpeedUnit, speedUnitLabel(UNITS_IMPERIAL));
```

The underlying `currentState.speedKmh` field, the Hall/GPS fusion, and the odometer accumulator are **never touched** — conversion happens in the single line right before the renderer call, nowhere else.

### 6.3 Dirty-rendering cache invalidation — important

The dirty-rendering pipeline only redraws an element when its tracked value changes frame-to-frame. If a user toggles `UNITS_IMPERIAL` while the underlying metric value (e.g. `speedKmh`) hasn't changed, a naive diff will see "no change" and **leave the stale value/label on screen**.

Fix: when the toggle changes (detected in the config-apply path, wherever `web.cpp`'s POST handler feeds back into the main render loop), force every affected element's "last drawn value" cache to an impossible sentinel (`NAN`, `-99999`, or whatever idiom the dirty-render system already uses for "force redraw") so the very next frame redraws unconditionally. Do this for both the numeric value **and** the unit-label text — if labels are drawn once at layout-build time rather than every frame (confirm in §3), you also need to explicitly call whatever `layoutLabels()`/`buildStaticText()` function originally drew them, on toggle.

### 6.4 Digit boundary / overflow safety

- Fahrenheit values run numerically higher than Celsius (`F = C×1.8+32`; e.g. 100°C → 212°F) — if a numeric temperature readout exists, confirm its digit-width config can hold 3 integer digits, or clamp/warn if not.
- Odometer in miles is always numerically smaller than in km — no overflow risk.
- MPG runs ~2.35× larger than the equivalent km/L — if `INST_INT_DIGITS`/`AVG_INT_DIGITS` are sized tightly for km/L magnitudes (e.g. 2 digits), consider whether Imperial mode needs +1 digit of headroom. This is user-configurable already, so treat as a UX note rather than a hard requirement.

---

## 7. Config Layer (`src/dashboard.h`, `src/config.cpp`)

- Add `UNITS_IMPERIAL` as a global bool, in the same style as `ENABLE_DYNAMIC_CPU` / `ENABLE_ANTIALIASING`.
- In `config.cpp`'s `processConfig()`, duplicate the exact 4-point macro pattern found in §3.2 for an existing boolean, with default `false`. This alone makes it round-trip through `/api/config` GET and POST — no REST endpoint code changes needed, since that's the entire point of the shared macro system.
- Append `"UNITS_IMPERIAL": false` to `dashboard_backup.json` as a documentation-complete reference default.

---

## 8. Web UI Toggle Control (`src/web.cpp` embedded `index_html`)

**Placement:** either the existing "System & Modes" section (where Demo Mode's toggle already lives) or a new "Units" collapsible section — recommend a new section for clarity and to leave room for future locale options (e.g. L/100km).

**Markup**, matching whichever boolean idiom is confirmed in §3.4 (checkbox shown here; adapt if the project uses a custom switch component):

```html
<div class="config-group" id="group-units">
  <div class="group-header" onclick="toggleGroup('group-units')">
    <span>Units</span>
  </div>
  <div class="group-body">
    <div class="field-row">
      <label for="UNITS_IMPERIAL">Measurement System</label>
      <label class="switch">
        <input type="checkbox" id="UNITS_IMPERIAL" data-key="UNITS_IMPERIAL" onchange="markDirty(this)">
        <span class="slider"></span>
      </label>
      <span id="unitsSystemLabel">Metric (km/h · °C · L)</span>
    </div>
  </div>
</div>
```

```js
document.getElementById('UNITS_IMPERIAL').addEventListener('change', function () {
  document.getElementById('unitsSystemLabel').textContent =
    this.checked ? 'Imperial (mph · °F · gal)' : 'Metric (km/h · °C · L)';
});
```

**Wiring:** locate the config-serialize function found in §3.4 and confirm the checkbox's `data-key="UNITS_IMPERIAL"` is picked up the same way existing booleans are (as `0`/`1` or `true`/`false`, matching whatever the JSON schema already expects). No new POST logic should be needed if this attribute convention is followed — this is the same autosave path everything else already uses.

---

## 9. Phase 2 (recommended, but separable): convert calibration input fields

The core telemetry conversion in §6–8 fully satisfies "switch the dashboard and Web UI reading system." This phase additionally converts the handful of Web UI **input fields** whose values are physical quantities, so a user working entirely in Imperial never has to think in mm or km/h:

| Field | Canonical unit | Imperial display unit |
|---|---|---|
| `WHEEL_CIRCUMFERENCE_MM` | mm | in |
| `ACCEL_START_SPEED` | km/h | mph |
| `ACCEL_TARGET_SPEED` | km/h | mph |
| `MAX_SPEED_DELTA_KMH` | km/h | mph |

**Rule:** the value POSTed to `/api/config` and stored in NVS stays canonical metric, always. Only the on-screen displayed number and its adjoining unit-label span change with the toggle. This avoids ever storing an imperial value and needing a "which unit was this saved in" flag.

```js
// Mirror of units.h — keep constants identical to avoid drift.
// Consider a code comment pointing back to src/units.h so the two never diverge.
const KMH_TO_MPH = 0.621371;
const MM_TO_IN   = 1 / 25.4;

function metricToDisplay(key, value, imperial) {
  if (!imperial) return value;
  switch (key) {
    case 'WHEEL_CIRCUMFERENCE_MM': return (value * MM_TO_IN).toFixed(2);
    case 'ACCEL_START_SPEED':
    case 'ACCEL_TARGET_SPEED':
    case 'MAX_SPEED_DELTA_KMH':    return (value * KMH_TO_MPH).toFixed(1);
    default: return value;
  }
}

function displayToMetric(key, value, imperial) {
  if (!imperial) return value;
  switch (key) {
    case 'WHEEL_CIRCUMFERENCE_MM': return value / MM_TO_IN;
    case 'ACCEL_START_SPEED':
    case 'ACCEL_TARGET_SPEED':
    case 'MAX_SPEED_DELTA_KMH':    return value / KMH_TO_MPH;
    default: return value;
  }
}
```

Wire `metricToDisplay()` into the field-populate function (after `/api/config` GET) and `displayToMetric()` into the field-serialize function (before `/api/config` POST) — gate this on a `data-unit="length"` / `data-unit="speed"` attribute added to each affected `<input>`, and update each field's adjoining unit-label span text on toggle, same as §8.

---

## 10. Live-apply vs. reboot

Per the documented behavior, `POST /api/config` "updates NVS parameters and applies changes" — implying most parameters (colors, digit boundaries, Demo Mode) already apply live. `UNITS_IMPERIAL` should follow the same path: confirm in `web.cpp` where the POST handler calls back into the render loop, and hook the §6.3 cache-invalidation there. If the codebase turns out to gate some parameters behind a reboot flag, add `UNITS_IMPERIAL` to whichever list applies live rather than the reboot-required set — a unit toggle has no reason to need a restart.

---

## 11. NVS Backup/Restore & OTA compatibility

- An old backup JSON lacking `UNITS_IMPERIAL` must load without error and fall back to the compile-time default (`false`/Metric) — this is the same behavior the project must already have for every parameter added since v1.0, so reuse that existing fallback mechanism verbatim; don't build a new one.
- `dashboard_backup.json`'s reference template gets the new key (§7) purely for documentation completeness.

---

## 12. Demo Mode / Simulation compatibility

Demo Mode's sine-wave speed generator (10–110 km/h per the README) stays in km/h internally and is unaffected by this change — **as long as its value flows through the same `ui.cpp` render call sites as live sensor data**, which it should already, since Demo Mode's whole purpose is bench-testing the same rendering path. Don't special-case Demo Mode's value in the new conversion code; if it needs a separate code path today, that's worth flagging as a pre-existing wrinkle, not something to work around.

---

## 13. Testing / verification checklist

- [ ] Fresh NVS flash boots with `UNITS_IMPERIAL = false` (Metric).
- [ ] Toggling Metric → Imperial in the Web UI updates the TFT dashboard (speed, odometer, avg speed, coolant temp, fuel economy, fuel volume if applicable) without a reboot, value **and** label together.
- [ ] Toggling back reverts correctly.
- [ ] Battery voltage, accel timer, satellite count, compass heading are unaffected by the toggle.
- [ ] Demo Mode respects the toggle.
- [ ] `/api/config` GET in both states shows the correct `UNITS_IMPERIAL` value.
- [ ] Importing an old backup JSON without the key defaults safely to Metric.
- [ ] Rebooting with Imperial saved boots directly into Imperial (persistence confirmed).
- [ ] If Phase 2 implemented: enter wheel circumference in inches while Imperial is active, save, switch to Metric, confirm the underlying mm value round-trips without drift across repeated conversions.
- [ ] No digit truncation/overflow in either system for realistic values, especially coolant °F and fuel economy MPG (§6.4).

---

## 14. File-by-file change summary

| File | Change |
|---|---|
| `src/units.h` (new) | Conversion functions + unit label helpers (§5) |
| `src/dashboard.h` | Add `UNITS_IMPERIAL` global bool; include `units.h` |
| `src/config.cpp` | Add `UNITS_IMPERIAL` to `processConfig()` macro list, default `false` |
| `src/ui.cpp` | Route every unit-bearing value through the matching `disp*()` wrapper; make unit labels dynamic; force cache invalidation on toggle |
| `src/gfx.cpp` | Touched only if digit-overflow handling (§6.4) needs a shared helper — otherwise unchanged |
| `src/web.cpp` (`index_html`) | New "Units" toggle control (§8); Phase 2 bidirectional field conversion (§9) |
| `dashboard_backup.json` | Add `"UNITS_IMPERIAL": false` reference key |

---

## 15. Open decisions to confirm

1. **US gallon vs. UK/Imperial gallon** — this plan defaults to US gallon (`0.264172 gal/L`) and US MPG, the conventional pairing with "MPH." Swap to UK gallon (`0.219969 gal/L`) and UK MPG (`2.824809`) by changing two constants in `units.h` if that's preferred.
2. **Scope for this pass** — §6–8 (core telemetry + toggle) fully satisfies the stated goal on their own; §9 (Phase 2 input-field conversion) is a nice-to-have that can be deferred to a follow-up change without blocking the main feature.
3. **Coolant temperature rendering** — confirm during §3 discovery whether it's a numeric digit readout or bar-only; this changes whether §6's digit-conversion pattern or a threshold-remap approach applies.
4. **Live-apply confirmation** — verify no existing parameter actually requires a reboot before assuming §10's live-apply behavior for `UNITS_IMPERIAL`.
