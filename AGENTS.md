# AGENTS.md — Dashboard++ for ESP32

Rules for ALL agents (AI or human) working in this repository. These rules override agent defaults.

## 1. Decision Policy: Ask, Don't Guess
When you are unsure about anything — ambiguous requirements, conflicting options, risky assumptions, missing context — **stop and ask the user to decide**. Do not overthink or silently guess. The only exception is when the user explicitly instructs you to work alone / make your own calls for that task.

## 2. Commits
- **Every change must be committed on `main`.** No feature branches, no PR flow — work and commit directly on `main`.
- Commit after each logical unit of work so the history is incremental.

## 3. Releasing ESP Code
- When the user says **"release the ESP code"**, bump the version by **+0.0.1** (patch bump, e.g. `1.3.2` → `1.3.3`) unless the user explicitly states the exact target version.
- The firmware version string is `FW_VERSION` in `src/config.cpp` — **one compiled-in constant**. It is not stored in NVS and nothing fetched from the network can change it. (`VERSION_OVERRIDE` is a separate, user-set label for display and pull testing — not the build identity. The old `OTA_CURRENT_VERSION` / NVS `OTA_VER` pair is gone.)
- Commit the bump on `main` (per rule 2).
- **No signing, anywhere.** Updates are not signed, the firmware carries no public key, and the build flags that used to control verification (`-DOTA_REQUIRE_SIG`, `-DOTA_ALLOW_UNSIGNED`) are deleted. The maintainer asked for signatures out of every update path on 2026-10-30; **do not reintroduce signature verification, `.sig` release assets, or those flags without asking first** (rule 1). `scripts/ota_sign.py` stays in the repo as an out-of-band tool only — nothing on a device looks at its output.

## 4. README on Important Releases
- If a version bump includes **important changes** that must be reflected in the documentation, update `README.md` too (changelog entry and any affected reference sections).
- Push to GitHub as part of that release flow.

## 5. Flashing the ESP32
- Default flashing method: **PlatformIO over USB serial on COM7** (e.g. `pio run -t upload --upload-port COM7`), after `pio run` builds.
- The upload layout is repaired by `scripts/fix_upload_layout.py` (a `post:` extra script in `platformio.ini`): it moves the otadata write to this table's real offset (`0x1D000`) and removes PlatformIO's fixed `0xe000` write, which sits **inside `nvs`** here and was destroying two settings pages per flash. Keep that script: without it a wired flash may not boot what it just wrote, and it silently loses NVS settings.
- If esptool reports `Failed to start stub flasher. There was no response.`, retry once (at 115200 baud if needed); if it keeps failing, hold **BOOT** while tapping **RESET** to force download mode. Confirm the port with `esptool --port COM7 --baud 115200 read_mac`.
- If COM7 is unreachable / not detected, **stop and ask the user how to continue** — do not auto-fallback to a guessed port or method.
- When asking, list the available flashing methods:
  1. USB serial (PlatformIO `--upload-port <other COM port>`)
  2. WiFi OTA via PlatformIO: `pio run -t upload --upload-port <ESP_IP>` (device must be on the `Dashboard_Config` SoftAP or a known LAN IP)
  3. Web Portal OTA upload (browser → `http://192.168.4.1` → System Actions → upload `firmware.bin`)
  4. Android app firmware update (the app lists the published GitHub releases, downloads
     the chosen one to phone storage, verifies it, then pushes it to the unit over
     `POST /api/ota`) — the intended end-user path. There is **no** cloud pull on the
     unit: it never fetches firmware by itself, and there is no self-update code left.

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
- **Always push when committing** — every commit on `main` is pushed to GitHub right after it is made.

## 24. Android versionCode
- Bump `versionCode` in `android/app/build.gradle.kts` on every shared/test APK (per the app implementation plan).

## 25. Leave No Broken Build
- Every task ends green: `pio run` must pass (plus the APK build when `android/` was touched).

## 26. Plan-First for Big Features
- Non-trivial features: first write an implementation plan into `Implementation plans/` and get the user's sign-off before coding starts.

## 27. Release Assets — Always Ship firmware.bin and the APK
- Every GitHub release must attach **both** artifacts: the ESP firmware binary (`.pio/build/esp32dev/firmware.bin`) and the Android app debug APK (`android/app/build/outputs/apk/debug/app-debug.apk`). A release missing either is incomplete.
- **Do not attach a `.sig` asset** — no device can verify one (rule 3). Ship exactly **one** `.bin` asset per release: the pull path takes the first `.bin` it sees.

## 28. GitHub Release Title Format
- Release titles are **just `Vx.x.x`** (e.g. `V1.3.3`) — no extra text, matching how they were titled before.

## 29. Graphify Output — Code-Only Sync
- When the **code** of the project changes, update the graph with the **AST path only**: `graphify update .` (local AST, no API key, no LLM tokens). It refreshes `graphify-out/` (`graph.json`, `graph.html`, `GRAPH_REPORT.md`) for the code in the repo. Run it **once, at the end of the code change** — not after every file edit, and not at all on doc-only work.
- **Never run `graphify . --update`** as part of normal work: it queues doc/paper/image files for LLM semantic extraction, needs an API key (`GEMINI_API_KEY` / `ANTHROPIC_API_KEY` / …), and aborts without writing anything when no key is set. Semantic extraction and community naming (`graphify label`) only when the user explicitly asks for it.
- Doc / non-code file changes (`AGENTS.md`, `README.md`, images, plans, etc.) do **not** trigger any graph update.
- `graphify-out/` and `graphify-watch.bat` are gitignored local-only — never commit them. For live updates while coding, run `graphify-watch.bat` (`graphify watch .`, also AST-only, no LLM).

## 30. PCB Work — KiCad's DRC Is the Referee
- In `hardware/`, the **only** clearance/courtyard/edge/text verdict that counts is KiCad's own:
  `kicad-cli pcb drc --severity-all -o drc-report.txt hardware/dashboard-plusplus-carrier.kicad_pcb`
  (`scripts/make_pcb.py` runs it at the end; target = exit 0, `Found 0 DRC violations`,
  `unconnected_items` allowed while nothing is routed).
- **Do not build or "fix" a second geometry checker** to predict DRC results. The bounding-box
  notes printed by `make_pcb.py` (`OUTSIDE BOARD` / `OVERLAP` / `SHORT` / `HOLES TOO CLOSE`) are
  hints to aim at, nothing more — never spend time making them agree with KiCad
  (circles/arc courtyards, mirrored text and back-layer swaps make an exact clone expensive).
- Read the DRC report (it names the exact pad/edge and coordinates), fix that, re-run. Iterate
  on KiCad's output, not on a local model of it.

## Project Quick Reference
- **Firmware:** ESP32 WROOM-32, PlatformIO project in repo root (`platformio.ini`, env `esp32dev`), Arduino framework, LovyanGFX 4.0" ILI9488 480×320 display.
- **Build:** `pio run` (pre-scripts gzip the web UI and compile VLW fonts — run `pio run`, not a plain compile).
- **Version:** `FW_VERSION` in `src/config.cpp` (see rule 3).
- **Docs:** `README.md` is the full technical reference (architecture, pinout, REST API, NVS params, changelog).
- **Plans:** `Implementation plans/` — local-only (never committed/pushed; see rule 6).
- **Android app:** `android/` — companion app (Kotlin/Compose, Gradle). Always finish with a debug APK build (rule 8).
- **Config backups:** NVS backup/restore handled via Web UI; reference template in code.
- **Demo mode:** Web UI → System & Modes → enable; synth telemetry for bench testing.
- **Graphify:** `graphify-out/` knowledge graph — local-only, code-only AST updates (`graphify update .`, once per code change; never `graphify . --update` in normal work — rule 29); `graphify-watch.bat` runs the live AST watcher.
- **Hardware:** `hardware/` — 85×55 mm carrier PCB generated by `hardware/scripts/make_pcb.py` (board + `footprints/dpp.pretty` + DRC report in one run). Board-side reference: `hardware/PINOUT.md`. Rules referee = KiCad DRC (rule 30).
