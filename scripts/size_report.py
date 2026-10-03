#!/usr/bin/env python3
"""size_report.py - firmware size + symbol snapshot for the cleanup work.

Usage:
    python scripts/size_report.py [--no-build] [--top N] [--out FILE]

Runs `pio run` (unless --no-build), then reports:
  * RAM / Flash usage lines straight from PlatformIO
  * the N largest symbols in .pio/build/esp32dev/firmware.elf
  * the size delta against the newest previous report in Implementation plans/

Reports land in "Implementation plans/" (local-only, AGENTS.md rule 6) as
size-<gitshort>.txt. Not a release tool: it is the before/after evidence trail
for the cleanup plan, so every phase can prove it made flash go DOWN.
"""

import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = "esp32dev"
BUILD = os.path.join(ROOT, ".pio", "build", ENV)
ELF = os.path.join(BUILD, "firmware.elf")
PLANS = os.path.join(ROOT, "Implementation plans")


def find_nm():
    """Locate xtensa-esp32-elf-nm from the PlatformIO packages folder."""
    home = os.path.expanduser("~")
    pats = [
        os.path.join(home, ".platformio", "packages", "toolchain-*", "bin",
                     "xtensa-esp32-elf-nm*.exe"),
        os.path.join(home, ".platformio", "packages", "toolchain-*", "bin",
                     "xtensa-esp32-elf-nm"),
    ]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[0]
    return "xtensa-esp32-elf-nm"  # hope it is on PATH


def git_short():
    try:
        return subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True,
                              timeout=10).stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def build():
    r = subprocess.run(["pio", "run", "-e", ENV], cwd=ROOT, capture_output=True,
                       text=True, timeout=3600)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SystemExit("pio run FAILED - no report written")
    return r.stdout


def usage_lines(build_out):
    out = []
    for pat in (r"RAM:\s*\[[^\]]*\]\s*[\d.]+%\s*\(used [\d,]+ bytes from [\d,]+\)",
                r"Flash:\s*\[[^\]]*\]\s*[\d.]+%\s*\(used [\d,]+ bytes from [\d,]+\)"):
        m = re.search(pat, build_out)
        out.append(m.group(0) if m else pat[:12] + " NOT FOUND")
    # Absolute section sizes are the ones to diff, not the percentage bars.
    m = re.search(r"program size is (\d+)", build_out)
    if m:
        out.append("program size: %s bytes" % m.group(1))
    return out


def symbols(nm, top):
    r = subprocess.run([nm, "-S", "--size-sort", "-C", ELF], capture_output=True,
                       text=True, timeout=300)
    if r.returncode != 0:
        raise SystemExit("nm failed: " + r.stderr[:400])
    rows = []
    for line in r.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3:
            continue
        try:
            size = int(parts[1], 16)
        except ValueError:
            continue
        if size == 0:
            continue
        rows.append((size, parts[2]))
    rows.sort(reverse=True)
    total = sum(s for s, _ in rows)
    return rows[:top], total


def previous_report():
    if not os.path.isdir(PLANS):
        return None
    cands = [p for p in glob.glob(os.path.join(PLANS, "size-*.txt"))]
    if not cands:
        return None
    return max(cands, key=os.path.getmtime)


def main():
    args = sys.argv[1:]
    no_build = "--no-build" in args
    top = 30
    if "--top" in args:
        top = int(args[args.index("--top") + 1])
    out_path = None
    if "--out" in args:
        out_path = args[args.index("--out") + 1]

    build_out = "" if no_build else build()
    if not os.path.exists(ELF):
        raise SystemExit("no ELF at " + ELF)

    sha = git_short()
    if out_path is None:
        out_path = os.path.join(PLANS, "size-%s.txt" % sha)
    os.makedirs(PLANS, exist_ok=True)

    rows, total = symbols(find_nm(), top)
    lines = []
    lines.append("size report @ %s" % sha)
    lines.append("=" * 62)
    if build_out:
        lines += usage_lines(build_out)
    lines.append("sum of sized symbols: %d bytes" % total)
    lines.append("")
    lines.append("%10s  symbol" % "bytes")
    for size, name in rows:
        lines.append("%10d  %s" % (size, name))

    prev = previous_report()
    if prev and os.path.abspath(prev) != os.path.abspath(out_path):
        lines.append("")
        lines.append("delta vs %s" % os.path.basename(prev))
        old = {}
        with open(prev, encoding="utf-8") as f:
            for line in f:
                m = re.match(r"^\s*(\d+)\s\s(\S.*)$", line)
                if m:
                    old[m.group(2).strip()] = int(m.group(1))
        moves = []
        for size, name in rows:
            if name in old:
                moves.append((size - old[name], name, size, old[name]))
        moves.sort()
        for d, name, new, oldsz in moves[:15]:
            lines.append("  %+8d  %s  (%d -> %d)" % (d, name, oldsz, new))

    text = "\n".join(lines) + "\n"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)
    print(text)
    print("written: %s" % out_path)


if __name__ == "__main__":
    main()
