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
static char s_prevVersion[32] = "";

// Every value in this core's esp_reset_reason_t (esp_system.h). The old subset
// fell through to "UNKNOWN" for exactly the reasons that matter in crash triage
// - power glitch, CPU lock-up, the other-watchdog case - so /api/boot and the
// bootinfo JSON mislabelled them (issue #42). An unmapped value prints its
// number instead, so a future core that adds a reason stays distinguishable.
static void resetReasonStr(esp_reset_reason_t r, char *out, size_t outLen) {
  const char *s = NULL;
  switch (r) {
    case ESP_RST_UNKNOWN:    s = "UNKNOWN"; break;
    case ESP_RST_POWERON:    s = "POWERON"; break;
    case ESP_RST_EXT:        s = "EXT"; break;
    case ESP_RST_SW:         s = "SW (clean restart)"; break;
    case ESP_RST_PANIC:      s = "PANIC (crash/abort)"; break;
    case ESP_RST_INT_WDT:    s = "INT_WDT"; break;
    case ESP_RST_TASK_WDT:   s = "TASK_WDT"; break;
    case ESP_RST_WDT:        s = "WDT (other)"; break;
    case ESP_RST_DEEPSLEEP:  s = "DEEP_SLEEP_WAKE"; break;
    case ESP_RST_BROWNOUT:   s = "BROWNOUT"; break;
    case ESP_RST_SDIO:       s = "SDIO"; break;
    case ESP_RST_USB:        s = "USB"; break;
    case ESP_RST_JTAG:       s = "JTAG"; break;
    case ESP_RST_EFUSE:      s = "EFUSE_ERR"; break;
    case ESP_RST_PWR_GLITCH: s = "PWR_GLITCH"; break;
    case ESP_RST_CPU_LOCKUP: s = "CPU_LOCKUP"; break;
    default: break;
  }
  if (s)
    snprintf(out, outLen, "%s", s);
  else
    snprintf(out, outLen, "RST_%d", (int)r);
}

void bootinfo_init() {
  esp_reset_reason_t rr = esp_reset_reason();
  resetReasonStr(rr, s_resetReason, sizeof(s_resetReason));

  // Last clean-reboot tag (written by bootinfo_tag_reboot before the reboot)
  // and the firmware identity history. One read-only session for both reads
  // instead of two back-to-back opens, and every open goes through NvsSession
  // so the locking pattern is the same as everywhere else (issue #32).
  {
    char tag[48] = "";
    char ran[32] = "";
    NvsSession session("bootinfo", true);  // read-only
    if (session.opened()) {
      session.nvs.getString("rebootTag", tag, sizeof(tag));
      s_lastRebootHeap = session.nvs.getUInt("rebootHeap", 0);
      s_lastMinHeap = session.nvs.getUInt("minHeap", 0);
      session.nvs.getString("ranVer", ran, sizeof(ran));
      session.nvs.getString("prevVer", s_prevVersion, sizeof(s_prevVersion));
    }
    s_prevVersion[sizeof(s_prevVersion) - 1] = 0;
    strncpy(s_lastRebootTag, tag, sizeof(s_lastRebootTag) - 1);
    s_lastRebootTag[sizeof(s_lastRebootTag) - 1] = 0;

  // Firmware identity bookkeeping. "ranVer" is the build version recorded at
  // the previous boot, so a difference means different firmware is running -
  // reached by OTA, USB flash or a downgrade, all logged the same way. The
  // value always comes from FW_VERSION (compiled in), never from the update
  // manifest, so the history cannot be rewritten by whatever the device was
  // told to believe. One NVS write, and only when the version actually
  // changes.
    if (strcmp(ran, FW_VERSION) != 0) {
      strncpy(s_prevVersion, ran, sizeof(s_prevVersion) - 1);
      s_prevVersion[sizeof(s_prevVersion) - 1] = 0;
      NvsSession write("bootinfo", false);
      if (write.opened()) {
        nvsWriteFailed("ranVer", write.nvs.putString("ranVer", FW_VERSION));
        nvsWriteFailed("prevVer", write.nvs.putString("prevVer", s_prevVersion));
      }
      logPrintf("BOOTINFO: firmware v%s running (previous: %s)\n", FW_VERSION,
                s_prevVersion[0] ? s_prevVersion : "unknown");
    }
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
  // This is the last thing written before the reboot - if it does not stick,
  // the next boot can only report "no tag", which is exactly the case that
  // looks like a crash. So the result is checked and reported (issue #32).
  NvsSession session("bootinfo", false);
  if (!session.opened()) {
    logPrintf("BOOTINFO: cannot open NVS for the reboot tag ('%s' not recorded)\n", w);
    return;
  }
  nvsWriteFailed("rebootTag", session.nvs.putString("rebootTag", w));
  nvsWriteFailed("rebootHeap", session.nvs.putUInt("rebootHeap", (uint32_t)ESP.getFreeHeap()));
  nvsWriteFailed("minHeap", session.nvs.putUInt("minHeap", (uint32_t)ESP.getMinFreeHeap()));
}

bool bootinfo_storm_active() { return s_stormActive; }

uint32_t bootinfo_boot_count() { return s_bootCountNow; }

const char *bootinfo_previous_version() { return s_prevVersion; }

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
