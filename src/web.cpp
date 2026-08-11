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

// Heartbeat counter bumped by the web task loop; the display task watches it
// and reboots the device if the web server stalls (e.g. a handler hangs).
volatile unsigned long webLoopCount = 0;

// Wipes the configuration NVS namespaces. Used by /api/reset and by the
// physical recovery gesture (hold BOOT for 8 seconds after boot).
//
// The WiFi join credentials (SSIDs, passwords, TX power) survive the reset:
// the reset exists to recover from a forgotten config PIN or web lockout, and
// wiping the network creds would leave the device stranded in AP-only mode
// exactly when a recovery reset is needed. The SSID/password values written
// back are the ones loaded from NVS at boot (mode 0), never the compiled
// defaults.
void factoryResetConfig() {
  const char *wifiKeys[] = {
      "WIFI_SSID", "WIFI_S1", "WIFI_S2", "WIFI_S3", "WIFI_S4",
      "WIFI_PWD",  "WIFI_P1", "WIFI_P2", "WIFI_P3", "WIFI_P4",
      "WIFI_TXP"};
  const int wifiKeyCount = sizeof(wifiKeys) / sizeof(wifiKeys[0]);
  Preferences pref;
  pref.begin("cfg", false);
  String saved[11];
  for (int i = 0; i < wifiKeyCount && i < 11; i++)
    saved[i] = pref.getString(wifiKeys[i], "");
  pref.clear();
  for (int i = 0; i < wifiKeyCount && i < 11; i++) {
    if (saved[i].length() > 0)
      pref.putString(wifiKeys[i], saved[i].c_str());
  }
  pref.end();
  pref.begin("dashboard", false);
  pref.clear();
  pref.end();
  logPrintf("Factory reset done (WiFi credentials preserved)\n");
}

volatile bool otaUpdateSuccess = false;

// OTA Pull state
static unsigned long lastOtaPullCheck = 0;
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
static bool weatherTaskRunning = false;
static unsigned long weatherTaskStartedMs = 0;
// Set when a config save changes the weather location/city/interval so the
// fetch loop re-queries immediately instead of waiting for the next interval.
static volatile bool weatherRefreshRequested = false;

// Reverse-geocode a coordinate into a short display name (city/locality/region).
// Free keyless BigDataCloud lookup; returns true and fills `out` on success.
static bool reverseGeocode(double lat, double lon, String &out) {
  if (WiFi.status() != WL_CONNECTED) return false;
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

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  HTTPClient http;
  
  double lat = WEATHER_LAT;
  double lon = WEATHER_LON;
  bool gpsFix = gps.location.isValid();
  if (gpsFix) {
    lat = gps.location.lat();
    lon = gps.location.lng();
  }

  // City name follows the coordinates: only reverse-geocode a live GPS fix,
  // otherwise fall back to the saved WEATHER_CITY derived from WEATHER_LAT/LON.
  String resolvedCity;
  if (gpsFix) reverseGeocode(lat, lon, resolvedCity);
  
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
    while (!otaMemReleased && (millis() - t0) < 3000)
      vTaskDelay(pdMS_TO_TICKS(1));
    otaMemReleaseRequested = false;
    logPrintf("Weather: UI mem released for TLS heap=%lu max=%lu\n",
              (unsigned long)ESP.getFreeHeap(),
              (unsigned long)ESP.getMaxAllocHeap());
  }
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_weatherData.temperature = doc["current"]["temperature_2m"] | 0.0f;
        g_weatherData.humidity = doc["current"]["relative_humidity_2m"] | 0;
        g_weatherData.weatherCode = doc["current"]["weather_code"] | 0;
        g_weatherData.cloudCover = doc["current"]["cloud_cover"] | 0;
        g_weatherData.windSpeed = doc["current"]["wind_speed_10m"] | 0.0f;
        g_weatherData.windDirection = doc["current"]["wind_direction_10m"] | 0.0f;
        
        if (doc["daily"]["sunrise"].is<JsonArray>()) {
          const char* sunrise = doc["daily"]["sunrise"][0] | "";
          if (strlen(sunrise) >= 16) {
            g_weatherData.sunriseTime = String(sunrise).substring(11);
          } else {
            g_weatherData.sunriseTime = "--:--";
          }
        } else {
          g_weatherData.sunriseTime = "--:--";
        }
        if (doc["daily"]["sunset"].is<JsonArray>()) {
          const char* sunset = doc["daily"]["sunset"][0] | "";
          if (strlen(sunset) >= 16) {
            g_weatherData.sunsetTime = String(sunset).substring(11);
          } else {
            g_weatherData.sunsetTime = "--:--";
          }
        } else {
          g_weatherData.sunsetTime = "--:--";
        }
        g_weatherData.valid = true;
        g_weatherData.lastUpdated = millis();
        g_weatherData.cityName = (resolvedCity.length() > 0) ? resolvedCity : String(WEATHER_CITY);
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

void weatherFetchTask(void *pvParameters) {
  updateWeather();
  weatherTaskRunning = false;
  weatherTaskHandle = NULL;
  vTaskDelete(NULL);
}

bool startWeatherFetch() {
  if (weatherTaskRunning) return true;
  weatherTaskRunning = true;
  weatherTaskStartedMs = millis();
  BaseType_t res = xTaskCreatePinnedToCore(weatherFetchTask, "WeatherFetchTask",
                                           8192, NULL, 1, &weatherTaskHandle, 0);
  if (res != pdPASS) {
    logPrintf("Weather: task creation failed, will retry\n");
    weatherTaskRunning = false;
    weatherTaskHandle = NULL;
    return false;
  }
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
  bool manual = (bool)(uintptr_t)pvParameters;
  checkForFirmwareUpdate(manual);
  otaPullTaskRunning = false;
  otaPullDownloading = false;   // safety: never leave the web loop paused
  vTaskDelete(NULL);
}

void startOtaPull(bool manual) {
  if (otaPullTaskRunning || otaUpdateInProgress) return;
  otaPullManualFlag = manual;
  otaPullTaskRunning = true;
  BaseType_t res = xTaskCreatePinnedToCore(otaPullTask, "OtaPullTask", 16384,
                                           (void *)(uintptr_t)manual, 1, NULL, 0);
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

void checkForFirmwareUpdate(bool manual) {
  if (!OTA_PULL_ENABLED && !manual) return;
  if (WiFi.status() != WL_CONNECTED) {
    setOtaPullStatus("error: not connected to WiFi");
    return;
  }
  if (OTA_PULL_URL[0] == 0) {
    setOtaPullStatus("error: no OTA URL configured");
    return;
  }
  unsigned long sinceLast = millis() - lastOtaCheckMs;
  if (sinceLast < OTA_RECHECK_MIN_MS) {
    unsigned long waitS = (OTA_RECHECK_MIN_MS - sinceLast) / 1000 + 1;
    logPrintf("OTA Pull: recheck throttled, wait %lus\n", waitS);
    setOtaPullStatus(("waiting " + String(waitS) + "s before retry").c_str());
    return;
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
      WiFiClient *client = nullptr;

      if (strncmp(OTA_PULL_URL, "https://", 8) == 0) {
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
    snprintf(curVer, sizeof(curVer), "%s", OTA_CURRENT_VERSION);
    if (curVer[0] == 'v' || curVer[0] == 'V')
      memmove(curVer, curVer + 1, strlen(curVer));
    if (latestVersion.startsWith("v") || latestVersion.startsWith("V"))
      latestVersion = latestVersion.substring(1);

    logPrintf("OTA Pull: latest=%s current=%s\n", latestVersion.c_str(), curVer);

    if (latestVersion.equals(curVer)) {
      logPrintf("OTA Pull: already up-to-date\n");
      setOtaPullStatus((String("up-to-date (v") + curVer + ")").c_str());
      return;
    }

    logPrintf("OTA Pull: new firmware v%s available, downloading\n", latestVersion.c_str());
    setOtaPullStatus(("updating to v" + latestVersion).c_str());
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

  // Each attempt re-establishes a fresh TLS connection and resumes the flash
  // write from the last byte via an HTTP Range request. A single stalled link
  // no longer aborts the whole 1.4MB firmware pull.
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
      if (Update.begin(sz)) return true;
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

    WiFiClient *client = nullptr;
    if (strncmp(firmwareUrl, "https://", 8) == 0) {
      WiFiClientSecure *ssl = new WiFiClientSecure();
      ssl->setInsecure();
      client = ssl;
    } else {
      client = new WiFiClient();
    }

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
        updateOpen = true;
        logPrintf("OTA Pull: flash open, %lu bytes, heap=%lu maxAlloc=%lu\n",
                  (unsigned long)totalSize, (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
      } else if (httpCode == HTTP_CODE_OK) {
        // Server ignored the Range header and re-sent the whole body. The only
        // safe response is to restart the flash write and stream from zero.
        Update.abort();
        if (!openFlash((size_t)totalSize)) {
          setOtaPullStatus("error: update.begin failed");
          http.end();
          delete client;
          break;
        }
        written = 0;
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
    stalled = false; // next attempt resumes from `written`
    if (written < (size_t)totalSize)
      logPrintf("OTA Pull: reconnecting to resume at %lu/%lu (heap=%lu maxAlloc=%lu)\n",
                (unsigned long)written, (unsigned long)totalSize,
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());
  }

  otaPullDownloading = false;   // resume web serving

  if (updateOpen) {
    if (written >= (size_t)totalSize && Update.end(true)) {
      logPrintf("OTA Pull: success %zu bytes\n", written);
      otaUpdateSuccess = true;
      otaProgressTarget = 258;
      // Record the new version in NVS so the next check reports up-to-date
      // instead of re-downloading the same release. A failed write is not
      // fatal: the worst case is one re-download of the same version.
      {
        Preferences p;
        if (p.begin("cfg", false)) {
          p.putString("OTA_VER", newVersion);
          p.end();
        }
      }
      // The display task animates the bar to 100% and calls ESP.restart().
      // Fall back to rebooting here so a freshly-flashed image always boots,
      // even if the UI thread is wedged after the update.
      unsigned long fillStart = millis();
      while (millis() - fillStart < 6000) vTaskDelay(pdMS_TO_TICKS(100));
      logPrintf("OTA Pull: rebooting\n");
      ESP.restart();
    } else if (written >= (size_t)totalSize) {
      Update.printError(Serial);
      setOtaPullStatus("error: update.end failed");
      logPrintf("OTA Pull: end failed after %zu bytes\n", written);
    } else {
      logPrintf("OTA Pull: incomplete download (%zu/%lu bytes), discarding\n",
                written, (unsigned long)totalSize);
      Update.abort();
      setOtaPullStatus("error: download incomplete");
    }
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
  WiFi.mode(WIFI_AP_STA);
  int txPower = WIFI_TX_POWER_DBM;
  if (txPower < -1) txPower = -1;
  if (txPower > 20) txPower = 20;
  WiFi.setTxPower((wifi_power_t)txPower);
  WiFi.softAP("Dashboard_Config", "12345678");
  logPrintf("AP: Dashboard_Config\n");
  logPrintf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  delay(100);

  // WiFi networks are only recorded here; the actual STA connect attempts run
  // as a non-blocking state machine inside the task loop below, AFTER the
  // config server is up, so the config page is reachable via the AP within
  // ~1s of boot even while the ESP keeps trying to join a LAN network.
  const int MAX_WIFI_NETS = 5;
  struct WifiNetwork { const char *ssid; const char *pass; };
  WifiNetwork wifiNets[MAX_WIFI_NETS];
  int wifiNetCount = 0;

  wifiNets[wifiNetCount++] = {WIFI_SSID, WIFI_PASSWORD};
  if (WIFI_SSID_1[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_1, WIFI_PASSWORD_1};
  if (WIFI_SSID_2[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_2, WIFI_PASSWORD_2};
  if (WIFI_SSID_3[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_3, WIFI_PASSWORD_3};
  if (WIFI_SSID_4[0] != 0) wifiNets[wifiNetCount++] = {WIFI_SSID_4, WIFI_PASSWORD_4};

  WiFi.setHostname("dashboard-pp");
  bool staConnected = false;
  int staNetIdx = 0;
  bool staInFlight = false;
  bool staDone = false;
  bool staFinalized = false;
  unsigned long staDeadline = 0;
  unsigned long staRetryAt = 0;

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

    processConfig(2, &doc);
    recalculateDerivedParams();
    display.applyBusConfig();
    // If the save touched the weather location/city/locale/interval, ask the
    // fetch loop to refresh right away so the widget shows the new city's
    // weather immediately instead of after the next scheduled interval.
    if (!doc["WEATHER_CITY"].isNull() || !doc["WEATHER_LAT"].isNull() ||
        !doc["WEATHER_LON"].isNull() || !doc["WEATHER_REFRESH_MIN"].isNull() ||
        !doc["WEATHER_LOCALE"].isNull()) {
      weatherRefreshRequested = true;
    }
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

  server.on("/api/ambient/cal-dark", HTTP_POST, []() {
    LIGHT_SENSOR_DARK_VAL = ambientLightValue;
    { Preferences p; p.begin("cfg", false);
      p.putInt("LIGHT_DARK", LIGHT_SENSOR_DARK_VAL); p.end(); }
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"value\":%d}", LIGHT_SENSOR_DARK_VAL);
    server.send(200, "application/json", buf);
  });

  server.on("/api/ambient/cal-bright", HTTP_POST, []() {
    LIGHT_SENSOR_BRIGHT_VAL = ambientLightValue;
    { Preferences p; p.begin("cfg", false);
      p.putInt("LIGHT_BRIGHT", LIGHT_SENSOR_BRIGHT_VAL); p.end(); }
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"value\":%d}", LIGHT_SENSOR_BRIGHT_VAL);
    server.send(200, "application/json", buf);
  });

  server.on("/api/compass/cal-start", HTTP_POST, []() {
    if (!compassReady) {
      server.send(200, "application/json",
                  "{\"status\":\"error\",\"error\":\"Compass not detected\"}");
      return;
    }
    compassCalStart(30);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/compass/cal-cancel", HTTP_POST, []() {
    compassCalCancel();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/compass/cal-status", HTTP_GET, []() {
    long remain = compassCalActive ? (long)((compassCalEndTime - millis()) / 1000) : 0;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"remaining\":%ld,\"minX\":%d,\"maxX\":%d,"
             "\"minY\":%d,\"maxY\":%d,\"minZ\":%d,\"maxZ\":%d,"
             "\"offX\":%d,\"offY\":%d,\"offZ\":%d,\"tX\":%d,\"tY\":%d,\"tZ\":%d,"
             "\"result\":\"%s\"}",
             compassCalActive ? "capturing" : "idle", remain,
             compassCalMinX, compassCalMaxX, compassCalMinY, compassCalMaxY,
             compassCalMinZ, compassCalMaxZ, COMPASS_CAL_X, COMPASS_CAL_Y,
             COMPASS_CAL_Z, COMPASS_CAL_TX, COMPASS_CAL_TY, COMPASS_CAL_TZ,
             compassCalResult);
    server.send(200, "application/json", buf);
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
      // upload.totalSize is cumulative bytes received so far during upload.
      // Scaling target progress up to max 240 during writing prevents
      // premature reboot (fillW >= 258) before Update.end(true) runs.
      size_t written = Update.progress();
      int targetW = (240L * (long)written) / (long)(written + 300000);
      if (targetW > 240) targetW = 240;
      if (targetW > otaProgressTarget) otaProgressTarget = targetW;
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        logPrintf("OTA web: success %u bytes\n", upload.totalSize);
        otaUpdateSuccess = true;
        otaProgressTarget = 258;
      } else {
        Update.printError(Serial);
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
    // Copy the echo ring window into a static scratch buffer (LOG_BUF_SIZE
    // max, plus NUL) so this frequently-polled endpoint never allocates heap.
    static char out[LOG_BUF_SIZE + 1];
    int tail = logTail;
    int head = logHead;
    int len = (head >= tail) ? (head - tail) : (LOG_BUF_SIZE - tail + head);

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

  unsigned long lastClientTime = millis();
  unsigned long webStartMs = millis();

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
      logPrintf("OTA Pull: task overrun, resetting OTA state\n");
      otaUpdateInProgress = false;
      otaPullTaskRunning = false;
      otaPullDownloading = false;
      pullStartedAt = 0;
      forceFullRedraw = true;
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

    // Non-blocking STA connect: try each configured network for up to 5s,
    // while the config server keeps serving AP clients. mDNS/NTP run once
    // after a network is joined.
    if (!staDone) {
      if (staRetryAt && millis() < staRetryAt) {
        // backoff between attempts
      } else if (staNetIdx < wifiNetCount) {
        if (!staInFlight) {
          if (strlen(wifiNets[staNetIdx].ssid) == 0) {
            staNetIdx++;
          } else {
            logPrintf("Trying WiFi[%d]: %s\n", staNetIdx, wifiNets[staNetIdx].ssid);
            WiFi.begin(wifiNets[staNetIdx].ssid, wifiNets[staNetIdx].pass);
            staDeadline = millis() + 5000;
            staInFlight = true;
          }
        } else if (WiFi.status() == WL_CONNECTED) {
          staConnected = true;
          staDone = true;
          logPrintf("STA connected: %s\n", WiFi.localIP().toString().c_str());
          logPrintf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        } else if (millis() >= staDeadline) {
          logPrintf("WiFi[%d] failed, trying next...\n", staNetIdx);
          WiFi.disconnect(false);
          staInFlight = false;
          staNetIdx++;
          staRetryAt = millis() + 200;
        }
      } else {
        staDone = true;
        logPrintf("All WiFi networks failed, using AP only\n");
      }
    }

    if (staDone && staConnected && !staFinalized) {
      staFinalized = true;
      startWeatherFetch();
      if (MDNS.begin("dashboard-pp")) {
        MDNS.addService("http", "tcp", 80);
        logPrintf("mDNS: http://dashboard-pp.local\n");
      }

      if (NTP_ENABLED) {
        logPrintf("Syncing time via NTP: %s\n", NTP_SERVER);
        configTime(0, 0, NTP_SERVER);
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
    }

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
      if (weatherTaskRunning && millis() - weatherTaskStartedMs > 30000) {
        logPrintf("Weather: fetch task hung >30s, terminating task\n");
        if (weatherTaskHandle != NULL) {
          vTaskDelete(weatherTaskHandle);
          weatherTaskHandle = NULL;
        }
        weatherTaskRunning = false;
      }
      if (lastWeatherCheck == 0) {
        lastWeatherCheck = millis();
      }
      unsigned long weatherIntervalMs = (unsigned long)WEATHER_REFRESH_MIN * 60000UL;
      if (millis() - lastWeatherCheck >= weatherIntervalMs) {
        lastWeatherCheck = millis();
        startWeatherFetch();
      }
    }

    // Allow 10 minutes before disabling the STA uplink (for testing) - the
    // AP and the config web server always stay alive so the config page
    // keeps working even after the LAN connection idles out.
    if (WIFI_AUTO_OFF_ENABLED && WiFi.status() == WL_CONNECTED &&
        millis() - lastClientTime > 600000) {
      logPrintf("WiFi STA timeout, disconnecting LAN uplink (AP stays up)\n");
      WiFi.disconnect();
    }

    // Low-heap watchdog: the display keeps speed sprite + tape sprite + the
    // 120px VLW buffer (~60KB total), which can starve /api/config (~30-45KB
    // transient) and the TLS stack. Ask the display task to drop the big
    // sprites (memory-saver mode); if even that is not enough, reboot to clear
    // heap fragmentation. Armed 5s after start.
    //
    // Fully-loaded steady state is ~54KB with WiFi up (sprites + VLW120 +
    // STA/TLS stacks), rising to ~118KB after memory-saver drops the buffers.
    // The background ARM threshold (30KB) therefore sits far BELOW normal WiFi
    // operation so it only engages when heap really collapses; the /api/config
    // handler still self-protects at its own 60KB "drop sprites first" gate,
    // the OTA pull does its own pre-TLS release, and the weather fetch
    // releases before an https connect. Memory-saver is REVERSIBLE: once free
    // heap stays >= 108KB for a minute the flags are cleared and
    // ensureSpeedSprite()/the tape path rebuild the buffers again. The
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
        logPrintf("Heap critical (%lu B) even with memory-saver, rebooting\n",
                  (unsigned long)fh);
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
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
