# Fix Plan — 5 items

Saved for review. All changes are compile-time only; no new dependencies.

## 1. `cpuUsagePct` is a constant (fake CPU usage)
**Location:** `src/main.cpp` (computation), `src/sensors.cpp:657` (declaration, unchanged), `src/web.cpp` `/api/perf` + telemetry log (consumers, unchanged)

**Root cause:** The metric samples `rsr ccount` over 1s. `ccount` ticks at the XTAL crystal
frequency (fixed 40/80 MHz) regardless of CPU load, so the value is essentially constant
(~100% on a 40 MHz crystal board). The comment "APB_CLK (80 MHz)" is wrong.

**Fix (idle-probe method):**
- Add two probe tasks (one per core, priority 1, 2 KB stack), created in `setup()`:
  ```cpp
  static volatile uint32_t cpuProbeTicks[2];
  static void cpuProbeTask(void *pv) {
    for (;;) { cpuProbeTicks[(int)pv]++; vTaskDelay(1); }
  }
  ```
  Priority 1 = above the idle task (priority 0, which the probe effectively replaces), below
  all work tasks (sensorTask prio 2 core 1, gpsTask prio 2 core 0, webServerTask prio 1,
  loopTask prio 1). Each probe ticks once per 1 ms slot in which its core was not doing
  real work.
- Replace the ccount block with: over the existing 1 s window, per-core busy =
  `1 - probeTicks / elapsedMs`; `cpuUsagePct` = average of both cores (clamped 0-100).
- Delete the `rsr ccount` asm and the wrong comment.

**Expected result:** ~1-3% baseline when idle (probe overhead floor), rises under load
(e.g. OTA upload, weather fetch). `cpuUsagePct` is only consumed by the telemetry log line
and `/api/perf` — no display impact.

**Trade-off accepted:** probe tasks cost ~4 KB stack RAM and ~1-3% baseline reading.

## 2. Fuel touch table breaks if `FUEL_TOUCH_POINTS > 8`
**Location:** `src/config.cpp` — `touchTable` definition (line 234), `processConfig()`
touchTable section, `seedNVSWithFactoryDefaults()`

**Root cause:** `int touchTable[20] = {950, 840, 750, 670, 600, 530, 460, 400};` — indices
8-19 are zero-initialized. If `FUEL_TOUCH_POINTS` is set above 8 without all `TCH_0..19` keys
present in NVS, the table becomes `[...400, 0, 0, ...]` — interpolation in
`processFuelSensor()` caps the gauge (an empty tank reads ~8 L on a 12-point scale instead
of 0), and `demoAdcForFuelLiters()` produces wrong demo values.

**Fix (3 parts, all in `config.cpp`):**
- **a.** Extend the C default table to all 20 entries, continuing the same smooth
  descending curve:
  `950, 840, 750, 670, 600, 530, 460, 400, 350, 310, 275, 245, 220, 195, 170, 145, 120, 95, 70, 45`
  (values are factory defaults — a user with more points will re-tune via upload anyway).
- **b.** In `seedNVSWithFactoryDefaults()`, after `processConfig(2, &doc)`, explicitly write
  `TCH_0..19` to NVS. After a factory reset every key exists — raising `FUEL_TOUCH_POINTS`
  later just works.
- **c.** In `processConfig()` mode 2 (JSON upload), write `TCH_i` for `i` from `written` to
  `FUEL_TOUCH_POINTS-1` using the previous in-RAM `touchTable[i]` value — a short upload
  (or an upload with no `touchTable` array at all) can no longer leave NVS holes.
  Mode-0 load is already safe once (a) exists, since `pref.getInt(key, touchTable[i])`
  falls back to the extended default table.

**Result:** no zero entries, no gauge cap, no silent behavior change; old devices self-heal
on next boot via (a).

## 3. Weather fetch race condition
**Location:** `src/web.cpp` — `weatherFetchTask()`, `startWeatherFetch()`, hung-fetch guard
in `webServerTask` loop, flag declarations

**Root cause:** `weatherTaskRunning` / `weatherTaskHandle` are written by both
`webServerTask` and the fetch task with no synchronization. If the fetch task's cleanup
(`flag=false; handle=NULL`) interleaves with `startWeatherFetch()`, a new fetch starts while
the old cleanup wipes the new handle → concurrent fetches + the hung-fetch guard can lose
track of a hung task (handle wiped → guard clears the flag while the task is still hanging
→ repeated spawns).

**Fix:**
- Add `static SemaphoreHandle_t weatherFetchMutex` (created in `webServerTask` startup
  alongside `otaStatusMutex`).
- `startWeatherFetch()`: take mutex → check `weatherTaskRunning` → set flag + create task →
  give mutex.
- `weatherFetchTask` cleanup: take mutex → clear flag/handle **only if
  `weatherTaskHandle == xTaskGetCurrentTaskHandle()`** (a stale task can never clear a flag
  it doesn't own) → give mutex.
- Hung-fetch guard: wrap the existing timeout block (delete + flag clear) in take/give of
  the same mutex, so delete+clear is atomic vs. new starts.
- Mark `weatherTaskRunning` `volatile` (it's also read outside the mutex).

**Result:** exactly one fetch can ever be in flight; hung tasks are always findable by the
guard.

## 4. Light sensor double `analogRead` (minor note → comment)
**Location:** `src/sensors.cpp` `processLightSensor()`

**Verified:** the first read is a deliberate settling discard, the second is the real
sample — intentional, not a bug. **Fix:** add a comment so nobody "fixes" it away. No
behavioral change.

## 5. OTA file-select broken on mobile + no re-select
**Location:** `src/webui.html` — `doOta()`, `otaBtn`

**Root cause:** (a) The file picker creates a detached `<input type="file">` via
`createElement` and calls `.click()` without it being in the DOM — mobile browsers (iOS
Safari, Android Chrome) won't open the picker. (b) Once a file is picked, the button
becomes "Upload X.bin" with no way to pick a different file ("Retry" after failure only
re-uploads the same file).

**Fix (follows the existing `importFile` pattern, which is proven on mobile):**
- Add `<input type="file" id="otaFile" accept=".bin" style="display:none"
  onchange="otaFileChosen(this)">` in the OTA section.
- `otaFileChosen(el)`: store `el.files[0]` into `_otaFile`, reset `el.value = ''` (so the
  same file can be re-picked), set button label to `Upload <name>`.
- Rewrite `doOta()`: no file selected → `document.getElementById('otaFile').click()`
  (in-DOM click works on mobile); file selected → existing confirm/upload flow unchanged.
- Re-select path: small "Change file" secondary button (hidden until a file is selected)
  that clears `_otaFile`, resets the label to "Firmware OTA", and reopens the picker.

**Verification:** desktop picker still works (regression), plus the in-DOM input pattern is
the same one `importFile` uses successfully on mobile.

---

## Files touched
| File | Changes |
|---|---|
| `src/main.cpp` | probe tasks in `setup()`, replace ccount block |
| `src/config.cpp` | 20-point default table, NVS seed loop, mode-2 fill-in |
| `src/web.cpp` | weather mutex + ownership check + guard wrapping |
| `src/sensors.cpp` | light-sensor comment only |
| `src/webui.html` | `#otaFile` input + `doOta()`/`otaFileChosen` rewrite |

## Verified OK — no changes needed
- Light-sensor double read (intentional settling) → item 4 comment only
- UBX checksum logic, NMEA `feedGpsLine` checksum, `demoGpsSentence` field order
- Hall speed / odometer / average speed math
- `units.h` all conversion factors
- `gfx.cpp` `loadVLWFont` path-index checks and fallback
- WiFi STA retry state machine (`staInFlight/staDone/stataConnected`)
- `seedNVSWithFactoryDefaults` `JsonDocument doc;` (valid — ArduinoJson v7.4.3 vendored)
- `doOtaPull()` auto-pull path and shared `Update.begin/write/end` flash mechanism
- `confirmDialog` usage, compass calibration/heading math

## Verification plan
1. `pio` build (no new deps; all changes compile-time).
2. **cpu usage:** check `[ESP] cpu=...` log — low % when idle, rises during an OTA upload.
3. **Fuel table:** factory-reset → upload config with `FUEL_TOUCH_POINTS=12` and no
   `touchTable` → check NVS keys `TCH_8..11` exist and gauge reads 0 when empty.
4. **Weather race:** code-level (race window is tiny, not reproducible by hand); build +
   review.
5. **OTA:** desktop browser regression (picker + upload + Done/retry), change-file button.

---

## Status — implemented

1. `src/main.cpp` — `cpuProbeTask` (one per core, prio 1, 2 KB, pinned) added in
   `setup()`; ccount block replaced with probe-window computation (`cpuUsagePct` =
   average busy of both cores, clamped 0-100). `rsr ccount` asm deleted.
2. `src/config.cpp` — `touchTable` extended to 20 entries (350…45 continuation curve);
   `seedNVSWithFactoryDefaults()` writes `TCH_0..19`; `processConfig()` mode 2 writes
   `TCH_i` for `i = written..FUEL_TOUCH_POINTS-1` (covers short uploads and uploads with
   no `touchTable` array).
3. `src/web.cpp` — `weatherFetchMutex` (created in `webServerTask` next to
   `otaStatusMutex`); `startWeatherFetch()` and hung-fetch guard take/give the mutex;
   fetch-task cleanup clears flag/handle only when it owns the handle
   (`weatherTaskHandle == xTaskGetCurrentTaskHandle()`); `weatherTaskRunning` marked
   `volatile`.
4. `src/sensors.cpp` — settling comment added to `processLightSensor()` (no behavior
   change).
5. `src/webui.html` — `#otaFile` persistent in-DOM input + `otaFileChosen(el)`/
   `otaChangeFile()` handlers + hidden "Change file" button; `doOta()` rewritten to
   click the in-DOM input. `src/webui_html_gz.h` regenerated (26501 bytes, now
   `uint32_t` — the old generated header had a typo'd `uint3_t`).

**Build:** `pio run` (release) — SUCCESS, zero warnings. RAM 22.0% (72232/327680 B),
Flash 78.7% (1495753/1900544 B).

**Remaining (needs hardware):** items 1-3 of the verification plan
(`[ESP] cpu=` line, fuel-table NVS check, browser test).
