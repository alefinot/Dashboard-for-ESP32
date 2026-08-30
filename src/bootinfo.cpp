// bootinfo.cpp — boot/reboot forensics (Phase 0 of the crash & boot-loop plan).
// See bootinfo.h for the rationale.
#include "bootinfo.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_attr.h>

#include "dashboard.h"  // logPrintf

// RTC_NOINIT_ATTR: survives ESP.restart() (not a full power cycle), which is
// exactly what lets us detect a fast-reboot storm at boot.
RTC_NOINIT_ATTR static uint32_t bootCount;
RTC_NOINIT_ATTR static uint32_t bootStampSec;

static bool s_stormActive = false;
static uint32_t s_bootCountNow = 0;
static char s_lastRebootTag[48] = "";
static uint32_t s_lastRebootHeap = 0;
static uint32_t s_lastMinHeap = 0;
static char s_resetReason[24] = "?";

static const char *resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW (clean restart)";
    case ESP_RST_PANIC:     return "PANIC (crash/abort)";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                 return "UNKNOWN";
  }
}

void bootinfo_init() {
  esp_reset_reason_t rr = esp_reset_reason();
  strncpy(s_resetReason, resetReasonStr(rr), sizeof(s_resetReason) - 1);
  s_resetReason[sizeof(s_resetReason) - 1] = 0;

  // Last clean-reboot tag (written by bootinfo_tag_reboot before the reboot).
  {
    char tag[48] = "";
    Preferences pref;
    pref.begin("bootinfo", true);  // read-only
    pref.getString("rebootTag", tag, sizeof(tag));
    s_lastRebootHeap = pref.getUInt("rebootHeap", 0);
    s_lastMinHeap = pref.getUInt("minHeap", 0);
    pref.end();
    strncpy(s_lastRebootTag, tag, sizeof(s_lastRebootTag) - 1);
    s_lastRebootTag[sizeof(s_lastRebootTag) - 1] = 0;
  }

  // Fast-reboot storm latch (moved here from main.cpp so it lives with the
  // forensics). A boot >120 s after the previous one is "clean" (counter
  // resets); otherwise the counter rises and >=4 marks a storm.
  uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  bootCount++;
  if (bootStampSec == 0 || bootCount > 10 || nowSec - bootStampSec > 120) {
    bootCount = 1;
  }
  s_bootCountNow = bootCount;
  s_stormActive = (bootCount >= 4);
  bootStampSec = nowSec;

  logPrintf(
      "BOOTINFO: reset=%s boot#%u storm=%d lastRebootTag='%s' "
      "heap@lastReboot=%lu minHeapLast=%lu minHeapSinceBoot=%lu freeHeap=%lu\n",
      s_resetReason, s_bootCountNow, (int)s_stormActive, s_lastRebootTag,
      (unsigned long)s_lastRebootHeap, (unsigned long)s_lastMinHeap,
      (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getFreeHeap());
}

void bootinfo_tag_reboot(const char *why) {
  char w[48];
  snprintf(w, sizeof(w), "%s", (why && why[0]) ? why : "?");
  Preferences pref;
  pref.begin("bootinfo", false);
  pref.putString("rebootTag", w);
  pref.putUInt("rebootHeap", (uint32_t)ESP.getFreeHeap());
  pref.putUInt("minHeap", (uint32_t)ESP.getMinFreeHeap());
  pref.end();
}

bool bootinfo_storm_active() { return s_stormActive; }

uint32_t bootinfo_boot_count() { return s_bootCountNow; }

String bootinfo_json() {
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"reset\":\"%s\",\"bootCount\":%u,\"storm\":%d,"
           "\"lastRebootTag\":\"%s\",\"heapAtLastReboot\":%lu,"
           "\"minHeapLastBoot\":%lu,\"minHeapSinceBoot\":%lu,"
           "\"maxAllocHeap\":%lu,\"freeHeap\":%lu}",
           s_resetReason, s_bootCountNow, (int)s_stormActive, s_lastRebootTag,
           (unsigned long)s_lastRebootHeap, (unsigned long)s_lastMinHeap,
           (unsigned long)ESP.getMinFreeHeap(),
           (unsigned long)ESP.getMaxAllocHeap(),
           (unsigned long)ESP.getFreeHeap());
  return String(buf);
}
