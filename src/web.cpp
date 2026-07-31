#include "dashboard.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

Preferences preferences;
WebServer server(80);

static bool otaUpdateSuccess = false;

// OTA Pull state
static unsigned long lastOtaPullCheck = 0;
static String otaPullStatus = "idle";
static bool otaPullStatusUpdated = false;
static SemaphoreHandle_t otaStatusMutex = NULL;
static bool otaPullTaskRunning = false;
static bool otaPullManualFlag = false;

void setOtaPullStatus(const char *status) {
  if (otaStatusMutex) xSemaphoreTake(otaStatusMutex, portMAX_DELAY);
  otaPullStatus = status;
  otaPullStatusUpdated = true;
  if (otaStatusMutex) xSemaphoreGive(otaStatusMutex);
}

void otaPullTask(void *pvParameters) {
  checkForFirmwareUpdate(otaPullManualFlag);
  otaPullTaskRunning = false;
  vTaskDelete(NULL);
}

void startOtaPull(bool manual) {
  if (otaPullTaskRunning || otaUpdateInProgress) return;
  otaPullManualFlag = manual;
  otaPullTaskRunning = true;
  // 32KB stack: the TLS download handshake plus HTTPClient redirect handling
  // overflows 16KB, silently corrupting adjacent heap chunks (subsequent
  // mallocs then fail with "malloc failed" despite plenty of free heap).
  xTaskCreatePinnedToCore(otaPullTask, "OtaPullTask", 32768, NULL, 1, NULL, 0);
}

// Suspends the display loop task while the OTA task does TLS work, so the
// heap does not get fragmented by per-frame UI allocations. The mbedTLS
// handshake needs large contiguous blocks and fails (ALLOC_FAILED) otherwise.
// Before suspending, asks the display task to free the speed sprite (~70KB,
// the biggest UI allocation) at a safe point — see processOtaMemRelease in
// ui.cpp. The display task rebuilds it after the check finishes.
namespace {
struct OtaHeapGuard {
  TaskHandle_t h;
  OtaHeapGuard() {
    otaMemReleaseRequested = true;
    otaMemReleased = false;
    unsigned long t0 = millis();
    while (!otaMemReleased && (millis() - t0) < 3000)
      vTaskDelay(pdMS_TO_TICKS(1));
    h = xTaskGetHandle("loopTask");
    if (h) vTaskSuspend(h);
    logPrintf("OTA Pull: UI mem released heap=%lu max=%lu\n",
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap());
  }
  ~OtaHeapGuard() {
    if (h) vTaskResume(h);
    otaMemReleaseRequested = false;
  }
};
}

void checkForFirmwareUpdate(bool manual) {
  if (!OTA_PULL_ENABLED && !manual) return;
  if (WiFi.status() != WL_CONNECTED) {
    setOtaPullStatus("error: not connected to WiFi");
    return;
  }
  if (OTA_PULL_URL.length() == 0) {
    setOtaPullStatus("error: no OTA URL configured");
    return;
  }

  logPrintf("OTA Pull: checking %s\n", OTA_PULL_URL.c_str());
  logPrintf("OTA Pull: heap free=%lu maxAlloc=%lu\n",
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  logPrintf("OTA Pull: staIP=%s gw=%s wifiStatus=%d\n",
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(), (int)WiFi.status());

  OtaHeapGuard heapGuard;

  String host = OTA_PULL_URL;
  int protoEnd = host.indexOf("://");
  if (protoEnd >= 0) host = host.substring(protoEnd + 3);
  int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);

  bool dnsOk = false;
  bool tcpOk = false;
  String tlsErr = "";
  IPAddress resolvedIp;
  if (WiFi.hostByName(host.c_str(), resolvedIp)) {
    logPrintf("OTA Pull: DNS ok %s -> %s\n", host.c_str(),
              resolvedIp.toString().c_str());
    dnsOk = true;
  } else {
    logPrintf("OTA Pull: DNS FAILED for %s\n", host.c_str());
  }
  {
    WiFiClient probe;
    if (probe.connect(host.c_str(), 443, 5000)) {
      logPrintf("OTA Pull: TCP 443 connect OK\n");
      tcpOk = true;
      probe.stop();
    } else {
      logPrintf("OTA Pull: TCP 443 connect FAILED\n");
    }
  }
  {
    WiFiClientSecure tls;
    tls.setInsecure();
    if (!heap_caps_check_integrity_all(true)) {
      logPrintf("OTA Pull: HEAP CORRUPTED before TLS probe\n");
    }
    if (tls.connect(host.c_str(), 443, 5000)) {
      logPrintf("OTA Pull: TLS handshake OK\n");
      tls.stop();
    } else {
      char errBuf[128] = "";
      tls.lastError(errBuf, sizeof(errBuf));
      logPrintf("OTA Pull: TLS handshake FAILED: %s\n", errBuf);
      tlsErr = errBuf;
    }
  }

  String payload;
  int httpCode = 0;
  bool beginFailed = false;

  for (int attempt = 0; attempt < 3; attempt++) {
    WiFiClient *client = nullptr;

    if (OTA_PULL_URL.startsWith("https://")) {
      WiFiClientSecure *ssl = new WiFiClientSecure();
      ssl->setInsecure();
      client = ssl;
    } else {
      client = new WiFiClient();
    }

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
        }
        http.end();
      }
    }

    delete client;

    if (beginFailed) {
      logPrintf("OTA Pull: begin failed\n");
      String diag = " dns=" + String(dnsOk ? "ok" : "fail") +
                    " tcp443=" + String(tcpOk ? "ok" : "fail");
      setOtaPullStatus(("error: connection begin failed" + diag).c_str());
      return;
    }
    if (httpCode == HTTP_CODE_OK) break;
    if (attempt < 2) delay(2000);
  }

  if (httpCode != HTTP_CODE_OK) {
    String diag = " dns=" + String(dnsOk ? "ok" : "fail") +
                  " tcp443=" + String(tcpOk ? "ok" : "fail") +
                  " tls=" + (tlsErr.length() ? tlsErr : "ok") +
                  " heap=" + String(ESP.getFreeHeap()) +
                  " max=" + String(ESP.getMaxAllocHeap());
    setOtaPullStatus(("error: HTTP " + String(httpCode) + " (" +
                      HTTPClient::errorToString(httpCode) + ")" + diag).c_str());
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    logPrintf("OTA Pull: JSON parse error: %s\n", err.c_str());
    setOtaPullStatus("error: invalid manifest JSON");
    return;
  }

  String latestVersion = doc["version"] | doc["tag_name"] | "";
  String firmwareUrl = doc["firmware_url"] | "";

  if (firmwareUrl.length() == 0) {
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
      if (String(asset["name"] | "").endsWith(".bin")) {
        firmwareUrl = asset["browser_download_url"] | "";
        break;
      }
    }
  }

  if (latestVersion.length() == 0 || firmwareUrl.length() == 0) {
    logPrintf("OTA Pull: invalid manifest (missing version/firmware_url)\n");
    setOtaPullStatus("error: manifest missing version or firmware url");
    return;
  }

  String curVer = OTA_CURRENT_VERSION;
  if (curVer.startsWith("v") || curVer.startsWith("V")) curVer = curVer.substring(1);
  if (latestVersion.startsWith("v") || latestVersion.startsWith("V"))
    latestVersion = latestVersion.substring(1);

  logPrintf("OTA Pull: latest=%s current=%s\n", latestVersion.c_str(), curVer.c_str());

  if (latestVersion.equals(curVer)) {
    logPrintf("OTA Pull: already up-to-date\n");
    setOtaPullStatus(("up-to-date (v" + curVer + ")").c_str());
    return;
  }

  logPrintf("OTA Pull: new firmware v%s available, downloading\n", latestVersion.c_str());
  setOtaPullStatus(("updating to v" + latestVersion).c_str());
  performFirmwareUpdate(firmwareUrl, latestVersion);
}

void performFirmwareUpdate(const String &firmwareUrl, const String &newVersion) {
  logPrintf("OTA Pull: downloading %s\n", firmwareUrl.c_str());
  logPrintf("OTA Pull: heap free=%lu maxAlloc=%lu\n",
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  otaUpdateInProgress = true;
  otaUpdateSuccess = false;
  pendingOtaScreen = true;

  WiFiClient *client = nullptr;
  if (firmwareUrl.startsWith("https://")) {
    WiFiClientSecure *ssl = new WiFiClientSecure();
    ssl->setInsecure();
    client = ssl;
  } else {
    client = new WiFiClient();
  }

  {
    HTTPClient http;
    if (!http.begin(*client, firmwareUrl)) {
      logPrintf("OTA Pull: begin failed\n");
      setOtaPullStatus("error: update begin failed");
    } else {
      http.setTimeout(30000);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.addHeader("Accept-Encoding", "identity");

      int httpCode = http.GET();
      if (httpCode != HTTP_CODE_OK) {
        logPrintf("OTA Pull: download HTTP %d (%s)\n", httpCode,
                  HTTPClient::errorToString(httpCode).c_str());
        setOtaPullStatus(("error: download HTTP " + String(httpCode)).c_str());
      } else {
        int totalSize = http.getSize();

        if (!heap_caps_check_integrity_all(true)) {
          logPrintf("OTA Pull: HEAP CORRUPTED before Update.begin\n");
        }
        if (!Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
          setOtaPullStatus("error: update.begin failed");
        } else {
          size_t written = 0;
          while (http.connected() && (totalSize <= 0 || written < (size_t)totalSize)) {
            size_t available = client->available();
            if (available) {
              uint8_t buf[512];
              size_t n = client->readBytes(buf, min(available, sizeof(buf)));
              size_t w = Update.write(buf, n);
              if (w != n) {
                logPrintf("OTA Pull: write error\n");
                break;
              }
              written += w;
              if (totalSize > 0) {
                updateOTAProgress(written, totalSize);
              }
            }
            delay(1);
          }

          if (Update.end(true)) {
            logPrintf("OTA Pull: success %zu bytes\n", written);
            otaUpdateSuccess = true;
            otaProgressTarget = 258;
            // Record the new version in NVS so the next check reports
            // up-to-date instead of re-downloading the same release.
            { Preferences p; p.begin("cfg", false);
              p.putString("OTA_VER", newVersion); p.end(); }
          } else {
            Update.printError(Serial);
            setOtaPullStatus("error: update.end failed");
          }
        }
      }
      http.end();
    }
  }

  delete client;

  if (!otaUpdateSuccess) {
    otaUpdateInProgress = false;
    forceFullRedraw = true;
  }
}

const char *index_html = R"rawliteral(
<!DOCTYPE html>
<!-- saved from url=(0021)http://192.168.1.136/ -->
<html lang="en"><head><meta http-equiv="Content-Type" content="text/html; charset=UTF-8">

<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dashboard++ Config</title>
<link rel="preconnect" href="https://fonts.googleapis.com/">
<link rel="preconnect" href="https://fonts.gstatic.com/" crossorigin="">
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;600;700&family=Outfit:wght@500;600;700;800&display=swap" rel="stylesheet">
<style>
:root {
    --bg-base: #080b11;
    --bg-card: rgba(16, 22, 34, 0.75);
    --bg-card-hover: rgba(23, 31, 48, 0.85);
    --bg-input: #121824;
    --bg-tertiary: #0f1422;
    --border-color: rgba(255, 255, 255, 0.08);
    --border-focus: #00e5ff;
    --accent-cyan: #00e5ff;
    --accent-blue: #2979ff;
    --accent-purple: #a855f7;
    --accent-emerald: #00e676;
    --accent-amber: #ff9100;
    --accent-rose: #ff1744;
    --text-main: #f1f5f9;
    --text-muted: #94a3b8;
    --text-dim: #64748b;
    --glass-blur: blur(12px);
    --card-shadow: 0 8px 32px rgba(0, 0, 0, 0.4), inset 0 1px 0 rgba(255, 255, 255, 0.05);
}

html { background-color: #080b11; }

* { box-sizing: border-box; }

body {
    font-family: 'Inter', system-ui, -apple-system, sans-serif;
    background-color: var(--bg-base);
    background-image: 
        radial-gradient(circle at 15% 15%, rgba(0, 229, 255, 0.04) 0%, transparent 40%),
        radial-gradient(circle at 85% 85%, rgba(168, 85, 247, 0.04) 0%, transparent 40%);
    background-attachment: fixed;
    color: var(--text-main);
    padding: 16px 12px 60px 12px;
    margin: 0 auto;
    max-width: 920px;
    min-height: 100vh;
    animation: pageFadeIn 0.5s cubic-bezier(0.16, 1, 0.3, 1);
}

@keyframes pageFadeIn {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
}

@keyframes pulseGlow {
    0%, 100% { box-shadow: 0 0 12px rgba(0, 229, 255, 0.2); }
    50% { box-shadow: 0 0 24px rgba(0, 229, 255, 0.5); }
}

/* Header & Connectivity Bar */
.header {
    text-align: center;
    margin-bottom: 16px;
    position: relative;
}

.brand-badge {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 4px 12px;
    background: rgba(0, 229, 255, 0.1);
    border: 1px solid rgba(0, 229, 255, 0.25);
    border-radius: 20px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1px;
    color: var(--accent-cyan);
    text-transform: uppercase;
    margin-bottom: 8px;
}

.brand-badge::before {
    content: '';
    display: inline-block;
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--accent-cyan);
    box-shadow: 0 0 8px var(--accent-cyan);
}

h2 {
    font-family: 'Outfit', sans-serif;
    background: linear-gradient(135deg, #ffffff 30%, #00e5ff 100%);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    margin: 0 0 6px 0;
    font-size: 26px;
    font-weight: 800;
    letter-spacing: -0.5px;
}

p.subtitle {
    color: var(--text-muted);
    font-size: 13px;
    margin: 0 0 12px 0;
    font-weight: 500;
}

.ip-banner {
    display: inline-flex;
    align-items: center;
    gap: 12px;
    background: var(--bg-card);
    backdrop-filter: var(--glass-blur);
    padding: 6px 16px;
    border-radius: 30px;
    border: 1px solid var(--border-color);
    font-size: 12px;
    color: var(--text-muted);
}

.ip-banner a {
    color: var(--accent-cyan);
    text-decoration: none;
    font-family: 'JetBrains Mono', monospace;
    font-weight: 600;
    transition: color 0.2s;
}

.ip-banner a:hover {
    color: #fff;
    text-decoration: underline;
}

/* Status Message Box */
#msg {
    text-align: center;
    font-weight: 600;
    font-size: 13px;
    margin-bottom: 14px;
    min-height: 24px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 8px;
    transition: all 0.3s ease;
    opacity: 0;
}

#msg:not(:empty) {
    padding: 6px 12px;
    opacity: 1;
    background: rgba(15, 23, 42, 0.6);
    border: 1px solid var(--border-color);
}

.msg-success { color: var(--accent-emerald) !important; border-color: rgba(0, 230, 118, 0.3) !important; background: rgba(0, 230, 118, 0.08) !important; }
.msg-error { color: var(--accent-rose) !important; border-color: rgba(255, 23, 68, 0.3) !important; background: rgba(255, 23, 68, 0.08) !important; }

/* Quick Control Actions Top Toolbar */
.bottom-toolbar {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 10px;
    margin-top: 20px;
}

.btn-reset-wide {
    grid-column: 1 / -1;
}

.bottom-toolbar button svg {
    width: 16px;
    height: 16px;
    stroke-width: 2;
    flex-shrink: 0;
}

button {
    font-family: 'Inter', system-ui, sans-serif;
    background: linear-gradient(135deg, rgba(0, 229, 255, 0.15), rgba(41, 121, 255, 0.25));
    color: #fff;
    padding: 9px 14px;
    border: 1px solid rgba(0, 229, 255, 0.3);
    border-radius: 8px;
    cursor: pointer;
    font-size: 12px;
    font-weight: 600;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    transition: all 0.25s cubic-bezier(0.16, 1, 0.3, 1);
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
    user-select: none;
}

button:hover {
    background: linear-gradient(135deg, rgba(0, 229, 255, 0.3), rgba(41, 121, 255, 0.45));
    border-color: var(--accent-cyan);
    transform: translateY(-1px);
    box-shadow: 0 4px 16px rgba(0, 229, 255, 0.25);
}

button:active {
    transform: translateY(0) scale(0.98);
}

.btn-primary {
    background: linear-gradient(135deg, #00e5ff 0%, #0091ea 100%) !important;
    color: #050b14 !important;
    border: none !important;
    font-weight: 700 !important;
    box-shadow: 0 4px 14px rgba(0, 229, 255, 0.35) !important;
}

.btn-primary:hover {
    background: linear-gradient(135deg, #33ebff 0%, #00a1ff 100%) !important;
    box-shadow: 0 6px 20px rgba(0, 229, 255, 0.5) !important;
}

.btn-secondary {
    background: rgba(30, 41, 59, 0.7);
    border-color: rgba(255, 255, 255, 0.1);
    color: var(--text-main);
}

.btn-secondary:hover {
    background: rgba(51, 65, 85, 0.9);
    border-color: rgba(255, 255, 255, 0.25);
}

.btn-warning {
    background: linear-gradient(135deg, rgba(255, 145, 0, 0.2), rgba(216, 67, 21, 0.3));
    border-color: rgba(255, 145, 0, 0.4);
    color: #ffb74d;
}

.btn-warning:hover {
    background: linear-gradient(135deg, rgba(255, 145, 0, 0.35), rgba(216, 67, 21, 0.5));
    color: #fff;
    box-shadow: 0 4px 14px rgba(255, 145, 0, 0.3);
}

.btn-danger {
    background: linear-gradient(135deg, rgba(255, 23, 68, 0.2), rgba(198, 40, 40, 0.3));
    border-color: rgba(255, 23, 68, 0.4);
    color: #ff8a80;
}

.btn-danger:hover {
    background: linear-gradient(135deg, rgba(255, 23, 68, 0.4), rgba(198, 40, 40, 0.6));
    color: #fff;
    box-shadow: 0 4px 14px rgba(255, 23, 68, 0.35);
}

/* Search Box */
.search-wrapper {
    position: relative;
    margin-bottom: 16px;
}

#searchBar {
    width: 100%;
    padding: 11px 16px 11px 40px;
    background: var(--bg-card);
    backdrop-filter: var(--glass-blur);
    color: var(--text-main);
    border: 1px solid var(--border-color);
    border-radius: 10px;
    font-size: 13px;
    font-family: 'Inter', system-ui, sans-serif;
    transition: all 0.25s ease;
    box-shadow: var(--card-shadow);
}

#searchBar:focus {
    border-color: var(--accent-cyan);
    outline: none;
    background: rgba(20, 28, 45, 0.9);
    box-shadow: 0 0 16px rgba(0, 229, 255, 0.2);
}

#searchBar::placeholder {
    color: var(--text-dim);
}

.search-icon {
    position: absolute;
    left: 14px;
    top: 50%;
    transform: translateY(-50%);
    display: flex;
    align-items: center;
    pointer-events: none;
    z-index: 1;
}

.search-icon svg {
    width: 18px;
    height: 18px;
    stroke: var(--text-dim);
    stroke-width: 2;
    fill: none;
}

/* Accordion Details Section */
details {
    background: var(--bg-card);
    backdrop-filter: var(--glass-blur);
    border: 1px solid var(--border-color);
    border-radius: 12px;
    margin-bottom: 12px;
    box-shadow: var(--card-shadow);
    overflow: hidden;
    transition: border-color 0.3s ease, box-shadow 0.3s ease;
}

details[open] {
    border-color: rgba(0, 229, 255, 0.25);
    box-shadow: 0 12px 36px rgba(0, 0, 0, 0.5), 0 0 1px rgba(0, 229, 255, 0.3);
}

summary {
    background: rgba(18, 25, 38, 0.6);
    padding: 12px 16px;
    font-family: 'Outfit', sans-serif;
    font-weight: 700;
    font-size: 14px;
    color: var(--text-main);
    cursor: pointer;
    user-select: none;
    display: flex;
    align-items: center;
    justify-content: space-between;
    transition: background 0.25s ease, color 0.25s ease;
}

summary:hover {
    background: rgba(28, 38, 56, 0.8);
    color: var(--accent-cyan);
}

summary::-webkit-details-marker { display: none; }
summary::marker { content: ''; }

.summary-title {
    display: flex;
    align-items: center;
    gap: 10px;
}

.summary-left {
    display: flex;
    align-items: center;
    gap: 12px;
}

.summary-icon-badge {
    width: 32px;
    height: 32px;
    border-radius: 8px;
    background: rgba(0, 242, 254, 0.08);
    border: 1px solid rgba(0, 242, 254, 0.2);
    color: var(--accent-cyan);
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.3s ease;
    flex-shrink: 0;
}

.summary-icon-badge svg {
    width: 18px;
    height: 18px;
    stroke-width: 2;
}

.summary-chevron {
    width: 18px;
    height: 18px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.05);
    color: var(--text-muted);
    font-size: 10px;
    transition: transform 0.3s ease, background 0.3s ease, color 0.3s ease;
}

details[open] summary .summary-chevron {
    transform: rotate(90deg);
    background: rgba(0, 229, 255, 0.15);
    color: var(--accent-cyan);
}

.unit {
    font-size: 11px;
    color: var(--text-muted);
    margin-left: 4px;
    white-space: nowrap;
    flex-shrink: 0;
}

.details-content {
    padding: 12px;
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 10px;
    overflow: hidden;
}

/* Card & Grouping Elements */
.card {
    display: flex;
    flex-direction: column;
    justify-content: center;
    background: rgba(22, 30, 46, 0.6);
    border: 1px solid rgba(255, 255, 255, 0.05);
    padding: 10px 12px;
    border-radius: 8px;
    transition: transform 0.2s ease, border-color 0.2s ease, background 0.2s ease;
}

.card:hover {
    border-color: rgba(255, 255, 255, 0.12);
    background: rgba(26, 36, 56, 0.8);
}

.card h4 {
    margin: 0 0 6px 0;
    color: var(--accent-cyan);
    font-family: 'Outfit', sans-serif;
    font-size: 13px;
    font-weight: 700;
    letter-spacing: 0.2px;
}

.section-title {
    margin: 10px 0 4px 0;
    color: var(--accent-cyan);
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.8px;
}

label {
    font-size: 12px;
    color: var(--text-muted);
    font-weight: 500;
}

/* Form Controls Styling */
input[type="text"], input[type="number"], select {
    width: 100%;
    padding: 6px 10px;
    margin-top: 4px;
    background: var(--bg-input);
    color: var(--text-main);
    border: 1px solid var(--border-color);
    border-radius: 6px;
    font-size: 12px;
    font-family: 'Inter', system-ui, sans-serif;
    transition: all 0.2s ease;
}

input[type="text"]:focus, input[type="number"]:focus, select:focus {
    border-color: var(--accent-cyan);
    outline: none;
    background: #182030;
    box-shadow: 0 0 10px rgba(0, 229, 255, 0.2);
}

/* Checkbox Modern Toggle */
.checkbox-card {
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
}

input[type="checkbox"] {
    appearance: none;
    -webkit-appearance: none;
    width: 36px;
    height: 20px;
    background: #1e293b;
    border: 1px solid var(--border-color);
    border-radius: 12px;
    position: relative;
    cursor: pointer;
    outline: none;
    transition: background 0.3s ease, border-color 0.3s ease;
    margin: 0;
}

input[type="checkbox"]::after {
    content: '';
    position: absolute;
    top: 2px;
    left: 2px;
    width: 14px;
    height: 14px;
    background: #94a3b8;
    border-radius: 50%;
    transition: transform 0.3s ease, background 0.3s ease;
}

input[type="checkbox"]:checked {
    background: rgba(0, 229, 255, 0.25);
    border-color: var(--accent-cyan);
}

input[type="checkbox"]:checked::after {
    transform: translateX(16px);
    background: var(--accent-cyan);
    box-shadow: 0 0 8px var(--accent-cyan);
}

/* XY Coordinates Group */
.xy-group h4 {
    margin: 0 0 8px 0;
    color: #fff;
    font-size: 12px;
    font-weight: 700;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    padding-bottom: 4px;
}

.xy-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 6px;
}

.xy-row:last-child { margin-bottom: 0; }

.xy-row label {
    width: 14px;
    color: var(--accent-cyan);
    font-weight: 700;
    text-align: center;
    font-size: 12px;
    font-family: 'JetBrains Mono', monospace;
}

input[type="range"] {
    touch-action: none;
}

.xy-row input[type="range"] {
    flex-grow: 1;
    accent-color: var(--accent-cyan);
    padding: 0;
    cursor: pointer;
    height: 4px;
    background: #1e293b;
    border-radius: 2px;
}

.xy-row input[type="number"] {
    width: 85px;
    padding: 4px 6px;
    margin: 0;
    text-align: center;
    font-weight: 600;
    font-family: 'JetBrains Mono', monospace;
}

/* Color Pickers */
input[type="color"] {
    width: 34px;
    height: 26px;
    border: 1px solid var(--border-color);
    border-radius: 6px;
    background: transparent;
    cursor: pointer;
    padding: 1px;
}

input[type="color"]::-webkit-color-swatch-wrapper { padding: 0; }
input[type="color"]::-webkit-color-swatch { border: none; border-radius: 4px; }

/* Performance Dashboard Panel */
.perf-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    gap: 10px;
}

.perf-card {
    background: rgba(18, 25, 38, 0.8);
    padding: 12px;
    border-radius: 8px;
    border: 1px solid var(--border-color);
    transition: transform 0.2s ease, box-shadow 0.2s ease;
}

.perf-card:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.3);
}

.perf-card h4 {
    margin: 0 0 8px 0;
    color: var(--accent-cyan);
    font-family: 'Outfit', sans-serif;
    font-size: 13px;
    font-weight: 700;
}

.perf-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 3px 0;
    font-size: 12px;
}

.perf-label { color: var(--text-muted); }
.perf-value {
    color: #fff;
    font-weight: 600;
    font-family: 'JetBrains Mono', monospace;
}

.perf-bar {
    height: 20px;
    background: var(--bg-tertiary);
    border-radius: 4px;
    margin-top: 6px;
    overflow: hidden;
    display: flex;
    padding: 0;
}

.perf-bar-fill {
    height: 100%;
    border-radius: 4px;
    transition: width 0.8s ease, background 0.4s ease;
}

#serial-out {
    background: #05080e;
    color: #00ff88;
    font-family: 'JetBrains Mono', monospace;
    font-size: 11px;
    line-height: 1.4;
    padding: 10px;
    border-radius: 6px;
    border: 1px solid var(--border-color);
    height: 160px;
    overflow-y: auto;
    white-space: pre-wrap;
    word-break: break-all;
}

/* Floating Scroll Top Button */
#scrollTopBtn {
    position: fixed;
    bottom: 20px;
    right: 20px;
    width: 42px;
    height: 42px;
    border-radius: 50%;
    background: linear-gradient(135deg, #00e5ff, #0091ea);
    color: #050b14;
    border: none;
    font-size: 18px;
    cursor: pointer;
    box-shadow: 0 4px 20px rgba(0, 229, 255, 0.4);
    display: none;
    align-items: center;
    justify-content: center;
    z-index: 999;
    transition: all 0.3s ease;
}

#scrollTopBtn:hover {
    transform: scale(1.1) translateY(-2px);
    box-shadow: 0 6px 24px rgba(0, 229, 255, 0.6);
}

#scrollTopBtn:active { transform: scale(0.95); }

/* Responsive Tweaks */
@media (max-width: 600px) {
    body { padding: 10px 8px 50px 8px; }
    .bottom-toolbar { grid-template-columns: repeat(2, 1fr); }
    .details-content { grid-template-columns: 1fr; }
}
</style>
</head>
<body>

<div class="header">
    <div class="brand-badge">ESP32 Telemetry Engine</div>
    <h2>Dashboard++ Setup</h2>
    <p class="subtitle">Device Configuration &amp; UI Layout Manager</p>
    <div class="ip-banner">
        <span id="ip-info">LAN: <a href="http://192.168.1.136/" target="_blank">192.168.1.136</a> &nbsp;|&nbsp; AP: <a href="http://192.168.4.1/" target="_blank">192.168.4.1</a></span>
    </div>
</div>

<div id="msg" class="msg-success">Time synced successfully!</div>

<input type="file" id="importFile" accept=".json" style="display:none" onchange="importBackup(event)">

<div class="search-wrapper">
    <span class="search-icon"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="18" height="18"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg></span>
    <input type="text" id="searchBar" placeholder="Search configuration parameters..." oninput="filterConfig()" autocomplete="off">
</div>

<div id="form-container">
            <details>
                <summary>
                    <div class="summary-title">System &amp; General</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content"><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">⚙️ General</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Target Refresh Rate (FPS)</label><input type="number" id="TARGET_FPS" value="60" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Show FPS Counter</label><input type="checkbox" id="SHOW_FPS_COUNTER_DEFAULT" checked=""></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Sensor Demo Mode</label><input type="checkbox" id="ENABLE_DEMO_MODE"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Power Sensing</label><input type="checkbox" id="ENABLE_POWER_SENSE"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Show UI Element Bounds (Debug)</label><input type="checkbox" id="SHOW_ELEMENT_BOUNDS"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">⚡ CPU</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:2px 0;"><label style="font-size:12px;">Manual CPU Frequency</label><select id="MANUAL_CPU_FREQ" style="width:auto;min-width:120px;"><option value="80">80 MHz</option><option value="160">160 MHz</option><option value="240" selected="">240 MHz</option></select></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Dynamic CPU Frequency</label><input type="checkbox" id="ENABLE_DYNAMIC_CPU"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Thermal Throttling</label><input type="checkbox" id="ENABLE_CPU_THROTTLE" checked=""></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Throttle to 160 MHz At (°C)</label><input type="number" id="CPU_THROTTLE_TEMP_WARN" value="60" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Throttle to 80 MHz At (°C)</label><input type="number" id="CPU_THROTTLE_TEMP_CRIT" value="70" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">✍️ Custom Signatures</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Boot Screen Signature</label><input type="text" id="SPLASH_SIGNATURE" value="by @ale.finot" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Reboot Screen Signature</label><input type="text" id="REBOOT_SIGNATURE" value="Dashboard++ by @ale.finot" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Main Dashboard Watermark</label><input type="text" id="DASHBOARD_SIGNATURE" value="&lt;&lt;&lt;&lt;&lt;&lt;    Dashboard++ by @ale.finot    &gt;&gt;&gt;&gt;&gt;&gt;" step="any" style="width:85px;text-align:center;"></div></div></div></div>
            </details>
            <details>
                <summary>
                    <div class="summary-title">WiFi</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content"><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📶 Network Settings</h4><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">TX Power (dBm)</label>
                        <div class="xy-row">
                            <input type="range" id="WIFI_TX_POWER_DBM_slider" min="-1" max="20" step="1" value="20" oninput="document.getElementById(&#39;WIFI_TX_POWER_DBM&#39;).value = this.value">
                            <input type="number" id="WIFI_TX_POWER_DBM" value="20" min="-1" max="20" step="1" oninput="document.getElementById(&#39;WIFI_TX_POWER_DBM_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">📡 Primary Network</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Network Name (SSID)</label><input type="text" id="WIFI_SSID" value="Xiaomi 15" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Password</label><input type="text" id="WIFI_PASSWORD" value="Saaaaaaso" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">🔄 Fallback Networks</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Network 1</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Network Name (SSID)</label><input type="text" id="WIFI_SSID_1" value="D-Link-627F3B" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Password</label><input type="text" id="WIFI_PASSWORD_1" value="GDk2DxjVDc" step="any" style="width:85px;text-align:center;"></div></div><div class="section-title">Network 2</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Network Name (SSID)</label><input type="text" id="WIFI_SSID_2" value="TP-Link_F3EB" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Password</label><input type="text" id="WIFI_PASSWORD_2" value="14500763" step="any" style="width:85px;text-align:center;"></div></div><div class="section-title">Network 3</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Network Name (SSID)</label><input type="text" id="WIFI_SSID_3" value="" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Password</label><input type="text" id="WIFI_PASSWORD_3" value="" step="any" style="width:85px;text-align:center;"></div></div><div class="section-title">Network 4</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Network Name (SSID)</label><input type="text" id="WIFI_SSID_4" value="" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Password</label><input type="text" id="WIFI_PASSWORD_4" value="" step="any" style="width:85px;text-align:center;"></div></div></div></div>
            </details>
            <details>
                <summary>
                    <div class="summary-title">Display &amp; Colors</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content" style=""><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">🖥️ Display</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Shutdown Loading Duration (ms)</label><input type="number" id="SHUTDOWN_TIME_MS" value="3000" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Screen Rotation (0-3)</label><input type="number" id="DISPLAY_ROTATION" value="3" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Display Width (px)</label><input type="number" id="DISPLAY_WIDTH" value="480" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Display Height (px)</label><input type="number" id="DISPLAY_HEIGHT" value="320" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>SPI Bus Frequency (MHz)</label><input type="number" id="SPI_BUS_SPEED" value="60000000" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Invert Display Colors</label><input type="checkbox" id="DISPLAY_INVERT_COLORS"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Anti-Aliased Rendering</label><input type="checkbox" id="ENABLE_ANTIALIASING" checked=""></div>
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">AA Sharpness</label>
                        <div class="xy-row">
                            <input type="range" id="AA_SHARPNESS_slider" min="0.2" max="1" step="0.1" value="0.2" oninput="document.getElementById(&#39;AA_SHARPNESS&#39;).value = this.value">
                            <input type="number" id="AA_SHARPNESS" value="0.2" min="0.2" max="1" step="0.1" oninput="document.getElementById(&#39;AA_SHARPNESS_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">💡 Brightness Control</h4><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Backlight Brightness</label>
                        <div class="xy-row">
                            <input type="range" id="BACKLIGHT_BRIGHTNESS_slider" min="0" max="100" step="1" value="100" oninput="document.getElementById(&#39;BACKLIGHT_BRIGHTNESS&#39;).value = this.value">
                            <input type="number" id="BACKLIGHT_BRIGHTNESS" value="100" min="0" max="100" step="1" oninput="document.getElementById(&#39;BACKLIGHT_BRIGHTNESS_slider&#39;).value = this.value">
                        </div>
                    </div>
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Fade Duration (ms)</label>
                        <div class="xy-row">
                            <input type="range" id="FADE_DURATION_MS_slider" min="100" max="5000" step="100" value="700" oninput="document.getElementById(&#39;FADE_DURATION_MS&#39;).value = this.value">
                            <input type="number" id="FADE_DURATION_MS" value="700" min="100" max="5000" step="100" oninput="document.getElementById(&#39;FADE_DURATION_MS_slider&#39;).value = this.value">
                        </div>
                    </div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Enable Night Mode</label><input type="checkbox" id="ENABLE_NIGHT_MODE"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Start Time (Hour)</label><input type="number" id="NIGHT_MODE_START_HOUR" value="23" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>End Time (Hour)</label><input type="number" id="NIGHT_MODE_END_HOUR" value="0" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 Thresholds &amp; Colors</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Engine Temperature</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Low</label><input type="color" id="COLOR_TEMP_NORM" value="#00ffff"><input type="number" id="TEMP_BAR_MIN" value="10" step="any" style="width:75px;text-align:center;"> <span style="font-size:11px;color:var(--text-dim);">°C</span></div><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Normal</label><input type="color" id="COLOR_TEMP_WARN" value="#ff8c00"><input type="number" id="TEMP_WARN_YEL" value="45" step="any" style="width:75px;text-align:center;"> <span style="font-size:11px;color:var(--text-dim);">°C</span></div><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Critical</label><input type="color" id="COLOR_TEMP_CRIT" value="#ff0000"><input type="number" id="TEMP_WARN_RED" value="90" step="any" style="width:75px;text-align:center;"> <span style="font-size:11px;color:var(--text-dim);">°C</span></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Max (°C)</label><input type="number" id="TEMP_BAR_MAX" value="110" step="any" style="width:85px;text-align:center;"></div></div><div class="section-title">Fuel Level</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Normal</label><input type="color" id="COLOR_FUEL_NORM" value="#00ff00"></div><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Warning</label><input type="color" id="COLOR_FUEL_WARN" value="#ffff00"><input type="number" id="FUEL_WARN_YEL" value="45" step="any" style="width:75px;text-align:center;"> <span style="font-size:11px;color:var(--text-dim);">%</span></div><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="font-size:12px;min-width:65px;">Critical</label><input type="color" id="COLOR_FUEL_CRIT" value="#ff0000"><input type="number" id="FUEL_WARN_RED" value="20" step="any" style="width:75px;text-align:center;"> <span style="font-size:11px;color:var(--text-dim);">%</span></div><div style="display:flex;align-items:center;gap:10px;padding:3px 0;"><label style="min-width:65px;">Ghost Digit Color</label><input type="color" id="GHOST_COLOR_STR" value="#6b6b6b""></div></div></div></div>
            </details>
            <details>
                <summary>
                    <div class="summary-title">Sensors Tuning</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content" style="">
                <div class="card" style="grid-column:1/-1;">
                    <h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">⛽ Fuel Level Mapping Table</h4>
                    <label style="font-size:11px;">Number of points (tank capacity + 1):</label>
                    <div style="display:flex; align-items:center; gap:8px; margin:6px 0 10px 0;">
                        <input type="number" id="FUEL_TOUCH_POINTS" value="8" min="2" max="20" style="width:70px;">
                        <button onclick="save();setTimeout(() =&gt; location.reload(), 500)" class="btn-secondary" style="padding:5px 10px; font-size:11px;">↻ Refresh Table</button>
                    </div>
                    <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(130px, 1fr)); gap:6px 12px; align-items:center;"><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#0</span><input type="number" id="TT_0" value="950" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#1</span><input type="number" id="TT_1" value="840" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#2</span><input type="number" id="TT_2" value="750" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#3</span><input type="number" id="TT_3" value="670" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#4</span><input type="number" id="TT_4" value="600" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#5</span><input type="number" id="TT_5" value="530" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#6</span><input type="number" id="TT_6" value="460" style="width:100%;"></div><div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:&#39;JetBrains Mono&#39;,monospace;">#7</span><input type="number" id="TT_7" value="400" style="width:100%;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">🌡️ Temperature Sensor</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>NTC Balance Resistor (Ohms)</label><input type="number" id="NTC_R_BALANCE" value="10000" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>NTC Beta Value</label><input type="number" id="NTC_BETA" value="3950" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">⏱️ Acceleration Timer</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Timer Start Speed (km/h)</label><input type="number" id="ACCEL_START_SPEED" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Timer Target Speed (km/h)</label><input type="number" id="ACCEL_TARGET_SPEED" value="50" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Badge Top Text (e.g., '0-50')</label><input type="text" id="ACCEL_BADGE_LINE1" value="0-50" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Badge Bottom Text (e.g., 'km/h')</label><input type="text" id="ACCEL_BADGE_LINE2" value="km/h" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📦 Miscellaneous</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Wheel Circumference (mm)</label><input type="number" id="WHEEL_CIRCUMFERENCE_MM" value="1650" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Minimum Speed Threshold (km/h)</label><input type="number" id="MIN_SPEED_THRESHOLD" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Fuel Sensor Smoothing Alpha</label><input type="number" id="FUEL_FILTER_ALPHA" value="0.08" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">⏱️ Polling Rates</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Speed (ms)</label><input type="number" id="REFRESH_SPEED_MS" value="250" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Satellites (ms)</label><input type="number" id="REFRESH_SAT_MS" value="1000" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Accel Timer (ms)</label><input type="number" id="REFRESH_TMR_MS" value="10" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Battery Voltage (ms)</label><input type="number" id="REFRESH_BAT_MS" value="2500" step="any" style="width:85px;text-align:center;"></div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">🔢 Digit Count Configuration</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Speed Digits</label><input type="number" id="SPEED_DIGITS" value="3" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Satellites Digits</label><input type="number" id="SAT_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Timer Integer Digits</label><input type="number" id="TMR_INT_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Timer Decimal Digits</label><input type="number" id="TMR_DEC_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Battery Integer Digits</label><input type="number" id="BAT_INT_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Battery Decimal Digits</label><input type="number" id="BAT_DEC_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Instant KM/L Integer Digits</label><input type="number" id="INST_INT_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Instant KM/L Decimal Digits</label><input type="number" id="INST_DEC_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Average KM/L Integer Digits</label><input type="number" id="AVG_INT_DIGITS" value="2" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Average KM/L Decimal Digits</label><input type="number" id="AVG_DEC_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Fuel Integer Digits</label><input type="number" id="FUEL_INT_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Fuel Decimal Digits</label><input type="number" id="FUEL_DEC_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Odometer Integer Digits</label><input type="number" id="ODO_INT_DIGITS" value="5" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Odometer Decimal Digits</label><input type="number" id="ODO_DEC_DIGITS" value="1" step="any" style="width:85px;text-align:center;"></div></div></div></div>
            </details>
            <details>
                <summary>
                    <div class="summary-title">GNSS</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content" style=""><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">🛰️ GNSS Settings</h4><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Minimum Satellites Required</label><input type="number" id="MIN_SATELLITES" value="5" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Optimal Satellites Count</label><input type="number" id="OPTIMAL_SATELLITES" value="8" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Max Deviation Hall vs GPS (km/h)</label><input type="number" id="MAX_SPEED_DELTA_KMH" value="5" step="any" style="width:85px;text-align:center;"></div></div></div></div>
            </details>
            <details>
                <summary>
                    <div class="summary-title">UI Layout</div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                <div class="details-content" style=""><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 System</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Viewport Center</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="BIG_CENTER_X_slider" min="0" max="480" value="240" oninput="document.getElementById(&#39;BIG_CENTER_X&#39;).value = this.value">
                            <input type="number" id="BIG_CENTER_X" value="240" step="1" oninput="document.getElementById(&#39;BIG_CENTER_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="BIG_CENTER_Y_slider" min="0" max="320" value="160" oninput="document.getElementById(&#39;BIG_CENTER_Y&#39;).value = this.value">
                            <input type="number" id="BIG_CENTER_Y" value="160" step="1" oninput="document.getElementById(&#39;BIG_CENTER_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">FPS Counter</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_FPS_X_slider" min="-240" max="240" value="0" oninput="document.getElementById(&#39;OFFSET_BIG_FPS_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_FPS_X" value="0" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_FPS_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_FPS_Y_slider" min="-160" max="160" value="-33" oninput="document.getElementById(&#39;OFFSET_BIG_FPS_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_FPS_Y" value="-33" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_FPS_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Signature</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_SIGNATURE_X_slider" min="-240" max="240" value="0" oninput="document.getElementById(&#39;OFFSET_BIG_SIGNATURE_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SIGNATURE_X" value="0" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SIGNATURE_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_SIGNATURE_Y_slider" min="-160" max="160" value="-102" oninput="document.getElementById(&#39;OFFSET_BIG_SIGNATURE_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SIGNATURE_Y" value="-102" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SIGNATURE_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 Speed &amp; Odo</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Acceleration Timer</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_TMR_X_slider" min="-240" max="240" value="-53" oninput="document.getElementById(&#39;OFFSET_BIG_TMR_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_TMR_X" value="-53" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_TMR_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_TMR_Y_slider" min="-160" max="160" value="-68" oninput="document.getElementById(&#39;OFFSET_BIG_TMR_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_TMR_Y" value="-68" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_TMR_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Speed Source Icon</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_HALL_ICON_X_slider" min="-240" max="240" value="0" oninput="document.getElementById(&#39;OFFSET_HALL_ICON_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_HALL_ICON_X" value="0" step="1" oninput="document.getElementById(&#39;OFFSET_HALL_ICON_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_HALL_ICON_Y_slider" min="-160" max="160" value="-140" oninput="document.getElementById(&#39;OFFSET_HALL_ICON_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_HALL_ICON_Y" value="-140" step="1" oninput="document.getElementById(&#39;OFFSET_HALL_ICON_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Main Speed Number</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_SPEED_NUM_X_slider" min="-240" max="240" value="-20" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_NUM_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SPEED_NUM_X" value="-20" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_NUM_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_SPEED_NUM_Y_slider" min="-160" max="160" value="-15" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_NUM_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SPEED_NUM_Y" value="-15" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_NUM_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Speed Unit</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_SPEED_UNIT_X_slider" min="-240" max="240" value="103" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_UNIT_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SPEED_UNIT_X" value="103" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_UNIT_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_SPEED_UNIT_Y_slider" min="-160" max="160" value="26" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_UNIT_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SPEED_UNIT_Y" value="26" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SPEED_UNIT_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Odometer</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_ODO_X_slider" min="-240" max="240" value="15" oninput="document.getElementById(&#39;OFFSET_BIG_ODO_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_ODO_X" value="15" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_ODO_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_ODO_Y_slider" min="-160" max="160" value="150" oninput="document.getElementById(&#39;OFFSET_BIG_ODO_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_ODO_Y" value="150" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_ODO_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 Fuel &amp; Battery</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Average KM/L</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_AVG_KML_X_slider" min="-240" max="240" value="150" oninput="document.getElementById(&#39;OFFSET_AVG_KML_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_AVG_KML_X" value="150" step="1" oninput="document.getElementById(&#39;OFFSET_AVG_KML_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_AVG_KML_Y_slider" min="-160" max="160" value="-49" oninput="document.getElementById(&#39;OFFSET_AVG_KML_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_AVG_KML_Y" value="-49" step="1" oninput="document.getElementById(&#39;OFFSET_AVG_KML_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Instant KM/L</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_INST_KML_X_slider" min="-240" max="240" value="60" oninput="document.getElementById(&#39;OFFSET_INST_KML_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_INST_KML_X" value="60" step="1" oninput="document.getElementById(&#39;OFFSET_INST_KML_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_INST_KML_Y_slider" min="-160" max="160" value="-49" oninput="document.getElementById(&#39;OFFSET_INST_KML_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_INST_KML_Y" value="-49" step="1" oninput="document.getElementById(&#39;OFFSET_INST_KML_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Fuel Liters</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_FUEL_LTRS_X_slider" min="-240" max="240" value="132" oninput="document.getElementById(&#39;OFFSET_FUEL_LTRS_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_FUEL_LTRS_X" value="132" step="1" oninput="document.getElementById(&#39;OFFSET_FUEL_LTRS_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_FUEL_LTRS_Y_slider" min="-160" max="160" value="150" oninput="document.getElementById(&#39;OFFSET_FUEL_LTRS_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_FUEL_LTRS_Y" value="150" step="1" oninput="document.getElementById(&#39;OFFSET_FUEL_LTRS_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Battery</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_BAT_X_slider" min="-240" max="240" value="-112" oninput="document.getElementById(&#39;OFFSET_BIG_BAT_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_BAT_X" value="-112" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_BAT_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_BAT_Y_slider" min="-160" max="160" value="149" oninput="document.getElementById(&#39;OFFSET_BIG_BAT_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_BAT_Y" value="149" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_BAT_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 Clock &amp; Date</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Clock</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_TIME_X_slider" min="-240" max="240" value="-98" oninput="document.getElementById(&#39;OFFSET_BIG_TIME_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_TIME_X" value="-98" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_TIME_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_TIME_Y_slider" min="-160" max="160" value="-132" oninput="document.getElementById(&#39;OFFSET_BIG_TIME_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_TIME_Y" value="-132" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_TIME_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Date</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_DATE_X_slider" min="-240" max="240" value="118" oninput="document.getElementById(&#39;OFFSET_BIG_DATE_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_DATE_X" value="118" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_DATE_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_DATE_Y_slider" min="-160" max="160" value="-132" oninput="document.getElementById(&#39;OFFSET_BIG_DATE_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_DATE_Y" value="-132" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_DATE_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 WiFi &amp; Satellites</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Satellites</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_BIG_SAT_X_slider" min="-240" max="240" value="-180" oninput="document.getElementById(&#39;OFFSET_BIG_SAT_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SAT_X" value="-180" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SAT_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_BIG_SAT_Y_slider" min="-160" max="160" value="-132" oninput="document.getElementById(&#39;OFFSET_BIG_SAT_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_BIG_SAT_Y" value="-132" step="1" oninput="document.getElementById(&#39;OFFSET_BIG_SAT_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">WiFi Icon</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="OFFSET_WIFI_ICON_X_slider" min="-240" max="240" value="215" oninput="document.getElementById(&#39;OFFSET_WIFI_ICON_X&#39;).value = this.value">
                            <input type="number" id="OFFSET_WIFI_ICON_X" value="215" step="1" oninput="document.getElementById(&#39;OFFSET_WIFI_ICON_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="OFFSET_WIFI_ICON_Y_slider" min="-160" max="160" value="-153" oninput="document.getElementById(&#39;OFFSET_WIFI_ICON_Y&#39;).value = this.value">
                            <input type="number" id="OFFSET_WIFI_ICON_Y" value="-153" step="1" oninput="document.getElementById(&#39;OFFSET_WIFI_ICON_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div></div><div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">📊 Sidebars</h4><div style="display:flex;flex-direction:column;gap:6px;"></div><div class="section-title">Left Bar (Engine Temp)</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="SIDEBAR_LEFT_X_slider" min="0" max="480" value="10" oninput="document.getElementById(&#39;SIDEBAR_LEFT_X&#39;).value = this.value">
                            <input type="number" id="SIDEBAR_LEFT_X" value="10" step="1" oninput="document.getElementById(&#39;SIDEBAR_LEFT_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="SIDEBAR_LEFT_Y_slider" min="0" max="320" value="64" oninput="document.getElementById(&#39;SIDEBAR_LEFT_Y&#39;).value = this.value">
                            <input type="number" id="SIDEBAR_LEFT_Y" value="64" step="1" oninput="document.getElementById(&#39;SIDEBAR_LEFT_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Right Bar (Fuel)</div><div style="display:flex;flex-direction:column;gap:6px;">
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">Position</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="SIDEBAR_RIGHT_X_slider" min="0" max="480" value="462" oninput="document.getElementById(&#39;SIDEBAR_RIGHT_X&#39;).value = this.value">
                            <input type="number" id="SIDEBAR_RIGHT_X" value="462" step="1" oninput="document.getElementById(&#39;SIDEBAR_RIGHT_X_slider&#39;).value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="SIDEBAR_RIGHT_Y_slider" min="0" max="320" value="64" oninput="document.getElementById(&#39;SIDEBAR_RIGHT_Y&#39;).value = this.value">
                            <input type="number" id="SIDEBAR_RIGHT_Y" value="64" step="1" oninput="document.getElementById(&#39;SIDEBAR_RIGHT_Y_slider&#39;).value = this.value">
                        </div>
                    </div></div><div class="section-title">Bar Dimensions</div><div style="display:flex;flex-direction:column;gap:6px;"><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Width (px)</label><input type="number" id="SIDEBAR_BAR_WIDTH" value="8" step="any" style="width:85px;text-align:center;"></div><div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>Height (px)</label><input type="number" id="SIDEBAR_BAR_HEIGHT" value="250" step="any" style="width:85px;text-align:center;"></div></div></div></div>
            </details></div>

<details id="perf-panel">
    <summary>
        <div class="summary-left">
            <div class="summary-icon-badge">
                <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>
            </div>
            <span>Performance Monitor</span>
        </div>
        <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
    </summary>
    <div class="details-content" id="perf-content" style="display: block;">
        <div class="perf-grid" id="perf-grid">
        <div class="perf-card">
            <h4>Display</h4>
            <div class="perf-row"><span class="perf-label">Current FPS</span><span class="perf-value" id="p-fps-cur">62.5</span></div>
            <div class="perf-row"><span class="perf-label">Average FPS</span><span class="perf-value" id="p-fps-avg">62.5</span></div>
            <div class="perf-row"><span class="perf-label">Target FPS</span><span class="perf-value" id="p-fps-tgt">60</span></div>
            <div class="perf-row"><span class="perf-label">Refresh</span><span class="perf-value" id="p-refresh">16 ms</span></div>
            <div class="perf-row"><span class="perf-label">Resolution</span><span class="perf-value" id="p-resolution">480x320</span></div>
        </div>
        <div class="perf-card">
            <h4>System</h4>
            <div class="perf-row"><span class="perf-label">Uptime</span><span class="perf-value" id="p-uptime">1h 22m 50s</span></div>
            <div class="perf-row"><span class="perf-label">WiFi Clients</span><span class="perf-value" id="p-wifi">0</span></div>
            <div class="perf-row"><span class="perf-label">SPI Bus</span><span class="perf-value" id="p-spi">60 MHz</span></div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>CPU Performance</h4>
            <div class="perf-row"><span class="perf-label">Frequency</span><span class="perf-value" id="p-cpu-freq">240 MHz</span></div>
            <div class="perf-row"><span class="perf-label">Mode</span><span class="perf-value" id="p-cpu-mode">Fixed 240 MHz</span></div>
            <div class="perf-row"><span class="perf-label">Temperature</span><span class="perf-value" id="p-cpu-temp">50.0 °C</span></div>
            <div style="margin-top:8px;">
                <div style="display:flex;justify-content:space-between;font-size:12px;color:var(--text-muted);margin-bottom:4px;">
                    <span>CPU Usage</span><span class="perf-value"><strong id="cpu-pct">100.0</strong>%</span>
                </div>
                <div class="perf-bar">
                    <div id="cpu-used-bar" class="perf-bar-fill" style="width:100%;background:var(--accent-rose);border-radius:4px;"></div>
                </div>
            </div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>Memory &amp; Storage</h4>
            <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;display:flex;justify-content:space-between;">
                <span>RAM Usage</span><span><strong id="mem-used" style="color:#fff;">118</strong> / <strong id="mem-total" style="color:#fff;">292</strong> KB (<strong id="mem-pct" style="color:#fff;">40.3</strong>%)</span>
            </div>
            <div class="perf-bar" style="margin-bottom:14px;">
                <div id="mem-used-bar" class="perf-bar-fill" style="width:40%;background:var(--accent-emerald);border-radius:4px;"></div>
            </div>
            <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;display:flex;justify-content:space-between;">
                <span>Flash Storage</span><span><strong id="fl-used" style="color:#fff;">--</strong> / <strong id="fl-total" style="color:#fff;">4096</strong> KB (<strong id="fl-pct" style="color:#fff;">--</strong>%)</span>
            </div>
            <div class="perf-bar" style="margin-bottom:6px;">
                <div id="fl-partitions-bar" style="display:flex;width:100%;height:100%;"></div>
            </div>
            <div id="fl-legend" style="font-size:11px;color:var(--text-muted);display:flex;flex-wrap:wrap;gap:2px 12px;margin-top:6px;"></div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>Serial Output Monitor</h4>
            <div id="serial-out">Waiting for data...0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.46L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.41L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.46L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.63L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.53L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.50L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.70L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.47L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.46L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.46L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.56L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.45L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.47L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.43L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.39L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.45L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.53L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.61L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.54L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.50L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.45L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.47L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.52L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.63L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.56L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.46L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.54L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.52L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.43L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.61L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.48L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.47L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.58L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.71L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.56L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.42L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.39L(91%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.50L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.77L(96%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.53L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.48L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.48L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.61L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.57L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.50L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.70L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.57L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.99L(99%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>RTC sync: 1785219220<br>HALL|6.65L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.68L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.79L(96%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.54L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.61L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.61L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.68L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.50L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.53L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.63L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.77L(96%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.55L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.52L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.57L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|61.9fps<br>HALL|6.51L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.57L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.54L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.55L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.64L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.60L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.71L(95%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.60L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.51L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.49L(92%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.53L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.55L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.55L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.56L(93%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.59L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br>HALL|6.64L(94%)|0.0V|0.0C|0kmh|0sat|0deg|19000.0km|0.00s|62.5fps<br></div>
        </div></div>
    </div>
</details>

<div class="bottom-toolbar">
    <button onclick="save()" class="btn-primary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/></svg> Save Settings</button>
    <button onclick="reboot()" class="btn-warning"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg> Reboot</button>
    <button onclick="deepSleep()" class="btn-secondary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M17.8 1.2l.6 2.4 2.4.6-2.4.6-.6 2.4-.6-2.4-2.4-.6 2.4-.6.6-2.4z"/><path d="M12 2a10 10 0 1 0 10 10c0-2.5-.9-4.8-2.4-6.6"/></svg> Deep Sleep</button>
    <button onclick="exportBackup()" class="btn-secondary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg> Backup</button>
    <button onclick="document.getElementById('importFile').click()" class="btn-secondary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg> Import</button>
    <button id="otaBtn" onclick="doOta()" class="btn-secondary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg> Firmware OTA</button>
    <button id="otaPullBtn" onclick="doOtaPull()" class="btn-secondary"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg> Check Updates</button>
    <button onclick="factoryReset()" class="btn-danger btn-reset-wide"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor"><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/></svg> Reset</button>
</div>

<button id="scrollTopBtn" onclick="window.scrollTo({top:0,behavior:&#39;smooth&#39;})" style="display: none;"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="18 15 12 9 6 15"/></svg></button>

<script>
const iconSVG = {
    system: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>',
    wifi: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1"/></svg>',
    display: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>',
    sensors: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/><line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/><line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/><line x1="1" y1="14" x2="7" y2="14"/><line x1="9" y1="8" x2="15" y2="8"/><line x1="17" y1="16" x2="23" y2="16"/></svg>',
    gnss: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>',
    layout: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><line x1="3" y1="9" x2="21" y2="9"/><line x1="9" y1="21" x2="9" y2="9"/></svg>'
};
const configMap = [
    {
        title: "System & General", icon: iconSVG.system,
        items: [
            { type: "card_header", label: "General" },
            { id: "TARGET_FPS", label: "Target Refresh Rate", unit: "FPS" },
            { id: "SHOW_FPS_COUNTER_DEFAULT", label: "Show FPS Counter" },
            { id: "ENABLE_DEMO_MODE", label: "Enable Sensor Demo Mode" },
            { id: "ENABLE_POWER_SENSE", label: "Enable Power Sensing" },
            { id: "SHOW_ELEMENT_BOUNDS", label: "Show UI Element Bounds (Debug)" },
            { type: "card_header", label: "CPU" },
            { 
                id: "MANUAL_CPU_FREQ", 
                label: "Manual CPU Frequency", 
                type: "select",
                options: [
                    { value: 80, label: "80 MHz" },
                    { value: 160, label: "160 MHz" },
                    { value: 240, label: "240 MHz" }
                ]
            },
            { id: "ENABLE_DYNAMIC_CPU", label: "Enable Dynamic CPU Frequency" },
            { id: "ENABLE_CPU_THROTTLE", label: "Enable Thermal Throttling" },
            { id: "CPU_THROTTLE_TEMP_WARN", label: "Throttle to 160 MHz At", unit: "°C" },
            { id: "CPU_THROTTLE_TEMP_CRIT", label: "Throttle to 80 MHz At", unit: "°C" },
            { type: "card_header", label: "Custom Signatures" },
            { id: "SPLASH_SIGNATURE", label: "Boot Screen Signature" },
            { id: "REBOOT_SIGNATURE", label: "Reboot Screen Signature" },
            { id: "DASHBOARD_SIGNATURE", label: "Main Dashboard Watermark" },
            { type: "card_header", label: "Time Settings" },
            { id: "NTP_ENABLED", label: "Enable NTP Time Sync" },
            { id: "NTP_SERVER", label: "NTP Server" },
            { id: "TZ_DST_ENABLED", label: "Enable Automatic DST (European rules)" },
            {
                id: "TZ_OFFSET_HOURS",
                label: "Time Zone",
                type: "select",
                options: [
                    { value: -12, label: "(GMT-12:00) Baker Island" },
                    { value: -11, label: "(GMT-11:00) American Samoa, Niue" },
                    { value: -10, label: "(GMT-10:00) Honolulu, Papeete" },
                    { value: -9, label: "(GMT-09:00) Anchorage" },
                    { value: -8, label: "(GMT-08:00) Los Angeles, Vancouver, Tijuana" },
                    { value: -7, label: "(GMT-07:00) Denver, Calgary, Phoenix" },
                    { value: -6, label: "(GMT-06:00) Chicago, Mexico City, Winnipeg" },
                    { value: -5, label: "(GMT-05:00) New York, Toronto, Bogota" },
                    { value: -4, label: "(GMT-04:00) Santiago, Caracas, Halifax" },
                    { value: -3, label: "(GMT-03:00) Buenos Aires, Sao Paulo, Montevideo" },
                    { value: -2, label: "(GMT-02:00) Fernando de Noronha" },
                    { value: -1, label: "(GMT-01:00) Azores, Cape Verde" },
                    { value: 0, label: "(GMT+00:00) London, Dublin, Lisbon, Accra" },
                    { value: 1, label: "(GMT+01:00) Paris, Berlin, Rome, Madrid, Lagos" },
                    { value: 2, label: "(GMT+02:00) Cairo, Athens, Helsinki, Kyiv" },
                    { value: 3, label: "(GMT+03:00) Moscow, Istanbul, Nairobi, Baghdad" },
                    { value: 4, label: "(GMT+04:00) Dubai, Baku, Muscat" },
                    { value: 5, label: "(GMT+05:00) Karachi, Tashkent, Yekaterinburg" },
                    { value: 6, label: "(GMT+06:00) Dhaka, Almaty, Omsk" },
                    { value: 7, label: "(GMT+07:00) Bangkok, Jakarta, Ho Chi Minh" },
                    { value: 8, label: "(GMT+08:00) Beijing, Singapore, Perth, Taipei" },
                    { value: 9, label: "(GMT+09:00) Tokyo, Seoul, Pyongyang" },
                    { value: 10, label: "(GMT+10:00) Sydney, Melbourne, Guam" },
                    { value: 11, label: "(GMT+11:00) Solomon Islands, Noumea" },
                    { value: 12, label: "(GMT+12:00) Auckland, Fiji, Kamchatka" },
                    { value: 13, label: "(GMT+13:00) Apia, Nuku'alofa" },
                    { value: 14, label: "(GMT+14:00) Kiritimati" }
                ]
            }
        ]
    },
    {
        title: "WiFi", icon: iconSVG.wifi,
        items: [
            { type: "card_header", label: "Network Settings" },
            { id: "WIFI_TX_POWER_DBM", label: "TX Power", type: "range", min: -1, max: 20, step: 1, unit: "dBm" },
            { type: "heading", label: "Primary Network" },
            { id: "WIFI_SSID", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD", label: "Password" },
            { type: "card_header", label: "Fallback Networks" },
            { type: "section_header", label: "Network 1" },
            { id: "WIFI_SSID_1", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_1", label: "Password" },
            { type: "section_header", label: "Network 2" },
            { id: "WIFI_SSID_2", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_2", label: "Password" },
            { type: "section_header", label: "Network 3" },
            { id: "WIFI_SSID_3", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_3", label: "Password" },
            { type: "section_header", label: "Network 4" },
            { id: "WIFI_SSID_4", label: "Network Name (SSID)" },
            { id: "WIFI_PASSWORD_4", label: "Password" },
            { type: "card_header", label: "OTA Pull Update" },
            { id: "OTA_PULL_ENABLED", label: "Enable Automatic OTA Pull" },
            { id: "OTA_PULL_URL", label: "Firmware Manifest URL" },
            { id: "OTA_PULL_INTERVAL_HOURS", label: "Check Interval", unit: "hours" },
            { id: "OTA_CURRENT_VERSION", label: "Current Firmware Version" }
        ]
    },
    {
        title: "Display & Colors", icon: iconSVG.display,
        items: [
            { type: "subtitle", label: "Display" },
            { id: "SHUTDOWN_TIME_MS", label: "Shutdown Loading Duration", type: "number", unit: "ms" },
            { id: "DISPLAY_ROTATION", label: "Screen Rotation (0-3)" },
            { id: "DISPLAY_WIDTH", label: "Display Width", unit: "px" },
            { id: "DISPLAY_HEIGHT", label: "Display Height", unit: "px" },
            { id: "SPI_BUS_SPEED", label: "SPI Bus Frequency", unit: "MHz" },
            { id: "DISPLAY_INVERT_COLORS", label: "Invert Display Colors" },
            { id: "ENABLE_ANTIALIASING", label: "Enable Anti-Aliased Rendering" },
            { id: "AA_SHARPNESS", label: "AA Sharpness", type: "range", min: 0.2, max: 1.0, step: 0.1 },
            { type: "subtitle", label: "Brightness Control" },
            { id: "BACKLIGHT_BRIGHTNESS", label: "Backlight Brightness", type: "range", min: 0, max: 100, step: 1 },
            { id: "FADE_DURATION_MS", label: "Fade Duration", type: "range", min: 100, max: 5000, step: 100, unit: "ms" },
            { id: "ENABLE_AUTO_BRIGHTNESS", label: "Enable Auto Brightness" },
            { id: "ENABLE_NIGHT_MODE", label: "Enable Night Mode" },
            { id: "NIGHT_MODE_START_HOUR", label: "Start Time", unit: "Hour" },
            { id: "NIGHT_MODE_END_HOUR", label: "End Time", unit: "Hour" },
            { type: "card_header", label: "Thresholds & Colors" },
            { type: "section_header", label: "Engine Temperature" },
            { type: "threshold-row", label: "Low", colorId: "COLOR_TEMP_NORM", valueId: "TEMP_BAR_MIN", unit: "°C" },
            { type: "threshold-row", label: "Normal", colorId: "COLOR_TEMP_WARN", valueId: "TEMP_WARN_YEL", unit: "°C" },
            { type: "threshold-row", label: "Critical", colorId: "COLOR_TEMP_CRIT", valueId: "TEMP_WARN_RED", unit: "°C" },
            { id: "TEMP_BAR_MAX", label: "Max", unit: "°C" },
            { type: "section_header", label: "Fuel Level" },
            { type: "threshold-row", label: "Normal", colorId: "COLOR_FUEL_NORM" },
            { type: "threshold-row", label: "Warning", colorId: "COLOR_FUEL_WARN", valueId: "FUEL_WARN_YEL", unit: "%" },
            { type: "threshold-row", label: "Critical", colorId: "COLOR_FUEL_CRIT", valueId: "FUEL_WARN_RED", unit: "%" },
            { type: "section_header", label: "General" },
            { id: "GHOST_COLOR_STR", label: "Ghost Digit Color", type: "color" }
        ]
    },
    {
        title: "Sensors Tuning", icon: iconSVG.sensors,
        items: [
            { type: "touch-table", label: "Fuel Level Mapping Table", pointCount: "FUEL_TOUCH_POINTS", arrayId: "touchTable" },
            { type: "card_header", label: "Auto Brightness Sensor", content: `<div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-bottom:6px;">
        <span style="font-size:12px;color:var(--text-muted);">Reading: <strong id="ambient-reading" style="color:#fff;font-family:'JetBrains Mono',monospace;">--</strong></span>
        <div style="display:flex;gap:6px;">
            <button onclick="calibrateDark()" class="btn-secondary" style="font-size:11px;padding:5px 10px;">Capture Dark</button>
            <button onclick="calibrateBright()" class="btn-secondary" style="font-size:11px;padding:5px 10px;">Capture Bright</button>
        </div>
    </div>` },
            { id: "LIGHT_SENSOR_DARK_VAL", label: "Dark Value (ADC)", type: "number" },
            { id: "LIGHT_SENSOR_BRIGHT_VAL", label: "Bright Value (ADC)", type: "number" },
            { id: "AUTO_BRIGHT_DARK", label: "Brightness When Dark", type: "range", min: 0, max: 100, step: 1 },
            { id: "AUTO_BRIGHT_LIGHT", label: "Brightness When Bright", type: "range", min: 0, max: 100, step: 1 },
            { id: "AUTO_BRIGHT_FADE_MS", label: "Transition Speed", type: "range", min: 100, max: 5000, step: 100, unit: "ms" },
            { type: "subtitle", label: "Temperature Sensor" },
            { id: "NTC_R_BALANCE", label: "NTC Balance Resistor", unit: "Ohms" },
            { id: "NTC_BETA", label: "NTC Beta Value" },
            { type: "card_header", label: "Acceleration Timer" },
            { id: "ACCEL_START_SPEED", label: "Timer Start Speed", unit: "km/h" },
            { id: "ACCEL_TARGET_SPEED", label: "Timer Target Speed", unit: "km/h" },
            { id: "ACCEL_MAX_TIME", label: "Timer Max Time (0 = off)", unit: "sec" },
            { id: "ACCEL_BADGE_LINE1", label: "Badge Line 1 (e.g., '0-50')" },
            { id: "ACCEL_BADGE_LINE2", label: "Badge Line 2 (e.g., 'km/h')" },
            { type: "subtitle", label: "Miscellaneous" },
            { type: "card_header", label: "Odometer (km)", content: `<div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-bottom:6px;">
        <span style="font-size:12px;color:var(--text-muted);">Current value: <strong id="odo-reading" style="color:#fff;font-family:'JetBrains Mono',monospace;">--</strong> km</span>
    </div>
    <div style="display:flex;align-items:center;gap:8px;">
        <input type="number" id="ODO_SET" step="0.1" min="0" placeholder="Set odometer (km)" style="flex:1;min-width:0;">
        <button onclick="setOdometer()" class="btn-secondary" style="font-size:11px;padding:5px 10px;white-space:nowrap;">Set Value</button>
    </div>` },
            { id: "WHEEL_CIRCUMFERENCE_MM", label: "Wheel Circumference", unit: "mm" },
            { id: "MIN_SPEED_THRESHOLD", label: "Minimum Speed Threshold", unit: "km/h" },
            { id: "FUEL_FILTER_ALPHA", label: "Fuel Sensor Smoothing Alpha" },
            { type: "subtitle", label: "Polling Rates" },
            { id: "REFRESH_SPEED_MS", label: "Speed", type: "number", unit: "ms" },
            { id: "REFRESH_SAT_MS", label: "Satellites", type: "number", unit: "ms" },
            { id: "REFRESH_TMR_MS", label: "Accel Timer", type: "number", unit: "ms" },
            { id: "REFRESH_BAT_MS", label: "Battery Voltage", type: "number", unit: "ms" },
            { id: "REFRESH_AVG_MS", label: "Average KM/L", type: "number", unit: "ms" },
            { id: "REFRESH_AVG_SPEED_MS", label: "Average Speed", type: "number", unit: "ms" },
            { id: "REFRESH_FUEL_MS", label: "Fuel Liters", type: "number", unit: "ms" },
            { id: "REFRESH_ODO_MS", label: "Odometer", type: "number", unit: "ms" },
            { type: "card_header", label: "Digit Count Configuration" },
            { id: "SPEED_DIGITS", label: "Speed Digits" },
            { id: "SAT_DIGITS", label: "Satellites Digits" },
            { id: "TMR_INT_DIGITS", label: "Timer Integer Digits" },
            { id: "TMR_DEC_DIGITS", label: "Timer Decimal Digits" },
            { id: "BAT_INT_DIGITS", label: "Battery Integer Digits" },
            { id: "BAT_DEC_DIGITS", label: "Battery Decimal Digits" },
            { id: "INST_INT_DIGITS", label: "Instant KM/L Integer Digits" },
            { id: "INST_DEC_DIGITS", label: "Instant KM/L Decimal Digits" },
            { id: "AVG_INT_DIGITS", label: "Average KM/L Integer Digits" },
            { id: "AVG_DEC_DIGITS", label: "Average KM/L Decimal Digits" },
            { id: "AVG_SPEED_INT_DIGITS", label: "Average Speed Integer Digits" },
            { id: "AVG_SPEED_DEC_DIGITS", label: "Average Speed Decimal Digits" },
            { id: "FUEL_INT_DIGITS", label: "Fuel Integer Digits" },
            { id: "FUEL_DEC_DIGITS", label: "Fuel Decimal Digits" },
            { id: "ODO_INT_DIGITS", label: "Odometer Integer Digits" },
            { id: "ODO_DEC_DIGITS", label: "Odometer Decimal Digits" }
        ]
    },
    {
        title: "GNSS", icon: iconSVG.gnss,
        items: [
            { type: "card_header", label: "GNSS Settings" },
            { id: "MIN_SATELLITES", label: "Minimum Satellites Required" },
            { id: "OPTIMAL_SATELLITES", label: "Optimal Satellites Count" },
            { id: "MAX_SPEED_DELTA_KMH", label: "Max Deviation Hall vs GPS", unit: "km/h" }
        ]
    },
    {
        title: "UI Layout", icon: iconSVG.layout,
        items: [
            { type: "card_header", label: "System" },
            { type: "section_header", label: "Viewport Center" },
            { type: 'xy', idX: "BIG_CENTER_X", idY: "BIG_CENTER_Y", label: "Position" },
            { type: "section_header", label: "FPS Counter" },
            { type: 'xy', idX: "OFFSET_BIG_FPS_X", idY: "OFFSET_BIG_FPS_Y", label: "Position" },
            { type: "section_header", label: "Signature" },
            { type: 'xy', idX: "OFFSET_BIG_SIGNATURE_X", idY: "OFFSET_BIG_SIGNATURE_Y", label: "Position" },
            { type: "card_header", label: "Speed & Odometer" },
            { type: "section_header", label: "Acceleration Timer" },
            { type: 'xy', idX: "OFFSET_BIG_TMR_X", idY: "OFFSET_BIG_TMR_Y", label: "Position" },
            { type: "section_header", label: "Speed Source Icon" },
            { type: 'xy', idX: "OFFSET_HALL_ICON_X", idY: "OFFSET_HALL_ICON_Y", label: "Position" },
            { type: "section_header", label: "Main Speed Number" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_NUM_X", idY: "OFFSET_BIG_SPEED_NUM_Y", label: "Position" },
            { type: "section_header", label: "Speed Unit" },
            { type: 'xy', idX: "OFFSET_BIG_SPEED_UNIT_X", idY: "OFFSET_BIG_SPEED_UNIT_Y", label: "Position" },
            { type: "section_header", label: "Odometer" },
            { type: 'xy', idX: "OFFSET_BIG_ODO_X", idY: "OFFSET_BIG_ODO_Y", label: "Position" },
            { type: "card_header", label: "Fuel & Battery" },
            { type: "section_header", label: "Average KM/L" },
            { type: 'xy', idX: "OFFSET_AVG_KML_X", idY: "OFFSET_AVG_KML_Y", label: "Position" },
            { type: "section_header", label: "Average Speed" },
            { type: 'xy', idX: "OFFSET_AVG_SPEED_X", idY: "OFFSET_AVG_SPEED_Y", label: "Position" },
            { type: "section_header", label: "Instant KM/L" },
            { type: 'xy', idX: "OFFSET_INST_KML_X", idY: "OFFSET_INST_KML_Y", label: "Position" },
            { type: "section_header", label: "Fuel Liters" },
            { type: 'xy', idX: "OFFSET_FUEL_LTRS_X", idY: "OFFSET_FUEL_LTRS_Y", label: "Position" },
            { type: "section_header", label: "Battery" },
            { type: 'xy', idX: "OFFSET_BIG_BAT_X", idY: "OFFSET_BIG_BAT_Y", label: "Position" },
            { type: "card_header", label: "Clock & Date" },
            { type: "section_header", label: "Clock" },
            { type: 'xy', idX: "OFFSET_BIG_TIME_X", idY: "OFFSET_BIG_TIME_Y", label: "Position" },
            { type: "section_header", label: "Date" },
            { type: 'xy', idX: "OFFSET_BIG_DATE_X", idY: "OFFSET_BIG_DATE_Y", label: "Position" },
            { type: "card_header", label: "WiFi & Satellites" },
            { type: "section_header", label: "Satellites" },
            { type: 'xy', idX: "OFFSET_BIG_SAT_X", idY: "OFFSET_BIG_SAT_Y", label: "Position" },
            { type: "section_header", label: "WiFi Icon" },
            { type: 'xy', idX: "OFFSET_WIFI_ICON_X", idY: "OFFSET_WIFI_ICON_Y", label: "Position" },
            { type: "card_header", label: "Sidebars" },
            { type: "section_header", label: "Left Bar (Engine Temp)" },
            { type: 'xy', idX: "SIDEBAR_LEFT_X", idY: "SIDEBAR_LEFT_Y", label: "Position" },
            { type: "section_header", label: "Right Bar (Fuel)" },
            { type: 'xy', idX: "SIDEBAR_RIGHT_X", idY: "SIDEBAR_RIGHT_Y", label: "Position" },
            { type: "section_header", label: "Bar Dimensions" },
            { id: "SIDEBAR_BAR_WIDTH", label: "Width", unit: "px" },
            { id: "SIDEBAR_BAR_HEIGHT", label: "Height", unit: "px" }
        ]
    }
];

document.addEventListener('DOMContentLoaded', function() {
    fetch('/api/config').then(r => r.json()).then(d => {
        renderForm(d);
    }).catch(() => {
        // Fallback demo dataset if API isn't present
        const demoData = {
            TARGET_FPS: 60, SHOW_FPS_COUNTER_DEFAULT: true, ENABLE_DEMO_MODE: true, ENABLE_POWER_SENSE: false, SHOW_ELEMENT_BOUNDS: false,
            NTP_ENABLED: true, NTP_SERVER: "pool.ntp.org", TZ_OFFSET_HOURS: 1, TZ_DST_ENABLED: true,
            MANUAL_CPU_FREQ: 240, ENABLE_DYNAMIC_CPU: false, ENABLE_CPU_THROTTLE: true, CPU_THROTTLE_TEMP_WARN: 60, CPU_THROTTLE_TEMP_CRIT: 70,
            SPLASH_SIGNATURE: "by @ale.finot", REBOOT_SIGNATURE: "Dashboard++ by @ale.finot", DASHBOARD_SIGNATURE: "<<<<<<    Dashboard++ by @ale.finot    >>>>>>",
            WIFI_TX_POWER_DBM: 20, WIFI_SSID: "Xiaomi 15", WIFI_PASSWORD: "••••••••", WIFI_SSID_1: "D-Link-627F3B", WIFI_PASSWORD_1: "••••••••",
            WIFI_SSID_2: "TP-Link_F3EB", WIFI_PASSWORD_2: "••••••••", WIFI_SSID_3: "", WIFI_PASSWORD_3: "", WIFI_SSID_4: "", WIFI_PASSWORD_4: "",
            SHUTDOWN_TIME_MS: 3000, DISPLAY_ROTATION: 3, DISPLAY_WIDTH: 480, DISPLAY_HEIGHT: 320, SPI_BUS_SPEED: 60000000, DISPLAY_INVERT_COLORS: false,
            ENABLE_ANTIALIASING: true, AA_SHARPNESS: 0.2, BACKLIGHT_BRIGHTNESS: 100, ENABLE_AUTO_BRIGHTNESS: false,
            LIGHT_SENSOR_DARK_VAL: 500, LIGHT_SENSOR_BRIGHT_VAL: 3000, AUTO_BRIGHT_DARK: 10, AUTO_BRIGHT_LIGHT: 100, AUTO_BRIGHT_FADE_MS: 1000,
            ambientLightValue: 2048, FADE_DURATION_MS: 700, ENABLE_NIGHT_MODE: false,
            NIGHT_MODE_START_HOUR: 23, NIGHT_MODE_END_HOUR: 0, COLOR_TEMP_NORM: "#00ffff", TEMP_BAR_MIN: 10, COLOR_TEMP_WARN: "#ff8c00",
            TEMP_WARN_YEL: 45, COLOR_TEMP_CRIT: "#ff0000", TEMP_WARN_RED: 90, TEMP_BAR_MAX: 110, COLOR_FUEL_NORM: "#00ff00", COLOR_FUEL_WARN: "#ffff00",
            FUEL_WARN_YEL: 45, COLOR_FUEL_CRIT: "#ff0000", FUEL_WARN_RED: 20, GHOST_COLOR_STR: "#6b6b6b", FUEL_TOUCH_POINTS: 8,
            touchTable: [950, 840, 750, 670, 600, 530, 460, 400], NTC_R_BALANCE: 10000, NTC_BETA: 3950, ACCEL_START_SPEED: 1, ACCEL_TARGET_SPEED: 50, ACCEL_MAX_TIME: 30,
            ACCEL_BADGE_LINE1: "0-50", ACCEL_BADGE_LINE2: "km/h", WHEEL_CIRCUMFERENCE_MM: 1650, MIN_SPEED_THRESHOLD: 1, FUEL_FILTER_ALPHA: 0.08,
            REFRESH_SPEED_MS: 250, REFRESH_SAT_MS: 1000, REFRESH_TMR_MS: 10, REFRESH_BAT_MS: 2500, SPEED_DIGITS: 3, SAT_DIGITS: 2, TMR_INT_DIGITS: 2,
            TMR_DEC_DIGITS: 2, BAT_INT_DIGITS: 2, BAT_DEC_DIGITS: 1, INST_INT_DIGITS: 2, INST_DEC_DIGITS: 1, AVG_INT_DIGITS: 2, AVG_DEC_DIGITS: 1,
            FUEL_INT_DIGITS: 1, FUEL_DEC_DIGITS: 1, ODO_INT_DIGITS: 5, ODO_DEC_DIGITS: 1, MIN_SATELLITES: 5, OPTIMAL_SATELLITES: 8, MAX_SPEED_DELTA_KMH: 5,
            BIG_CENTER_X: 240, BIG_CENTER_Y: 160, OFFSET_BIG_FPS_X: 0, OFFSET_BIG_FPS_Y: -33, OFFSET_BIG_SIGNATURE_X: 0, OFFSET_BIG_SIGNATURE_Y: -102,
            OFFSET_BIG_TMR_X: -53, OFFSET_BIG_TMR_Y: -68, OFFSET_HALL_ICON_X: 0, OFFSET_HALL_ICON_Y: -140, OFFSET_BIG_SPEED_NUM_X: -20, OFFSET_BIG_SPEED_NUM_Y: -15,
            OFFSET_BIG_SPEED_UNIT_X: 103, OFFSET_BIG_SPEED_UNIT_Y: 26, OFFSET_BIG_ODO_X: 15, OFFSET_BIG_ODO_Y: 150, OFFSET_AVG_KML_X: 150, OFFSET_AVG_KML_Y: -49,
            OFFSET_INST_KML_X: 60, OFFSET_INST_KML_Y: -49, OFFSET_FUEL_LTRS_X: 132, OFFSET_FUEL_LTRS_Y: 150, OFFSET_BIG_BAT_X: -112, OFFSET_BIG_BAT_Y: 149,
            OFFSET_BIG_TIME_X: -98, OFFSET_BIG_TIME_Y: -132, OFFSET_BIG_DATE_X: 118, OFFSET_BIG_DATE_Y: -132, OFFSET_BIG_SAT_X: -180, OFFSET_BIG_SAT_Y: -132,
            OFFSET_WIFI_ICON_X: 215, OFFSET_WIFI_ICON_Y: -153, SIDEBAR_LEFT_X: 10, SIDEBAR_LEFT_Y: 64, SIDEBAR_RIGHT_X: 462, SIDEBAR_RIGHT_Y: 64,
            SIDEBAR_BAR_WIDTH: 8, SIDEBAR_BAR_HEIGHT: 250
        };
        renderForm(demoData);
    });

    syncTime();
    pollAmbient();
    setInterval(pollAmbient, 2000);
    pollOdo();
    setInterval(pollOdo, 1000);

    fetch('/api/perf').then(r => r.json()).then(perf => {
        let ipEl = document.getElementById('ip-info');
        let parts = ['AP: <a href="http://192.168.4.1" target="_blank">192.168.4.1</a>'];
        if (perf.lan_ip && perf.lan_ip.length > 0) {
            parts.unshift('LAN: <a href="http://' + perf.lan_ip + '" target="_blank">' + perf.lan_ip + '</a>');
        }
        ipEl.innerHTML = parts.join(' &nbsp;|&nbsp; ');
    }).catch(() => {});
});

function renderForm(d) {
    let html = '';
    const maxW = d.DISPLAY_WIDTH || 480;
    const maxH = d.DISPLAY_HEIGHT || 320;

    configMap.forEach(group => {
        let groupHtml = `<div class="details-content">`;
        let hasItems = false;
        let inSubCard = false;
        let inCardSubCard = false;

        function closeSubCard() {
            if (inCardSubCard || inSubCard) {
                groupHtml += `</div></div>`;
                inCardSubCard = false;
                inSubCard = false;
            }
        }

        group.items.forEach(item => {
            let iType = item.type || 'standard';

            if (iType === 'card_header' || iType === 'subtitle') {
                hasItems = true;
                closeSubCard();
                groupHtml += `<div class="card" style="grid-column:1/-1;"><h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">${item.label}</h4><div style="display:flex;flex-direction:column;gap:6px;">`;
                if (item.content) groupHtml += item.content;
                inCardSubCard = true;
            } else if (iType === 'section_header' || iType === 'heading') {
                hasItems = true;
                groupHtml += `</div><div class="section-title">${item.label}</div><div style="display:flex;flex-direction:column;gap:6px;">`;
            } else if (iType === 'select') {
                if (d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    let val = d[item.id];
                    let opts = item.options.map(o => `<option value="${o.value}" ${o.value == val ? 'selected' : ''}>${o.label}</option>`).join('');
                    if (inCardSubCard) {
                        groupHtml += `<div style="display:flex;justify-content:space-between;align-items:center;padding:2px 0;"><label style="font-size:12px;">${item.label}</label><select id="${item.id}" style="width:auto;min-width:120px;">${opts}</select></div>`;
                    } else {
                        closeSubCard();
                        groupHtml += `<div class="card"><label>${item.label}</label><select id="${item.id}" style="margin-top:4px;">${opts}</select></div>`;
                    }
                }
            } else if (iType === 'threshold-row') {
                if (d.hasOwnProperty(item.colorId)) {
                    hasItems = true;
                    let valHtml = '';
                    if (item.valueId && d.hasOwnProperty(item.valueId)) {
                        let unitHtml = item.unit ? `<span class="unit">${item.unit}</span>` : '';
                        valHtml = `${unitHtml}<input type="number" id="${item.valueId}" value="${d[item.valueId]}" step="any" style="width:85px;text-align:center;">`;
                    } else {
                        valHtml = `<span style="display:inline-block;width:115px;"></span>`;
                    }
                    groupHtml += `<div style="display:flex;align-items:center;padding:3px 0;"><label style="min-width:170px;">${item.label}</label><div style="display:flex;align-items:center;gap:10px;margin-left:auto;width:200px;justify-content:flex-end;"><input type="color" id="${item.colorId}" value="${d[item.colorId]}">${valHtml}</div></div>`;
                }
            } else if (iType === 'color') {
                if (d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    let val = d[item.id];
                    groupHtml += `<div style="display:flex;align-items:center;padding:3px 0;"><label style="min-width:170px;">${item.label}</label><div style="display:flex;align-items:center;gap:10px;margin-left:auto;width:200px;justify-content:flex-end;"><input type="color" id="${item.id}" value="${val}"><span style="display:inline-block;width:115px;"></span></div></div>`;
                }
            } else if (iType === 'range') {
                if (d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    let val = d[item.id];
                    let unitHtml = item.unit ? `<span class="unit">${item.unit}</span>` : '';
                    let sliderHtml = `
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">${item.label}</label>
                        <div class="xy-row">
                            <input type="range" id="${item.id}_slider" min="${item.min}" max="${item.max}" step="${item.step}" value="${val}" oninput="document.getElementById('${item.id}').value = this.value">
                            ${unitHtml}<input type="number" id="${item.id}" value="${val}" min="${item.min}" max="${item.max}" step="${item.step}" oninput="document.getElementById('${item.id}_slider').value = this.value">
                        </div>
                    </div>`;
                    groupHtml += sliderHtml;
                }
            } else if (iType === 'xy') {
                if (d.hasOwnProperty(item.idX) && d.hasOwnProperty(item.idY)) {
                    hasItems = true;
                    let valX = d[item.idX], valY = d[item.idY];
                    let minX = item.idX.includes("OFFSET") ? -Math.floor(maxW/2) : 0;
                    let maxX = item.idX.includes("OFFSET") ? Math.floor(maxW/2) : maxW;
                    let minY = item.idY.includes("OFFSET") ? -Math.floor(maxH/2) : 0;
                    let maxY = item.idY.includes("OFFSET") ? Math.floor(maxH/2) : maxH;

                    let xyContent = `
                    <div style="padding:4px 0;">
                        <label style="font-size:12px;display:block;margin-bottom:4px;">${item.label}</label>
                        <div class="xy-row">
                            <label>X</label>
                            <input type="range" id="${item.idX}_slider" min="${minX}" max="${maxX}" value="${valX}" oninput="document.getElementById('${item.idX}').value = this.value">
                            <input type="number" id="${item.idX}" value="${valX}" step="1" oninput="document.getElementById('${item.idX}_slider').value = this.value">
                        </div>
                        <div class="xy-row">
                            <label>Y</label>
                            <input type="range" id="${item.idY}_slider" min="${minY}" max="${maxY}" value="${valY}" oninput="document.getElementById('${item.idY}').value = this.value">
                            <input type="number" id="${item.idY}" value="${valY}" step="1" oninput="document.getElementById('${item.idY}_slider').value = this.value">
                        </div>
                    </div>`;
                    groupHtml += xyContent;
                }
            } else if (iType === 'touch-table') {
                closeSubCard();
                hasItems = true;
                let tableArr = d[item.arrayId] || [950, 840, 750, 670, 600, 530, 460, 400];
                let tableHtml = `
                <div class="card" style="grid-column:1/-1;">
                    <h4 style="margin:0 0 6px 0;color:var(--accent-cyan);font-size:13px;">${item.label}</h4>
                    <label style="font-size:11px;">Number of points (tank capacity + 1):</label>
                    <div style="display:flex; align-items:center; gap:8px; margin:6px 0 10px 0;">
                        <input type="number" id="${item.pointCount}" value="${tableArr.length}" min="2" max="20" style="width:70px;">
                        <button onclick="save();setTimeout(() => location.reload(), 500)" class="btn-secondary" style="padding:5px 10px; font-size:11px;">↻ Refresh Table</button>
                    </div>
                    <div style="display:grid; grid-template-columns:repeat(auto-fit, minmax(130px, 1fr)); gap:6px 12px; align-items:center;">`;
                
                tableArr.forEach((val, idx) => {
                    tableHtml += `<div style="display:flex;align-items:center;gap:6px;"><span style="font-weight:700;color:var(--accent-cyan);font-size:11px;font-family:'JetBrains Mono',monospace;">#${idx}</span><input type="number" id="TT_${idx}" value="${val}" style="width:100%;"></div>`;
                });
                tableHtml += `</div></div>`;
                groupHtml += tableHtml;
            } else {
                if (d.hasOwnProperty(item.id)) {
                    hasItems = true;
                    let val = d[item.id];
                    let isBool = (typeof val === 'boolean');

                    if (inCardSubCard) {
                        if (isBool) {
                            groupHtml += `<div style="display:flex;justify-content:space-between;align-items:center;padding:3px 0;"><label>${item.label}</label><input type="checkbox" id="${item.id}" ${val ? 'checked' : ''}></div>`;
                        } else {
                            groupHtml += `<div style="display:flex;align-items:center;padding:3px 0;"><label style="min-width:170px;">${item.label}</label><div style="display:flex;align-items:center;gap:4px;margin-left:auto;width:135px;justify-content:flex-end;">${item.unit ? `<span class="unit">${item.unit}</span>` : ''}<input type="${typeof val === 'number' ? 'number' : 'text'}" id="${item.id}" value="${val}" step="any" style="width:85px;text-align:center;"></div></div>`;
                        }
                    } else {
                        closeSubCard();
                        if (isBool) {
                            groupHtml += `<div class="card checkbox-card"><label>${item.label}</label><input type="checkbox" id="${item.id}" ${val ? 'checked' : ''}></div>`;
                        } else {
                            groupHtml += `<div class="card"><div style="display:flex;align-items:center;gap:8px;"><label style="min-width:170px;">${item.label}</label><div style="display:flex;align-items:center;gap:4px;margin-left:auto;width:135px;justify-content:flex-end;">${item.unit ? `<span class="unit">${item.unit}</span>` : ''}<input type="${typeof val === 'number' ? 'number' : 'text'}" id="${item.id}" value="${val}" step="any" style="width:85px;text-align:center;"></div></div></div>`;
                        }
                    }
                }
            }
        });

        closeSubCard();
        groupHtml += `</div>`;
        if (hasItems) {
            html += `
            <details>
                <summary>
                    <div class="summary-left">
                        <div class="summary-icon-badge">${group.icon || ''}</div>
                        <span class="summary-title">${group.title}</span>
                    </div>
                    <svg class="summary-chevron" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"/></svg>
                </summary>
                ${groupHtml}
            </details>`;
        }
    });

    document.getElementById('form-container').innerHTML = html;
    initDetailsAnim();
    protectSliders();
    updateCpuMode();
    bindPowerSense();
}

function initDetailsAnim() {
    document.querySelectorAll('details:not(#perf-panel)').forEach(el => {
        if (el._animReady) return;
        el._animReady = true;
        const summary = el.querySelector('summary');
        if (!summary) return;
        summary.addEventListener('click', function(e) {
            e.preventDefault();
            if (el.open) {
                el.removeAttribute('open');
            } else {
                document.querySelectorAll('details:not(#perf-panel)[open]').forEach(other => {
                    if (other !== el) other.removeAttribute('open');
                });
                el.setAttribute('open', '');
            }
        });
    });
}

function protectSliders() {
    document.querySelectorAll('input[type="range"]').forEach(el => {
        if (el.dataset.wheelProtected) return;
        el.dataset.wheelProtected = '1';
        el.addEventListener('wheel', function(e) {
            e.preventDefault();
        }, {passive: false});
    });
}

function updateCpuMode() {
    let cb = document.getElementById('ENABLE_DYNAMIC_CPU');
    let sel = document.getElementById('MANUAL_CPU_FREQ');
    if (cb && sel) sel.disabled = cb.checked;
}

function bindPowerSense() {
    document.getElementById('ENABLE_DYNAMIC_CPU')?.addEventListener('change', updateCpuMode);
    document.getElementById('ENABLE_POWER_SENSE')?.addEventListener('change', function() {
        if (this.checked && !confirm("Enabling power sensing will start monitoring vehicle power draw. Continue?")) {
            this.checked = false;
        }
    });
}

window.addEventListener('scroll', function() {
    document.getElementById('scrollTopBtn').style.display = window.scrollY > 300 ? 'flex' : 'none';
});

let autosaveTimer = null;
document.getElementById('form-container').addEventListener('input', function(e) {
    if (e.target.type === 'file') return;
    clearTimeout(autosaveTimer);
    autosaveTimer = setTimeout(save, 2000);
});

function filterConfig() {
    const q = document.getElementById('searchBar').value.toLowerCase().trim();
    document.querySelectorAll('#form-container details').forEach(d => {
        if (d.id === 'perf-panel') return;
        const cards = d.querySelectorAll('.details-content > .card, .details-content > .xy-group');
        let visible = 0;
        cards.forEach(c => {
            const match = !q || c.textContent.toLowerCase().includes(q);
            c.style.display = match ? '' : 'none';
            if (match) visible++;
        });
        d.style.display = (!q || visible > 0 || d.querySelector('summary').textContent.toLowerCase().includes(q)) ? '' : 'none';
        if (q) d.open = true;
    });
}

function save() {
    let out = {};
    document.querySelectorAll('input, select').forEach(el => {
        if (el.id.endsWith('_slider') || el.type === 'file') return;
        if (el.id.startsWith('TT_')) {
            let idx = parseInt(el.id.substring(3));
            if (!out['touchTable']) out['touchTable'] = [];
            out['touchTable'][idx] = parseFloat(el.value) || 0;
        } else if (el.type === 'checkbox') out[el.id] = el.checked;
        else if (el.type === 'number' || el.tagName.toLowerCase() === 'select') {
            let val = parseFloat(el.value);
            out[el.id] = isNaN(val) ? el.value : val;
        } else if (el.id.match(/^WIFI_PASSWORD(_\d+)?$/)) {
            if (el.value === '' && el.hasAttribute('data-orig')) {
                out[el.id] = el.getAttribute('data-orig');
            } else {
                out[el.id] = el.value;
            }
        } else out[el.id] = el.value;
    });
    sendConfigToDevice(out, "Saving settings to device...", "Configuration saved successfully! Display updated.");
}

function reboot() {
    if (!confirm("Are you sure you want to reboot the device?")) return;
    let msgBox = document.getElementById('msg');
    msgBox.className = ''; msgBox.innerText = "Rebooting device...";
    fetch('/api/reboot', {method:'POST'}).then(() => {
        msgBox.className = 'msg-success'; msgBox.innerText = "Reboot command sent. Please refresh shortly.";
        setTimeout(() => location.reload(), 4000);
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = "Connection lost during reboot. Please refresh shortly.";
        setTimeout(() => location.reload(), 4000);
    });
}

function deepSleep() {
    if (!confirm("Put device into deep sleep?")) return;
    let msgBox = document.getElementById('msg');
    msgBox.className = ''; msgBox.innerText = 'Device going to sleep...';
    fetch('/api/sleep', {method:'POST'}).then(() => {
        msgBox.className = 'msg-success'; msgBox.innerText = 'Device is now sleeping.';
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = 'Connection lost (device is sleeping).';
    });
}

function exportBackup() {
    fetch('/api/config').then(r => r.json()).then(d => {
        const blob = new Blob([JSON.stringify(d, null, 2)], {type: "application/json"});
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url; a.download = "dashboard_backup.json";
        document.body.appendChild(a); a.click(); document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }).catch(() => alert("Failed to fetch configuration for backup."));
}

function syncTime() {
    let msgBox = document.getElementById('msg');
    let epoch = Math.floor(Date.now() / 1000);
    fetch('/api/time', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({timestamp: epoch})
    }).then(r => r.json()).then(d => {
        msgBox.className = 'msg-success'; msgBox.innerText = "Time synced successfully!";
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = "Failed to sync time.";
    });
}

function importBackup(event) {
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = function(e) {
        try {
            const jsonConfig = JSON.parse(e.target.result);
            if (!confirm("Are you sure you want to restore these settings? This will overwrite the current configuration.")) {
                event.target.value = ""; return;
            }
            sendConfigToDevice(jsonConfig, "Restoring configuration...", "Backup restored successfully!");
        } catch(err) {
            alert("Error: Invalid JSON file!");
        }
        event.target.value = "";
    };
    reader.readAsText(file);
}

function sendConfigToDevice(jsonPayload, startMsg, successMsg) {
    let msgBox = document.getElementById('msg');
    msgBox.className = ''; msgBox.innerText = startMsg;
    fetch('/api/config', {method:'POST', body:JSON.stringify(jsonPayload)})
    .then(r => r.json()).then(r => {
        msgBox.className = 'msg-success'; msgBox.innerText = successMsg;
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = "Error communicating with the device.";
    });
}

function calibrateDark() {
    fetch('/api/ambient/cal-dark', {method:'POST'}).then(r => r.json()).then(d => {
        let el = document.getElementById('LIGHT_SENSOR_DARK_VAL');
        if (el) el.value = d.value;
        let msg = document.getElementById('msg');
        msg.className = 'msg-success'; msg.innerText = 'Dark calibration saved: ' + d.value;
        setTimeout(() => save(), 100);
    }).catch(() => {});
}

function calibrateBright() {
    fetch('/api/ambient/cal-bright', {method:'POST'}).then(r => r.json()).then(d => {
        let el = document.getElementById('LIGHT_SENSOR_BRIGHT_VAL');
        if (el) el.value = d.value;
        let msg = document.getElementById('msg');
        msg.className = 'msg-success'; msg.innerText = 'Bright calibration saved: ' + d.value;
        setTimeout(() => save(), 100);
    }).catch(() => {});
}

function pollAmbient() {
    fetch('/api/ambient').then(r => r.json()).then(d => {
        let el = document.getElementById('ambient-reading');
        if (el) el.textContent = d.raw;
    }).catch(() => {});
}

function pollOdo() {
    fetch('/api/odo').then(r => r.json()).then(d => {
        let el = document.getElementById('odo-reading');
        if (el) el.textContent = d.km.toFixed(1);
    }).catch(() => {});
}

function setOdometer() {
    let input = document.getElementById('ODO_SET');
    if (!input || input.value === '' || isNaN(parseFloat(input.value))) {
        alert("Enter an odometer value first.");
        return;
    }
    let msgBox = document.getElementById('msg');
    msgBox.className = ''; msgBox.innerText = "Setting odometer...";
    fetch('/api/odo', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({km: parseFloat(input.value)})
    }).then(r => r.json()).then(d => {
        msgBox.className = 'msg-success'; msgBox.innerText = 'Odometer set to ' + d.km.toFixed(1) + ' km';
        input.value = '';
        pollOdo();
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = "Failed to set odometer.";
    });
}

let serialPollTimer = null;
let serialHasData = false;

function pollSerial() {
    fetch('/api/serial').then(r => r.text()).then(data => {
        let el = document.getElementById('serial-out');
        if (!data || data.length === 0) {
            if (!serialHasData) el.innerHTML = 'Waiting for data...';
            return;
        }
        serialHasData = true;
        let html = data.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/\n/g, '<br>');
        let atBottom = el.scrollTop + el.clientHeight >= el.scrollHeight - 30;
        el.insertAdjacentHTML('beforeend', html);
        if (atBottom) el.scrollTop = el.scrollHeight;
    }).catch(() => {});
}

function factoryReset() {
    if (!confirm("Are you sure you want to reset to factory settings? All configurations will be lost!")) return;
    let msgBox = document.getElementById('msg');
    msgBox.className = ''; msgBox.innerText = "Restoring factory settings...";
    fetch('/api/reset', {method:'POST'}).then(r => r.json()).then(r => {
        msgBox.className = 'msg-success'; msgBox.innerText = "Factory reset successful! Device is rebooting...";
        setTimeout(() => location.reload(), 5000);
    }).catch(() => {
        msgBox.className = 'msg-error'; msgBox.innerText = "Connection lost during reboot. Please refresh shortly.";
    });
}

var _otaFile = null;
function doOta() {
    const btn = document.getElementById('otaBtn');
    if (!_otaFile) {
        var inp = document.createElement('input');
        inp.type = 'file'; inp.accept = '.bin';
        inp.onchange = function() {
            if (!inp.files || !inp.files[0]) return;
            _otaFile = inp.files[0];
            btn.textContent = 'Upload ' + _otaFile.name;
        };
        inp.click();
        return;
    }
    if (!confirm('Upload ' + _otaFile.name + ' and reboot the device?')) return;
    btn.disabled = true; btn.textContent = 'Uploading...';
    const fd = new FormData();
    fd.append('firmware', _otaFile);
    fetch('/api/ota', {method:'POST', body:fd}).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            btn.textContent = 'Done';
            setTimeout(() => location.reload(), 10000);
        } else {
            btn.disabled = false; btn.textContent = 'Retry';
        }
    }).catch(() => {
        btn.textContent = 'Rebooting...';
        setTimeout(() => location.reload(), 30000);
    });
}

function doOtaPull() {
    const btn = document.getElementById('otaPullBtn');
    const msgBox = document.getElementById('msg');
    btn.disabled = true; btn.textContent = 'Checking...';
    msgBox.className = ''; msgBox.innerText = 'Checking for firmware updates...';
    fetch('/api/ota/pull', {method:'POST'}).then(r => r.json()).then(d => {
        if (d.status === 'ok') {
            const poll = setInterval(() => {
                fetch('/api/ota/check').then(r => r.json()).then(c => {
                    if (c.status && c.status !== 'checking...') {
                        clearInterval(poll);
                        btn.disabled = false; btn.textContent = 'Check Updates';
                        if (c.status.startsWith('error')) {
                            msgBox.className = 'msg-error';
                            msgBox.innerText = 'Update check failed: ' + c.status;
                        } else if (c.status.startsWith('up-to-date')) {
                            msgBox.className = 'msg-success';
                            msgBox.innerText = 'Firmware is ' + c.status;
                        } else if (c.status.startsWith('updating')) {
                            msgBox.className = 'msg-success';
                            msgBox.innerText = c.status + ' - check the display for progress. Device will reboot.';
                        } else {
                            msgBox.className = 'msg-success';
                            msgBox.innerText = c.status;
                        }
                    }
                }).catch(() => {});
            }, 1500);
            setTimeout(() => { clearInterval(poll); btn.disabled = false; btn.textContent = 'Check Updates'; }, 120000);
        } else {
            msgBox.className = 'msg-error';
            msgBox.innerText = d.msg || 'Update check failed.';
            btn.disabled = false; btn.textContent = 'Check Updates';
        }
    }).catch(() => {
        msgBox.className = 'msg-error';
        msgBox.innerText = 'Communication error. Device may be rebooting for update.';
        setTimeout(() => { btn.disabled = false; btn.textContent = 'Check Updates'; }, 10000);
    });
}

let perfTimer = null;
function buildPerfPanel() {
    let grid = document.getElementById('perf-grid');
    if (!grid || grid.children.length > 0) return;
    grid.innerHTML = `
        <div class="perf-card">
<h4>Display</h4>
            <div class="perf-row"><span class="perf-label">Current FPS</span><span class="perf-value" id="p-fps-cur">--</span></div>
            <div class="perf-row"><span class="perf-label">Average FPS</span><span class="perf-value" id="p-fps-avg">--</span></div>
            <div class="perf-row"><span class="perf-label">Target FPS</span><span class="perf-value" id="p-fps-tgt">--</span></div>
            <div class="perf-row"><span class="perf-label">Refresh</span><span class="perf-value" id="p-refresh">--</span></div>
            <div class="perf-row"><span class="perf-label">Resolution</span><span class="perf-value" id="p-resolution">--</span></div>
        </div>
        <div class="perf-card">
<h4>System</h4>
            <div class="perf-row"><span class="perf-label">Uptime</span><span class="perf-value" id="p-uptime">--</span></div>
            <div class="perf-row"><span class="perf-label">WiFi Clients</span><span class="perf-value" id="p-wifi">--</span></div>
            <div class="perf-row"><span class="perf-label">SPI Bus</span><span class="perf-value" id="p-spi">--</span></div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
<h4>CPU Performance</h4>
            <div class="perf-row"><span class="perf-label">Frequency</span><span class="perf-value" id="p-cpu-freq">--</span></div>
            <div class="perf-row"><span class="perf-label">Mode</span><span class="perf-value" id="p-cpu-mode">--</span></div>
            <div class="perf-row"><span class="perf-label">Temperature</span><span class="perf-value" id="p-cpu-temp">--</span></div>
            <div style="margin-top:8px;">
                <div style="display:flex;justify-content:space-between;font-size:12px;color:var(--text-muted);margin-bottom:4px;">
                    <span>CPU Usage</span><span class="perf-value"><strong id="cpu-pct">--</strong>%</span>
                </div>
                <div class="perf-bar">
                    <div id="cpu-used-bar" class="perf-bar-fill" style="width:0;background:var(--accent-emerald);"></div>
                </div>
            </div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>Memory &amp; Storage</h4>
            <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;display:flex;justify-content:space-between;">
                <span>RAM Usage</span><span><strong id="mem-used" style="color:#fff;">--</strong> / <strong id="mem-total" style="color:#fff;">--</strong> KB (<strong id="mem-pct" style="color:#fff;">--</strong>%)</span>
            </div>
            <div class="perf-bar" style="margin-bottom:8px;">
                <div id="mem-used-bar" class="perf-bar-fill" style="width:0;background:var(--accent-emerald);"></div>
            </div>
            <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;display:flex;justify-content:space-between;">
                <span>Flash Storage</span><span><strong id="fl-used" style="color:#fff;">--</strong> / <strong id="fl-total" style="color:#fff;">--</strong> KB (<strong id="fl-pct" style="color:#fff;">--</strong>%)</span>
            </div>
            <div class="perf-bar">
                <div id="fl-used-bar" class="perf-bar-fill" style="width:0;background:var(--accent-blue);"></div>
            </div>
        </div>
        <div class="perf-card" style="grid-column:1/-1;">
            <h4>Serial Output Monitor</h4>
            <div id="serial-out">Waiting for data...</div>
        </div>`;
}

function updatePerf() {
    fetch('/api/perf').then(r => r.json()).then(d => {
        function set(id, v) { let el = document.getElementById(id); if (el) el.textContent = v; }
        set('p-cpu-freq', d.cpu_freq + ' MHz');
        set('p-cpu-mode', d.cpu_dynamic ? 'Dynamic' : 'Fixed ' + d.cpu_freq + ' MHz');
        set('p-cpu-temp', d.cpu_temp.toFixed(1) + ' °C');
        let cPct = Math.min(d.cpu_usage, 100);
        let cBar = document.getElementById('cpu-used-bar');
        if (cBar) {
            cBar.style.width = cPct.toFixed(0) + '%';
            cBar.style.background = cPct < 50 ? 'var(--accent-emerald)' : cPct < 80 ? 'var(--accent-amber)' : 'var(--accent-rose)';
        }
        set('cpu-pct', cPct.toFixed(1));

        let rUsed = d.heap_size - d.free_heap;
        let rPct = d.heap_size > 0 ? (rUsed / d.heap_size * 100) : 0;
        let rBar = document.getElementById('mem-used-bar');
        if (rBar) {
            rBar.style.width = rPct.toFixed(0) + '%';
            rBar.style.background = rPct < 70 ? 'var(--accent-emerald)' : rPct < 85 ? 'var(--accent-amber)' : 'var(--accent-rose)';
        }
        set('mem-used', (rUsed / 1024).toFixed(0));
        set('mem-total', (d.heap_size / 1024).toFixed(0));
        set('mem-pct', rPct.toFixed(1));

        let flTotal = d.flash_total;
        let flUsed = d.flash_used || 0;
        let flPct = flTotal > 0 ? (flUsed / flTotal * 100) : 0;
        set('fl-used', (flUsed / 1024).toFixed(0));
        set('fl-total', (flTotal / 1024).toFixed(0));
        set('fl-pct', flPct.toFixed(1));
        let partBar = document.getElementById('fl-partitions-bar');
        let legend = document.getElementById('fl-legend');
        if (partBar && d.partitions && d.partitions.length) {
            let colors = ['#a855f7','#f59e0b','#06b6d4','#3b82f6','#10b981'];
            let usedTotal = 0, barHtml = '', legendHtml = '';
            d.partitions.forEach((p, i) => {
                let used = p.used !== undefined ? p.used : 0;
                if (used <= 0) return;
                let color = colors[i % colors.length];
                let pct = flTotal > 0 ? (used / flTotal * 100) : 0;
                usedTotal += used;
                barHtml += '<div style="width:' + pct.toFixed(2) + '%;background:' + color + ';height:100%;min-width:2px;" title="' + p.label + ': ' + (used/1024).toFixed(0) + 'KB"></div>';
                let info = p.label + ' ' + (used/1024).toFixed(0) + 'KB (' + pct.toFixed(1) + '%)';
                legendHtml += '<span style="display:inline-flex;align-items:center;gap:3px;"><span style="width:8px;height:8px;border-radius:2px;background:' + color + ';display:inline-block;"></span>' + info + '</span>';
            });
            let freePct = flTotal > usedTotal ? ((flTotal - usedTotal) / flTotal * 100) : 0;
            if (freePct > 0) {
                barHtml += '<div style="flex:1;background:var(--bg-tertiary);height:100%;" title="Free: ' + ((flTotal - usedTotal)/1024).toFixed(0) + 'KB"></div>';
                legendHtml += '<span style="display:inline-flex;align-items:center;gap:3px;color:var(--text-dim);"><span style="width:8px;height:8px;border-radius:2px;background:var(--bg-tertiary);border:1px solid rgba(255,255,255,0.1);display:inline-block;"></span>free ' + ((flTotal - usedTotal)/1024).toFixed(0) + 'KB (' + freePct.toFixed(1) + '%)</span>';
            }
            partBar.innerHTML = barHtml;
            if (legend) legend.innerHTML = legendHtml;
        }

        set('p-fps-cur', d.fps_current.toFixed(1));
        set('p-fps-avg', d.fps_average.toFixed(1));
        set('p-fps-tgt', d.fps_target);
        set('p-refresh', d.refresh_ms + ' ms');
        set('p-resolution', d.resolution);

        let s = d.uptime_s;
        set('p-uptime', Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m '+(s%60)+'s');
        set('p-wifi', d.wifi_clients);
        set('p-spi', (d.spi_speed / 1e6).toFixed(0) + ' MHz');
    }).catch(() => {});
}

document.getElementById('perf-panel').addEventListener('toggle', function() {
    if (this.open) {
        updatePerf();
        perfTimer = setInterval(updatePerf, 1000);
        serialPollTimer = setInterval(pollSerial, 200);
        pollSerial();
    } else {
        clearInterval(perfTimer); perfTimer = null;
        clearInterval(serialPollTimer); serialPollTimer = null;
    }
});
</script>


</body></html>
)rawliteral";

void webServerTask(void *pvParameters) {
  otaStatusMutex = xSemaphoreCreateMutex();
  WiFi.mode(WIFI_AP_STA);
  int txPower = WIFI_TX_POWER_DBM;
  if (txPower < -1) txPower = -1;
  if (txPower > 20) txPower = 20;
  WiFi.setTxPower((wifi_power_t)txPower);
  WiFi.softAP("Dashboard_Config", "12345678");
  logPrintf("AP: Dashboard_Config\n");
  logPrintf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  delay(100);

  struct WifiNetwork { const char *ssid; const char *pass; };
  const int MAX_WIFI_NETS = 5;
  WifiNetwork wifiNets[MAX_WIFI_NETS];
  int wifiNetCount = 0;

  wifiNets[wifiNetCount++] = {WIFI_SSID.c_str(), WIFI_PASSWORD.c_str()};
  if (WIFI_SSID_1.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_1.c_str(), WIFI_PASSWORD_1.c_str()};
  if (WIFI_SSID_2.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_2.c_str(), WIFI_PASSWORD_2.c_str()};
  if (WIFI_SSID_3.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_3.c_str(), WIFI_PASSWORD_3.c_str()};
  if (WIFI_SSID_4.length() > 0) wifiNets[wifiNetCount++] = {WIFI_SSID_4.c_str(), WIFI_PASSWORD_4.c_str()};

  bool connected = false;
  WiFi.setHostname("dashboard-pp");

  for (int i = 0; i < wifiNetCount && !connected; i++) {
    if (strlen(wifiNets[i].ssid) == 0) continue;

    logPrintf("Trying WiFi[%d]: %s\n", i, wifiNets[i].ssid);
    WiFi.begin(wifiNets[i].ssid, wifiNets[i].pass);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      logPrintf("STA connected: %s\n", WiFi.localIP().toString().c_str());
      logPrintf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    } else {
      logPrintf("WiFi[%d] failed, trying next...\n", i);
      WiFi.disconnect(false);
      delay(200);
    }
  }

  if (connected) {
    if (MDNS.begin("dashboard-pp")) {
      MDNS.addService("http", "tcp", 80);
      logPrintf("mDNS: http://dashboard-pp.local\n");
    }

    if (NTP_ENABLED) {
      logPrintf("Syncing time via NTP: %s\n", NTP_SERVER.c_str());
      configTime(0, 0, NTP_SERVER.c_str());
      time_t now = 0;
      struct tm timeinfo = {0};
      int retry = 0;
      while (timeinfo.tm_year < (2024 - 1900) && retry < 10) {
        delay(500);
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
      }
      if (retry < 10) {
        logPrintf("NTP time sync OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                  timeinfo.tm_mday, timeinfo.tm_hour,
                  timeinfo.tm_min, timeinfo.tm_sec);
      } else {
        logPrintf("NTP time sync failed after %d retries\n", retry);
      }
    }
  } else {
    logPrintf("All WiFi networks failed, using AP only\n");
  }

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

  server.on("/", HTTP_GET, []() { server.send_P(200, PSTR("text/html"), index_html); });
  server.on("/debug", HTTP_GET, []() {
    String out = "strlen(index_html) = " + String(strlen(index_html)) + "\n";
    out += "First 100 hex: ";
    for (int i = 0; i < 100; i++) {
      char hex[4];
      sprintf(hex, "%02X ", (unsigned char)index_html[i]);
      out += String(hex);
    }
    server.send(200, "text/plain", out);
  });

  server.on("/api/config", HTTP_GET, []() {
    JsonDocument doc;
    processConfig(1, &doc);
    doc["ambientLightValue"] = ambientLightValue;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    processConfig(2, &doc);
    recalculateDerivedParams();
    display.applyBusConfig();
    // Apply CPU frequency immediately
    {
      uint32_t freq = ENABLE_DYNAMIC_CPU ? 240 : MANUAL_CPU_FREQ;
      setCpuFrequencyMhz(freq);
      logPrintf("CPU: %dMHz (config change)\n", freq);
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    forceFullRedraw = true;
    pendingInvertDisplay = true;
    if (!ENABLE_AUTO_BRIGHTNESS)
      pendingBacklightValue = BACKLIGHT_BRIGHTNESS;
  });

  server.on("/api/time", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400);
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc["timestamp"].is<long>()) {
      long epoch = doc["timestamp"];
      struct timeval tv;
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      logPrintf("RTC sync: %ld\n", epoch);
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/odo", HTTP_GET, []() {
    JsonDocument doc;
    doc["km"] = totalDistanceKm;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
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
      resp["km"] = totalDistanceKm;
      String out;
      serializeJson(resp, out);
      server.send(200, "application/json", out);
    } else {
      server.send(400, "application/json", "{\"status\":\"error\"}");
    }
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
    Preferences pref;

    pref.begin("cfg", false);
    pref.clear();
    pref.end();

    pref.begin("dashboard", false);
    pref.clear();
    pref.end();

    server.send(200, "application/json", "{\"status\":\"ok\"}");
    logPrintf("Factory reset, rebooting\n");

    pendingReboot = true;
  });

  server.on("/api/ambient", HTTP_GET, []() {
    server.send(200, "application/json",
                "{\"raw\":" + String(ambientLightValue) + "}");
  });

  server.on("/api/ambient/cal-dark", HTTP_POST, []() {
    LIGHT_SENSOR_DARK_VAL = ambientLightValue;
    { Preferences p; p.begin("cfg", false);
      p.putInt("LIGHT_DARK", LIGHT_SENSOR_DARK_VAL); p.end(); }
    server.send(200, "application/json",
                "{\"status\":\"ok\",\"value\":" + String(LIGHT_SENSOR_DARK_VAL) + "}");
  });

  server.on("/api/ambient/cal-bright", HTTP_POST, []() {
    LIGHT_SENSOR_BRIGHT_VAL = ambientLightValue;
    { Preferences p; p.begin("cfg", false);
      p.putInt("LIGHT_BRIGHT", LIGHT_SENSOR_BRIGHT_VAL); p.end(); }
    server.send(200, "application/json",
                "{\"status\":\"ok\",\"value\":" + String(LIGHT_SENSOR_BRIGHT_VAL) + "}");
  });

  server.on("/api/ota", HTTP_POST, []() {
    if (otaUpdateSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Update OK\"}");
      delay(100);
      ESP.restart();
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"msg\":\"Update failed\"}");
      otaUpdateInProgress = false;
      forceFullRedraw = true;
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      otaUpdateSuccess = false;
      otaUpdateInProgress = true;
      pendingOtaScreen = true;
      logPrintf("OTA web: start %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      if (upload.totalSize > 0) {
        updateOTAProgress(Update.progress(), upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        logPrintf("OTA web: success %u bytes\n", upload.totalSize);
        otaUpdateSuccess = true;
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.on("/api/ota/pull", HTTP_POST, []() {
    if (otaUpdateInProgress || otaPullTaskRunning) {
      server.send(200, "application/json", "{\"status\":\"busy\",\"msg\":\"OTA already in progress\"}");
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      server.send(200, "application/json", "{\"status\":\"error\",\"msg\":\"Not connected to WiFi\"}");
      return;
    }
    setOtaPullStatus("checking...");
    server.send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"OTA pull started\"}");
    logPrintf("OTA Pull: triggered from web UI\n");
    startOtaPull(true);
  });

  server.on("/api/ota/check", HTTP_GET, []() {
    JsonDocument doc;
    doc["enabled"] = OTA_PULL_ENABLED;
    doc["url"] = OTA_PULL_URL;
    doc["interval_hours"] = OTA_PULL_INTERVAL_HOURS;
    doc["current_version"] = OTA_CURRENT_VERSION;
    if (otaStatusMutex) xSemaphoreTake(otaStatusMutex, portMAX_DELAY);
    doc["status"] = otaPullStatus;
    doc["status_updated"] = otaPullStatusUpdated;
    if (otaStatusMutex) xSemaphoreGive(otaStatusMutex);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/serial", HTTP_GET, []() {
    // Read all available data and advance the tail atomically
    int tail = logTail;
    int head = logHead;
    int len = (head >= tail) ? (head - tail) : (LOG_BUF_SIZE - tail + head);
    
    String out;
    if (len > 0) {
      out.reserve(len + 1);
      if (head >= tail) {
        for (int i = tail; i < head; i++)
          out += logBuf[i];
      } else {
        for (int i = tail; i < LOG_BUF_SIZE; i++)
          out += logBuf[i];
        for (int i = 0; i < head; i++)
          out += logBuf[i];
      }
      logTail = head;
    }
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

  server.begin();

  unsigned long lastClientTime = millis();

  for (;;) {
    server.handleClient();
    ArduinoOTA.handle();

    if (lastOtaPullCheck == 0) {
      lastOtaPullCheck = millis();
    }
    if (OTA_PULL_ENABLED && OTA_PULL_INTERVAL_HOURS > 0 && !otaUpdateInProgress &&
        !otaPullTaskRunning) {
      unsigned long intervalMs = (unsigned long)OTA_PULL_INTERVAL_HOURS * 3600000UL;
      if (millis() - lastOtaPullCheck >= intervalMs) {
        lastOtaPullCheck = millis();
        startOtaPull(false);
      }
    }

    if (WiFi.softAPgetStationNum() > 0) {
      lastClientTime = millis();
    }

    if (WiFi.status() == WL_CONNECTED) {
      lastClientTime = millis();
    }

    // Allow 10 minutes before powering off (for testing)
    if (millis() - lastClientTime > 600000) {
      logPrintf("WiFi timeout, disabled\n");
      WiFi.mode(WIFI_OFF);
      vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
