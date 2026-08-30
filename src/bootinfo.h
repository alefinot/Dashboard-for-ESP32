// bootinfo.h — boot/reboot forensics (Phase 0 of the crash & boot-loop plan).
//
// Answers "why did the device reboot?" with zero behavioral change in steady
// state:
//   * esp_reset_reason() at boot (PANIC / WDT / BROWNOUT / SW / PWR / LOAD ..)
//   * a tag persisted by every intentional ESP.restart() (reason + heap)
//   * the fast-reboot-storm latch (so a boot loop can be *broken*, not just
//     delayed) — see bootinfo_storm_active()
//   * the heap watermark since boot (ESP.getMinFreeHeap / getMaxAllocHeap)
//
// No custom crash handler: a hard fault already dumps its backtrace to UART
// via the IDF default panic handler; we capture the *reason* + *watermark* so
// it survives the reboot and is readable in /api/boot and the serial banner.
#pragma once
#include <Arduino.h>

// Call once, early in setup(). Reads the reset reason, the last-reboot tag
// from NVS, latches the fast-reboot storm, and prints the boot banner.
void bootinfo_init();

// Call BEFORE every intentional ESP.restart(). Persists the reboot reason +
// free heap to NVS so the next boot's /api/boot / serial banner says why the
// previous clean reboot happened. Cheap (one NVS write per reboot).
void bootinfo_tag_reboot(const char *why);

// True if this boot is part of a fast-reboot storm (>=4 reboots within 120 s).
// Used to suppress clean auto-reboots so a boot loop breaks instead of looping.
bool bootinfo_storm_active();

// The RTC-persistent boot counter (1 on a clean boot, rising during a storm).
uint32_t bootinfo_boot_count();

// The /api/boot JSON payload (reset reason, boot count, storm state, last
// reboot tag + heap, heap watermark, live free heap).
String bootinfo_json();
