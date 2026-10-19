"""Freerouting yardstick - run a professional external router over this board.

Master plan step 5 (Implementation plans/REMAINING-WORK-MASTER-PLAN-2026-10-19)
plus the stackup plan's section 10 item 6, which permits the run.  The point is
not to adopt Freerouting - it is to learn how far our own router's 2934.9 mm of
copper and 271 vias sit from a third party's optimum on the SAME board.  A
failed or ugly Freerouting run is a result too; nothing here touches the
production path.

What it does, all inside hardware/out/yardstick/ (gitignored):

  1. copies the committed board and strips EVERY track and via (pads, placement
     and the zones stay), so the external router starts from the same placement
     we did - it re-routes, nothing more;
  2. exports KiCad's own Specctra DSN (`pcbnew.ExportSpecctraDSN`, the same
     call test_drc_classes.py uses - the net classes travel in it) and edits the
     layer types: In1.Cu and In2.Cu become plane layers, so the autorouter can
     only route on F.Cu/B.Cu and the inner layers keep their GND-plane role;
  3. runs the Freerouting CLI headless (gitignored binary under
     hardware/tools/freerouting/) with -l en for parseable logs.  The v2.5.0
     native build crashes on the first board snapshot for every KiCad 10 DSN
     (upstream issue #957, fix PR #966, milestone 2.6, not yet released), so
     the SNAPSHOT-20261014 native build from the nightly channel is preferred;
     --fr-exe overrides the pick, and RESULT.md records the binary's hash;
  4. imports the .ses back into a scratch copy and lets KICAD's filler fill the
     zones, exactly like make_pcb.py does for its DRC copy;
  5. judges the result with OUR instruments only - KiCad DRC on the filled
     board, return_path.py, test_crosstalk.py, test_stitch.py - plus length and
     via counts, and writes RESULT.md with every number.

Known, documented asymmetry: KiCad does not export the copper-to-edge
clearance into the DSN (KiCad issue 24077, freerouting issue #558), so
Freerouting keeps wires only at its default clearance (0.25 mm here) from the
outline while our project enforces 0.30 mm.  Any `copper_edge_clearance`
violations in the DRC report are that gap, not a routing defect; the report
prints them split out so the rest of the comparison stays fair.

Usage (KiCad's own python - it is the only one with pcbnew):
    "<Kicad>/10.0/bin/python.exe" hardware/scripts/freerouting_yardstick.py
        [--board <path>] [--passes N] [--threads N] [--timeout S] [--skip-fr]
        [--fr-exe <path>]

--skip-fr re-judges the existing hardware/out/yardstick/fr-output.ses without
re-running the router (the scratch dir is kept).
"""
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, os.pardir))
BOARD = os.path.join(HW, "dashboard-plusplus-carrier.kicad_pcb")
PRO = os.path.splitext(BOARD)[0] + ".kicad_pro"
PRL = os.path.splitext(BOARD)[0] + ".kicad_prl"
FR_DIR = os.path.join(HW, "tools", "freerouting")
OUT = os.path.join(HW, "out", "yardstick")

sys.path.insert(0, HERE)
import kicadfmt                                             # noqa: E402

KICAD_PY = kicadfmt.KICAD_PY
KICAD_CLI = kicadfmt.KICAD_CLI

WITH_PROJECT = ["fr-input", "fr-routed", "fr-routed-filled"]


def setup(keep=False):
    if not keep:
        shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT, exist_ok=True)
    for extra in ("fp-lib-table", "footprints"):
        src = os.path.join(HW, extra)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(OUT, extra), dirs_exist_ok=True)
        elif os.path.isfile(src):
            shutil.copy(src, OUT)
    for base in WITH_PROJECT:
        if os.path.isfile(PRO):
            shutil.copy(PRO, os.path.join(OUT, base + ".kicad_pro"))
        if os.path.isfile(PRL):
            shutil.copy(PRL, os.path.join(OUT, base + ".kicad_prl"))


def pcb_op(*args):
    """Run one pcb_ops.py subcommand under KiCad's python and return its
    "RESULT " payload.  Each board load gets its own process (multi-load
    interpreters poison KiCad 10 SWIG; see pcb_ops.py) and the payload comes
    back on stderr because SWIG's raw leak-noise writes on stdout can split
    lines mid-way."""
    r = subprocess.run([KICAD_PY, os.path.join(HERE, "pcb_ops.py")] +
                       list(args), capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("pcb_ops %s failed:\n%s" % (args[0],
                                                     r.stdout + r.stderr))
    res = re.findall(r"RESULT ([^\r\n]+)", r.stderr)
    if not res:
        raise SystemExit("pcb_ops %s: no RESULT line in stderr %r" %
                         (args[0], r.stderr[-300:]))
    return res[-1]


def edit_dsn(dsn):
    """In1/In2 become power layers so the router cannot put signals there."""
    txt = open(dsn, encoding="utf-8").read()
    txt, n = re.subn(r"(\(layer (?:In1|In2)\.Cu\s*\n\s*\(type )signal(\))",
                     r"\1power\2", txt)
    if n != 2:
        raise SystemExit("DSN edit: expected 2 inner layers, found %d" % n)
    open(dsn, "w", encoding="utf-8", newline="").write(txt)
    return n


def find_fr_exe(explicit):
    """Prefer the newest snapshot native build (v2.5.0 native is broken -
    upstream issue #957), then the plain/generic names."""
    if explicit:
        return explicit
    if os.path.isdir(FR_DIR):
        snaps = sorted(f for f in os.listdir(FR_DIR)
                       if re.match(r"freerouting-cli-snapshot-\d+\.exe$", f))
        if snaps:
            return os.path.join(FR_DIR, snaps[-1])
        for name in ("freerouting-cli.exe", "freerouting-cli-2.5.0.exe"):
            p = os.path.join(FR_DIR, name)
            if os.path.isfile(p):
                return p
    return os.path.join(FR_DIR, "freerouting-cli.exe")


def sha256_short(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:16]


def run_freerouting(fr_exe, dsn, ses, passes, threads, timeout):
    if not os.path.isfile(fr_exe):
        raise SystemExit("no Freerouting binary at %s (gitignored; see the "
                         "module docstring)" % fr_exe)
    cmd = [fr_exe, "-de", dsn, "-do", ses, "-mp", str(passes),
           "-mt", str(threads), "-l", "en"]
    log = os.path.join(OUT, "freerouting.log")
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=OUT, capture_output=True, text=True,
                           timeout=timeout)
        rc, timed_out = r.returncode, False
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired as e:
        rc, timed_out = -1, True
        out = (e.stdout or "") + (e.stderr or "")
    dt = time.time() - t0
    open(log, "w", encoding="utf-8").write(out)
    for line in out.splitlines():
        if any(k in line for k in ("pass", "Pass", "route", "Route",
                                   "unrouted", "completed", "Completed",
                                   "failed", "Failed", "INFO")):
            print("  FR " + line[:160])
    return rc, dt, timed_out


def fill(board, dst):
    r = subprocess.run([KICAD_PY, os.path.join(HERE, "fill_zones.py"),
                        board, dst], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("zone fill failed:\n" + r.stdout + r.stderr)
    for line in r.stdout.splitlines():
        if "filled" in line.lower() or "mm2" in line:
            print("  FILL " + line)
    return r.stdout


def kicad_drc(filled, report):
    r = subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all",
                        "-o", report, filled], capture_output=True, text=True)
    txt = open(report, encoding="utf-8", errors="replace").read() \
        if os.path.exists(report) else ""
    nums = {}
    for what, pat in (("violations", r"Found (\d+) DRC violation"),
                      ("unconnected", r"Found (\d+) unconnected"),
                      ("footprint", r"Found (\d+) Footprint error")):
        m = re.search(pat, txt)
        nums[what] = int(m.group(1)) if m else -1
    edge = len(re.findall(r"\[copper_edge_clearance\]", txt))
    other = len(re.findall(r"^\[(?!copper_edge_clearance)[a-z_]+\]", txt,
                           re.M))
    nums["edge_clears"] = edge
    print("  DRC exit %d | %s | edge-clearance items %d, other items %d"
          % (r.returncode, nums, edge, other))
    return nums, txt


def audit(script, board, extra=None):
    cmd = [KICAD_PY, os.path.join(HERE, script), board] + (extra or [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    lines = [l for l in (r.stdout + r.stderr).splitlines()
             if l.strip() and "duplicate image handler" not in l]
    tail = "\n".join(lines[-12:])
    print("  %s (exit %d):" % (script, r.returncode))
    for l in lines[-6:]:
        print("    " + l[:160])
    return r.returncode, tail


def counts(board):
    """(total mm on F.Cu/B.Cu, via count, per-layer mm dict)."""
    d = json.loads(pcb_op("counts", board))
    return d["bcu_mm"], d["vias"], d["per"]


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--board", default=BOARD)
    ap.add_argument("--passes", type=int, default=25)
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--skip-fr", action="store_true")
    ap.add_argument("--fr-exe", default=None,
                    help="Freerouting CLI binary (default: newest snapshot "
                         "build under hardware/tools/freerouting/)")
    args = ap.parse_args(argv[1:])
    fr_exe = find_fr_exe(args.fr_exe)

    print("== setup (scratch %s%s)" % (OUT, ", kept" if args.skip_fr else ""))
    setup(keep=args.skip_fr)
    t_in = os.path.join(OUT, "fr-input.kicad_pcb")
    t_dsn = os.path.join(OUT, "fr-input.dsn")
    t_ses = os.path.join(OUT, "fr-output.ses")

    n_stripped = int(pcb_op("strip", args.board, t_in))
    print("  stripped %d tracks/vias from the committed board" % n_stripped)
    pcb_op("dsn", t_in, t_dsn)
    print("  DSN: %d inner layers switched to plane type" % edit_dsn(t_dsn))

    dt = fr = None
    if args.skip_fr:
        if not os.path.exists(t_ses):
            raise SystemExit("--skip-fr: no %s to reuse" % t_ses)
        print("== freerouting skipped (--skip-fr, reusing %s)" % t_ses)
    else:
        print("== freerouting %s sha256:%s (passes %d, threads %d, timeout "
              "%ds)" % (os.path.basename(fr_exe), sha256_short(fr_exe),
                        args.passes, args.threads, args.timeout))
        fr, dt, timed = run_freerouting(fr_exe, t_dsn, t_ses, args.passes,
                                        args.threads, args.timeout)
        print("  exit %s after %.0f s%s" % (fr, dt,
              " (TIMED OUT - partial run)" if timed else ""))
        if not os.path.exists(t_ses):
            raise SystemExit("no .ses produced - the run above is the result")

    t_routed = os.path.join(OUT, "fr-routed.kicad_pcb")
    pcb_op("ses", t_in, t_ses, t_routed)
    print("== imported %s" % t_routed)

    ours_mm, ours_vias, _ = counts(args.board)
    fr_mm, fr_vias, fr_per = counts(t_routed)
    print("  routable-length F+B: ours %.1f mm / %d vias | freerouting %.1f mm "
          "/ %d vias" % (ours_mm, ours_vias, fr_mm, fr_vias))
    print("  per layer: " + ", ".join("%s %.1f" % (k, v)
                                      for k, v in sorted(fr_per.items())))

    t_filled = os.path.join(OUT, "fr-routed-filled.kicad_pcb")
    print("== fill zones (KiCad's filler)")
    fill(t_routed, t_filled)

    print("== KiCad DRC (the referee)")
    nums, drc_txt = kicad_drc(t_filled,
                              os.path.join(OUT, "fr-drc.txt"))

    print("== audits (our instruments, on the freerouting board)")
    rp_rc, rp_out = audit("return_path.py", t_filled,
                          ["--json", os.path.join(OUT, "return_path.json")])
    ct_rc, ct_out = audit("test_crosstalk.py", t_routed)
    st_rc, st_out = audit("test_stitch.py", t_routed)

    res = ["# Freerouting yardstick - %s" % datetime.date.today().isoformat(),
           "",
           "Board: `%s` (tracks stripped, re-routed by `%s` sha256:%s)" %
           (os.path.basename(args.board), os.path.basename(fr_exe),
            sha256_short(fr_exe)),
           "FR run: exit %s, %.0f s, passes %s, threads %s" %
           (fr, dt or 0, args.passes, args.threads),
           "",
           "| number | ours | freerouting |", "|---|---|---|",
           "| routable length F+B | %.1f mm | %.1f mm |" % (ours_mm, fr_mm),
           "| vias | %d | %d |" % (ours_vias, fr_vias),
           "| DRC violations | 0 | %d |" % nums["violations"],
           "| ...of which copper_edge_clearance | 0 | %d |" %
           nums["edge_clears"],
           "| unconnected | 0 | %d |" % nums["unconnected"],
           "| footprint errors | 0 | %d |" % nums["footprint"],
           "| return_path exit | 0 | %d |" % rp_rc,
           "| test_crosstalk exit | 0 | %d |" % ct_rc,
           "| test_stitch exit | 0 | %d |" % st_rc,
           "",
           "Freerouting per-layer length: " +
           ", ".join("%s %.1f mm" % (k, v) for k, v in sorted(fr_per.items())),
           "",
           "## return_path (tail)", "```", rp_out, "```",
           "## test_crosstalk (tail)", "```", ct_out, "```",
           "## test_stitch (tail)", "```", st_out, "```",
           "## KiCad DRC items (non-edge, first 40 lines)",
           "```"] + [l for l in drc_txt.splitlines()
                     if re.match(r"^\[", l)][:40] + ["```"]
    open(os.path.join(OUT, "RESULT.md"), "w", encoding="utf-8").write(
        "\n".join(res) + "\n")
    print("== wrote %s" % os.path.join(OUT, "RESULT.md"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
