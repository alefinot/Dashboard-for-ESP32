"""Gate for the net-class rules: prove KiCad's DRC really polices them.

Run from hardware/:

    python scripts/test_drc_classes.py

Why this exists: the class numbers in make_pcb.py (Power 0.80 mm / 0.60 mm
keep-away, HV 0.60 mm creepage, Ref 0.40 / 0.30) are worthless if no referee
enforces them.  KiCad 10 does not let a board file carry a net_class block -
"Failed to load board: Expected ''symbol''" - the classes live in the PROJECT
file, and a net joins one through net_settings.netclass_patterns.  A rule that
never fires is not a rule, so every case below breaks one rule on purpose, in a
throwaway copy of the board under hardware/scratch/, and demands that
`kicad-cli pcb drc` names it.

Cases are (name, mutate, expect) so later steps of the 4-layer plan can add
their own (a severed copper island, a dangling track) without touching this.
"""
import os
import re
import shutil
import subprocess
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
BOARD = os.path.join(HW, "dashboard-plusplus-carrier.kicad_pcb")
PRJ = os.path.splitext(BOARD)[0] + ".kicad_pro"
PRL = os.path.splitext(BOARD)[0] + ".kicad_prl"
SCRATCH = os.path.join(HW, "scratch", "drc-classes")
FOOT = os.path.join(HW, "footprints")

KICAD_CLI = os.environ.get(
    "KICAD_CLI",
    r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli")

# KiCad's own interpreter - needed only for the Specctra DSN check below.
KICAD_PY = os.environ.get(
    "KICAD_PY",
    r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0/bin/python.exe")

SEG = re.compile(r'\(segment \(net (\d+)\) \(start ([-\d.]+) ([-\d.]+)\) '
                 r'\(end ([-\d.]+) ([-\d.]+)\) \(width ([-\d.]+)\) '
                 r'\(layer "([^"]+)"\)')


def drc(board):
    """Run KiCad's DRC on one board file; return (text, n_violations, n_unconnected)."""
    rep = os.path.join(SCRATCH, os.path.basename(board) + "_drc.txt")
    r = subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all",
                        "-o", rep, board], capture_output=True, text=True)
    if not os.path.exists(rep):
        raise SystemExit("DRC produced no report (%s): %s"
                         % (r.returncode, (r.stderr or "").strip()[:300]))
    txt = open(rep, encoding="utf-8", errors="replace").read()
    v = re.search(r"Found (\d+) DRC violation", txt)
    u = re.search(r"Found (\d+) unconnected", txt)
    return txt, int(v.group(1)) if v else -1, int(u.group(1)) if u else -1


def net_id(lines, name):
    for l in lines:
        m = re.match(r'\t\(net (\d+) "%s"\)' % re.escape(name), l)
        if m:
            return int(m.group(1))
    raise SystemExit("net %r not declared in the board" % name)


def segments(lines, ids):
    """Every routed segment of the given net numbers, as dicts."""
    out = []
    for n, l in enumerate(lines):
        m = SEG.match(l)
        if m and int(m.group(1)) in ids:
            x1, y1, x2, y2, w = (float(m.group(i)) for i in range(2, 7))
            out.append(dict(i=n, net=int(m.group(1)), x1=x1, y1=y1, x2=x2,
                            y2=y2, w=w, layer=m.group(7), text=l))
    return out


# ------------------------------------------------------------------ cases ---
def case_power_near_gnd(lines, pw_cls):
    """Pull a Power track until it sits 0.30 mm from a parallel GND track.

    0.30 mm is inside the Power class keep-away (0.60) but outside the default
    (0.25): if the class assignment is not live, DRC sees nothing.
    """
    gnd = net_id(lines, "GND")
    power = {net_id(lines, n) for n in ("DC_IN", "DC_F", "DC_12", "V5")}
    segs = segments(lines, power | {gnd})
    # rough copper bbox so the moved track does not leave the board
    xs = sorted(v for s in segs for v in (s["x1"], s["x2"]))
    ys = sorted(v for s in segs for v in (s["y1"], s["y2"]))
    best = None
    for p in [s for s in segs if s["net"] in power]:
        horiz = abs(p["y1"] - p["y2"]) < 1e-9
        pc = p["y1"] if horiz else p["x1"]
        for g in [s for s in segs if s["net"] == gnd and s["layer"] == p["layer"]]:
            if (abs(g["y1"] - g["y2"]) < 1e-9) != horiz:
                continue
            gc = g["y1"] if horiz else g["x1"]
            lo_a, hi_a = sorted((p["x1"], p["x2"])) if horiz else sorted((p["y1"], p["y2"]))
            lo_b, hi_b = sorted((g["x1"], g["x2"])) if horiz else sorted((g["y1"], g["y2"]))
            if min(hi_a, hi_b) - max(lo_a, lo_b) < 2.0:
                continue            # they would not actually face each other
            off = 0.30 + g["w"] / 2.0 + p["w"] / 2.0
            for want in (gc + off, gc - off):
                band = ys if horiz else xs
                if abs(want - pc) < 1e-6 or not (band[0] - 1.0 < want < band[-1] + 1.0):
                    continue
                if best and abs(want - pc) >= abs(best[2] - best[4]):
                    continue
                best = (p, g, want, horiz, pc)
    if best is None:
        return None
    p, g, want, horiz, pc = best
    a, b = (p["y1"], p["y2"]) if horiz else (p["x1"], p["x2"])
    s1 = (p["x1"] if horiz else a), (want if horiz else p["y1"])
    s2 = (p["x2"] if horiz else b), (want if horiz else p["y2"])
    head = ('(segment (net %d) (start %.3f %.3f) (end %.3f %.3f)'
            ' (width %.3f) (layer "%s"' % (p["net"], s1[0], s1[1],
                                           s2[0], s2[1], p["w"], p["layer"]))
    j = p["text"].index(')', p["text"].index('(layer '))
    lines[p["i"]] = head + p["text"][j:]
    return ("moved %s (%.3f,%.3f)-(%.3f,%.3f) %s %+.3f mm (now 0.30 mm from %s)"
            % (_netname(lines, p["net"]), p["x1"], p["y1"], p["x2"], p["y2"],
               p["layer"], want - pc, _netname(lines, gnd)), want)


def _netname(lines, nid):
    for l in lines:
        m = re.match(r'\t\(net %d "([^"]*)"\)' % nid, l)
        if m:
            return m.group(1)
    return str(nid)


def dsn_check(board, cls):
    """Prove the class rules travel to an external router.

    Freerouting (and any other Specctra reader) cannot see a .kicad_pro, so the
    classes have to survive the DSN export.  There is no `kicad-cli pcb export
    specctra` subcommand in 10.0.6, but pcbnew.ExportSpecctraDSN does the job.
    Returns (ok, note); ok is None when the check could not run at all.
    """
    if not os.path.isfile(KICAD_PY):
        return None, "skipped: no KiCad python at %s (set KICAD_PY)" % KICAD_PY
    dsn = os.path.join(SCRATCH, os.path.basename(board)[:-4] + ".dsn")
    if os.path.exists(dsn):
        os.remove(dsn)
    code = ("import pcbnew,sys;"
            "sys.exit(0 if pcbnew.ExportSpecctraDSN(pcbnew.LoadBoard(sys.argv[1]),"
            " sys.argv[2]) else 1)")
    subprocess.run([KICAD_PY, "-c", code, board, dsn],
                   capture_output=True, text=True, cwd=SCRATCH)
    if not os.path.exists(dsn):
        return False, "DSN export produced nothing"
    txt = open(dsn, encoding="utf-8", errors="replace").read().splitlines()
    bad, seen = [], []
    for klass in ("Power", "HV", "Ref"):
        at = next((i for i, l in enumerate(txt)
                   if l.strip().startswith("(class %s " % klass)), None)
        if at is None:
            bad.append("%s: class block missing from the DSN" % klass)
            continue
        blk = "\n".join(txt[at:at + 14])
        w = re.search(r"\(width (\d+)\)", blk)
        c = re.search(r"\(clearance (\d+)\)", blk)
        via = re.search(r'use_via "Via\[[0-9]-\d\]_(\d+):(\d+)_um"', blk)
        got = (int(w.group(1)) / 1000.0 if w else None,
               int(c.group(1)) / 1000.0 if c else None,
               (int(via.group(1)) / 1000.0, int(via.group(2)) / 1000.0)
               if via else None)
        want = (cls[klass]["track_width"], cls[klass]["clearance"],
                cls[klass]["via_diameter"], cls[klass]["via_drill"])
        seen.append("%s w %s clr %s via %s" % (klass, got[0], got[1], got[2]))
        if got != (want[0], want[1], (want[2], want[3])):
            bad.append("%s: DSN w %s clr %s via %s vs project w %s clr %s "
                       "via (%s, %s)" % (klass, got[0], got[1], got[2],
                                         want[0], want[1], want[2], want[3]))
    return (not bad), ("; ".join(seen) + ((" | " + " | ".join(bad)) if bad else ""))


def fired(txt, tag, klass):
    """True if the report has a `tag` item whose own line names the class."""
    return any(l.startswith("[%s]" % tag) and 'netclass "%s"' % klass in l
               for l in txt.splitlines())


CASES = [
    # (name, mutate(lines) -> note, expect(report text) -> (ok, why))
    ("power track pulled to 0.30 mm from GND copper",
     case_power_near_gnd,
     lambda txt: (fired(txt, "clearance", "Power"),
                  'no [clearance] item naming netclass "Power"')),
]


def main():
    if not os.path.isfile(BOARD):
        raise SystemExit("run me from hardware/: no " + BOARD)
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH)
    shutil.copytree(FOOT, os.path.join(SCRATCH, "footprints"))
    shutil.copy(os.path.join(HW, "fp-lib-table"), SCRATCH)

    import json
    prj = json.load(open(PRJ, encoding="utf-8"))
    cls = {c["name"]: c for c in prj["net_settings"]["classes"]}
    pw = cls.get("Power")
    if pw is None:
        raise SystemExit("FAIL: the project has no Power net class at all")

    base = open(BOARD, encoding="utf-8").read().splitlines(True)

    # 0. the board as committed must be clean WITH the classes live
    shutil.copy(PRL, os.path.join(SCRATCH, "clean.kicad_prl"))
    json.dump(prj, open(os.path.join(SCRATCH, "clean.kicad_pro"), "w",
                        encoding="utf-8"), indent=2)
    open(os.path.join(SCRATCH, "clean.kicad_pcb"), "w",
         encoding="utf-8").write("".join(base))
    txt, nv, nu = drc(os.path.join(SCRATCH, "clean.kicad_pcb"))
    # A2's stitching vias are joined to GND by the pour, and on this UNFILLED
    # copy there is no pour: KiCad calls each of them `via_dangling` (a violation
    # type) plus an unconnected item.  That is a property of the fill state, not
    # of the board - scripts/fill_zones.py + the filled DRC that make_pcb.py and
    # scripts/test_plane_fill.py both run require 0 violations AND 0 unconnected,
    # and that is where a stitch via that fails to join the pour is caught.  This
    # gate exists to prove the NET CLASSES are policed, so it counts everything
    # except the fill-state kinds here and says what it left out.
    fill_state = sum(1 for l in txt.splitlines()
                     if l.startswith("[via_dangling]"))
    hard = nv - fill_state
    print("baseline    %d violations, %d unconnected (classes: %s)"
          % (nv, nu, ", ".join("%s clr %s w %s" % (n, c["clearance"],
                                                   c["track_width"])
                               for n, c in cls.items())))
    if fill_state or nu:
        print("            (%d via_dangling + %d unconnected item(s) ignored: both "
              "are what an unfilled board says about pour-connected stitching "
              "vias; the filled DRC is the verdict)" % (fill_state, nu))
    bad = 0 if hard == 0 else 1
    if bad:
        for l in txt.splitlines():
            if l.startswith("[") and not l.startswith("[via_dangling]"):
                print("   ", l)

    # the classes must survive a Specctra DSN export - that is how an external
    # router (Freerouting, Task 5) gets to see them at all
    ok, note = dsn_check(os.path.join(SCRATCH, "clean.kicad_pcb"), cls)
    print("dsn           %s" % note)
    if ok is False:
        bad = 1

    # 1..n. deliberately broken copies
    for name, mutate, expect in CASES:
        lines = list(base)
        note = mutate(lines, pw)
        if note is None:
            print("FAIL: could not set up the case: %s" % name)
            bad = 1
            continue
        out = os.path.join(SCRATCH, "brk.kicad_pcb")
        open(out, "w", encoding="utf-8").write("".join(lines))
        shutil.copy(PRL, os.path.join(SCRATCH, "brk.kicad_prl"))
        json.dump(prj, open(os.path.join(SCRATCH, "brk.kicad_pro"), "w",
                            encoding="utf-8"), indent=2)
        t2, nv2, _ = drc(out)
        items = [l for l in t2.splitlines() if l.startswith("[")]
        ok, why = expect(t2)
        print("break       %s\n            (%s): %d violations" % (name, note[0], nv2))
        for l in items:
            if "netclass" in l:
                print("            ", l)
        if not ok:
            print("FAIL: %s -> %s" % (name, why))
            bad = 1
        else:
            print("            the rule fired: KiCad polices this class")

    print("class gate: %s" % ("PASS" if not bad else "FAIL"))
    return bad


if __name__ == "__main__":
    sys.exit(main())
