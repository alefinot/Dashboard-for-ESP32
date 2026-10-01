#include "dashboard.h"
#include "bootinfo.h"
#include "otasign.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_app_format.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

Preferences preferences;
WebServer server(80);

// Heartbeat counter bumped by the web task loop; the display task watches it
// and reboots the device if the web server stalls (e.g. a handler hangs).
volatile unsigned long webLoopCount = 0;

// SAFE MODE (Phase 2 loop-breaker): when memory-saver cannot hold free heap
// above ~16KB we stay UP in the most-frugal state instead of rebooting. This
// is what breaks the heap-critical boot loop: the device keeps serving /api
// (including /api/boot forensics) with mem-saver on and weather/TLS
// suppressed, and only reboots at the OOM floor — and never reboots during a
// fast-reboot storm. A hard crash at the floor still reboots, but the reset
// reason then reads PANIC (crash), not a clean SW loop.
static volatile bool safeModeActive = false;

bool isSafeModeActive() { return safeModeActive; }

// Wipes the configuration NVS namespaces. Used by /api/reset and by the
// physical recovery gesture (hold BOOT 4 s, release, hold 4 s again).
//
// The WiFi join credentials (SSIDs, passwords, TX power) survive the reset:
// the reset exists to recover from a forgotten config PIN or web lockout, and
// wiping the network creds would leave the device stranded in AP-only mode
// exactly when a recovery reset is needed. The SSID/password values written
// back are the ones loaded from NVS at boot (mode 0), never the compiled
// defaults.
//
// The odometer (namespace `dashboard`, key `odo`) also survives. Recovering
// from a forgotten PIN gives no claim on the vehicle's lifetime mileage, and
// re-reading a trip computer's odometer by hand is not possible; the same
// reasoning is why the credentials are kept. Boot diagnostics live in
// `bootinfo` and are untouched either way.
void factoryResetConfig() {
  const char *wifiKeys[] = {
      "WIFI_SSID", "WIFI_S1", "WIFI_S2", "WIFI_S3", "WIFI_S4",
      "WIFI_PWD",  "WIFI_P1", "WIFI_P2", "WIFI_P3", "WIFI_P4",
      "WIFI_TXP"};
  const int wifiKeyCount = sizeof(wifiKeys) / sizeof(wifiKeys[0]);
  // Locked session, and every write checked: a factory reset that silently
  // failed to put the WiFi credentials back would leave the unit unreachable
  // with no explanation (issue #32).
  NvsSession session("cfg", false);
  int restoreFailures = 0;
  if (session.opened()) {
    String saved[11];
    for (int i = 0; i < wifiKeyCount && i < 11; i++)
      saved[i] = session.nvs.getString(wifiKeys[i], "");
    nvsWriteFailed("cfg clear", session.nvs.clear());
    for (int i = 0; i < wifiKeyCount && i < 11; i++) {
      if (saved[i].length() > 0 &&
          nvsWriteFailed(wifiKeys[i], session.nvs.putString(wifiKeys[i], saved[i].c_str())))
        restoreFailures++;
    }
  }
  // `dashboard` is deliberately not cleared any more - it holds only the
  // odometer (see the note above), so this used to cost a unit's whole mileage
  // every time someone held BOOT a little too long.
  if (restoreFailures)
    logPrintf("Factory reset done, but %d WiFi credential(s) could not be rewritten\n", restoreFailures);
  else
    logPrintf("Factory reset done (WiFi credentials and odometer preserved)\n");
}

volatile bool otaUpdateSuccess = false;

// ---------------------------------------------------------------------------
// WiFi STA search: driver-side state written from the WiFi event callback (ESP
// event-task context) and read by the search state machine in webServerTask.
// Plain volatile scalars only - no heap, no String, nothing blocking in the
// callback.
//
// Why the events are needed at all: ESP-IDF 5.5 (arduino-esp32 3.3.x) made
// esp_wifi_set_config() fail with ESP_ERR_WIFI_STATE while a connect attempt is
// in flight - esp_wifi.h: "ESP_ERR_WIFI_STATE: WiFi still connecting when
// invoke esp_wifi_set_config", and espressif/esp-idf#17484 confirms the check
// ships in every 5.x. The Arduino core also auto-reconnects on its own by
// default (STAClass _autoReconnect = true, and WIFI_REASON_NO_AP_FOUND counts as
// a reconnectable reason), so a failed network keeps an attempt armed forever
// and every WiFi.begin() for the next saved network is refused: the STA config
// never changes and the unit stays pinned to the first SSID. The search loop
// therefore owns reconnects here and waits for the driver to report an attempt
// over before installing the next network.
static volatile bool staDrvAttemptOpen = false;  // driver holds/works on an attempt
static volatile bool staLinkDropped = false;     // DISCONNECTED / LOST_IP seen while up
static volatile uint8_t staDrvReason = 0;        // last STA_DISCONNECTED reason code

// Search phase / network index mirrors for the heartbeat line, so a unit in the
// field can be diagnosed without replaying a serial session. Phase values match
// the StaPhase enum in webServerTask.
volatile uint8_t staDbgPhase = 0;
volatile uint8_t staDbgReason = 0;
volatile int staDbgNetIdx = -1;

static void wifiStaEventHandler(arduino_event_t *ev) {
  switch (ev->event_id) {
    case ARDUINO_EVENT_WIFI_STA_STOP:
      staDrvAttemptOpen = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:  // associated, DHCP still pending
      staDrvAttemptOpen = true;
      staLinkDropped = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // The attempt is over: esp_wifi_set_config() accepts a new network again.
      staDrvAttemptOpen = false;
      staDrvReason = ev->event_info.wifi_sta_disconnected.reason;
      staDbgReason = staDrvReason;
      staLinkDropped = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staDrvAttemptOpen = false;
      staLinkDropped = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      // DHCP expired on a live association: treat it as a lost link instead of
      // keeping a dead uplink.
      staLinkDropped = true;
      break;
    default:
      break;
  }
}

// ── Wi-Fi scan cache ────────────────────────────────────────────────────────
// What the radio can actually see. Without this, "it will not join my network"
// and "it cannot see my network" produce the same log, and reason=201
// (WIFI_REASON_NO_AP_FOUND) cannot be trusted on its own. Written by the search
// state machine in webServerTask - the async scan API is non-blocking, so a
// scan never stalls server.handleClient() - and read by GET /api/wifi/scan plus
// the failed-cycle log line.
//
// Fixed static storage, no per-request heap (AGENTS §14): an SSID is at most 32
// bytes on the wire, and 30 entries is more than a home radio ever reports, so
// the cache and the response body are buffers that exist for the life of the
// binary. The web task runs with a 6 KB stack, so neither may be a local.
static const int WIFI_SCAN_MAX = 30;
struct WifiScanEntry {
  char ssid[33];
  int32_t rssi;
  uint8_t ch;
  uint8_t auth;  // wifi_auth_mode_t of the strongest reported copy
};
static WifiScanEntry s_scan[WIFI_SCAN_MAX];
static volatile int s_scanCount = 0;
static volatile unsigned long s_scanDoneMs = 0;   // 0 = no scan has finished
static volatile bool s_scanBusy = false;          // async scan in flight
static volatile bool s_scanRequested = false;     // scan wanted (UI button / auto)
static char s_scanBody[3072];                     // GET response, built in place
                                                  // 30 nets + the saved list fit
                                                  // with the guards in wifiScanJson

// Names for wifi_auth_mode_t (esp_wifi_types_generic.h). The values past OWE
// are rare enough to be reported as "other" with the number alongside.
static const char *wifiAuthName(uint8_t auth) {
  static const char *const names[] = {"open", "WEP", "WPA", "WPA2", "WPA/WPA2",
                                      "enterprise", "WPA3", "WPA2/WPA3", "WAPI",
                                      "OWE"};
  return auth < sizeof(names) / sizeof(names[0]) ? names[auth] : "other";
}

// The five credential pairs, addressed three ways. /api/config, the boot-time
// search and the factory-reset keeper all use these same keys, so the picker
// below stores nothing new - it fills the slots the rest of the firmware already
// reads.
static const int WIFI_SLOT_COUNT = 5;
static const char *const WIFI_SLOT_SSID_KEY[WIFI_SLOT_COUNT] = {"WIFI_SSID", "WIFI_S1",
                                                                "WIFI_S2", "WIFI_S3", "WIFI_S4"};
static const char *const WIFI_SLOT_PASS_KEY[WIFI_SLOT_COUNT] = {"WIFI_PWD", "WIFI_P1",
                                                                "WIFI_P2", "WIFI_P3", "WIFI_P4"};

static char *wifiSlotSsid(int i) {
  switch (i) {
    case 0: return WIFI_SSID;
    case 1: return WIFI_SSID_1;
    case 2: return WIFI_SSID_2;
    case 3: return WIFI_SSID_3;
    default: return WIFI_SSID_4;
  }
}

static char *wifiSlotPass(int i) {
  switch (i) {
    case 0: return WIFI_PASSWORD;
    case 1: return WIFI_PASSWORD_1;
    case 2: return WIFI_PASSWORD_2;
    case 3: return WIFI_PASSWORD_3;
    default: return WIFI_PASSWORD_4;
  }
}

// True when this SSID already has a credential slot. The picker uses it to
// connect straight away instead of asking for a known password again.
static bool wifiSsidSaved(const char *ssid) {
  for (int i = 0; i < WIFI_SLOT_COUNT; i++)
    if (wifiSlotSsid(i)[0] && strncmp(wifiSlotSsid(i), ssid, 64) == 0) return true;
  return false;
}

// JSON-safe SSID: quotes and backslashes are legal in an SSID and would break
// the body, and anything outside printable ASCII is replaced rather than
// shipped as bytes the browser may not decode as UTF-8.
static void wifiSsidEscaped(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (const char *p = src; *p && o + 2 < n; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"' || c == '\\') { dst[o++] = '\\'; dst[o++] = (char)c; }
    else if (c >= 0x20 && c < 0x7F) dst[o++] = (char)c;
    else dst[o++] = '?';
  }
  dst[o] = 0;
}

// Merge one beacon into the cache, keeping the strongest copy per SSID. When the
// cache is full the weakest entry is replaced, so the list that ships is the 30
// strongest signals rather than the first 30 beacons.
static void wifiScanAdd(const char *ssid, int32_t rssi, uint8_t ch, uint8_t auth) {
  if (!ssid || !ssid[0]) return;
  int n = s_scanCount;
  for (int i = 0; i < n; i++) {
    if (strncmp(s_scan[i].ssid, ssid, sizeof(s_scan[i].ssid)) == 0) {
      if (rssi > s_scan[i].rssi) {
        s_scan[i].rssi = rssi;
        s_scan[i].ch = ch;
        s_scan[i].auth = auth;
      }
      return;
    }
  }
  int slot;
  if (n < WIFI_SCAN_MAX) {
    slot = n;
    s_scanCount = n + 1;
  } else {
    int weakest = 0;
    for (int i = 1; i < WIFI_SCAN_MAX; i++)
      if (s_scan[i].rssi < s_scan[weakest].rssi) weakest = i;
    if (rssi <= s_scan[weakest].rssi) return;
    slot = weakest;
  }
  strncpy(s_scan[slot].ssid, ssid, sizeof(s_scan[slot].ssid) - 1);
  s_scan[slot].ssid[sizeof(s_scan[slot].ssid) - 1] = 0;
  s_scan[slot].rssi = rssi;
  s_scan[slot].ch = ch;
  s_scan[slot].auth = auth;
}

// Strongest signal first, so the UI list and the log line lead with what the
// unit is most likely to join.
static void wifiScanSortBySignal() {
  int n = s_scanCount;
  for (int i = 1; i < n; i++) {
    WifiScanEntry key = s_scan[i];
    int j = i - 1;
    while (j >= 0 && s_scan[j].rssi < key.rssi) { s_scan[j + 1] = s_scan[j]; j--; }
    s_scan[j + 1] = key;
  }
}

// Channel of a saved network when the radio has just seen it, 0 when it has not
// been seen or the scan is too old to trust. Passing it to begin() lets the
// driver start its FAST_SCAN on the right channel instead of sweeping 1-13
// first, which is the difference between a join in ~1s and one in ~4s. A router
// that hopped channels would be re-reported by the scan that runs after every
// failed cycle, so the 30s limit is what makes pinning safe rather than stale.
static uint8_t wifiScanChannel(const char *ssid) {
  if (!s_scanDoneMs || millis() - s_scanDoneMs > 30000UL || !ssid || !ssid[0])
    return 0;
  for (int i = 0; i < s_scanCount; i++)
    if (strncmp(s_scan[i].ssid, ssid, sizeof(s_scan[i].ssid)) == 0)
      return s_scan[i].ch;
  return 0;
}

// SSID of the network the STA is on, empty when it is not connected. Kept here
// so the scan response can mark the connected row without asking the core for a
// String on a request path.
static char s_joinedSsid[33] = "";

// Set by POST /api/wifi/join and /api/wifi/forget; consumed by the search state
// machine, which owns the driver. The index is the slot to aim at, -1 = start
// from the top.
static volatile bool s_wifiSearchRequested = false;
static volatile int s_wifiSearchIdx = -1;

// Store one network into one slot: the globals the search machine reads, plus
// NVS so it survives a reboot. Returns false when NVS refused the write, which
// the caller reports rather than hiding - a credential that is in RAM but not in
// flash works until the next reboot and then looks like a random failure.
static bool wifiSlotWrite(int slot, const char *ssid, const char *pass) {
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) return false;
  char *dstSsid = wifiSlotSsid(slot), *dstPass = wifiSlotPass(slot);
  strncpy(dstSsid, ssid ? ssid : "", 63);
  dstSsid[63] = 0;
  strncpy(dstPass, pass ? pass : "", 63);
  dstPass[63] = 0;
  NvsSession session("cfg", false);
  if (!session.opened()) {
    logPrintf("WiFi slot %d: NVS could not be opened\n", slot);
    return false;
  }
  bool ok = true;
  if (nvsWriteFailed(WIFI_SLOT_SSID_KEY[slot],
                     session.nvs.putString(WIFI_SLOT_SSID_KEY[slot], dstSsid))) ok = false;
  if (nvsWriteFailed(WIFI_SLOT_PASS_KEY[slot],
                     session.nvs.putString(WIFI_SLOT_PASS_KEY[slot], dstPass))) ok = false;
  return ok;
}

// Drop one saved network. Credentials are only ever removed by an explicit
// request from the user - a failing network is never forgotten on its own, since
// "it is out of range right now" is the normal case for a vehicle.
static bool wifiSlotForget(int slot) {
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) return false;
  NvsSession session("cfg", false);
  bool ok = session.opened();
  if (ok) {
    if (nvsWriteFailed(WIFI_SLOT_SSID_KEY[slot],
                       session.nvs.remove(WIFI_SLOT_SSID_KEY[slot]))) ok = false;
    if (nvsWriteFailed(WIFI_SLOT_PASS_KEY[slot],
                       session.nvs.remove(WIFI_SLOT_PASS_KEY[slot]))) ok = false;
  }
  wifiSlotSsid(slot)[0] = 0;
  wifiSlotPass(slot)[0] = 0;
  return ok;
}

// Move the network that just joined to the front of the saved list. The search
// starts every cycle at index 0, so this is what makes "connect to whatever is
// actually here" automatic: the network this unit reached last is the first one
// it tries next time, with no manual primary/fallback ordering. Pairs that
// already match are not rewritten, and joins are rare (a handful a day), so this
// stays inside the NVS wear rule (AGENTS §12).
static void wifiRotateToTop(int idx) {
  if (idx < 1 || idx >= WIFI_SLOT_COUNT) return;  // already first
  static char newSsid[WIFI_SLOT_COUNT][64];
  static char newPass[WIFI_SLOT_COUNT][64];
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) { newSsid[i][0] = 0; newPass[i][0] = 0; }
  int out = 0;
  for (int pass = 0; pass < 2; pass++) {  // the joined network, then the rest in order
    for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
      bool take = (pass == 0) ? (i == idx) : (i != idx && wifiSlotSsid(i)[0] != 0);
      if (!take) continue;
      strncpy(newSsid[out], wifiSlotSsid(i), 63);
      newSsid[out][63] = 0;
      strncpy(newPass[out], wifiSlotPass(i), 63);
      newPass[out][63] = 0;
      out++;
    }
  }
  NvsSession session("cfg", false);
  int fails = 0;
  if (!session.opened()) fails = 1;
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
    char *dstSsid = wifiSlotSsid(i), *dstPass = wifiSlotPass(i);
    if (strcmp(dstSsid, newSsid[i]) != 0) {
      strncpy(dstSsid, newSsid[i], 63);
      dstSsid[63] = 0;
      if (session.opened() &&
          nvsWriteFailed(WIFI_SLOT_SSID_KEY[i], session.nvs.putString(WIFI_SLOT_SSID_KEY[i], dstSsid)))
        fails++;
    }
    if (strcmp(dstPass, newPass[i]) != 0) {
      strncpy(dstPass, newPass[i], 63);
      dstPass[63] = 0;
      if (session.opened() &&
          nvsWriteFailed(WIFI_SLOT_PASS_KEY[i], session.nvs.putString(WIFI_SLOT_PASS_KEY[i], dstPass)))
        fails++;
    }
  }
  logPrintf("WiFi: %s is now the first network tried%s\n", newSsid[0],
            fails ? " (NVS write failed)" : "");
}

// Build the /api/wifi/scan body with snprintf into a static buffer: no
// JsonDocument and no growing String on a request path (AGENTS §14).
static void wifiSavedJson(size_t cap, int *off);  // defined below, uses the slots
static void wifiScanJson() {
  size_t cap = sizeof(s_scanBody) - 1;
  int off = snprintf(s_scanBody, cap,
                     "{\"scanning\":%s,\"ageMs\":%lu,\"count\":%d,\"max\":%d,\"nets\":[",
                     s_scanBusy ? "true" : "false",
                     s_scanDoneMs ? (unsigned long)(millis() - s_scanDoneMs) : 0UL,
                     (int)s_scanCount, WIFI_SCAN_MAX);
  if (off < 0 || (size_t)off >= cap) off = 0;
  int shown = 0;
  char esc[67];
  for (int i = 0; i < s_scanCount && (size_t)off < cap - 140; i++) {
    wifiSsidEscaped(esc, sizeof(esc), s_scan[i].ssid);
    int w = snprintf(s_scanBody + off, cap - off,
                     "%s{\"ssid\":\"%s\",\"rssi\":%ld,\"ch\":%u,\"auth\":%u,"
                     "\"authName\":\"%s\",\"saved\":%s}",
                     shown ? "," : "", esc, (long)s_scan[i].rssi,
                     (unsigned)s_scan[i].ch, (unsigned)s_scan[i].auth,
                     wifiAuthName(s_scan[i].auth),
                     wifiSsidSaved(s_scan[i].ssid) ? "true" : "false");
    if (w <= 0 || (size_t)(off + w) >= cap) break;
    off += w;
    shown++;
  }
  wifiSavedJson(cap, &off);
}

// The five credential slots, as the picker needs them: which names are stored,
// in which slot, and which one is live. Kept in the same response as the scan so
// the panel is one request, and so a saved-but-currently-invisible network is
// still listed (a network out of range is normal for a vehicle, and forgetting
// it must stay the user's choice).
static void wifiSavedJson(size_t cap, int *off) {
  char esc[67];
  if ((size_t)*off >= cap - 16) {
    if ((size_t)*off < cap - 2) snprintf(s_scanBody + *off, cap - *off, "]}");
    return;
  }
  int w = snprintf(s_scanBody + *off, cap - *off, "],\"saved\":[");
  if (w <= 0 || (size_t)(*off + w) >= cap) return;
  *off += w;
  int shown = 0;
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
    char *sid = wifiSlotSsid(i);
    if (!sid[0]) continue;
    if ((size_t)*off >= cap - 120) break;
    wifiSsidEscaped(esc, sizeof(esc), sid);
    w = snprintf(s_scanBody + *off, cap - *off,
                 "%s{\"slot\":%d,\"ssid\":\"%s\",\"connected\":%s}",
                 shown ? "," : "", i, esc,
                 (s_joinedSsid[0] && strcmp(s_joinedSsid, sid) == 0) ? "true" : "false");
    if (w <= 0 || (size_t)(*off + w) >= cap) break;
    *off += w;
    shown++;
  }
  if ((size_t)*off < cap - 2) *off += snprintf(s_scanBody + *off, cap - *off, "]}");
}

static void wifiScanGetHandler() {
  wifiScanJson();
  server.send(200, "application/json", s_scanBody);
}

// POST /api/wifi/join {"ssid":"...","pass":"..."}
// The phone-style flow: pick a name from the scan list, type the passphrase once.
// An SSID already stored keeps its slot (so a corrected password does not create
// a duplicate); otherwise the first free slot is used. With all five slots full
// the caller gets "full" back plus the list, and has to name the slot to
// overwrite - nothing is silently replaced.
static void wifiJoinPostHandler() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"no body\"}");
    return;
  }
  // A join is a person pressing a button, not a poller, so the parse pool here
  // matches what POST /api/config already does.
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"bad JSON\"}");
    return;
  }
  const char *ssid = doc["ssid"];
  JsonVariantConst passVal = doc["pass"];
  const char *passIn = passVal;  // null when the caller sent no passphrase at all
  int reqSlot = doc["slot"] | -1;
  if (!ssid || !ssid[0]) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"missing ssid\"}");
    return;
  }
  char ssidClean[33];  // 32 bytes is the wire limit: a longer name is refused by
  strncpy(ssidClean, ssid, 32);
  ssidClean[32] = 0;   // the core, so do not store a half-name either
  const size_t passLen = passIn ? strlen(passIn) : 0;
  if (passLen > 63) {  // WPA2/3 passphrases are 8-63 printable characters
    server.send(400, "application/json",
                "{\"status\":\"error\",\"error\":\"passphrase too long\"}");
    return;
  }

  int slot = -1, freeSlot = -1;
  bool matchedExisting = false;
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
    char *sid = wifiSlotSsid(i);
    if (sid[0] && strcmp(sid, ssidClean) == 0) { slot = i; matchedExisting = true; break; }
    if (freeSlot < 0 && !sid[0]) freeSlot = i;
  }
  bool overwrite = false;
  if (slot < 0) {
    if (freeSlot >= 0) {
      slot = freeSlot;
    } else if (reqSlot >= 0 && reqSlot < WIFI_SLOT_COUNT) {
      slot = reqSlot;  // the user named which network may be displaced
      overwrite = true;
    } else {
      // All five slots hold a network: say so, with the list, and let the UI ask
      // which one may go.
      static char fullBody[640];
      int off = snprintf(fullBody, sizeof(fullBody),
                         "{\"status\":\"full\",\"max\":%d,\"saved\":[", WIFI_SLOT_COUNT);
      int shown = 0;
      char esc[67];
      for (int i = 0; i < WIFI_SLOT_COUNT && (size_t)off < sizeof(fullBody) - 110; i++) {
        char *sid = wifiSlotSsid(i);
        if (!sid[0]) continue;
        wifiSsidEscaped(esc, sizeof(esc), sid);
        off += snprintf(fullBody + off, sizeof(fullBody) - off, "%s{\"slot\":%d,\"ssid\":\"%s\"}",
                        shown ? "," : "", i, esc);
        shown++;
      }
      snprintf(fullBody + off, sizeof(fullBody) - off, "]}");
      server.send(200, "application/json", fullBody);
      return;
    }
  }

  // Resolve the passphrase against the slot. "Reconnect" from the stored list
  // sends no passphrase field, and must reuse what is already stored rather than
  // write an empty one; a tap on an open network sends "" on purpose.
  char passBuf[64];
  const char *pass = passIn;
  if (!passIn) {
    if (matchedExisting) {
      strncpy(passBuf, wifiSlotPass(slot), 63);
      passBuf[63] = 0;
      pass = passBuf;
    } else {
      pass = "";
    }
  }

  bool nvsOk = wifiSlotWrite(slot, ssidClean, pass);
  s_wifiSearchIdx = slot;
  s_wifiSearchRequested = true;
  char esc[67];
  wifiSsidEscaped(esc, sizeof(esc), ssidClean);
  char body[192];
  snprintf(body, sizeof(body),
           "{\"status\":\"ok\",\"slot\":%d,\"ssid\":\"%s\",\"nvsOk\":%s,\"overwrite\":%s}",
           slot, esc, nvsOk ? "true" : "false", overwrite ? "true" : "false");
  logPrintf("WiFi: slot %d set to %s (%s), searching now\n", slot, ssidClean,
            strlen(pass) ? "secured" : "open network");
  if (!nvsOk)
    logPrintf("WiFi: %s is set for this session but NVS refused the write\n", ssidClean);
  server.send(200, "application/json", body);
}

// POST /api/wifi/forget {"slot":n}
static void wifiForgetPostHandler() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"bad JSON\"}");
    return;
  }
  int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) {
    server.send(400, "application/json", "{\"status\":\"error\",\"error\":\"bad slot\"}");
    return;
  }
  char gone[67];
  wifiSsidEscaped(gone, sizeof(gone), wifiSlotSsid(slot));
  bool nvsOk = wifiSlotForget(slot);
  s_wifiSearchIdx = -1;  // whatever else is saved decides what to try next
  s_wifiSearchRequested = true;
  char body[160];
  snprintf(body, sizeof(body),
           "{\"status\":\"ok\",\"slot\":%d,\"ssid\":\"%s\",\"nvsOk\":%s}", slot, gone,
           nvsOk ? "true" : "false");
  logPrintf("WiFi: slot %d (%s) forgotten\n", slot, gone);
  server.send(200, "application/json", body);
}

// The list refreshes only when the user asks for it: a scan makes the radio hop
// off the current channel, which is a call for the person holding the phone,
// not for a poller. The search machine raises the same flag on its own, but only
// when there is no link to disturb.
static void wifiScanPostHandler() {
  bool wasBusy = s_scanBusy;
  s_scanRequested = true;
  server.send(200, "application/json",
              wasBusy ? "{\"status\":\"scanning\"}" : "{\"status\":\"requested\"}");
}

// One line that settles visibility: the networks the radio reported, strongest
// first, with the age of that scan. Called when a search cycle fails.
static void logWifiScanSummary() {
  static char line[512];
  int n = s_scanCount;
  size_t off = snprintf(line, sizeof(line), "WiFi scan (%lus ago): ",
                        s_scanDoneMs ? (unsigned long)((millis() - s_scanDoneMs) / 1000UL)
                                     : 0UL);
  if (off >= sizeof(line)) off = sizeof(line) - 1;
  int shown = 0;
  for (int i = 0; i < n && off < sizeof(line) - 60; i++) {
    int w = snprintf(line + off, sizeof(line) - off, "%s%s ch%u %ld %s",
                     shown ? " | " : "", s_scan[i].ssid, (unsigned)s_scan[i].ch,
                     (long)s_scan[i].rssi, wifiAuthName(s_scan[i].auth));
    if (w <= 0 || off + (size_t)w >= sizeof(line) - 1) break;
    off += w;
    shown++;
  }
  if (!n)
    snprintf(line + off, sizeof(line) - off, "nothing reported yet");
  else if (shown < n)
    snprintf(line + off, sizeof(line) - off, " (+%d more)", n - shown);
  logPrintf("%s\n", line);
}

// OTA Pull state
static bool otaPullBootCheckDone = false;  // automatic check runs once per boot
static char otaPullStatus[96] = "idle";
static bool otaPullStatusUpdated = false;
static SemaphoreHandle_t otaStatusMutex = NULL;
static bool otaPullTaskRunning = false;
static bool otaPullManualFlag = false;
// Set while the OTA pull task is actively streaming a firmware download. The
// web loop pauses HTTP serving for that window so the TLS download gets the
// radio and the heap: serving the browser pollers starves lwIP pbufs (errno
// 11 write-fail spam) and fragments RAM below what the mbedTLS handshake needs.
static volatile bool otaPullDownloading = false;

// ----------------------------------------------------------------------------
// Background Weather Fetch
// ----------------------------------------------------------------------------
static volatile bool weatherTaskRunning = false;  // guarded by weatherFetchMutex
static unsigned long weatherTaskStartedMs = 0;
// Issue #23: a fetch that sticks is asked to stop, never killed. vTaskDelete() on
// a task blocked inside HTTPClient/mbedTLS never unwinds: the task stack, the
// HTTPClient, the TLS client and ~34 KB of mbedTLS buffers leak, and two or three
// hangs are enough to push the unit into memory-saver/SAFE MODE. weatherAbort is
// polled by the fetch at its own checkpoints so it returns through its normal
// destructors and deletes itself; weatherAbandoned stops a second fetch piling up
// behind a task that ignores the request.
static volatile bool weatherAbort = false;
static bool weatherAbandoned = false;  // guarded by weatherFetchMutex
// Set when a config save changes the weather location/city/interval so the
// fetch loop re-queries immediately instead of waiting for the next interval.
static volatile bool weatherRefreshRequested = false;

// Reverse-geocode a coordinate into a short display name (city/locality/region).
// Free keyless BigDataCloud lookup; returns true and fills `out` on success.
static bool reverseGeocode(double lat, double lon, String &out) {
  if (WiFi.status() != WL_CONNECTED || weatherAbort) return false;
  char url[192];
  if (WEATHER_LOCALE[0] != 0) {
    snprintf(url, sizeof(url),
             "http://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.6f&longitude=%.6f&localityLanguage=%s",
             lat, lon, WEATHER_LOCALE);
  } else {
    snprintf(url, sizeof(url),
             "http://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.6f&longitude=%.6f",
             lat, lon);
  }

  HTTPClient http;
  if (!http.begin(url)) return false;
  // Bounded on purpose: the library defaults leave a black-hole host sitting in
  // connect/read long enough to trip the 30 s fetch guard in the web loop.
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    logPrintf("Weather: geocode HTTP error %d\n", httpCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  const char *city     = doc["city"] | "";
  const char *locality = doc["locality"] | "";
  const char *region   = doc["principalSubdivision"] | "";
  const char *country  = doc["countryName"] | "";
  String name;
  if (strlen(city) > 0)       name = city;
  else if (strlen(locality) > 0) name = locality;
  else if (strlen(region) > 0)   name = region;
  else if (strlen(country) > 0)  name = country;
  if (name.length() == 0) return false;
  out = name;
  logPrintf("Weather: geocoded to \"%s\"\n", out.c_str());
  return true;
}

// Bounded text copy: always leaves a NUL inside dst, so a display task that
// samples the shared weather arrays mid-copy still sees a terminated string
// (issue #8). The copy is UTF-8 aware: a localized city name (Monachium,
// São Paulo) is cut on a character boundary, never through the middle of a
// multi-byte sequence, because the renderer walks lead bytes and a dangling
// continuation byte draws as a missing glyph.
void copyFixed(char *dst, size_t n, const char *src) {
  size_t len = strlen(src);
  if (len > n - 1) len = n - 1;
  while (len > 0 && ((unsigned char)src[len - 1] & 0xC0) == 0x80) len--;  // trailing continuation bytes
  if (len > 0 && (unsigned char)src[len - 1] >= 0xC0) len--;              // lead byte whose tail got cut
  memcpy(dst, src, len);
  dst[len] = 0;
}

// Percent-encode one query value. Everything outside [A-Za-z0-9-_.~] becomes
// %XX byte by byte, so a UTF-8 city name goes out as its raw UTF-8 bytes.
static void urlEncode(const char *src, char *dst, size_t n) {
  static const char *hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const char *p = src; *p; p++) {
    unsigned char c = (unsigned char)*p;
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    size_t need = safe ? 1 : 3;
    if (o + need >= n) break;
    if (safe) {
      dst[o++] = (char)c;
    } else {
      dst[o++] = '%';
      dst[o++] = hex[c >> 4];
      dst[o++] = hex[c & 0x0F];
    }
  }
  dst[o] = 0;
}

// Great-circle distance, used only to sanity-check a geocoding hit.
static double haversineKm(double aLat, double aLon, double bLat, double bLon) {
  const double d2r = 0.017453292519943295;
  double s1 = sin((bLat - aLat) * d2r / 2);
  double s2 = sin((bLon - aLon) * d2r / 2);
  double h = s1 * s1 + cos(aLat * d2r) * cos(bLat * d2r) * s2 * s2;
  return 2 * 6371.0 * atan2(sqrt(h), sqrt(1 - h));
}

// The panel draws the city name with the Conthrax VLW subset, whose glyph range
// is not documented anywhere in this repo. Until it is measured (font precheck
// in the SYSTEM_LANGUAGE plan), only accept a localized spelling for locales
// whose place names are written in the Latin alphabet - a Cyrillic or CJK name
// would draw as blanks on the dashboard.
static bool localeIsLatinScript(const char *loc) {
  static const char *const latin[] = {"it", "en", "fr", "de", "es", "pt", "nl", "pl", "tr"};
  for (size_t i = 0; i < sizeof(latin) / sizeof(latin[0]); i++)
    if (strcasecmp(loc, latin[i]) == 0) return true;
  return false;
}

// No GPS fix (or a reverse geocode that came back empty) leaves the typed
// WEATHER_CITY as the widget label. Look that spelling up on Open-Meteo's
// geocoding service and show the place's name in WEATHER_LOCALE, so the widget
// follows the language with the GNSS dark too: "Milan" reads Mailand in German
// and Monachium reads Munich in Polish. Keyless, plain HTTP, same provider
// family as the forecast. Note the service matches names in the requested
// language, so an exonym it does not know ("Milano" asked in German) simply
// yields no nearby match and the typed label stays as written.
static void localizeCityName(double lat, double lon, char *out, size_t outN) {
  out[0] = 0;
  if (WEATHER_CITY[0] == 0 || WEATHER_LOCALE[0] == 0) return;

  static char warnedLocale[16] = "";
  if (!localeIsLatinScript(WEATHER_LOCALE)) {
    if (strcmp(warnedLocale, WEATHER_LOCALE) != 0) {
      copyFixed(warnedLocale, sizeof(warnedLocale), WEATHER_LOCALE);
      logPrintf("Weather: locale %s is not drawn by the display font, keeping the typed city name\n",
                WEATHER_LOCALE);
    }
    return;
  }

  // With no position to check against, a name search cannot be validated: a
  // bare "Milano" resolves first on Milano, Texas. Only localize when the
  // widget has coordinates to anchor the match on.
  if (lat == 0.0 && lon == 0.0) return;

  char key[64];
  snprintf(key, sizeof(key), "%s\n%s", WEATHER_CITY, WEATHER_LOCALE);

  // Cached across refreshes: the lookup costs one request per typed-name or
  // locale change, never one per weather refresh. Key is short enough that
  // copyFixed cannot truncate it (47 + 1 + locale).
  static char cacheKey[64] = "";
  static char cacheName[48] = "";
  if (strcmp(key, cacheKey) == 0) {
    copyFixed(out, outN, cacheName);
    return;
  }

  if (weatherAbort || WiFi.status() != WL_CONNECTED) return;

  char encoded[160];
  urlEncode(WEATHER_CITY, encoded, sizeof(encoded));
  char url[320];
  snprintf(url, sizeof(url),
           "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=3&language=%s",
           encoded, WEATHER_LOCALE);

  HTTPClient http;
  if (!http.begin(url)) return;
  http.setConnectTimeout(8000);  // plain HTTP, generous bound (issue #11)
  http.setTimeout(8000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    logPrintf("Weather: city lookup failed, HTTP %d\n", code);
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();
  if (weatherAbort) return;  // fetch aborted: drop the result, retry next round

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    logPrintf("Weather: city lookup returned invalid JSON\n");
    return;
  }
  if (!doc["results"].is<JsonArray>()) {
    logPrintf("Weather: no %s name found for \"%s\", keeping it as typed\n",
              WEATHER_LOCALE, WEATHER_CITY);
    return;
  }

  // Take the nearest candidate and discard ones from the other side of the
  // anchor: the un-hinted "Milano" search first hits Milano, Texas (8,694 km).
  for (JsonVariant hit : doc["results"].as<JsonArray>()) {
    const char *name = hit["name"] | "";
    if (name[0] == 0) continue;
    double hLat = hit["latitude"] | 0.0;
    double hLon = hit["longitude"] | 0.0;
    if (haversineKm(lat, lon, hLat, hLon) > 150.0) continue;
    copyFixed(out, outN, name);
    break;
  }

  if (out[0] == 0) return;  // nothing nearby: not cached, so a corrected name is picked up next refresh
  copyFixed(cacheKey, sizeof(cacheKey), key);
  copyFixed(cacheName, sizeof(cacheName), out);
  if (strcmp(out, WEATHER_CITY) != 0)
    logPrintf("Weather: city \"%s\" reads \"%s\" in %s\n", WEATHER_CITY, out, WEATHER_LOCALE);
}

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  // Aborted before it even started (issue #23): the web loop already gave up on
  // an earlier fetch, so return through the normal path and let the task exit.
  if (weatherAbort) {
    logPrintf("Weather: aborted before fetch\n");
    return;
  }
  HTTPClient http;
  
  // Read the GPS position from the published snapshot, NOT from
  // gps.location.lat()/lng(): this task runs on a different core than
  // gpsTask, and consuming the one-shot TinyGPS++ "updated" flag here
  // silently robs the odometer of a fix (and a 64-bit cross-task read can
  // tear mid-write).
  double lat = WEATHER_LAT;
  double lon = WEATHER_LON;
  bool gpsFix = gpsFixSnapshot(lat, lon);

  // City name follows the coordinates: only reverse-geocode a live GPS fix,
  // otherwise fall back to the saved WEATHER_CITY derived from WEATHER_LAT/LON.
  String resolvedCity;
  if (gpsFix && !weatherAbort) reverseGeocode(lat, lon, resolvedCity);

  // Nothing resolved the name from position - translate the typed label
  // instead, so the widget follows WEATHER_LOCALE with the GNSS dark too.
  if (!weatherAbort && resolvedCity.length() == 0) {
    char localized[48];
    localizeCityName(lat, lon, localized, sizeof(localized));
    if (localized[0] != 0) resolvedCity = localized;
  }
  
  char url[256];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,relative_humidity_2m,weather_code,cloud_cover,wind_speed_10m,wind_direction_10m&daily=sunrise,sunset&timezone=auto&forecast_days=1",
           lat, lon);
           
  logPrintf("Weather: fetching from %s\n", url);

  // https endpoints (a future weather provider, or a user pointing the URL at
  // an https host) must not negotiate TLS while the big UI buffers fragment
  // the heap: ask the display task to drop the sprites first, exactly like the
  // OTA pull does. Plain http (open-meteo) skips this.
  if (strncmp(url, "https://", 8) == 0) {
    otaMemReleaseRequested = true;
    otaMemReleased = false;
    unsigned long t0 = millis();
    while (!otaMemReleased && (millis() - t0) < 3000 && !weatherAbort)
      vTaskDelay(pdMS_TO_TICKS(1));
    otaMemReleaseRequested = false;
    logPrintf("Weather: UI mem released for TLS heap=%lu max=%lu\n",
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap());
  }
  http.begin(url);
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    if (weatherAbort) {
      // Abandoned while the body was still arriving: drop the result instead of
      // publishing weather nobody asked for any more.
      logPrintf("Weather: aborted while reading, result dropped\n");
      http.end();
      return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Build the whole update locally first, then commit it as one
        // straight-line copy into the shared struct: the display task reads
        // these fields without the mutex, so the window in which it can sample
        // a half-written update must contain no free and no allocation
        // (issue #8).
        float t = doc["current"]["temperature_2m"] | 0.0f;
        int hum = doc["current"]["relative_humidity_2m"] | 0;
        int code = doc["current"]["weather_code"] | 0;
        int cloud = doc["current"]["cloud_cover"] | 0;
        float wind = doc["current"]["wind_speed_10m"] | 0.0f;
        float windDir = doc["current"]["wind_direction_10m"] | 0.0f;
        const char *sunriseSrc = "--:--", *sunsetSrc = "--:--";
        if (doc["daily"]["sunrise"].is<JsonArray>()) {
          const char *v = doc["daily"]["sunrise"][0] | "";
          if (strlen(v) >= 16) sunriseSrc = v + 11;  // "HH:MM" of the ISO stamp
        }
        if (doc["daily"]["sunset"].is<JsonArray>()) {
          const char *v = doc["daily"]["sunset"][0] | "";
          if (strlen(v) >= 16) sunsetSrc = v + 11;
        }
        char sunrise[8], sunset[8], city[48];
        copyFixed(sunrise, sizeof(sunrise), sunriseSrc);
        copyFixed(sunset, sizeof(sunset), sunsetSrc);
        copyFixed(city, sizeof(city),
                  resolvedCity.length() > 0 ? resolvedCity.c_str() : WEATHER_CITY);

        g_weatherData.temperature = t;
        g_weatherData.humidity = hum;
        g_weatherData.weatherCode = code;
        g_weatherData.cloudCover = cloud;
        g_weatherData.windSpeed = wind;
        g_weatherData.windDirection = windDir;
        memcpy(g_weatherData.sunriseTime, sunrise, sizeof(sunrise));
        memcpy(g_weatherData.sunsetTime, sunset, sizeof(sunset));
        memcpy(g_weatherData.cityName, city, sizeof(city));
        g_weatherData.valid = true;
        g_weatherData.lastUpdated = millis();
        xSemaphoreGive(g_stateMutex);
      }
      logPrintf("Weather: success! Temp=%.1fC, Hum=%d%%\n", 
                g_weatherData.temperature, g_weatherData.humidity);
    } else {
      logPrintf("Weather JSON error: %s\n", error.c_str());
    }
  } else {
    logPrintf("Weather HTTP error: %d\n", httpCode);
  }
  http.end();
}

static TaskHandle_t weatherTaskHandle = NULL;
// Guards weatherTaskRunning/weatherTaskHandle: the fetch task cleans up its
// own flag while the web task starts/deletes fetches, and an unsynchronized
// stale cleanup can wipe the handle of a fetch started while it was winding
// down (concurrent fetches + a hung task the guard can no longer find).
static SemaphoreHandle_t weatherFetchMutex = NULL;

void weatherFetchTask(void *pvParameters) {
  updateWeather();
  // Only clear the flag if this task still owns it: a finishing fetch must
  // not wipe the flag/handle of a fetch started while it was winding down.
  xSemaphoreTake(weatherFetchMutex, portMAX_DELAY);
  if (weatherTaskHandle == xTaskGetCurrentTaskHandle()) {
    weatherTaskRunning = false;
    weatherTaskHandle = NULL;
    weatherAbandoned = false;  // this fetch is done, abandon-request cleared with it
  }
  xSemaphoreGive(weatherFetchMutex);
  vTaskDelete(NULL);
}

bool startWeatherFetch() {
  xSemaphoreTake(weatherFetchMutex, portMAX_DELAY);
  if (weatherTaskRunning) {
    // Issue #23 fallback: a fetch that was told to abort and has not come back
    // yet still owns the slot. Starting a second one would stack another
    // HTTPClient + TLS context on top of the stuck one, so refuse; leaking at
    // most one stuck task beats leaking N.
    if (weatherAbandoned) {
      logPrintf("Weather: previous fetch still stuck after abort request - skipping\n");
      xSemaphoreGive(weatherFetchMutex);
      return false;
    }
    xSemaphoreGive(weatherFetchMutex);
    return true;
  }
  weatherTaskRunning = true;
  weatherTaskStartedMs = millis();
  weatherAbort = false;
  weatherAbandoned = false;
  BaseType_t res = xTaskCreatePinnedToCore(weatherFetchTask, "WeatherFetchTask",
                                           8192, NULL, 1, &weatherTaskHandle, 0);
  if (res != pdPASS) {
    logPrintf("Weather: task creation failed, will retry\n");
    weatherTaskRunning = false;
    weatherTaskHandle = NULL;
    xSemaphoreGive(weatherFetchMutex);
    return false;
  }
  xSemaphoreGive(weatherFetchMutex);
  return true;
}

void setOtaPullStatus(const char *status) {
  if (otaStatusMutex) xSemaphoreTake(otaStatusMutex, portMAX_DELAY);
  strncpy(otaPullStatus, status, sizeof(otaPullStatus) - 1);
  otaPullStatus[sizeof(otaPullStatus) - 1] = 0;
  otaPullStatusUpdated = true;
  if (otaStatusMutex) xSemaphoreGive(otaStatusMutex);
}

void otaPullTask(void *pvParameters) {
  uint32_t args = (uint32_t)(uintptr_t)pvParameters;
  checkForFirmwareUpdate((args & 1) != 0, (args & 2) != 0);
  otaPullTaskRunning = false;
  otaPullDownloading = false;   // safety: never leave the web loop paused
  vTaskDelete(NULL);
}

void startOtaPull(bool manual, bool skipThrottle) {
  if (otaPullTaskRunning || otaUpdateInProgress) return;
  otaPullManualFlag = manual;
  otaPullTaskRunning = true;
  uint32_t args = (uint32_t)(manual ? 1 : 0) | (uint32_t)(skipThrottle ? 2 : 0);
  BaseType_t res = xTaskCreatePinnedToCore(otaPullTask, "OtaPullTask", 16384,
                                           (void *)(uintptr_t)args, 1, NULL, 0);
  if (res != pdPASS) {
    logPrintf("OTA Pull: task creation failed\n");
    otaPullTaskRunning = false;
  }
}

// Suspends the display loop task while the OTA task does TLS work, so the
// heap does not get fragmented by per-frame UI allocations. The mbedTLS
// handshake needs large contiguous blocks and fails (ALLOC_FAILED) otherwise.
// Before suspending, asks the display task to free the speed sprite (~70KB,
// the biggest UI allocation) at a safe point â€” see processOtaMemRelease in
// ui.cpp. The display task rebuilds it after the check finishes.
namespace {
struct OtaHeapGuard {
  OtaHeapGuard() {
    otaMemReleaseRequested = true;
    otaMemReleased = false;
    unsigned long t0 = millis();
    while (!otaMemReleased && (millis() - t0) < 3000)
      vTaskDelay(pdMS_TO_TICKS(1));
    logPrintf("OTA Pull: UI mem released heap=%lu max=%lu\n",
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap());
  }
  ~OtaHeapGuard() {
    otaMemReleaseRequested = false;
  }
};

// A refused connection is often a rate limiter or the CDN telling us to back
// off. Rapid retries only burn connections (and lwIP pbufs) and make the
// refusal last longer, so throttle any check to one per minute.
static unsigned long lastOtaCheckMs = 0;
const unsigned long OTA_RECHECK_MIN_MS = 60000;
}

namespace {
// One flash-write session at a time. The pull task and the Web UI upload
// handler both drive the singleton Update object; two sessions open at once
// write interleaved into the same OTA slot and produce an image that will not
// boot. The 15-minute pull-overrun reset used to clear the "busy" latch while
// the first session was still open, which is exactly how that happened
// (issue #24).
volatile bool otaFlashSessionOpen = false;
// Set when a pull task was reset while it still held a flash session open. The
// slot state is unknown at that point, so no further OTA is attempted until the
// unit reboots.
volatile bool otaSessionPoisoned = false;
}

// Update.begin / abort wrapped in the interlock, so the two OTA entry points can
// never overlap and no caller can activate an image from a session it did not
// open.
static bool otaFlashOpen(size_t size) {
  if (otaSessionPoisoned) {
    logPrintf("OTA: no flash session - a previous OTA was reset mid-write (reboot required)\n");
    return false;
  }
  if (otaFlashSessionOpen) {
    logPrintf("OTA: Update.begin refused - a flash session is already open\n");
    return false;
  }
  if (!Update.begin(size)) return false;
  otaFlashSessionOpen = true;
  return true;
}

static void otaFlashClose() { otaFlashSessionOpen = false; }

static void otaFlashAbort() {
  if (!otaFlashSessionOpen) return;  // already closed / never opened
  otaFlashSessionOpen = false;
  Update.abort();
}

// TLS: check the server certificate against the CA bundle compiled into the
// core. setInsecure() accepted any certificate, which made an "HTTPS" pull
// trivially interceptable by anyone on the network (SECURITY plan 2).
static WiFiClient *newHttpClient(const char *url) {
  if (strncmp(url, "https://", 8) == 0) {
    WiFiClientSecure *ssl = new WiFiClientSecure();
    ssl->useBuiltinCACertBundle();
    return ssl;
  }
  return new WiFiClient();
}

// mbedTLS keeps a human-readable reason for a failed handshake; without it a
// rejected certificate looks exactly like a dead server.
static void logTlsError(const char *url, WiFiClient *client) {
  if (strncmp(url, "https://", 8) != 0 || !client) return;
  char err[128] = "";
  static_cast<WiFiClientSecure *>(client)->lastError(err, sizeof(err));
  logPrintf("OTA Pull: TLS: %s\n", err);
}

// Fetch the detached signature for an image: <firmwareUrl>.sig, kept as a
// sibling release asset (firmware.bin -> firmware.bin.sig). Returns the number
// of bytes read, or 0 if the asset is missing or unreadable - and 0 always
// means "refuse the update", never "no signature needed".
static size_t fetchSignatureAsset(const char *firmwareUrl, uint8_t *buf,
                                  size_t cap) {
  char sigUrl[300];
  if (snprintf(sigUrl, sizeof(sigUrl), "%s.sig", firmwareUrl) >=
      (int)sizeof(sigUrl)) {
    logPrintf("OTA Pull: signature URL too long\n");
    return 0;
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClient *client = newHttpClient(sigUrl);
    size_t got = 0;
    {
      HTTPClient http;
      if (!http.begin(*client, sigUrl)) {
        logPrintf("OTA Pull: signature begin failed\n");
      } else {
        http.setTimeout(10000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.addHeader("Accept-Encoding", "identity");
        int httpCode = http.GET();
        long len = http.getSize();
        if (httpCode != HTTP_CODE_OK) {
          logPrintf("OTA Pull: signature fetch HTTP %d (%s)\n", httpCode,
                    HTTPClient::errorToString(httpCode).c_str());
          logTlsError(sigUrl, client);
        } else if (len > (long)cap) {
          logPrintf("OTA Pull: signature asset too big (%ld bytes)\n", len);
        } else {
          // Read exactly what the server announced: Stream::readBytes would
          // otherwise sit out its timeout waiting for bytes that never come.
          size_t want = (len >= 0 && len <= (long)cap) ? (size_t)len : cap;
          got = http.getStream().readBytes((char *)buf, (int)want);
          if (len >= 0 && got != want) {
            logPrintf("OTA Pull: signature short read (%zu of %zu bytes)\n",
                      got, want);
            got = 0;
          }
        }
        http.end();
      }
    }
    delete client;
    if (got > 0 && got <= cap) {
      logPrintf("OTA Pull: signature asset %zu bytes\n", got);
      return got;
    }
    if (attempt == 0) vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return 0;
}

void checkForFirmwareUpdate(bool manual, bool skipThrottle) {
  if (!OTA_PULL_ENABLED && !manual) return;
  if (WiFi.status() != WL_CONNECTED) {
    setOtaPullStatus("error: not connected to WiFi");
    return;
  }
  if (OTA_PULL_URL[0] == 0) {
    setOtaPullStatus("error: no OTA URL configured");
    return;
  }
  // Throttling guards repeated checks (manual button spam); the once-per-boot
  // automatic check skips it since it fires within a second of boot. A
  // throttled check waits here and retries by itself, publishing a live
  // countdown in the status string, instead of making the user re-click.
  // lastOtaCheckMs == 0 means "never checked": the first check always runs.
  if (!skipThrottle && lastOtaCheckMs != 0 &&
      millis() - lastOtaCheckMs < OTA_RECHECK_MIN_MS) {
    logPrintf("OTA Pull: throttled, auto-retry in %lus\n",
              (unsigned long)((OTA_RECHECK_MIN_MS -
                              (millis() - lastOtaCheckMs)) /
                             1000UL));
    while (millis() - lastOtaCheckMs < OTA_RECHECK_MIN_MS) {
      unsigned long remainMs = OTA_RECHECK_MIN_MS - (millis() - lastOtaCheckMs);
      setOtaPullStatus(("waiting " + String(remainMs / 1000 + 1) +
                        "s before retry (auto)").c_str());
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
  lastOtaCheckMs = millis();

  logPrintf("OTA Pull: checking %s\n", OTA_PULL_URL);
  logPrintf("OTA Pull: heap free=%lu maxAlloc=%lu\n",
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  logPrintf("OTA Pull: staIP=%s gw=%s wifiStatus=%d\n",
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(), (int)WiFi.status());

  OtaHeapGuard heapGuard;

  char fwUrl[256] = "";
  char newVer[32] = "";
  {
    // Everything allocated by the manifest phase (host, payload, JsonDocument,
    // version Strings) lives in this block and is destroyed before the
    // download starts. The TLS handshake needs ~34,816 contiguous bytes (two
    // 17,408-byte record buffers); observed largest free block was 34,804 â€”
    // a few hundred bytes of manifest Strings left between the freed TLS
    // blocks split the heap and starved the handshake by 12 bytes.
    char host[192];
    snprintf(host, sizeof(host), "%s", OTA_PULL_URL);
    {
      char *proto = strstr(host, "://");
      if (proto) memmove(host, proto + 3, strlen(proto + 3) + 1);
      char *slash = strchr(host, '/');
      if (slash) *slash = 0;
    }

    bool dnsOk = false;
    IPAddress resolvedIp;
    if (WiFi.hostByName(host, resolvedIp)) {
      logPrintf("OTA Pull: DNS ok %s -> %s\n", host,
                resolvedIp.toString().c_str());
      dnsOk = true;
    } else {
      logPrintf("OTA Pull: DNS FAILED for %s\n", host);
    }

    String payload;
    int httpCode = 0;
    bool beginFailed = false;

    for (int attempt = 0; attempt < 3; attempt++) {
      WiFiClient *client = newHttpClient(OTA_PULL_URL);

      {
        HTTPClient http;
        if (!http.begin(*client, OTA_PULL_URL)) {
          beginFailed = true;
        } else {
          http.setTimeout(10000);
          http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
          http.addHeader("Cache-Control", "no-cache");
          // GitHub API gzips responses by default, which the ESP32 cannot decode
          http.addHeader("Accept-Encoding", "identity");

          httpCode = http.GET();
          if (httpCode == HTTP_CODE_OK) {
            payload = http.getString();
            logPrintf("OTA Pull: manifest %d bytes, heap free=%lu\n", payload.length(),
                      (unsigned long)ESP.getFreeHeap());
          } else {
            logPrintf("OTA Pull: attempt %d/%d -> HTTP %d (%s)\n", attempt + 1, 3,
                      httpCode, HTTPClient::errorToString(httpCode).c_str());
            logTlsError(OTA_PULL_URL, client);
          }
          http.end();
        }
      }

      delete client;

      if (beginFailed) {
        logPrintf("OTA Pull: begin failed\n");
        String diag = " dns=" + String(dnsOk ? "ok" : "fail");
        setOtaPullStatus(("error: connection begin failed" + diag).c_str());
        return;
      }
      if (httpCode == HTTP_CODE_OK) break;
      if (attempt < 2) delay(2000);
    }

    if (httpCode != HTTP_CODE_OK) {
      String diag = " dns=" + String(dnsOk ? "ok" : "fail") +
                    " heap=" + String(ESP.getFreeHeap()) +
                    " max=" + String(ESP.getMaxAllocHeap());
      setOtaPullStatus(("error: HTTP " + String(httpCode) + " (" +
                        HTTPClient::errorToString(httpCode) + ")" + diag).c_str());
      return;
    }

    String latestVersion;
    String firmwareUrl;
    {
      // JsonDocument scoped: freed right after the manifest is parsed.
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        logPrintf("OTA Pull: JSON parse error: %s\n", err.c_str());
        setOtaPullStatus("error: invalid manifest JSON");
        return;
      }

      latestVersion = doc["version"] | doc["tag_name"] | "";
      firmwareUrl = doc["firmware_url"] | "";

      if (firmwareUrl.length() == 0) {
        JsonArray assets = doc["assets"].as<JsonArray>();
        for (JsonObject asset : assets) {
          if (String(asset["name"] | "").endsWith(".bin")) {
            firmwareUrl = asset["browser_download_url"] | "";
            break;
          }
        }
      }
    }
    payload = String();   // drop the manifest body buffer now that it is parsed

    if (latestVersion.length() == 0 || firmwareUrl.length() == 0) {
      logPrintf("OTA Pull: invalid manifest (missing version/firmware_url)\n");
      setOtaPullStatus("error: manifest missing version or firmware url");
      return;
    }

    char curVer[32];
    snprintf(curVer, sizeof(curVer), "%s", effectiveVersion());
    if (latestVersion.startsWith("v") || latestVersion.startsWith("V"))
      latestVersion = latestVersion.substring(1);

    // Numeric compare, not string equality: "1.3.10" is newer than "1.3.9",
    // and "1.3.8" vs "1.3.8.0" is the same release rather than a new one (the
    // old string compare re-flashed it forever). The optional 'v' prefix is
    // handled by versionCmp().
    int vcmp = versionCmp(latestVersion.c_str(), curVer);
    logPrintf("OTA Pull: latest=%s current=%s (build v%s%s)\n",
              latestVersion.c_str(), curVer, FW_VERSION,
              VERSION_OVERRIDE[0] ? ", override active" : "");

    if (vcmp == 0) {
      logPrintf("OTA Pull: already up-to-date\n");
      setOtaPullStatus((String("up-to-date (v") + curVer + ")").c_str());
      return;
    }

    if (vcmp < 0)
      // Going backwards stays available on purpose (bench reflashing of an
      // older build); it is simply never silent.
      logPrintf("OTA Pull: downgrade v%s -> v%s, downloading\n", curVer,
                latestVersion.c_str());
    else
      logPrintf("OTA Pull: new firmware v%s available, downloading\n", latestVersion.c_str());
    setOtaPullStatus(((vcmp > 0 ? String("updating to v") : String("downgrading to v")) + latestVersion).c_str());
    snprintf(fwUrl, sizeof(fwUrl), "%s", firmwareUrl.c_str());
    snprintf(newVer, sizeof(newVer), "%s", latestVersion.c_str());
  }
  // All manifest-phase heap allocations are gone and the freed TLS region has
  // coalesced. Download with a compacted heap.
  performFirmwareUpdate(fwUrl, newVer);
}

void performFirmwareUpdate(const char *firmwareUrl, const char *newVersion) {
  logPrintf("OTA Pull: downloading %s\n", firmwareUrl);
  logPrintf("OTA Pull: heap free=%lu maxAlloc=%lu\n",
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  otaUpdateInProgress = true;
  otaUpdateSuccess = false;
  pendingOtaScreen = true;

  // --- signature gate (SECURITY plan 2) -----------------------------------
  // The image is only activated when a detached signature (<url>.sig) verifies
  // against the public key compiled into this unit (include/ota_pubkey.h, see
  // scripts/ota_sign.py). A missing signature is a refusal, not a pass. The
  // hash has to cover every byte exactly once, so the byte-range resume below
  // is switched off when signing is enforced.
#ifdef OTA_ALLOW_UNSIGNED
  const bool requireSig = false;
  logPrintf("OTA Pull: WARNING - signature check OFF (OTA_ALLOW_UNSIGNED bench build)\n");
#else
  const bool requireSig = true;
#endif
  static uint8_t sigBuf[OTA_SIG_MAX_LEN];
  size_t sigLen = 0;
  if (otaSessionPoisoned) {
    logPrintf("OTA Pull: a previous OTA was reset mid-write - no further OTA until reboot\n");
    setOtaPullStatus("error: OTA locked until reboot");
    otaUpdateInProgress = false;
    pendingOtaScreen = false;
    forceFullRedraw = true;
    return;
  }
  if (requireSig) {
    if (!otaSigningAvailable()) {
      logPrintf("OTA Pull: no signing key compiled in - update refused\n");
      setOtaPullStatus("error: no signing key compiled in");
      otaUpdateInProgress = false;
      pendingOtaScreen = false;
      forceFullRedraw = true;
      return;
    }
    sigLen = fetchSignatureAsset(firmwareUrl, sigBuf, sizeof(sigBuf));
    if (sigLen == 0) {
      logPrintf("OTA Pull: no signature asset for %s - update refused\n",
                firmwareUrl);
      setOtaPullStatus("error: signature not found - update refused");
      otaUpdateInProgress = false;
      pendingOtaScreen = false;
      forceFullRedraw = true;
      return;
    }
  }

  // Each attempt re-establishes a fresh TLS connection. When the link stalls
  // mid-image the pull resumes from the last byte via an HTTP Range request -
  // unless a signature is required, where a resumed range would skip bytes the
  // hash has to cover, so the attempt restarts the whole image instead.
  const unsigned long READ_STALL_MS = 10000; // no data this long -> reconnect
  const int MAX_OTA_ATTEMPTS = 5;

  int totalSize = 0;        // firmware Content-Length (from the first 200)
  size_t written = 0;       // bytes flashed so far (across resumes)
  bool updateOpen = false;
  bool stalled = false;

  otaPullDownloading = true;   // web loop yields radio + heap to this download

  // Update.begin allocates its 4KB flash buffer. Called right after the TLS
  // handshake, the heap is at its most fragmented (mbedTLS chunks are still
  // being reaped), and a single transient failure must not kill the pull.
  auto openFlash = [](size_t sz) -> bool {
    for (int b = 0; b < 3; b++) {
      if (otaFlashOpen(sz)) return true;
      logPrintf("OTA Pull: Update.begin failed (try %d/3) heap=%lu maxAlloc=%lu\n",
                b + 1, (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
      if (!heap_caps_check_integrity_all(true))
        logPrintf("OTA Pull: HEAP CORRUPTED\n");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
  };

  for (int attempt = 0; attempt < MAX_OTA_ATTEMPTS; attempt++) {
    // Exponential backoff between attempts: a weak link or a rate-limited CDN
    // needs time to recover, and back-to-back TLS reconnects were hammering a
    // socket still closing out (and OOMing the handshake under fragmentation).
    if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(500L + (1UL << attempt) * 1000L));

    WiFiClient *client = newHttpClient(firmwareUrl);

    {
      HTTPClient http;
      if (!http.begin(*client, firmwareUrl)) {
        logPrintf("OTA Pull: begin failed (attempt %d)\n", attempt + 1);
        setOtaPullStatus("error: update begin failed");
        delete client;
        continue;
      }

      http.setTimeout(15000);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Accept-Encoding", "identity");

      // Resume from the last byte flashed. HTTPClient carries custom headers
      // through redirects, so the Range request survives the CDN redirect.
      if (written > 0)
        http.addHeader("Range", String("bytes=") + String(written) + "-");

      int httpCode = http.GET();

      // First (full) GET: read Content-Length and open the flash write.
      if (!updateOpen) {
        if (httpCode != HTTP_CODE_OK) {
          logPrintf("OTA Pull: HTTP %d (%s) (attempt %d/%d) heap=%lu maxAlloc=%lu\n",
                    httpCode, HTTPClient::errorToString(httpCode).c_str(),
                    attempt + 1, MAX_OTA_ATTEMPTS,
                    (unsigned long)ESP.getFreeHeap(),
                    (unsigned long)ESP.getMaxAllocHeap());
          // -1 is a connect-level failure (TCP refused / DNS / TLS drop): the
          // link or the CDN is temporarily unreachable, so tell the user to
          // retry later instead of implying the file is broken.
          setOtaPullStatus(httpCode == -1
                               ? "error: connection to update server failed"
                               : (("error: download HTTP " + String(httpCode)).c_str()));
          http.end();
          delete client;
          continue;
        }
        totalSize = http.getSize();
        // Update.end(true) re-sizes the image to whatever bytes arrived, so
        // flashing without a known Content-Length used to mark a truncated
        // download bootable and reboot into corrupt firmware ("invalid segment
        // length" / "Could Not Activate The Firmware"). Refuse it outright.
        if (totalSize <= 0) {
          logPrintf("OTA Pull: server sent no Content-Length, refusing\n");
          setOtaPullStatus("error: missing content-length");
          http.end();
          delete client;
          break;
        }
        if (!heap_caps_check_integrity_all(true)) {
          logPrintf("OTA Pull: HEAP CORRUPTED before Update.begin\n");
          setOtaPullStatus("error: heap corrupt");
          http.end();
          delete client;
          break;
        }
        if (!openFlash((size_t)totalSize)) {
          setOtaPullStatus("error: update.begin failed");
          http.end();
          delete client;
          break;
        }
        otaImageHashBegin();
        updateOpen = true;
        logPrintf("OTA Pull: flash open, %lu bytes, heap=%lu maxAlloc=%lu\n",
                  (unsigned long)totalSize, (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
      } else if (httpCode == HTTP_CODE_OK) {
        // Server ignored the Range header and re-sent the whole body. The only
        // safe response is to restart the flash write and stream from zero.
        otaFlashAbort();
        if (!openFlash((size_t)totalSize)) {
          setOtaPullStatus("error: update.begin failed");
          http.end();
          delete client;
          break;
        }
        written = 0;
        otaImageHashBegin();
      } else if (httpCode != HTTP_CODE_PARTIAL_CONTENT) {
        logPrintf("OTA Pull: resume HTTP %d (%s)\n", httpCode,
                  HTTPClient::errorToString(httpCode).c_str());
        // Transient failure (TLS handshake, refused socket). Try again with a
        // fresh connection rather than giving up the whole update.
        setOtaPullStatus("error: download resume failed");
        http.end();
        delete client;
        continue;
      }

      unsigned long lastReadMs = millis();
      while (http.connected() && written < (size_t)totalSize) {
        size_t available = client->available();
        if (available) {
          uint8_t buf[1024];
          size_t n = client->readBytes(buf, min(available, sizeof(buf)));
          if (n > 0) {
            size_t w = Update.write(buf, n);
            if (w != n) {
              logPrintf("OTA Pull: write error\n");
              stalled = true;
              break;
            }
            written += w;
            lastReadMs = millis();
            updateOTAProgress(written, totalSize);
            if (requireSig && !otaImageHashUpdate(buf, w)) {
              // The hash has to cover exactly the bytes sitting in the slot. If
              // it breaks there is no honest way to continue - the restart path
              // re-begins the hash at byte 0.
              logPrintf("OTA Pull: hash update failed at %zu bytes\n", written);
              stalled = true;
              break;
            }
          }
        } else {
          if (!client->connected() || millis() - lastReadMs > READ_STALL_MS) {
            logPrintf("OTA Pull: stalled at %zu/%d bytes (attempt %d/%d)\n",
                      written, totalSize, attempt + 1, MAX_OTA_ATTEMPTS);
            stalled = true;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(5));
        }
      }

      if (written >= (size_t)totalSize) stalled = false; // finished, not stalled
      http.end();
    }
    delete client;

    if (!stalled) break;
    stalled = false;
    if (requireSig && updateOpen) {
      // Signed images are never resumed: the signature covers the whole file,
      // so bytes skipped by a Range request would never reach the hash. Throw
      // the partial slot away and stream the image again from byte 0.
      logPrintf("OTA Pull: signed mode - partial image discarded, restarting from byte 0\n");
      otaFlashAbort();
      updateOpen = false;
      written = 0;
      totalSize = 0;
    } else if (written < (size_t)totalSize) {
      // Unsigned (bench) builds keep the byte-range resume behaviour.
      logPrintf("OTA Pull: reconnecting to resume at %lu/%lu (heap=%lu maxAlloc=%lu)\n",
                (unsigned long)written, (unsigned long)totalSize,
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
    }
  }

  otaPullDownloading = false;   // resume web serving

  // The signature is checked here, before Update.end(true): end() is the call
  // that makes the new slot bootable, so nothing may reach it unless the image
  // is complete AND (when signing is enforced) signed by the key compiled into
  // this unit.
  bool accepted = false;
  if (updateOpen && written >= (size_t)totalSize) {
    if (!requireSig) {
      accepted = true;  // bench build with OTA_ALLOW_UNSIGNED only
    } else {
      uint8_t digest[32];
      int pkErr = 0;
      if (!otaImageHashFinish(digest)) {
        logPrintf("OTA Pull: image hash could not be finalized\n");
        setOtaPullStatus("error: image hash failed");
      } else if (otaVerifyImage(digest, newVersion, sigBuf, sigLen, &pkErr)) {
        logPrintf("OTA Pull: signature VALID for v%s\n", newVersion);
        accepted = true;
      } else {
        logPrintf("OTA Pull: signature INVALID (mbedTLS -0x%x) - v%s NOT flashed\n",
                  (unsigned)(-pkErr), newVersion);
        setOtaPullStatus("error: signature invalid - update refused");
      }
    }
  }

  if (updateOpen) {
    if (accepted && written >= (size_t)totalSize && Update.end(true)) {
      otaFlashClose();
      logPrintf("OTA Pull: success %zu bytes\n", written);
      otaUpdateSuccess = true;
      otaProgressTarget = 258;
      // Nothing is recorded about the version here. The manifest's claim used
      // to be written to NVS and then reported back as this device's identity,
      // which let a unit end up "up-to-date" with an image it never actually
      // ran (and let a config restore set the version). Identity now comes
      // from the image itself: after the reboot, FW_VERSION of the new build
      // is what the device is, and bootinfo logs the version it replaced.
      logPrintf("OTA Pull: manifest claimed v%s - the rebooted image reports its own build version\n",
                newVersion);
      // The display task animates the bar to 100% and calls ESP.restart().
      // Fall back to rebooting here so a freshly-flashed image always boots,
      // even if the UI thread is wedged after the update.
      unsigned long fillStart = millis();
      while (millis() - fillStart < 6000) vTaskDelay(pdMS_TO_TICKS(100));
      logPrintf("OTA Pull: rebooting\n");
      bootinfo_tag_reboot("ota-pull");
      ESP.restart();
    } else if (accepted) {
      Update.printError(Serial);
      setOtaPullStatus("error: update.end failed");
      logPrintf("OTA Pull: end failed after %zu bytes\n", written);
    } else if (written >= (size_t)totalSize) {
      logPrintf("OTA Pull: %lu bytes in the slot but the image was not activated\n",
                (unsigned long)totalSize);
    } else {
      logPrintf("OTA Pull: incomplete download (%zu/%lu bytes), discarding\n",
                written, (unsigned long)totalSize);
      setOtaPullStatus("error: download incomplete");
    }
    otaFlashAbort();  // no-op when the session was already closed by end()/abort
  }

  if (!otaUpdateSuccess) {
    otaUpdateInProgress = false;
    forceFullRedraw = true;
  }
}

// Config page HTML is embedded pre-gzipped (generated by scripts/gzip_webui.py
// from the raw literal that used to live here).
#include "webui_html_gz.h"

void webServerTask(void *pvParameters) {
  otaStatusMutex = xSemaphoreCreateMutex();
  weatherFetchMutex = xSemaphoreCreateMutex();
  WiFi.mode(WIFI_AP_STA);
  // Take the reconnect away from the WiFi core: with its default auto-reconnect
  // a fresh attempt is always armed on the network that just failed, which is
  // exactly the state in which IDF 5.5 refuses a new STA config. The search
  // state machine below is the only thing that arms a connect.
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(wifiStaEventHandler);
  // Fixed at the module maximum - there is no WIFI_TX_POWER_DBM any more. The
  // value is in 0.25 dBm steps (WIFI_POWER_19_5dBm = 78 = 19.5 dBm), which is
  // the ceiling for the classic ESP32: esp_wifi_set_max_tx_power() accepts 8-78
  // on this chip, and the ESP32-WROOM-32 datasheet quotes 19.5 dBm typical for
  // 802.11b (18.0 dBm for OFDM/HT20 - the PHY backs the PA off by rate on its
  // own). The old code cast the configured dBm figure straight into the enum, so
  // a "20 dBm" setting selected raw value 20 = WIFI_POWER_5dBm and the radio ran
  // at 5 dBm while the log claimed 20 - a 16x power loss that also weakens probe
  // and ACK power, i.e. the networks the unit could not see at all.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  logPrintf("WiFi TX power: 19.5 dBm (raw 78, module maximum)\n");
  // softAP() returns false when the passphrase is rejected (1-7 characters),
  // which would otherwise leave the config portal unreachable in silence.
  if (!WiFi.softAP("Dashboard_Config", AP_PASSWORD)) {
    logPrintf("AP: Dashboard_Config FAILED TO START - AP_PASSWORD must be empty or 8-63 chars\n");
  } else {
    logPrintf("AP: Dashboard_Config\n");
    logPrintf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  }

  delay(100);

  // WiFi networks are only recorded here; the actual STA connect attempts run
  // as a non-blocking state machine inside the task loop below, AFTER the
  // config server is up, so the config page is reachable via the AP within
  // ~1s of boot even while the ESP keeps trying to join a LAN network.
  const int MAX_WIFI_NETS = 5;
  struct WifiNetwork { const char *ssid; const char *pass; };
  WifiNetwork wifiNets[MAX_WIFI_NETS];
  int wifiNetCount = 0;

  // Built at the start of every search cycle rather than once at task start: a
  // network added in the Web UI after boot used to stay invisible to the search
  // until the next reboot, which reads as "it never connects to the backups".
  // Entries are pointers into the config globals, so editing an existing slot
  // was already picked up - the count was the stale half. Only called at cycle
  // boundaries, where staNetIdx restarts at 0, so indices never shift mid-cycle.
  auto buildWifiList = [&]() {
    wifiNetCount = 0;
    wifiNets[wifiNetCount++] = {WIFI_SSID, WIFI_PASSWORD};
    if (WIFI_SSID_1[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_1, WIFI_PASSWORD_1};
    if (WIFI_SSID_2[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_2, WIFI_PASSWORD_2};
    if (WIFI_SSID_3[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_3, WIFI_PASSWORD_3};
    if (WIFI_SSID_4[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_4, WIFI_PASSWORD_4};
  };
  buildWifiList();

  WiFi.setHostname("dashboard-pp");
  bool staConnected = false;
  int staNetIdx = 0;
  bool staFinalized = false;
  bool staHasConnectedBefore = false;
  unsigned long staDeadline = 0;
  unsigned long staAttemptStart = 0;
  int staRefusals = 0;  // consecutive begin() refusals for the current index
  // Start of the in-flight Wi-Fi scan, for the timeout guard below.
  unsigned long scanStartMs = 0;
  // GAP exists because of the IDF 5.5 config rule above: the machine never
  // installs the next network until the driver has reported the previous
  // attempt over. Values are mirrored into staDbgPhase for the heartbeat.
  enum StaPhase : uint8_t { STA_INIT = 0, STA_BACKOFF = 1, STA_TRY = 2,
                            STA_ATTEMPT = 3, STA_GAP = 4, STA_UP = 5,
                            STA_GIVEUP = 6 };
  StaPhase staPhase = STA_INIT;

  ArduinoOTA.onStart([]() {
    logPrintf("OTA started\n");
    otaUpdateInProgress = true;
    pendingOtaScreen = true;
  });
  ArduinoOTA.onEnd([]() {
    logPrintf("OTA finished\n");
    otaUpdateSuccess = true;
    otaProgressTarget = 258;
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    updateOTAProgress(progress, total);
    logPrintf("OTA: %u%%\n", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logPrintf("OTA error: %d\n", error);
    otaUpdateInProgress = false;
    forceFullRedraw = true;
  });
  ArduinoOTA.begin();
  logPrintf("ArduinoOTA ready\n");

  // Config page PIN enforcement is disabled: the config page and admin API
  // are open. A previously stored PIN in NVS is ignored.

  server.on("/", HTTP_GET, []() {
    // The page itself is always served without PIN: it contains no secrets.
    // The /api/config endpoints (which carry WiFi passwords etc.) still
    // require the PIN when one is set. The HTML is pre-gzipped at build time
    // (~93KB raw -> ~20KB) so slow clients receive the page in a few seconds
    // instead of >10s; every modern browser sends Accept-Encoding: gzip.
    server.sendHeader("Content-Encoding", "gzip");
    server.setContentLength(index_html_gz_len);
    server.send(200, "text/html", "");
    server.sendContent_P((const char *)index_html_gz, index_html_gz_len);
  });
  server.on("/debug", HTTP_GET, []() {
    char buf[420];
    int pos = snprintf(buf, sizeof(buf), "index_html_gz_len = %u\n",
                       (unsigned int)index_html_gz_len);
    pos += snprintf(buf + pos, sizeof(buf) - pos, "First 100 hex: ");
    for (int i = 0; i < 100 && pos < (int)sizeof(buf) - 4; i++)
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", index_html_gz[i]);
    server.send(200, "text/plain", buf);
  });

  server.on("/api/config", HTTP_GET, []() {
    // The full-config serialization needs ~30-40KB of transient heap
    // (JsonDocument + JSON text). Fully-loaded steady state with WiFi is only
    // ~40-50KB free (see the HB log), so when a fetch lands in that band we
    // must ask the display task to drop the big UI sprites (memory-saver) and
    // WAIT until the heap actually recovers instead of guessing at a fixed
    // delay. 503 only if even that is not enough.
    unsigned long memT0 = millis();
    uint32_t fh0 = ESP.getFreeHeap();
    while (ESP.getFreeHeap() < 25000 && (millis() - memT0) < 1000) {
      if (!memSaverRequested) {
        logPrintf("GET /api/config: heap %lu B, requesting memory-saver\n",
                  (unsigned long)ESP.getFreeHeap());
        memSaverRequested = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (ESP.getFreeHeap() < 15000) {
      logPrintf("GET /api/config: heap too low (%lu), skipping serialization\n",
                (unsigned long)ESP.getFreeHeap());
      char buf[96];
      snprintf(buf, sizeof(buf),
               "{\"status\":\"error\",\"error\":\"device memory low\",\"heap\":%lu}",
               (unsigned long)ESP.getFreeHeap());
      server.send(503, "application/json", buf);
      return;
    }
    JsonDocument doc;
    processConfig(1, &doc);
    doc["ambientLightValue"] = ambientLightValue;
    // Runtime-only field (read by the WebUI, ignored by processConfig on POST):
    // the arduino-esp32 core version this firmware was built with.
    doc["core_version"] = ESP.getCoreVersion();
    // Read-only build identity, shown next to the editable version override so
    // the compiled-in truth is always visible in the WebUI. Not a config key:
    // posting it back is ignored (no matching CFG_STR).
    doc["build_version"] = FW_VERSION;
    String out;
    serializeJson(doc, out);
    logPrintf("GW: entry=%lu heap=%lu wait=%lums mem_active=%d keys=%lu over=%d out=%u\n",
              (unsigned long)fh0, (unsigned long)ESP.getFreeHeap(),
              (unsigned long)(millis() - memT0),
              memSaverActive ? 1 : 0,
              (unsigned long)doc.size(), (int)doc.overflowed(),
              (unsigned int)out.length());
    server.send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      logPrintf("Config save rejected: JSON parse error: %s\n", err.c_str());
      server.send(400, "application/json",
                  "{\"status\":\"error\",\"error\":\"JSON parse failed\"}");
      return;
    }

    // A save parses the full config JSON and rewrites ~90 NVS keys; if free
    // heap is tight, drop the UI sprites first, same as the GET handler.
    unsigned long memT0 = millis();
    while (ESP.getFreeHeap() < 45000 && (millis() - memT0) < 3000) {
      if (!memSaverRequested) {
        logPrintf("POST /api/config: heap %lu B, requesting memory-saver\n",
                  (unsigned long)ESP.getFreeHeap());
        memSaverRequested = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    // The parameters are written as one group. Painting is paused for the
    // duration so the display never renders a half-old/half-new set (geometry,
    // offsets and digit counts belong together), and the live panel bus plus the
    // CPU-frequency switch are handed to the display loop, which owns the bus and
    // applies them between frames - never inside a LovyanGFX transaction
    // (issue #10).
    configSaveStartMs = millis();
    configSaveInProgress = true;
    processConfig(2, &doc);
    recalculateDerivedParams();
    configSaveInProgress = false;
    pendingApplyBusConfig = true;
    pendingCpuReeval = true;
    // If the save touched the weather location/city/locale/interval, ask the
    // fetch loop to refresh right away so the widget shows the new city's
    // weather immediately instead of after the next scheduled interval.
    if (!doc["WEATHER_CITY"].isNull() || !doc["WEATHER_LAT"].isNull() ||
        !doc["WEATHER_LON"].isNull() || !doc["WEATHER_REFRESH_MIN"].isNull() ||
        !doc["WEATHER_LOCALE"].isNull()) {
      weatherRefreshRequested = true;
    }
    // CPU frequency: applied by the display loop on the core that draws
    // (processConfigApply), so the clock never changes under an SPI transfer.
    // A save whose NVS writes never reached flash used to answer "ok" anyway.
    // The failing-write count rides along in the response so the Web UI can say
    // the settings are live but not stored, instead of a green "saved" banner
    // over values a reboot will undo.
    char saveResp[192];
    if (cfgNvsWriteErrors)
      snprintf(saveResp, sizeof(saveResp),
               "{\"status\":\"ok\",\"nvsErrors\":%u,\"nvsFailedKeys\":\"%s\",\"nvsAvailable\":%u}",
               (unsigned)cfgNvsWriteErrors, cfgNvsFailedKeys, (unsigned)nvsStatsAvailable);
    else
      snprintf(saveResp, sizeof(saveResp), "{\"status\":\"ok\"}");
    server.send(200, "application/json", saveResp);
    forceFullRedraw = true;
    pendingInvertDisplay = true;
    if (!ENABLE_AUTO_BRIGHTNESS)
      pendingBacklightValue = BACKLIGHT_BRIGHTNESS;
  });

  // What the radio sees, and the entry point for "scan, pick, type the
  // password" network setup. GET is read-only against the cached list; POST
  // only asks the network task to scan (see the scan runner in the task loop).
  server.on("/api/wifi/scan", HTTP_GET, wifiScanGetHandler);
  server.on("/api/wifi/scan", HTTP_POST, wifiScanPostHandler);
  server.on("/api/wifi/join", HTTP_POST, wifiJoinPostHandler);
  server.on("/api/wifi/forget", HTTP_POST, wifiForgetPostHandler);

  server.on("/api/time", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    // Same plausible window the GPS apply path uses (2020-01-01 .. 2100-01-01).
    // An unvalidated value put the clock in 1970 or 2106, which broke the date
    // display and the night-mode window, and made systemTimeToLocal() bail out
    // on its own epoch guard - so the clock read as dead rather than wrong
    // (issue #40).
    if (!doc["timestamp"].is<long long>()) {
      server.send(400, "application/json", "{\"status\":\"bad timestamp\"}");
      return;
    }
    long long epoch = doc["timestamp"].as<long long>();
    if (epoch <= 1577836800LL || epoch >= 4102444800LL) {
      logPrintf("RTC sync rejected: %lld outside 2020..2100\n", epoch);
      server.send(400, "application/json",
                  "{\"status\":\"timestamp out of range (2020..2100)\"}");
      return;
    }
    struct timeval tv;
    tv.tv_sec = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    logPrintf("RTC sync: %lld\n", epoch);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/odo", HTTP_GET, []() {
    JsonDocument doc;
    doc["km"] = odoGet();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Fuel readout. "raw" is the averaged ADC code on GPIO32, "ohm" the sender
  // resistance derived from it (issue #18) and "st" the input state
  // (0 = input disabled, 1 = ok, 2 = open circuit, 3 = shorted) - what the Web UI
  // fuel card shows while calibrating, so a broken wire never looks like a
  // plausible tank level.
  server.on("/api/fuel", HTTP_GET, []() {
    char buf[96];
    float liters = 0.0f;
    int pct = 0;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      liters = g_sensorData.fuelLiters;
      pct = g_sensorData.fuelPercentage;
      xSemaphoreGive(g_stateMutex);
    }
    float ohm = fuelMeasuredOhms > 9999.9f ? 9999.9f : fuelMeasuredOhms;
    snprintf(buf, sizeof(buf),
             "{\"raw\":%d,\"liters\":%.1f,\"pct\":%d,\"ohm\":%.1f,\"st\":%d}",
             rawFuelADC, liters, pct, ohm, (int)fuelInputState);
    server.send(200, "application/json", buf);
  });

  server.on("/api/odo", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc["km"].is<double>()) {
      setOdometerKm(doc["km"].as<double>());
      forceFullRedraw = true;
      JsonDocument resp;
      resp["km"] = odoGet();
      String out;
      serializeJson(resp, out);
      server.send(200, "application/json", out);
    } else {
      server.send(400, "application/json", "{\"status\":\"error\"}");
    }
  });

  // Trip reset (issue #17): the same zeroing the physical button on GPIO25
  // performs. The handler only raises the flag - the sensor task owns the trip
  // state, so it applies the reset on its next tick and the web task never
  // touches trip counters cross-core.
  server.on("/api/trip/reset", HTTP_POST, []() {
    pendingTripReset = true;
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/reboot", HTTP_POST, []() {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    pendingReboot = true;
  });

  server.on("/api/sleep", HTTP_POST, []() {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    pendingSleep = true;
  });

  server.on("/api/reset", HTTP_POST, []() {
    factoryResetConfig();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    logPrintf("Factory reset, rebooting\n");

    pendingReboot = true;
  });

  server.on("/api/ambient", HTTP_GET, []() {
    char buf[40];
    snprintf(buf, sizeof(buf), "{\"raw\":%d}", ambientLightValue);
    server.send(200, "application/json", buf);
  });

  // Live processed sensor values, used by the WebUI calibration "Current"
  // readings (battery voltage + engine temperature). Additive read-only
  // endpoint; values are the calibrated outputs, not raw ADC.
  server.on("/api/sensors", HTTP_GET, []() {
    char buf[96];
    float v = 0.0f, t = 0.0f;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      v = g_sensorData.batteryVoltage;
      t = g_sensorData.engineTemperature;
      xSemaphoreGive(g_stateMutex);
    }
    snprintf(buf, sizeof(buf), "{\"v\":%.2f,\"t\":%.1f}", v, t);
    server.send(200, "application/json", buf);
  });

  server.on("/api/ambient/cal-dark", HTTP_POST, []() {
    LIGHT_SENSOR_DARK_VAL = ambientLightValue;
    bool saved = false;
    { NvsSession s("cfg", false);
      if (s.opened())
        saved = !nvsWriteFailed("LIGHT_DARK", s.nvs.putInt("LIGHT_DARK", LIGHT_SENSOR_DARK_VAL)); }
    char buf[64];
    // The calibration is live in RAM either way; say when it did not reach NVS
    // so the Web UI can warn instead of letting the user assume it persisted.
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"value\":%d}",
             saved ? "ok" : "saved-ram-only", LIGHT_SENSOR_DARK_VAL);
    server.send(200, "application/json", buf);
  });

  server.on("/api/ambient/cal-bright", HTTP_POST, []() {
    LIGHT_SENSOR_BRIGHT_VAL = ambientLightValue;
    bool saved = false;
    { NvsSession s("cfg", false);
      if (s.opened())
        saved = !nvsWriteFailed("LIGHT_BRIGHT", s.nvs.putInt("LIGHT_BRIGHT", LIGHT_SENSOR_BRIGHT_VAL)); }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"value\":%d}",
             saved ? "ok" : "saved-ram-only", LIGHT_SENSOR_BRIGHT_VAL);
    server.send(200, "application/json", buf);
  });

  server.on("/api/ota", HTTP_POST, []() {
    if (otaUpdateSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Update OK\"}");
      delay(100);
      bootinfo_tag_reboot("ota");
      ESP.restart();
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"msg\":\"Update failed\"}");
      otaUpdateInProgress = false;
      forceFullRedraw = true;
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    // Per-upload bookkeeping (issue #28). Only one upload can be in flight and
    // this handler only runs from the web task, so function-scope statics are
    // safe; they are re-armed on UPLOAD_FILE_START.
    static size_t uploadWritten = 0;   // bytes accepted into the OTA slot
    static size_t uploadSeen = 0;      // bytes the client has handed us
    static bool uploadFailed = false;  // set once, ignores the rest of the body
    static uint8_t uploadMagic[2] = {0, 0};  // first bytes: image header check

    if (upload.status == UPLOAD_FILE_START) {
      uploadWritten = 0;
      uploadSeen = 0;
      uploadFailed = false;
      uploadMagic[0] = uploadMagic[1] = 0;
      if (otaSessionPoisoned || otaFlashSessionOpen) {
        // The slot is already being written by a pull (or a reset pull left it
        // in an unknown state). A second writer would corrupt the image.
        logPrintf("OTA web: upload refused - OTA slot busy\n");
        uploadFailed = true;
        return;
      }
      otaUpdateSuccess = false;
      otaUpdateInProgress = true;
      pendingOtaScreen = true;
      logPrintf("OTA web: start %s\n", upload.filename.c_str());
      if (!otaFlashOpen(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        uploadFailed = true;
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadFailed) return;
      uploadSeen += upload.currentSize;
      // UPDATE_SIZE_UNKNOWN means nothing bounds this write, and Update.end(true)
      // marks whatever arrived as bootable - so a too-big or truncated upload
      // used to be activated. Bound it by the real OTA partition and check the
      // image header before activating.
      const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
      size_t slotSize = slot ? slot->size : (size_t)ESP.getFreeSketchSpace();
      if (uploadSeen > slotSize) {
        logPrintf("OTA web: upload bigger than the OTA slot (%zu bytes) - aborted\n",
                  uploadSeen);
        otaFlashAbort();
        uploadFailed = true;
        return;
      }
      if (uploadWritten == 0 && upload.currentSize >= 2) {
        uploadMagic[0] = upload.buf[0];
        uploadMagic[1] = upload.buf[1];
      }
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        logPrintf("OTA web: write failed at %zu bytes - upload aborted\n",
                  uploadWritten);
        otaFlashAbort();
        uploadFailed = true;
        return;
      }
      uploadWritten += upload.currentSize;
      // upload.totalSize is cumulative bytes received so far during upload.
      // Scaling target progress up to max 240 during writing prevents
      // premature reboot (fillW >= 258) before Update.end(true) runs.
      size_t written = Update.progress();
      int targetW = (240L * (long)written) / (long)(written + 300000);
      if (targetW > 240) targetW = 240;
      if (targetW > otaProgressTarget) otaProgressTarget = targetW;
    } else if (upload.status == UPLOAD_FILE_END) {
      if (uploadFailed) {
        logPrintf("OTA web: upload discarded (%zu bytes)\n", uploadWritten);
        otaUpdateInProgress = false;
        forceFullRedraw = true;
        return;
      }
      // Reject a wrong or truncated file before end(true) would make it
      // bootable: ESP image magic, plausible segment count, plausible size, and
      // a byte count that matches what the client actually sent.
      if (uploadWritten != uploadSeen || uploadWritten < 4096 ||
          uploadMagic[0] != ESP_IMAGE_HEADER_MAGIC ||
          uploadMagic[1] > ESP_IMAGE_MAX_SEGMENTS) {
        logPrintf("OTA web: %zu bytes rejected (magic 0x%02X, segments %u) - not activated\n",
                  uploadWritten, uploadMagic[0], uploadMagic[1]);
        otaFlashAbort();
        otaUpdateInProgress = false;
        forceFullRedraw = true;
        return;
      }
      if (Update.end(true)) {
        otaFlashClose();
        logPrintf("OTA web: success %u bytes\n", upload.totalSize);
        otaUpdateSuccess = true;
        otaProgressTarget = 258;
      } else {
        Update.printError(Serial);
        otaFlashAbort();
        otaUpdateInProgress = false;
        forceFullRedraw = true;
      }
    }
  });

  server.on("/api/ota/pull", HTTP_POST, []() {
    if (otaUpdateInProgress || otaPullTaskRunning) {
      char buf[96];
      snprintf(buf, sizeof(buf),
               "{\"status\":\"busy\",\"msg\":\"OTA already in progress%s%s\"}",
               otaPullTaskRunning && !otaUpdateInProgress ? " (pull task running)" : "",
               !otaPullTaskRunning && otaUpdateInProgress ? " (update in progress)" : "");
      server.send(200, "application/json", buf);
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      server.send(200, "application/json", "{\"status\":\"error\",\"msg\":\"Not connected to WiFi\"}");
      return;
    }
    setOtaPullStatus("checking...");
    server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"OTA pull started\"}");
    logPrintf("OTA Pull: triggered from web UI\n");
    startOtaPull(true, false);
  });

  server.on("/api/ota/check", HTTP_GET, []() {
    JsonDocument doc;
    doc["enabled"] = OTA_PULL_ENABLED;
    doc["url"] = OTA_PULL_URL;
    doc["current_version"] = effectiveVersion();
    // Always-visible build truth next to the reported version, so an override
    // (or a mismatch) is obvious from the WebUI and the phone app. Additive:
    // existing consumers keep reading current_version.
    doc["build_version"] = FW_VERSION;
    if (VERSION_OVERRIDE[0]) doc["version_override"] = VERSION_OVERRIDE;
    doc["previous_version"] = bootinfo_previous_version();
    if (otaStatusMutex) xSemaphoreTake(otaStatusMutex, portMAX_DELAY);
    doc["status"] = otaPullStatus;
    doc["status_updated"] = otaPullStatusUpdated;
    if (otaStatusMutex) xSemaphoreGive(otaStatusMutex);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/boot", HTTP_GET, []() {
    // Boot/reboot forensics (Phase 0): reset reason, fast-reboot-storm state,
    // last reboot tag + heap, and the heap watermark since boot. /api/serial
    // keeps the *live* log; /api/boot keeps the *surviving* facts that outlive
    // a crash (the reset reason and the tag written before the reboot).
    server.send(200, "application/json", bootinfo_json());
  });

  server.on("/api/serial", HTTP_GET, []() {
    // Copy the echo ring window into a static scratch buffer (LOG_BUF_SIZE
    // max, plus NUL) so this frequently-polled endpoint never allocates heap.
    static char out[LOG_BUF_SIZE + 1];
    int len = 0;
    // Hold logMux so a writer task can't tear the buffer mid-copy and the
    // tail advance can't race a concurrent wrap. server.send happens AFTER
    // the lock (out is static; the response write may block on the client).
    portENTER_CRITICAL(&logMux);
    {
      int tail = logTail;
      int head = logHead;
      len = (head >= tail) ? (head - tail) : (LOG_BUF_SIZE - tail + head);

      if (len > 0) {
        if (head >= tail) {
          memcpy(out, &logBuf[tail], len);
        } else {
          int n1 = LOG_BUF_SIZE - tail;
          memcpy(out, &logBuf[tail], n1);
          memcpy(out + n1, logBuf, head);
        }
        logTail = head;
      }
    }
    portEXIT_CRITICAL(&logMux);
    out[len] = 0;
    server.send(200, "text/plain", out);
  });

  server.on("/api/perf", HTTP_GET, []() {
    JsonDocument doc;

    doc["cpu_freq"] = getCpuFrequencyMhz();
    doc["cpu_temp"] = temperatureRead();
    doc["cpu_dynamic"] = ENABLE_DYNAMIC_CPU;
    doc["cpu_usage"] = cpuUsagePct;
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();

    // Stack headroom in bytes (issue #27) - the smallest free stack each of our
    // tasks has had since boot. This is deliberately measurement only: the GPS
    // task stack and its 1 KB parse buffer stay exactly as they are, and these
    // numbers are the evidence for any future change. ESP-IDF's
    // uxTaskGetStackHighWaterMark() reports bytes (vanilla FreeRTOS reports
    // words), and a 0/NULL handle simply means the task has not started yet.
    JsonObject stackFree = doc["stack_free"].to<JsonObject>();
    stackFree["SensorTaskCore1"] = uxTaskGetStackHighWaterMark(sensorTaskHandle);
    stackFree["GpsTaskCore0"] = uxTaskGetStackHighWaterMark(gpsTaskHandle);
    stackFree["WebTaskCore0"] = uxTaskGetStackHighWaterMark(webTaskHandle);
    doc["mem_saver"] = memSaverActive ? 1 : 0;
    doc["heap_size"] = ESP.getHeapSize();
    doc["psram_size"] = ESP.getPsramSize();
    doc["psram_free"] = ESP.getFreePsram();
    doc["flash_total"] = ESP.getFlashChipSize();
    doc["flash_free"] = ESP.getFreeSketchSpace();

    {
      JsonArray parts = doc["partitions"].to<JsonArray>();
      const char *knownLabels[] = {"nvs","otadata","app0","app1","spiffs"};
      esp_partition_type_t knownTypes[] = {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_TYPE_DATA,ESP_PARTITION_TYPE_APP,ESP_PARTITION_TYPE_APP,ESP_PARTITION_TYPE_DATA};
      esp_partition_subtype_t knownSubtypes[] = {ESP_PARTITION_SUBTYPE_DATA_NVS,ESP_PARTITION_SUBTYPE_DATA_OTA,ESP_PARTITION_SUBTYPE_APP_OTA_0,ESP_PARTITION_SUBTYPE_APP_OTA_1,ESP_PARTITION_SUBTYPE_DATA_SPIFFS};
      uint32_t flashUsed = 0;
      const esp_partition_t *running = esp_ota_get_running_partition();
      for (int i = 0; i < 5; i++) {
        const esp_partition_t *p = esp_partition_find_first(knownTypes[i], knownSubtypes[i], knownLabels[i]);
        if (p) {
          JsonObject part = parts.add<JsonObject>();
          part["label"] = p->label;
          part["type"] = (int)p->type;
          part["subtype"] = (int)p->subtype;
          part["size"] = p->size;
          part["addr"] = p->address;
          if (strcmp(p->label, "spiffs") == 0) {
            uint32_t u = LittleFS.usedBytes();
            part["used"] = u;
            flashUsed += u;
          } else if (p == running) {
            part["used"] = p->size;
            flashUsed += p->size;
          } else {
            part["used"] = 0;
          }
        }
      }
      doc["flash_used"] = flashUsed;
    }
    // Settings-storage budget, in ENTRIES rather than bytes. This is the unit
    // that actually runs out: the old 20 KB partition held ~630 entries, the
    // factory settings alone occupy ~420 of them, and one lost WiFi save costs
    // three. Showing bytes here would have looked healthy right up to the
    // failure (throttled read - see refreshNvsStats()).
    refreshNvsStats(5000);
    doc["nvs_bytes"] = (uint32_t)nvsStatsBytes;
    doc["nvs_entries_used"] = (uint32_t)nvsStatsUsed;
    doc["nvs_entries_available"] = (uint32_t)nvsStatsAvailable;
    doc["nvs_entries_total"] = (uint32_t)nvsStatsTotal;
    doc["fps_current"] = currentMeasuredFps;
    doc["fps_average"] = currentAverageFps;
    doc["fps_target"] = TARGET_FPS;
    doc["refresh_ms"] = (unsigned long)DISPLAY_REFRESH_MS;
    doc["spi_speed"] = SPI_BUS_SPEED;
    doc["wifi_clients"] = WiFi.softAPgetStationNum();
    String lanIp = "";
    if (WiFi.status() == WL_CONNECTED) {
      lanIp = WiFi.localIP().toString();
      // Only set if it's a valid LAN IP (not the AP address)
      if (lanIp.length() > 0 && !lanIp.equals("192.168.4.1")) {
        doc["lan_ip"] = lanIp;
      } else {
        doc["lan_ip"] = "";
      }
    }
    doc["ambient_light"] = ambientLightValue;
    doc["resolution"] = String(DISPLAY_WIDTH) + "x" + String(DISPLAY_HEIGHT);

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/health", HTTP_GET, []() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"heap\":%lu,\"maxalloc\":%lu,\"minheap\":%lu,\"mem_saver\":%d,"
             "\"uptime\":%lu}",
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMaxAllocHeap(),
             (unsigned long)ESP.getMinFreeHeap(),
             memSaverActive ? 1 : 0,
             millis() / 1000);
    server.send(200, "application/json", buf);
  });

  server.begin();
  logPrintf("Web server started: heap=%lu B, maxalloc=%lu B\n",
            (unsigned long)ESP.getFreeHeap(),
            (unsigned long)ESP.getMaxAllocHeap());

  unsigned long webStartMs = millis();
  // NTP settle window: 0 = not waiting (see the check inside the loop).
  static unsigned long ntpWaitStart = 0;

  for (;;) {
    // OTA-pull state guard: a pull task that died without cleanup (or was
    // never able to start) must not leave the web loop paused or the
    // "busy" latch set forever. Also hard-limit how long a pull may run.
    static unsigned long pullStartedAt = 0;
    if (otaPullTaskRunning) {
      if (pullStartedAt == 0) pullStartedAt = millis();
    } else {
      pullStartedAt = 0;
    }
    if (otaPullDownloading && !otaPullTaskRunning && !otaUpdateInProgress)
      otaPullDownloading = false;
    if (otaPullTaskRunning && pullStartedAt &&
        millis() - pullStartedAt > 900000UL) {
      // The pull task has run 15 minutes. Freeing the web loop is right, but
      // clearing the busy latch while a flash session was still open let a
      // second pull call Update.begin() on top of the first, and both wrote
      // into the same slot (issue #24). With a session open the slot state is
      // unknown, so OTA stays locked until the unit reboots.
      if (otaFlashSessionOpen) {
        logPrintf("OTA Pull: task overrun with a flash session open - OTA locked until reboot\n");
        otaSessionPoisoned = true;
      } else {
        logPrintf("OTA Pull: task overrun, resetting OTA state\n");
        otaUpdateInProgress = false;
      }
      otaPullTaskRunning = false;
      otaPullDownloading = false;
      pullStartedAt = 0;
      forceFullRedraw = true;
    }

    // NTP settle check (issue #43): configTime() starts lwIP's SNTP client, and
    // the clock jumps forward whenever the reply lands. Polling one `time()`
    // per iteration keeps the same 5 s window and the same two log lines the
    // blocking version printed, without ever holding the server off for a reply
    // that arrives on its own.
    if (ntpWaitStart) {
      time_t ntpNow = 0;
      struct tm ntpTm = {0};
      time(&ntpNow);
      localtime_r(&ntpNow, &ntpTm);
      if (ntpTm.tm_year >= (2024 - 1900)) {
        logPrintf("NTP time sync OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                  ntpTm.tm_year + 1900, ntpTm.tm_mon + 1, ntpTm.tm_mday,
                  ntpTm.tm_hour, ntpTm.tm_min, ntpTm.tm_sec);
        ntpWaitStart = 0;
      } else if (millis() - ntpWaitStart >= 5000UL) {
        logPrintf("NTP time sync failed after 5 s - clock left as-is (GPS fix or /api/time can still set it)\n");
        ntpWaitStart = 0;
      }
    }

    // Heartbeat is this loop's own responsibility (webLoopCount). It is bumped
    // AFTER the serve/OTA calls so a server that is wedged but still spinning
    // looks stalled to the main-loop watchdog instead of healthy.
    if (!otaPullDownloading) {
      unsigned long serveStart = millis();
      server.handleClient();
      ArduinoOTA.handle();
      unsigned long serveMs = millis() - serveStart;
      // Self-heal a hung listener: the loop thread is alive but handleClient
      // never completes a request (e.g. a poisoned listen socket after a
      // half-open TCP flood). Rebinding the listener clears that state without
      // a full reboot. The budget must be generous: a slow phone client (poor
      // RSSI) can legitimately take ~10s to receive the multi-KB config page,
      // and killing the send mid-transfer makes the browser fetch fail and the
      // settings appear "missing". A genuinely blocked serve is caught by the
      // webLoopCount heartbeat, whose stall triggers a reboot anyway.
      if (serveMs > 30000) {
        logPrintf("WEB SERVER HUNG: handleClient blocked %lums, restarting listen socket\n",
                  (unsigned long)serveMs);
        server.close();
        delay(50);
        server.begin();
        forceFullRedraw = true;
      }
      webLoopCount++;
    }
    // While an OTA pull streams the firmware, stop serving the browser: fast
    // polls RAID the lwIP TX pbuf pool (errno 11 "No more processes" spam),
    // block this loop for seconds on a slow client, and fragment the heap
    // below what the next mbedTLS handshake needs (SSL -32512). The display
    // shows the progress; the pollers just see the connection stall until the
    // flash write ends.
    else { webLoopCount++; vTaskDelay(pdMS_TO_TICKS(20)); }

    // ---------------------------------------------------------------------
    // Wi-Fi scan runner. Deliberately here rather than in the request handler:
    // scanNetworks(async=true) returns straight away and the radio reports back
    // through scanComplete(), so a multi-second full-band scan cannot stall
    // server.handleClient() and trip the "web server hung" self-heal. A scan is
    // requested by the Web UI button, and by the search machine itself when a
    // cycle fails (nothing is connected at that point, so hopping costs
    // nothing).
    if (s_scanRequested && !s_scanBusy && !staDrvAttemptOpen) {
      s_scanRequested = false;
      // show_hidden stays false: a hidden SSID cannot be picked from a list, so
      // it stays typed-entry territory.
      int16_t startRes = WiFi.scanNetworks(true, false, false);
      if (startRes == WIFI_SCAN_RUNNING) {
        s_scanBusy = true;
        scanStartMs = millis();
        logPrintf("WiFi scan started%s\n",
                  staConnected ? " (link up: it may drop briefly)" : "");
      } else {
        logPrintf("WiFi scan refused by the driver (%d)\n", (int)startRes);
      }
    }
    if (s_scanBusy) {
      int16_t found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) {
        // IDF's own scan of 1-13 at ~300ms/channel finishes well inside this;
        // the guard only exists so a lost SCAN_DONE event cannot wedge the
        // search machine, which holds still while a scan is in flight.
        if (millis() - scanStartMs > 20000) {
          WiFi.scanDelete();
          s_scanBusy = false;
          logPrintf("WiFi scan timed out\n");
        }
      } else if (found == WIFI_SCAN_FAILED) {
        s_scanBusy = false;
        logPrintf("WiFi scan failed\n");
      } else {
        // One String reused for every result: its buffer grows once instead of
        // once per beacon (AGENTS §14).
        String ssidStr;
        s_scanCount = 0;
        for (int i = 0; i < found; i++) {
          uint8_t auth = 0;
          uint8_t *bssid = NULL;
          int32_t rssi = 0, ch = 0;
          if (!WiFi.getNetworkInfo((uint8_t)i, ssidStr, auth, rssi, bssid, ch))
            continue;
          wifiScanAdd(ssidStr.c_str(), rssi, (uint8_t)ch, auth);
        }
        WiFi.scanDelete();
        wifiScanSortBySignal();
        s_scanBusy = false;
        s_scanDoneMs = millis();
        logWifiScanSummary();
      }
    }

    // ---------------------------------------------------------------------
    // A network was just picked or forgotten in the Web UI. Restart the search
    // immediately and aim it at that slot rather than waiting for the current
    // cycle to walk around the list. Same shape as the link-loss re-arm in the
    // STA_UP branch below: end the old link through the driver, let GAP wait for
    // the event, then try.
    if (s_wifiSearchRequested) {
      s_wifiSearchRequested = false;
      buildWifiList();
      int configured = 0;
      for (int i = 0; i < wifiNetCount; i++)
        if (strlen(wifiNets[i].ssid) > 0) configured++;
      if (configured == 0) {
        // Everything was forgotten: hand it back to the init branch, which says
        // plainly that there is nothing to search for.
        s_joinedSsid[0] = 0;
        staConnected = false;
        if (staPhase == STA_ATTEMPT || staPhase == STA_UP) WiFi.disconnect(false);
        staFinalized = false;
        staPhase = STA_INIT;
      } else {
      int want = s_wifiSearchIdx;
      if (want < 0 || want >= wifiNetCount || strlen(wifiNets[want].ssid) == 0) want = 0;
      if (staConnected && want == staNetIdx) {
        logPrintf("WiFi: %s is already the connected network\n", wifiNets[staNetIdx].ssid);
      } else {
        if (staConnected)
          logPrintf("WiFi: switching from %s to %s on request\n", wifiNets[staNetIdx].ssid,
                    wifiNets[want].ssid);
        else
          logPrintf("WiFi: searching for %s now\n", wifiNets[want].ssid);
        // End whatever link or attempt is in flight before aiming at the new
        // network, otherwise the driver refuses the config change.
        WiFi.disconnect(false);
        staConnected = false;
        // Re-arms the weather fetch and re-binds mDNS for whatever IP the new
        // network hands out (same reason as the link-loss path).
        staFinalized = false;
        staNetIdx = want;
        staRefusals = 0;
        staPhase = STA_GAP;
        staDeadline = millis() + 1000;
      }
      }
    }

    // ---------------------------------------------------------------------
    // Non-blocking STA search. wifiNets[] is in priority order and index 0 is
    // the network that connected last (see the slot rotation on a join), so every
    // cycle - at boot and after a link loss - offers that one first and a
    // fallback is only ever used when it did not answer. There is no search
    // policy parameter: a full cycle that ends without a join is simply followed
    // by the next one, forever. STA_GIVEUP is only ever reached with nothing
    // configured to search for.
    if (staPhase == STA_INIT) {
      buildWifiList();
      int configuredNets = 0;
      for (int i = 0; i < wifiNetCount; i++) {
        if (strlen(wifiNets[i].ssid) > 0) configuredNets++;
      }
      if (configuredNets == 0) {
        // Nothing to search for: never busy-loop on empty SSID entries.
        staPhase = STA_GIVEUP;
        logPrintf("No WiFi networks configured, using AP only\n");
      } else {
        staPhase = STA_TRY;
      }
    } else if (staPhase == STA_UP) {
      // Link loss is taken from the driver's own DISCONNECTED / LOST_IP event,
      // not from WiFi.status(): status also leaves WL_CONNECTED for failed
      // attempts and for transient DHCP states, which used to re-arm the search
      // on a network that was perfectly fine.
      if (staLinkDropped) {
        staLinkDropped = false;
        logPrintf("STA link lost (reason=%u), re-searching networks\n",
                  (unsigned)staDrvReason);
        s_joinedSsid[0] = 0;
        staConnected = false;
        staNetIdx = 0;  // slot 0 is the network that worked last (see rotation)
        staRefusals = 0;
        // Clear the finalize latch so the post-join block re-runs on the
        // reconnect: re-arms the weather fetch and re-binds mDNS for the
        // freshly-assigned IP (otherwise dashboard-pp.local keeps advertising
        // the stale address). NTP stays one-shot via staHasConnectedBefore.
        staFinalized = false;
        buildWifiList();  // a network may have been added while the link was up
        staPhase = STA_BACKOFF;
        staDeadline = millis() + 500;
      }
    } else if (staPhase != STA_GIVEUP) {
      // esp_wifi_set_config() and begin() are refused while a scan owns the
      // radio, so the search waits for the scan to land rather than burning a
      // refusal counter. A scan is a few seconds; the backoff absorbs it.
      if (s_scanBusy) {
        staDeadline = millis() + 200;  // keep the current phase's budget alive
      } else if (staPhase == STA_BACKOFF) {
        if (millis() >= staDeadline) staPhase = STA_TRY;
      } else if (staPhase == STA_GAP) {
        // Wait for the driver to report the previous attempt over - only then
        // does it accept the next network. The guard keeps a lost event from
        // wedging the search forever.
        if (!staDrvAttemptOpen || millis() >= staDeadline) {
          staPhase = STA_BACKOFF;
          staDeadline = millis() + 150;
        }
      } else if (staPhase == STA_TRY) {
        if (staNetIdx >= wifiNetCount) {
          // A full cycle through every saved network finished without a join.
          // Search again after a short backoff so the radio is not hammered -
          // the window never expires, and the scan summary below is what makes
          // "out of range" distinguishable from "wrong password".
          logPrintf("WiFi cycle failed, searching again\n");
          logWifiScanSummary();
          s_scanRequested = true;  // nothing is connected: safe to look around
          staNetIdx = 0;
          staRefusals = 0;
          buildWifiList();  // pick up networks saved since the last cycle
          staPhase = STA_BACKOFF;
          staDeadline = millis() + 2000;
        } else if (strlen(wifiNets[staNetIdx].ssid) == 0) {
          staNetIdx++;  // empty slot: skip it without burning a backoff
        } else {
          // If the radio just saw this SSID, aim the join at its channel. A
          // whole-band FAST_SCAN costs 1-3s of every attempt window.
          uint8_t pinCh = wifiScanChannel(wifiNets[staNetIdx].ssid);
          char chNote[16];
          chNote[0] = 0;
          if (pinCh) snprintf(chNote, sizeof(chNote), " ch%u", (unsigned)pinCh);
          logPrintf("Trying WiFi[%d]: %s%s\n", staNetIdx, wifiNets[staNetIdx].ssid,
                    chNote);
          staDrvAttemptOpen = true;  // begin() arms it; the events clear it
          staAttemptStart = millis();
          wl_status_t beginRes = WiFi.begin(wifiNets[staNetIdx].ssid,
                                            wifiNets[staNetIdx].pass,
                                            (int32_t)pinCh);
          if (beginRes == WL_CONNECT_FAILED) {
            // The driver refused the config: this network was NOT tried, an
            // attempt was still in flight (ESP_ERR_WIFI_STATE). Retrying the
            // SAME index is deliberate - silently advancing is what used to
            // skip the whole fallback list. The counter stops a wedged driver
            // from blocking the search on one entry forever.
            WiFi.disconnect(false);  // drop whatever was still connecting
            staRefusals++;
            logPrintf("WiFi[%d] begin() refused by the driver (x%d)\n",
                      staNetIdx, staRefusals);
            if (staRefusals >= 3) {
              logPrintf("WiFi[%d] %s skipped: driver refused 3x\n", staNetIdx,
                        wifiNets[staNetIdx].ssid);
              staNetIdx++;
              staRefusals = 0;
              staPhase = STA_BACKOFF;
              staDeadline = millis() + 3000;
            } else {
              staPhase = STA_GAP;
              // Give the driver room to report the attempt over; the shorter
              // 1500ms budget expired before the event, which is how the next
              // begin() got refused (ESP_ERR_WIFI_STATE) and the refusal
              // counter climbed on a network that had never really been tried.
              staDeadline = millis() + 3000;
            }
          } else {
            staRefusals = 0;
            staPhase = STA_ATTEMPT;
            // One network's share of the search: WIFI_ATTEMPT_SECONDS, tunable
            // in the Web UI because a marginal signal on a busy channel needs
            // more than any compiled-in constant can guess.
            staDeadline =
                millis() + (unsigned long)WIFI_ATTEMPT_SECONDS * 1000UL;
          }
        }
      } else if (staPhase == STA_ATTEMPT) {
        if (WiFi.status() == WL_CONNECTED) {
          staConnected = true;
          staLinkDropped = false;
          staPhase = STA_UP;
          logPrintf("STA joined WiFi[%d]: %s\n", staNetIdx,
                    wifiNets[staNetIdx].ssid);
          logPrintf("STA connected: %s\n", WiFi.localIP().toString().c_str());
          logPrintf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
          // Last known good first: the network this unit can actually reach
          // becomes slot 0, so the next boot and the next dropout start with it
          // instead of walking a hand-ordered primary/fallback list.
          strncpy(s_joinedSsid, wifiNets[staNetIdx].ssid, sizeof(s_joinedSsid) - 1);
          s_joinedSsid[sizeof(s_joinedSsid) - 1] = 0;
          wifiRotateToTop(staNetIdx);
          // The rotation moved this network to slot 0, and wifiNets[] points at
          // the globals, so the connected network is now index 0. Keeping the
          // old index would make later logs and the picker name the wrong row.
          staNetIdx = 0;
        } else if (!staDrvAttemptOpen && millis() - staAttemptStart > 300) {
          // The driver already ended this attempt (NO_AP_FOUND, wrong password,
          // auth timeout...): no reason to burn the rest of the window.
          logPrintf("WiFi[%d] %s failed (reason=%u), trying next...\n",
                    staNetIdx, wifiNets[staNetIdx].ssid,
                    (unsigned)staDrvReason);
          staNetIdx++;
          staPhase = STA_BACKOFF;
          staDeadline = millis() + 200;
        } else if (millis() >= staDeadline) {
          // Still connecting when the window closed (out of range, silent
          // DHCP): stop the attempt first, otherwise the next config change is
          // rejected and the search pins itself to this network.
          logPrintf("WiFi[%d] %s timed out, trying next...\n", staNetIdx,
                    wifiNets[staNetIdx].ssid);
          WiFi.disconnect(false);
          staNetIdx++;
          staPhase = STA_GAP;
          staDeadline = millis() + 3000;
        }
      }
    }
    staDbgPhase = (uint8_t)staPhase;
    staDbgNetIdx = staNetIdx;

    if (staConnected && !staFinalized) {
      staFinalized = true;
      startWeatherFetch();
      // mDNS/NTP are one-shot services: running them again on every reconnect
      // would block this loop (NTP wait) or fail (MDNS.begin() double start).
      // On reconnects only the weather fetch is re-armed; mDNS is re-bound so
      // it advertises the freshly-assigned IP instead of a stale one.
      if (MDNS.begin("dashboard-pp")) {
        MDNS.addService("http", "tcp", 80);
        logPrintf("mDNS: http://dashboard-pp.local\n");
      } else if (staHasConnectedBefore) {
        // Already started once: tear down and rebind for the new IP.
        MDNS.end();
        if (MDNS.begin("dashboard-pp")) {
          MDNS.addService("http", "tcp", 80);
          logPrintf("mDNS re-bound: http://dashboard-pp.local\n");
        }
      }

      if (!staHasConnectedBefore && NTP_ENABLED) {
        logPrintf("Syncing time via NTP: %s\n", NTP_SERVER);
        configTime(0, 0, NTP_SERVER);
        // Arm the wait only; the settle check runs once per loop iteration
        // below. The old `while (...) delay(500)` retry ran here, inside the
        // web task, so handleClient() was stalled for up to 5 s after every
        // Wi-Fi join and the config page/API were unreachable while it spun
        // (issue #43).
        ntpWaitStart = millis();
      }
      staHasConnectedBefore = true;
    }

    // Automatic update check: once per boot, fired right after the STA
    // connection is established (replaces the old recurring interval timer).
    if (!otaPullBootCheckDone && OTA_PULL_ENABLED && staConnected &&
        !otaUpdateInProgress && !otaPullTaskRunning) {
      otaPullBootCheckDone = true;
      startOtaPull(false, true);
    }

    static unsigned long lastWeatherCheck = 0;
    static unsigned long lastWeatherAttemptMs = 0;
    if (WiFi.status() == WL_CONNECTED && !otaPullDownloading) {
      // Config save changed the weather settings: fetch now. If the task
      // could not be created (low heap), retry with a 2s backoff so a
      // failing fetch can never spin xTaskCreate every loop tick; the flag
      // is dropped only once a fetch actually starts, and a fetch already
      // in flight is left alone.
      if (weatherRefreshRequested) {
        lastWeatherCheck = millis();
        if (!weatherTaskRunning && millis() - lastWeatherAttemptMs >= 2000) {
          lastWeatherAttemptMs = millis();
          if (startWeatherFetch())
            weatherRefreshRequested = false;
        }
      }
      // Hung-fetch guard: if the HTTP request ever sticks longer than 30s,
      // abandon the task so the interval and future refreshes can retry
      // instead of the weather widget dying permanently.
      // Hung-fetch guard (issue #23): ask the fetch to stop instead of killing
      // it. vTaskDelete() on a task blocked in HTTPClient/mbedTLS leaks the task
      // stack plus the HTTP/TLS contexts and is how a flaky weather host pushed
      // the unit into SAFE MODE. The HTTP timeouts inside updateWeather() are 8 s,
      // so this request is normally answered within one checkpoint; if it is not,
      // the slot stays occupied and startWeatherFetch() refuses to stack another.
      xSemaphoreTake(weatherFetchMutex, portMAX_DELAY);
      if (weatherTaskRunning && !weatherAbandoned &&
          millis() - weatherTaskStartedMs > 30000) {
        logPrintf("Weather: fetch task stuck >30s, requesting abort (task not deleted)\n");
        weatherAbort = true;
        weatherAbandoned = true;
      }
      xSemaphoreGive(weatherFetchMutex);
      if (lastWeatherCheck == 0) {
        lastWeatherCheck = millis();
      }
      unsigned long weatherIntervalMs = (unsigned long)WEATHER_REFRESH_MIN * 60000UL;
      // SAFE MODE: don't start new weather fetches (the TLS handshake needs
      // ~34KB — exactly the heap that's short). In-flight fetches finish
      // normally (the 30 s abort request still applies to them).
      if (!safeModeActive && millis() - lastWeatherCheck >= weatherIntervalMs) {
        lastWeatherCheck = millis();
        startWeatherFetch();
      }
    }

    // Low-heap watchdog: the display keeps speed sprite + the 120px VLW
    // buffer (~60KB total), which can starve /api/config (~30-45KB
    // transient) and the TLS stack. Ask the display task to drop the big
    // sprites (memory-saver mode); if even that is not enough, enter SAFE
    // MODE (stay up, no reboot) instead of rebooting to clear heap
    // fragmentation — this is what breaks a heap-critical boot loop. Armed 5s
    // after start.
    //
    // Fully-loaded steady state is ~54KB with WiFi up (sprites + VLW120 +
    // STA/TLS stacks), rising to ~118KB after memory-saver drops the buffers.
    // The background ARM threshold (30KB) therefore sits far BELOW normal WiFi
    // operation so it only engages when heap really collapses; the /api/config
    // handler still self-protects at its own 60KB "drop sprites first" gate,
    // the OTA pull does its own pre-TLS release, and the weather fetch
    // releases before an https connect. Memory-saver is REVERSIBLE: once free
    // heap stays >= 108KB for a minute the flags are cleared and
    // ensureSpeedSprite() rebuilds the buffers again. The
    // 30KB / 108KB thresholds bracket real states (54 vs 118) so the UI cannot
    // flap between armed and recovered.
    if (millis() - webStartMs > 5000) {
      uint32_t fh = ESP.getFreeHeap();
      static unsigned long memRelaxSince = 0;
      if (!memSaverRequested && fh < 30000) {
        logPrintf("Low heap (%lu B), enabling memory-saver mode\n", (unsigned long)fh);
        memSaverRequested = true;
      }
      if (memSaverActive && fh < 16000) {
        // SAFE MODE: stay up instead of rebooting. The device keeps serving
        // /api (forensics included) in the most-frugal state; only the OOM
        // floor reboots, and never during a fast-reboot storm.
        if (!safeModeActive) {
          safeModeActive = true;
          logPrintf("SAFE MODE entered (heap=%lu B): staying up, no reboot\n",
                    (unsigned long)fh);
        }
        if (fh < 8000 && !bootinfo_storm_active()) {
          logPrintf("SAFE MODE: OOM floor (heap=%lu B), rebooting\n",
                    (unsigned long)fh);
          bootinfo_tag_reboot("heap-oom");
          vTaskDelay(pdMS_TO_TICKS(500));
          ESP.restart();
        }
      } else if (safeModeActive && fh > 40000) {
        safeModeActive = false;
        logPrintf("SAFE MODE lifted (heap=%lu B)\n", (unsigned long)fh);
      }
      if (memSaverActive && fh >= 108000) {
        if (memRelaxSince == 0) memRelaxSince = millis();
        if (millis() - memRelaxSince > 30000) {
          memRelaxSince = 0;
          memSaverRequested = false;
          memSaverActive = false;
          logPrintf("Low-heap recovered (%lu B), disabling memory-saver\n",
                    (unsigned long)fh);
        }
      } else {
        memRelaxSince = 0;
      }
    }

    // Diagnostics: flag any web-task iteration that runs long (DNS/TLS/API
    // work) - such bursts can stall the other core's frames via flash cache.
    // Gated to steady state (boot does NTP sync with multi-second delays).
    if (millis() > 30000) {
      static unsigned long webIterLast = 0;
      unsigned long webIterMs = millis() - webIterLast;
      webIterLast = millis();
      if (webIterMs > 100)
        logPrintf("WEB SLOW: iteration %lums heap=%lu\n",
                  (unsigned long)webIterMs, (unsigned long)ESP.getFreeHeap());
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
