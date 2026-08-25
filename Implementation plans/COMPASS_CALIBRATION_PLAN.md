# Compass Precision Fix — Complete Implementation Plan

## Problem

The compass heading is "not very precise" and "sometimes gets a completely
wrong reading". Root causes in the current pipeline (`src/sensors.cpp`):

1. **No soft-iron (scale) correction.** The calibration only corrects the
   *offset* (hard-iron). Soft-iron interference (metal, magnets, motor
   wiring) squishes the sphere of readings into an ellipse → `atan2` warps →
   direction-dependent heading errors ("completely wrong readings").
2. **No outlier rejection.** Raw min/max over the capture window: a single
   spike (motor interference) corrupts the offset.
3. **min/max assumes a full rotation** — incomplete turn → asymmetric range
   → wrong offset, silently saved.

Reference method: https://github.com/italocjs/magnetometer_calibration
(outlier filter → offset → scale correction → visual check).

## What is implemented

### 1. On-device fixes (always active, in `src/sensors.cpp`)

**New calibration parameters** — soft-iron scale factors
`COMPASS_CAL_SCALE_X/Y/Z` (float, default 1.0), persisted to NVS as
`CMP_SCALE_X/Y/Z` via the config system (`CFG_FLT`).

**Improved `compassCalFinish()`** (runs when the 30 s capture elapses):
- Per-axis outlier trim: sort the captured samples per axis and discard the
  top/bottom 5 % before computing min/max (the 5 % is `COMPASS_CAL_TRIM_PCT`).
- Offsets `COMPASS_CAL_X/Y/Z = (trimmedMin + trimmedMax) / 2`.
- Spans `spanX/Y/Z = trimmedMax - trimmedMin`. The Z span is set by
  the mounting tilt (geometry), not soft iron, so `scale_z` is **not**
  recomputed (kept as-is; default 1.0). X/Y are equalized:
`avgXY = (spanX + spanY) / 2`, `scale_x = avgXY / spanX`,
`scale_y = avgXY / spanY` — normalizes the ellipse back to a circle.
  Safety clamp: scales outside [0.2, 5.0] reset to 1.0.
- Tilt-axis fit unchanged (rotation-circle plane normal, same planarity
  check), but now fitted on **scale-corrected** samples so the fitted axis
  matches the heading math.
- **Refuses to save** when the X/Y span is too small (user didn't rotate),
  reporting the reason instead of persisting garbage.
- Persists offsets + scales + tilt, reports them in `compassCalResult`.

**Heading math** (in `processCompassSensor()`):
- After offset subtraction, before tilt projection:
  `px = (x - offX) * scale_x` etc. Flat-mount and default-scale (1.0)
  degenerate to the old formula.
- Demo mode keeps producing a clean circle; heading math unchanged for it.

**Config plumbing:**
- `src/config.cpp`: `CFG_FLT` lines for `CMP_SCALE_X/Y/Z` (load/save/serialize)
  next to the existing `CMP_CAL_*` block.
- `src/dashboard.h`: externs for the scale variables.

### 2. Web UI — best-possible calibration (`src/web.cpp` + `src/webui.html`)

The webui (served from the ESP32, no PC needed) becomes the calibration
workbench:

**New API endpoints (`src/web.cpp`):**
- `GET /api/compass/raw` → `{"x":..,"y":..,"z":..}` current raw sample
  (browser polls it and accumulates samples in its own memory — no ESP32
  RAM cost).
- `POST /api/compass/cal-apply` → body
  `{"offX","offY","offZ","sX","sY","sZ","tX","tY","tZ"}` → range-validates
  (offsets within int16, scales 0.2…5.0, tilt a unit vector × 32767), then
  applies + persists to NVS. This is how offline-computed values land.
- `GET /api/compass/cal-status` → extended with the scale values.

**Webui calibration card:**
- Keeps the existing Start/Cancel buttons (they drive the ESP-side capture,
  which now computes trimmed offsets + scales + tilt and auto-saves — the
  always-on safety net).
- While capturing, the browser polls `/api/compass/raw` at 10 Hz and draws
  a live 3-projection scatter plot (XY / XZ / YZ) of the captured cloud on a
  canvas, so the user sees the data being collected.
- After the capture, the browser computes the *best-possible* values with
  the reference-repo method in JavaScript:
  1. per-axis outlier trim (5 %),
  2. offset = (min+max)/2,
  3. scale = avg_span / span,
  4. tilt axis from the rotation-circle fit (ported cross-product fit +
     planarity check),
  then shows:
  - the raw cloud (blue) vs corrected cloud (red) plot,
  - a **quality score**: field uniformity = (max|B| - min|B|) / mean|B| after
    correction (small = good),
  - the computed offset/scale/tilt values,
  - an **Apply** button → `POST /api/compass/cal-apply`.
- The ESP-side auto-save already persisted the on-device computation; the
  browser's Apply commits the (identical-or-better) browser computation,
  which uses more samples.

### 3. Files touched

| File | Change |
|---|---|
| `src/sensors.cpp` | scale vars, trimmed min/max + scale in `compassCalFinish`, scale applied in heading, refuse-save on low span |
| `src/dashboard.h` | externs `COMPASS_CAL_SCALE_X/Y/Z` |
| `src/config.cpp` | `CFG_FLT` lines for `CMP_SCALE_X/Y/Z` |
| `src/web.cpp` | `/api/compass/raw`, `/api/compass/cal-apply`, scale fields in `cal-status` |
| `src/webui.html` | calibration card: live plot, computed values + quality, Apply button |
| `src/webui_html_gz.h` | regenerated via `scripts/gzip_webui.py` |

### 4. Verification

- Build with PlatformIO (`platformio compile`).
- Demo mode: heading sweeps cleanly; demo calibration produces scale ≈ 1.
- Real hardware: do the 30 s circle calibration; confirm
  - full 360° sweep, no stuck/compressed regions,
  - quality score < ~10 %,
  - heading within a few degrees of GPS course while driving.
