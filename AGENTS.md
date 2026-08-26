# AGENTS.md — Dashboard++ for ESP32

Rules for ALL agents (AI or human) working in this repository. These rules override agent defaults.

## 1. Decision Policy: Ask, Don't Guess
When you are unsure about anything — ambiguous requirements, conflicting options, risky assumptions, missing context — **stop and ask the user to decide**. Do not overthink or silently guess. The only exception is when the user explicitly instructs you to work alone / make your own calls for that task.

## 2. Commits
- **Every change must be committed on `main`.** No feature branches, no PR flow — work and commit directly on `main`.
- Commit after each logical unit of work so the history is incremental.

## 3. Releasing ESP Code
- When the user says **"release the ESP code"**, bump the version by **+0.0.1** (patch bump, e.g. `1.3.2` → `1.3.3`) unless the user explicitly states the exact target version.
- The firmware version string is `OTA_CURRENT_VERSION` in `src/config.cpp` — it appears in 3 places (initial value, `CFG_STR` default, and the backup JSON template). All three must be updated together.
- Commit the bump on `main` (per rule 2).

## 4. README on Important Releases
- If a version bump includes **important changes** that must be reflected in the documentation, update `README.md` too (changelog entry and any affected reference sections).
- Push to GitHub as part of that release flow.

## 5. Flashing the ESP32
- Default flashing method: **PlatformIO over USB serial on COM7** (e.g. `pio run -t upload --upload-port COM7`), after `pio run` builds.
- If COM7 is unreachable / not detected, **stop and ask the user how to continue** — do not auto-fallback to a guessed port or method.
- When asking, list the available flashing methods:
  1. USB serial (PlatformIO `--upload-port <other COM port>`)
  2. WiFi OTA via PlatformIO: `pio run -t upload --upload-port <ESP_IP>` (device must be on the `Dashboard_Config` SoftAP or a known LAN IP)
  3. Web Portal OTA upload (browser → `http://192.168.4.1` → System Actions → upload `firmware.bin`)
  4. Cloud OTA pull (device pulls `OTA_PULL_URL` firmware)

## 6. Implementation Plans — Never in Git
- All implementation plans must be written/exported into the `Implementation plans/` folder at the repo root.
- That folder is **private/local-only**: never commit it and never push it to GitHub. It is in `.gitignore`; keep it there and do not force-add its contents.

## 7. Unknowns: Search, Don't Hallucinate
- If you don't know something, **do a web search** — you have the tools for it. Verify facts (APIs, datasheets, register maps, library behavior) against sources instead of guessing or making things up.

## 8. Android App — Always Produce a Testable APK
- While working in `android/`, the work is **not done** until a **usable APK is built for testing**.
- Ship the **debug build** (per the distribution decision in the app implementation plan): from `android/`, run `gradlew.bat assembleDebug` → APK lands in `android/app/build/outputs/apk/debug/`.
- A code-only change that was not built into an APK is incomplete.

## 9. WebUI Changes — Always Rebuild the Firmware at the End
- The WebUI source is `src/webui.html`; it gets gzipped into the firmware at build time by the pre-script `scripts/gzip_webui.py`.
- Whenever the WebUI is touched, **end the process by building the ESP firmware** (`pio run`) so the new WebUI is compiled into the ESP firmware binary.
- WebUI work is incomplete until the firmware build succeeds with the new UI embedded.

## 10. Pins Are Sacred
- The README pinout matrix reflects the real board wiring. No GPIO reassignment or repurposing without user approval.

## 11. FreeRTOS Task Map
- Respect the core split: Core 0 = GNSS + network, Core 1 = vehicle sensors + display.
- Protect shared state with `g_stateMutex`; never block in ISRs.

## 12. NVS Wear Discipline
- Throttle flash writes: accumulate in RAM, write only on significant change (odometer pattern: write at full 1 km increments).
- Never write NVS in tight loops.

## 13. No Surprise Dependency Bumps
- Do not upgrade PlatformIO, the Arduino core, LovyanGFX, ArduinoJson, or TinyGPSPlus without asking first.

## 14. RAM Discipline
- ESP32 memory is tight (520 KB SRAM): no new per-frame or per-request heap allocations; use static / fixed-size buffers.
- Watch the build size — 4 MB flash with the custom `partitions.csv` layout.

## 15. Generated Files Are Off-Limits
- Never hand-edit generated files: `src/webui_html_gz.h` (from `scripts/gzip_webui.py`) or `include/*_vlw.h` (from `scripts/vlw_to_header.py`).
- Always regenerate via `pio run`.

## 16. English Only in Code
- All code comments, log strings, and UI text in English.

## 17. REST API Compatibility
- The WebUI and the Android app depend on the REST endpoints. Changing/renaming/removing an endpoint requires updating every consumer and the README endpoint table in the same change.

## 18. New NVS Config Param Checklist
- A new config param needs **all four in one commit**: default in `config.cpp`, backup-JSON template, WebUI control, README reference.

## 19. Self-Contained WebUI
- No external CDN / network assets in `webui.html` — the dashboard works offline.

## 20. Keep Demo Mode Working
- Simulation / demo mode is the primary no-hardware test path; UI/sensor changes must not break it.

## 21. No Secrets in the Repo
- Never commit credentials, WiFi passwords, or API keys.

## 22. Commit Message Style
- Imperative mood, matching existing history (e.g. `Fix hall speed spikes, add tunable filters, bump to 1.3.2`).
- Release commits include the version bump ("bump to X.Y.Z").

## 23. Push Policy
- **Push only on request** — no auto-push after commits. Push to GitHub when the user says so (important releases still push per rule 4).

## 24. Android versionCode
- Bump `versionCode` in `android/app/build.gradle.kts` on every shared/test APK (per the app implementation plan).

## 25. Leave No Broken Build
- Every task ends green: `pio run` must pass (plus the APK build when `android/` was touched).

## 26. Plan-First for Big Features
- Non-trivial features: first write an implementation plan into `Implementation plans/` and get the user's sign-off before coding starts.

## Project Quick Reference
- **Firmware:** ESP32 WROOM-32, PlatformIO project in repo root (`platformio.ini`, env `esp32dev`), Arduino framework, LovyanGFX 4.0" ILI9488 480×320 display.
- **Build:** `pio run` (pre-scripts gzip the web UI and compile VLW fonts — run `pio run`, not a plain compile).
- **Version:** `OTA_CURRENT_VERSION` in `src/config.cpp` (see rule 3).
- **Docs:** `README.md` is the full technical reference (architecture, pinout, REST API, NVS params, changelog).
- **Plans:** `Implementation plans/` — local-only (never committed/pushed; see rule 6).
- **Android app:** `android/` — companion app (Kotlin/Compose, Gradle). Always finish with a debug APK build (rule 8).
- **Config backups:** NVS backup/restore handled via Web UI; reference template in code.
- **Demo mode:** Web UI → System & Modes → enable; synth telemetry for bench testing.
