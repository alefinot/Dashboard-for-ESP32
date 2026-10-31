"""Upload-layout fix for this project's custom partition table.

PlatformIO's esptool upload writes the framework's `boot_app0.bin` (an "otadata:
boot slot 0" image) at the fixed address **0xe000**. That is right for the
default single-app layout, and wrong for ours: `partitions.csv` puts `otadata`
at 0x1D000 and `app0` at 0x20000, while 0xe000..0xefff falls **inside the 80 KB
`nvs` partition** (0x9000 + 0x14000). Two consequences, both real:

1. The write lands on two NVS pages (0xe000 and 0xf000) with bytes that are not
   NVS - the page header magic is wrong, so NVS treats those pages as
   uninitialised and everything stored in them is gone. That is why settings
   have historically looked half-lost after a USB re-flash.
2. The real otadata at 0x1D000 is never touched, so the chip keeps booting the
   slot an OTA install last selected. A USB flash then writes app0 and the unit
   boots app1 instead: the flash "did nothing".

Registered as a `post:` extra script in platformio.ini, so it runs after the
platform builder has produced the upload command but before anything is sent.
It removes the bogus 0xe000 write and puts the same image at the real otadata
offset, which makes every USB flash boot the image it just wrote. It only acts
on the serial `write-flash` path: an upload to an IP address (espota) has no
flash addresses to fix, and NVS is never erased by any path here.
"""

import os
import re

Import("env")  # noqa: F821  (SCons injects this)

PARTITIONS_CSV = os.path.join(env["PROJECT_DIR"], "partitions.csv")


def _partition_offsets(path):
    """label -> (offset, size) from a PlatformIO-style partitions CSV."""
    out = {}
    if not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as fp:
        for line in fp:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5 or not cols[0] or cols[0].lower() == "name":
                continue
            try:
                offset = int(cols[3], 0)
                size = int(cols[4], 0)
            except ValueError:
                continue
            out[cols[0].lower()] = (offset, size)
    return out


def _framework_dir(env):
    for key in ("PI_FRAMEWORK_DIR", "PI_LITTLEFS_DIR"):
        val = env.get(key)
        if val and os.path.isdir(val):
            return val
    pkg = env.get("PROJECT_PACKAGES_DIR") or os.path.join(
        os.path.expanduser("~"), ".platformio", "packages")
    return os.path.join(pkg, "framework-arduinoespressif32")


def fix_upload_layout(env):
    flags = env.get("UPLOADERFLAGS") or []
    # Only the serial esptool path deals in flash addresses.
    if "write-flash" not in [str(f).strip('"') for f in flags]:
        return

    parts = _partition_offsets(PARTITIONS_CSV)
    ota = parts.get("otadata")
    app0 = parts.get("app0")
    if not ota:
        print("[upload layout] no otadata row in partitions.csv - leaving the "
              "upload command alone")
        return

    ota_off, ota_size = ota
    framework = _framework_dir(env)
    boot_app0 = os.path.join(framework, "tools", "partitions", "boot_app0.bin")
    if not os.path.isfile(boot_app0):
        print(f"[upload layout] {boot_app0} not found - cannot fix the upload "
              f"command; USB flashes will keep booting the OTA-selected slot")
        return

    # Drop every (address, boot_app0.bin) pair that is not at the real offset.
    cleaned, dropped = [], 0
    i = 0
    while i < len(flags):
        item = str(flags[i]).strip().strip('"')
        nxt = str(flags[i + 1]) if i + 1 < len(flags) else ""
        if re.fullmatch(r"0x[0-9a-fA-F]+", item) and "boot_app0" in nxt.lower():
            if int(item, 16) != ota_off:
                dropped += 1
                i += 2
                continue
        cleaned.append(flags[i])
        i += 1

    cleaned += [hex(ota_off), boot_app0]
    env.Replace(UPLOADERFLAGS=cleaned)

    app_note = ""
    if app0:
        app_note = f", app0 at {hex(app0[0])}"
    print(f"[upload layout] otadata -> {hex(ota_off)} ({ota_size} bytes)"
          f"{app_note}; {dropped} write(s) at a wrong offset removed "
          f"(0xe000 sits inside nvs on this table). A USB flash now boots the "
          f"image it just wrote.")


def report_image_tag(source, target, env):
    """Print the DPPVER marker found in the built image.

    Registered as a post-action on firmware.bin itself, not run at script load:
    PlatformIO's pre:/post: extra_scripts run around the *build script*, not
    around compilation, so calling this at the end of the file printed the
    previous build's marker (observed 2026-10-31 - it said 1.5.1 while the image
    being written said 1.5.2).

    That marker (kImageVersionTag in src/config.cpp) is what lets a client read an
    image's version out of the .bin instead of being told it - the Web UI update
    card fills itself in from the chosen file because of it. If a build ever loses
    the marker (a linker-flag change, a refactor of FW_VERSION), this is where it
    shows up: clients fall back to guessing from the file name, and the update card
    has to show its version box again.
    """
    bin_path = os.path.join(str(env.subst("$BUILD_DIR")), "firmware.bin")
    if not os.path.isfile(bin_path):
        return
    with open(bin_path, "rb") as fh:
        data = fh.read()
    at = data.find(b"DPPVER=")
    if at < 0:
        print("[image tag] WARNING: no DPPVER= marker in firmware.bin - a client "
              "cannot read this image's version from the file "
              "(see kImageVersionTag in src/config.cpp)")
        return
    rest = data[at + 7:]
    end = 0
    while end < len(rest) and (rest[end:end + 1].isalnum() or rest[end:end + 1] in b".-"):
        end += 1
    print(f"[image tag] firmware.bin declares itself "
          f"{rest[:end].decode('ascii', 'replace')} (marker at {hex(at)})")


fix_upload_layout(env)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", report_image_tag)
