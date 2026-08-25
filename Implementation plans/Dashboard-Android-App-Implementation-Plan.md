# Dashboard++ Companion — Android App Implementation Plan

**Target repo:** https://github.com/alefinot/Dashboard-for-ESP32
**Purpose of this document:** Hand this directly to an AI coding agent (or a human dev) as a build spec for a native Android app that (1) auto-discovers the Dashboard++ ESP32's IP address on whatever network it's connected to, and (2) presents its existing embedded Web UI inside a polished, HUD-themed native shell.

---

## 0. How to use this document

This is a build spec, not a tutorial. Sections 1–2 are **verified facts** pulled directly from the firmware repo — treat them as ground truth. Sections 3+ are the **implementation plan** built on top of those facts. Anywhere a REST response schema isn't fully confirmed (I could read the README's API table but not the raw `web.cpp` source), it's called out explicitly — verify against the live device before hard-coding parsing logic.

---

## 1. Project Summary

Build **"Dashboard++ Companion"**, a single-purpose Android app that:

1. On launch, automatically finds the IP address of the user's Dashboard++ ESP32 on the currently connected Wi-Fi network (no manual IP entry required in the common case).
2. Loads the ESP32's existing embedded Web UI (the full settings/config portal — REST-driven single-page app already running on the device) inside an in-app WebView, styled to feel like a native app rather than "a browser tab."
3. Wraps that WebView in native chrome — splash screen, discovery/connecting screen, connection-status bar, error/reconnect screen — all styled in Dashboard++'s existing dark-HUD visual identity.
4. Handles the real-world edge cases of a battery/ignition-powered embedded device: it sleeps, reboots, changes IP on DHCP renewal, and lives on two different network topologies (SoftAP vs. home Wi-Fi).

**Explicit architecture decision:** This is a **WebView-shell app**, not a native reimplementation of the settings UI. The Web UI is described in the README as already feature-complete (card-based config, live search, color pickers, OTA upload, backup/restore, live telemetry). Rebuilding all of that natively would be a much larger, riskier project and would drift out of sync with the firmware every time it changes. The native layer's job is discovery, connection health, and presentation chrome — not reimplementing settings screens. If a fully-native rebuild is wanted later, it's a separate v2 effort layered on the REST API in Section 2.3.

---

## 2. Ground-Truth Facts From the Firmware (verified from the repo)

### 2.1 Network modes

- **SoftAP mode:** ESP32 emits SSID `Dashboard_Config`, always reachable at the fixed IP **`192.168.4.1`**. No IP guessing needed here — if the phone is connected to this SSID, skip discovery entirely.
- **Station (STA) mode:** Device can store up to 4 fallback Wi-Fi profiles (`WIFI_SSID_1`–`WIFI_SSID_4`) and auto-joins one on boot. **IP address here is DHCP-assigned by the user's router and unknown in advance** — this is the case that needs real discovery.
- **mDNS: not currently implemented.** No hostname advertising exists in the firmware today. See Section 6.4 for a recommended firmware addition and Section 6.5 for the fallback that works with zero firmware changes.
- **Authentication: none currently.** Every REST endpoint below is open on the LAN. Don't build UI that implies auth exists; do build confirmation dialogs client-side for destructive actions (see Section 8.5).

### 2.2 Web UI (what the WebView will display)

Per the README, the embedded single-page portal includes: collapsible card-based config sections, a live search bar filtering all parameters, autosave (2s after input change), color pickers, sliders with scroll-wheel protection, XY offset controls, a real-time performance panel (FPS, CPU freq/temp, RAM, flash), an OTA firmware file-upload control, and NVS backup/restore (JSON export/import).

### 2.3 REST API (confirmed from README)

| Endpoint | Method | Purpose | Content-Type |
|---|---|---|---|
| `/` | GET | Serves the embedded HTML/JS Web UI | `text/html` |
| `/api/config` | GET | Full NVS config export | `application/json` |
| `/api/config` | POST | Update NVS params | `application/json` |
| `/api/time` | POST | Sync clock (`?epoch=...`) | `text/plain` |
| `/api/reboot` | POST | Graceful restart | `text/plain` |
| `/api/sleep` | POST | Immediate deep sleep | `text/plain` |
| `/api/reset` | POST | Factory reset (clears NVS) | `text/plain` |
| `/api/ota` | POST | Firmware `.bin` upload | `multipart/form-data` |
| `/api/serial` | GET | Streams 4KB serial ring buffer | `text/plain` |
| `/api/perf` | GET | Live telemetry (CPU, heap, FPS, Wi-Fi) | `application/json` |

**Note:** `/api/perf` is the best discovery-verification target (see 6.6) because it's lightweight, unique to this firmware, and returns JSON. Its exact field names weren't visible in the README — confirm them with `curl http://<device-ip>/api/perf` against a live unit before writing the parser, and use the actual response instead of guessing field names.

### 2.4 Confirmed HUD color palette (from `dashboard_backup.json`, the device's own config)

This is the actual palette the physical dashboard renders with — use it as the app's design-token source of truth, not an approximation:

| Token | Hex | Used for (on-device) |
|---|---|---|
| `COLOR_TEMP_NORM` | `#00FFFF` | Cyan — normal engine temp / primary HUD accent |
| `COLOR_TEMP_WARN` | `#FF8C00` | Orange — warning state |
| `COLOR_TEMP_CRIT` | `#FF0000` | Red — critical state |
| `COLOR_FUEL_NORM` | `#00FF00` | Green — normal fuel level |
| `COLOR_FUEL_WARN` | `#FFFF00` | Yellow — fuel warning |
| `COLOR_FUEL_CRIT` | `#FF0000` | Red — fuel critical |
| `GHOST_COLOR_STR` | `#474747` | Dark gray — inactive "888" ghost-digit background |
| `SPLASH_SIGNATURE` | — | `"by @ale.finot"` |
| `DASHBOARD_SIGNATURE` | — | `"<<<<<< Dashboard++ by @ale.finot >>>>>>"` |

Fonts on-device: **Conthrax SemiBold** (headers/labels) and a **DS-DIGIT** seven-segment font (numeric readouts), loaded as VLW files. These are display fonts baked for the TFT, not necessarily licensed as installable app fonts — see Section 9.2 for the practical substitution plan.

---

## 3. Tech Stack

- **Language:** Kotlin
- **UI:** Jetpack Compose (single-Activity, Compose Navigation)
- **Compose BOM:** `2026.08.00` (Compose 1.12) or the latest stable at build time — check `https://developer.android.com/develop/ui/compose/bom/bom-mapping` since this moves fast. Note: Compose 1.12 requires **compileSdk 37 / AGP 9.1.1+**; downgrade to an earlier BOM if the environment's AGP is older.
- **minSdk 26** (Android 8.0) — comfortably covers `NsdManager`, `MulticastLock`, and modern WebView behavior without excluding many real devices.
- **targetSdk / compileSdk:** latest stable at build time (37 per above, or whatever the environment's Android Studio/AGP supports — keep compileSdk and the Compose BOM in lockstep).
- **Async:** Kotlin Coroutines + Flow/StateFlow
- **HTTP (discovery/health checks only, not for the main content):** OkHttp with short, explicit timeouts
- **Persistence:** Jetpack DataStore (Preferences) for the cached last-known IP and user settings
- **Networking APIs:** `ConnectivityManager`, `WifiManager`, `NsdManager`

No backend, no cloud, no analytics — this is a 100% local-network utility app.

---

## 4. Project Structure

```
app/
├── src/main/
│   ├── AndroidManifest.xml
│   ├── kotlin/com/alefinot/dashboardpp/
│   │   ├── MainActivity.kt
│   │   ├── DashboardApp.kt                  // Application class
│   │   ├── discovery/
│   │   │   ├── DiscoveryManager.kt          // orchestrates the strategy below
│   │   │   ├── NsdDiscoverer.kt             // mDNS/NSD path
│   │   │   ├── SubnetScanner.kt             // brute-force fallback path
│   │   │   ├── DeviceVerifier.kt            // confirms a candidate IP is really Dashboard++
│   │   │   └── ConnectionCache.kt           // DataStore-backed last-known-good IP
│   │   ├── network/
│   │   │   └── NetworkMonitor.kt            // ConnectivityManager.NetworkCallback wrapper
│   │   ├── ui/
│   │   │   ├── theme/ (Color.kt, Type.kt, Theme.kt, Shape.kt)
│   │   │   ├── splash/SplashScreen.kt
│   │   │   ├── discovery/DiscoveryScreen.kt
│   │   │   ├── main/DashboardWebScreen.kt
│   │   │   ├── error/ConnectionLostScreen.kt
│   │   │   ├── settings/AppSettingsScreen.kt
│   │   │   └── components/ (HudButton, HudCard, ConnectionPill, ScanProgressRing, GlowText…)
│   │   ├── webview/
│   │   │   ├── DashboardWebView.kt          // AndroidView wrapper + WebViewClient/WebChromeClient
│   │   │   └── FileChooserContract.kt       // handles OTA <input type=file>
│   │   └── viewmodel/
│   │       ├── ConnectionViewModel.kt
│   │       └── UiState.kt
│   └── res/
│       ├── xml/network_security_config.xml
│       ├── values/strings.xml, colors.xml
│       └── drawable/ (app icon, splash logo, connection icons)
```

---

## 5. Network Discovery Engine (the core deliverable)

This is the part that actually satisfies "auto fetches the ESP32 IP address." Design it as an explicit state machine, not a single function — the UI needs to reflect what stage discovery is at.

### 5.1 State machine

```
Idle
 → CheckingCachedIp         (try last-known-good IP first, ~400ms timeout)
 → CheckingSoftAp           (if current SSID == "Dashboard_Config", try 192.168.4.1 directly)
 → Discovering(mode: NSD | SUBNET_SCAN, progress: Float)
 → Found(ip: String)        → verified → Connected
 → NotFound                 → ManualEntry
 → Error(reason)
```

### 5.2 Order of operations (fastest-first)

1. **Cached IP check** (near-instant): On every app foreground, first try the last IP that worked (stored in DataStore). Single HTTP GET to `/api/perf` with a ~500ms timeout. If it succeeds and `DeviceVerifier` confirms it (5.6), skip everything else — this makes the overwhelming majority of app opens instant, since DHCP leases are usually stable session-to-session.
2. **SoftAP short-circuit**: Read the currently associated SSID via `WifiManager`/`ConnectivityManager`. If it's `Dashboard_Config`, go straight to `192.168.4.1` — no scanning needed, this IP is fixed by the firmware.
3. **mDNS/NSD discovery** (if the firmware patch in 5.4 is applied): Launch in parallel with step 4; whichever resolves a verified candidate first wins and the other is cancelled.
4. **Subnet scan fallback** (works with zero firmware changes): See 5.5.
5. **Manual entry**: If everything times out (~8–10s total), fall back to a manual-IP screen, pre-filled with the last cached IP if any, and a "Rescan" button.

### 5.3 Cached IP persistence

```kotlin
// ConnectionCache.kt
class ConnectionCache(private val dataStore: DataStore<Preferences>) {
    private val KEY_LAST_IP = stringPreferencesKey("last_known_ip")

    val lastKnownIp: Flow<String?> = dataStore.data.map { it[KEY_LAST_IP] }

    suspend fun save(ip: String) {
        dataStore.edit { it[KEY_LAST_IP] = ip }
    }
}
```

### 5.4 Recommended firmware addition: mDNS (small, optional, high value)

Without this, every discovery on STA mode falls back to a subnet scan. A 4-line addition to `web.cpp`'s Wi-Fi init routine (after `WiFi.begin()` succeeds, in both STA and SoftAP branches) makes discovery near-instant and eliminates the scan entirely:

```cpp
#include <ESPmDNS.h>

// ... after WiFi connects successfully (STA) or SoftAP is up ...
if (MDNS.begin("dashboardpp")) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "device", "dashboardpp");
    Serial.println("mDNS responder started: http://dashboardpp.local");
} else {
    Serial.println("Error setting up mDNS responder");
}
```

This is genuinely optional — the app must work fully without it via the subnet scan — but it's a small, low-risk addition worth including in the same effort since it makes discovery dramatically faster and more reliable on home routers that occasionally throttle broadcast scanning.

**Android side (NsdDiscoverer.kt), if the above is present:**

```kotlin
class NsdDiscoverer(private val nsdManager: NsdManager) {
    fun discover(): Flow<NsdServiceInfo> = callbackFlow {
        val listener = object : NsdManager.DiscoveryListener {
            override fun onServiceFound(service: NsdServiceInfo) {
                if (service.serviceType == "_http._tcp.") {
                    nsdManager.resolveService(service, object : NsdManager.ResolveListener {
                        override fun onServiceResolved(resolved: NsdServiceInfo) {
                            // Filter by TXT record "device=dashboardpp" or hostname prefix
                            trySend(resolved)
                        }
                        override fun onResolveFailed(s: NsdServiceInfo, e: Int) { /* ignore, keep scanning */ }
                    })
                }
            }
            override fun onDiscoveryStarted(t: String) {}
            override fun onServiceLost(s: NsdServiceInfo) {}
            override fun onDiscoveryStopped(t: String) {}
            override fun onStartDiscoveryFailed(t: String, e: Int) { close() }
            override fun onStopDiscoveryFailed(t: String, e: Int) {}
        }
        nsdManager.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
        awaitClose { nsdManager.stopServiceDiscovery(listener) }
    }
}
```

Also acquire a `WifiManager.MulticastLock` before starting NSD discovery and release it when done — some OEM Wi-Fi stacks drop multicast packets (which mDNS depends on) without it:

```kotlin
val lock = wifiManager.createMulticastLock("dashboardpp-mdns").apply { setReferenceCounted(true); acquire() }
// ... discovery ...
lock.release()
```

### 5.5 Subnet scan fallback (zero firmware changes required)

1. Get the phone's actual link address and prefix length from `ConnectivityManager.getLinkProperties(activeNetwork)` — **don't hardcode a `/24`**; compute the real host range from the reported prefix so it works on routers using `/23`, `/25`, etc. If the computed range is implausibly large (prefix < 22), cap the scan or skip straight to manual entry rather than trying to probe thousands of hosts.
2. Fan out concurrent probes (concurrency ~32–48 via a `Semaphore`) using OkHttp with a **short** timeout (~250–350ms connect/read) hitting `http://<candidate-ip>/api/perf`.
3. First candidate that returns HTTP 200 with JSON matching the verifier in 5.6 wins; cancel the rest of the in-flight scope.
4. Publish scan progress (`hosts checked / total`) as a `StateFlow<Float>` so the Discovery screen can show a real progress ring, not an indeterminate spinner — a 254-host scan at this concurrency typically completes in 1–3 seconds on a healthy LAN, but users should see it's actively working.

```kotlin
class SubnetScanner(private val client: OkHttpClient) {
    suspend fun scan(baseIp: String, hostRange: IntRange, onProgress: (Float) -> Unit): String? =
        coroutineScope {
            val semaphore = Semaphore(40)
            val checked = AtomicInteger(0)
            val total = hostRange.count()
            val found = CompletableDeferred<String?>()

            val jobs = hostRange.map { host ->
                launch(Dispatchers.IO) {
                    semaphore.withPermit {
                        val ip = "$baseIp.$host"
                        if (probe(ip)) found.complete(ip)
                        onProgress(checked.incrementAndGet() / total.toFloat())
                    }
                }
            }
            val result = select {
                found.onAwait { it }
                launch { jobs.joinAll() }.onJoin { null }
            }
            jobs.forEach { it.cancel() }
            result
        }

    private fun probe(ip: String): Boolean = try {
        val req = Request.Builder().url("http://$ip/api/perf").build()
        client.newCall(req).execute().use { it.isSuccessful && DeviceVerifier.looksLikeDashboardPp(it.body?.string()) }
    } catch (e: Exception) { false }
}
```

### 5.6 Verifying a candidate is actually the Dashboard++ device

Don't just accept "port 80 responded" — other devices on a home LAN (routers, printers, smart TVs) serve HTTP too, and a false positive means the app confidently shows garbage in the WebView. Verify by one of:

- Checking that `/api/perf`'s JSON body parses and contains the expected telemetry keys (confirm exact key names against a live device first — not fully specified in available docs).
- As a stronger check, fetch `/` and confirm the HTML contains the known signature string `"Dashboard++"` or the splash signature text (`@ale.finot`) before treating the IP as valid. This is cheap (one extra request) and essentially eliminates false positives.

### 5.7 Reacting to network changes while the app is open

Register a `ConnectivityManager.NetworkCallback` for the Wi-Fi transport. On `onCapabilitiesChanged`/`onLost`, if the SSID changes or Wi-Fi drops, invalidate the current connection and re-run discovery automatically (with a brief debounce) rather than leaving the WebView stuck on a dead session. This matters specifically because this device is used in two different Wi-Fi contexts (SoftAP for config, home Wi-Fi for normal use), and users will genuinely switch between them mid-session.

---

## 6. Permissions & Manifest

```xml
<uses-permission android:name="android.permission.INTERNET"/>
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
<uses-permission android:name="android.permission.CHANGE_WIFI_MULTICAST_STATE"/>
<!-- Needed on Android 12 (API 31) and below to read the current SSID / for some NSD paths -->
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"
    android:maxSdkVersion="32"/>
<!-- Android 13+ replacement for Wi-Fi-related access, doesn't require location -->
<uses-permission android:name="android.permission.NEARBY_WIFI_DEVICES"
    android:usesPermissionFlags="neverForLocation"/>
```

Request `ACCESS_FINE_LOCATION` / `NEARBY_WIFI_DEVICES` at runtime with a clear rationale dialog ("used only to detect which Wi-Fi network you're on, so the app can find your dashboard automatically — never sent anywhere"), since users will otherwise be confused why a local utility app wants location access.

### Cleartext traffic (critical, will silently break otherwise)

The ESP32 serves plain HTTP, no TLS. Android blocks cleartext by default for apps targeting API 28+, and the WebView will fail with `net::ERR_CLEARTEXT_NOT_PERMITTED` if this isn't handled. Two options:

**Simple (whole-app):**
```xml
<application android:usesCleartextTraffic="true" ... >
```

**More scoped**, via `res/xml/network_security_config.xml` (referenced with `android:networkSecurityConfig="@xml/network_security_config"`):
```xml
<network-security-config>
    <base-config cleartextTrafficPermitted="true">
        <trust-anchors>
            <certificates src="system"/>
        </trust-anchors>
    </base-config>
</network-security-config>
```
Since the target IP is only known at runtime (DHCP-assigned), Android's declarative XML can't scope this to "private IP ranges only" — a base-config allowance is the pragmatic choice here. It's a reasonable trade-off since the app makes no other outbound HTTP calls besides talking to the user's own LAN device.

---

## 7. WebView Integration Details

These are the specific things that make a WebView-wrapped ESP32 UI feel broken if skipped:

1. **JS dialogs won't appear by default.** The Web UI almost certainly uses `confirm()` for destructive actions (reboot/reset/OTA). Android's WebView does **not** show these unless you override them:
   ```kotlin
   webChromeClient = object : WebChromeClient() {
       override fun onJsConfirm(view: WebView, url: String, message: String, result: JsResult): Boolean {
           // show a themed native AlertDialog, then result.confirm() / result.cancel()
           return true
       }
       override fun onJsAlert(view: WebView, url: String, message: String, result: JsResult): Boolean {
           // themed native alert
           return true
       }
   }
   ```
   Without this, tapping "Reboot" or "Factory Reset" in the web UI can silently do nothing (the confirm dialog never renders), which will look like the app is broken.

2. **File chooser for OTA upload.** The `.bin` upload uses `<input type="file">`. WebView needs `onShowFileChooser` implemented and wired to `ActivityResultContracts.GetContent()` (or `OpenDocument`) — without it, tapping "Choose File" does nothing.
   ```kotlin
   override fun onShowFileChooser(
       webView: WebView, filePathCallback: ValueCallback<Array<Uri>>,
       fileChooserParams: FileChooserParams
   ): Boolean {
       pendingFilePathCallback = filePathCallback
       filePickerLauncher.launch("*/*") // .bin has no standard MIME type
       return true
   }
   ```

3. **Settings:**
   ```kotlin
   webView.settings.apply {
       javaScriptEnabled = true
       domStorageEnabled = true
       setSupportZoom(true)
       builtInZoomControls = true
       displayZoomControls = false
       loadWithOverviewMode = true
       useWideViewPort = true
       cacheMode = WebSettings.LOAD_NO_CACHE // always get fresh index_html/config state from the device
   }
   webView.setBackgroundColor(Color.parseColor("#000000")) // avoid a white flash before first paint
   ```

4. **Error/connection handling:** Override `onReceivedError` / `onReceivedHttpError` on `WebViewClient` to navigate to the native `ConnectionLostScreen` (Section 10.4) instead of letting WebView render its own default browser-style error page — this is the single biggest thing that makes it feel like a native app rather than an embedded browser.

5. **Progress + title:** Drive a themed top progress indicator from `onProgressChanged`, and optionally reflect `onReceivedTitle` in the native app bar.

6. **Keep navigation contained:** Override `shouldOverrideUrlLoading` to block any accidental external links from leaving the app (the device's own UI shouldn't link out anywhere, but guard against it).

7. **Keyboard:** Set `android:windowSoftInputMode="adjustResize"` on the activity — the config portal has many numeric/text inputs and the keyboard must not cover them.

---

## 8. Visual Design System — Dark HUD Aesthetic

Built directly from the confirmed palette in 2.4, extended into a full app design system.

### 8.1 Color tokens

| Role | Token | Value | Source |
|---|---|---|---|
| Background (deep) | `HudBackground` | `#0A0C0D` | design extension — near-black, consistent with an OLED-style automotive HUD |
| Surface / card | `HudSurface` | `#16191B` | design extension |
| Border / divider | `HudBorder` | `#2A2E30` | design extension |
| Primary accent | `HudCyan` | `#00FFFF` | `COLOR_TEMP_NORM` |
| Warning | `HudOrange` | `#FF8C00` | `COLOR_TEMP_WARN` |
| Critical / error | `HudRed` | `#FF0000` | `COLOR_TEMP_CRIT` / `COLOR_FUEL_CRIT` |
| Success / connected | `HudGreen` | `#00FF00` | `COLOR_FUEL_NORM` |
| Caution | `HudYellow` | `#FFFF00` | `COLOR_FUEL_WARN` |
| Ghost / disabled | `HudGhost` | `#474747` | `GHOST_COLOR_STR` |
| Primary text | `HudTextPrimary` | `#F2F2F2` | design extension |
| Secondary text | `HudTextSecondary` | `#9AA0A3` | design extension |

Only the background/surface/text tones are original design additions (not literally in the firmware); every functional color (accent, warning, critical, success) is pulled straight from the device's own config so the app and the physical dashboard read as the same product.

### 8.2 Typography

The on-device fonts (Conthrax SemiBold, DS-DIGIT) are VLW files baked for TFT rendering, not distributable app fonts. Practical substitution:
- **Headers / labels:** a geometric technical sans with similar DNA to Conthrax — e.g. **Rajdhani** or **Orbitron** (Google Fonts, OFL-licensed, safe to bundle).
- **Numeric/telemetry readouts** (connection status numbers, scan progress %, etc.): a monospaced/digital-style face — e.g. **Share Tech Mono** or **JetBrains Mono** — to echo the seven-segment feel without needing the licensed VLW font.
- If the actual VLW fonts have a redistributable license and the user wants pixel-perfect brand match, they can be converted to `.ttf`/`.otf` and bundled instead — flag this as an option, don't assume the license permits it.

### 8.3 Signature motifs to carry into the app

- Splash screen text: reuse `"Dashboard++"` as the wordmark and `"by @ale.finot"` as the subtitle, exactly as the device's own boot splash does (`SPLASH_SIGNATURE`).
- A thin animated cyan "scanline" or glow-pulse on the splash/discovery screens echoes the HUD identity without needing custom iconography.
- Status indicators (connected/warning/error) should reuse the exact three-color semantic system (`HudCyan`/`HudGreen` = good, `HudOrange`/`HudYellow` = caution, `HudRed` = critical) rather than inventing a new one — this is literally the device's own status language.

### 8.4 Core components to build

- `HudCard` — dark surface, subtle border, optional glow-on-focus
- `HudButton` — filled (cyan) primary, outlined secondary, destructive (red) variant with built-in confirm dialog
- `ConnectionPill` — small persistent status chip in the top bar: dot + label, colored by state (`Connected` green, `Reconnecting…` yellow, `Offline` red)
- `ScanProgressRing` — circular progress driven by the discovery `StateFlow<Float>`
- `GlowText` — text with a soft colored drop-shadow/glow, used sparingly for the wordmark and key status text

### 8.5 Destructive-action confirmations

Since the firmware has **no auth**, the app is the only guard against accidental taps on Reboot/Sleep/Factory-Reset/OTA-upload. Even though the web UI's own `confirm()` dialogs (wired via `onJsConfirm`) provide one layer, style those native dialogs prominently in `HudRed` for reset/OTA specifically so they read as unmistakably serious, not like a generic browser confirm box.

---

## 9. Screen-by-Screen Specification

### 9.1 Splash Screen
- Full-screen `HudBackground`, centered wordmark "Dashboard++" in the header font, subtitle "by @ale.finot," subtle animated cyan glow/pulse.
- Duration: as long as the initial `CheckingCachedIp` step takes (typically <1s) — this is functional, not just decorative; it's masking the first discovery attempt.
- Use Android 12+ `SplashScreen` API for the OS-level splash, then hand off to this Compose screen for anything past the initial system splash duration.

### 9.2 Discovery / Connecting Screen
Shown whenever discovery goes beyond the instant cached-IP path.
- `ScanProgressRing` with live percentage.
- Status text that changes with the state machine: "Checking last known address…" → "Looking for Dashboard++ on your network…" → "Found — connecting…"
- If it's taking a while, surface a hint after ~4s: "Make sure your phone is on the same Wi-Fi network as your dashboard" (a very common real-world failure mode — phone on cellular data, or on a guest Wi-Fi VLAN that isolates clients from each other).
- "Enter IP manually" text button always available, doesn't require waiting for timeout.

### 9.3 Main Screen (WebView)
- Thin top app bar: connection pill (8.4), current IP in small secondary text, overflow menu (Rescan, Enter IP manually, About).
- Below it, the WebView filling the rest of the screen, edge-to-edge, dark status/nav bars to blend with the HUD background.
- Pull-to-refresh reloads the WebView (`SwipeRefreshLayout`-equivalent in Compose, or a custom gesture detector wrapping the `AndroidView`).
- Thin progress bar under the app bar during page loads, in `HudCyan`.

### 9.4 Connection Lost / Error Screen
- Triggered by `WebViewClient.onReceivedError`, a failed health check, or `NetworkCallback.onLost`.
- Icon + short message ("Lost connection to Dashboard++"), likely-cause hints (device slept/rebooted, left Wi-Fi range, network changed), and a prominent "Reconnect" button that re-runs the discovery flow from the cached-IP step.
- Auto-retry quietly in the background every few seconds while this screen is showing (device reboots take ~5–15s; sleep/wake can be much longer and shouldn't hammer the network).

### 9.5 Settings / About Screen (native, small)
- Manually override/forget the cached IP.
- Toggle: "Always show discovery screen" (skip the instant-cached-IP fast path, useful for debugging).
- Link to the GitHub repo, firmware version display (pulled from `/api/config` if available), credits line matching `REBOOT_SIGNATURE`/`DASHBOARD_SIGNATURE`.

---

## 10. State Management

```kotlin
sealed interface ConnectionUiState {
    object CheckingCache : ConnectionUiState
    object CheckingSoftAp : ConnectionUiState
    data class Discovering(val mode: DiscoveryMode, val progress: Float) : ConnectionUiState
    data class Connected(val ip: String) : ConnectionUiState
    object ManualEntryNeeded : ConnectionUiState
    data class ConnectionLost(val lastIp: String, val reason: String) : ConnectionUiState
}
```

`ConnectionViewModel` owns this as a `StateFlow`, drives `DiscoveryManager`, listens to `NetworkMonitor`, and is the single source of truth the Compose navigation graph switches screens on.

---

## 11. Edge Cases & Resilience Checklist

- [ ] Phone on cellular data only (no Wi-Fi) → skip discovery entirely, go straight to a clear "Connect to your dashboard's Wi-Fi first" message, not a silent timeout.
- [ ] Phone on a guest/isolated Wi-Fi VLAN where client-to-client traffic is blocked → scan will legitimately find nothing; surface the hint from 9.2.
- [ ] Device is mid-reboot when the app launches → cached-IP check fails fast (short timeout), falls through to scan rather than hanging.
- [ ] Device just entered deep sleep (ignition off) → connection will drop mid-session; `ConnectionLostScreen` should say so rather than implying an error, since this is expected behavior per the firmware's power management.
- [ ] DHCP lease changed since last session (new IP) → cached check fails, full discovery re-runs automatically, new IP gets cached on success.
- [ ] User switches from home Wi-Fi to `Dashboard_Config` SoftAP mid-session (e.g., to reconfigure) → `NetworkCallback` should detect the SSID change and short-circuit straight to `192.168.4.1` rather than re-scanning.
- [ ] Large OTA `.bin` upload over a slow/weak Wi-Fi link → don't apply the short discovery-style timeouts to this specific WebView-driven upload; it's handled by the WebView's own network stack, not the app's OkHttp discovery client, so this should already be fine — just don't add anything that could interrupt it (e.g., don't force a WebView reload while an upload is in flight).
- [ ] Android "no internet, stay connected?" prompt when joining `Dashboard_Config` (which has no internet uplink) — this is an OS-level Wi-Fi prompt outside the app's control; mention it in first-run onboarding copy so it doesn't confuse users.

---

## 12. Testing Checklist

- [ ] Cold launch on SoftAP (`Dashboard_Config`) → connects to `192.168.4.1` with no scan.
- [ ] Cold launch on home Wi-Fi, first time (no cache) → full discovery path (NSD if patched, else subnet scan) succeeds.
- [ ] Warm launch on home Wi-Fi, cache hit → connects in <1s.
- [ ] Kill device mid-session → app surfaces `ConnectionLostScreen`, not a frozen WebView.
- [ ] Reboot device via the web UI's own Reboot button → confirm dialog actually appears (tests `onJsConfirm`), app detects the drop and reconnects once the device is back.
- [ ] OTA upload end-to-end: file picker opens, `.bin` selects, upload completes, device reboots, app reconnects.
- [ ] Rotate/resize (foldables, split-screen) → WebView content remains usable, zoom controls work as a fallback.
- [ ] Airplane mode toggle mid-session → clean error state, clean recovery on Wi-Fi restore.
- [ ] Manual IP entry path works standalone even if all auto-discovery fails.

---

## 13. App Identity Suggestions

- **Name:** "Dashboard++" or "Dashboard++ Companion" (distinguishes it from the firmware itself in the Play Store/launcher).
- **Package:** `com.alefinot.dashboardpp` (or whatever namespace matches the GitHub account).
- **Icon direction:** dark circular/rounded-square badge, a stylized cyan speedometer needle or "D++" monogram on near-black, consistent with the palette in 8.1 — matches the automotive-HUD identity already established by the project.

---

## 14. Explicitly Out of Scope (v1)

Keep the initial build focused on the discovery + WebView-shell problem. These are reasonable v2 ideas, not v1 requirements:

- Full native reimplementation of the config UI (bypassing the WebView).
- Home-screen widget showing live telemetry.
- Auto-joining `Dashboard_Config` Wi-Fi from within the app via `WifiNetworkSuggestion`/`WifiNetworkSpecifier` (nice convenience, meaningfully more complex and permission-heavy).
- Push notifications when the device becomes reachable.
- Wear OS companion.

---

## 15. Distribution & Signing — Decision: debug-signed APK, no Play Store

**Decision (free route):** ship the debug build (`assembleDebug`) sideloaded. No release keystore, no Play Developer account.

### What this means in practice

- The "Not verified by Play" / "install from unknown source" prompt is **one-time and per-device** — once the user allows it, installs and updates are silent.
- A **literally unsigned** APK cannot be installed on modern Android. The debug build is *always* signed, by the machine-local debug keystore (`~/.android/debug.keystore` on Android Studio / `keystore` managed by the build). "Keeping it unsigned" = not adding a `signingConfigs` block, not skipping signing.

### Constraints & rules (enforce these)

1. **Build from the same machine** (or keep a copy of the debug keystore). If the keystore is lost, updates will not install over an old version — users must uninstall first. If you must switch machines, copy `~/.android/debug.keystore` over.
2. **Bump `versionCode`** in `android/app/build.gradle.kts` on every APK you share — same `versionName` + higher `versionCode` is the only reliable update path over sideloaded installs.
3. **Never mix** a debug APK with a release-signed one for the same user — signature mismatch forces a manual uninstall.

### If this ever changes

- `$15` one-time Play Developer fee + internal testing track = installs from Play's own page, "Verified by Play", prompt gone.
- Free signing upgrade (no Play) = generate a stable keystore via `keytool` and add a `signingConfigs.release` block — removes the lost-keystore risk without the fee.

---

## Summary of what makes this "auto-fetch" actually work

1. Cache the last IP that worked — most opens are instant.
2. Recognize the SoftAP case and skip discovery entirely (fixed IP).
3. Prefer mDNS if the small firmware patch in 5.4 is applied — near-instant, reliable.
4. Fall back to a real subnet scan computed from the phone's actual link prefix (not a hardcoded `/24`), verified against `/api/perf` and the HTML signature so it can't misidentify some other device on the LAN.
5. Manual entry is always one tap away, never a dead end.
