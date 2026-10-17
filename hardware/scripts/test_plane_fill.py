"""Gate for the plane fill: prove the inner GND planes are really poured.

Run from hardware/:

    python scripts/test_plane_fill.py

Why: an unfilled zone is a drawing of a plane, not a plane.  The committed
board keeps its zones unfilled on purpose (small, diff-friendly file), so the
thing the fab and DRC must be judged on is a FILLED copy - and a filler that
silently fills nothing would sail through every other check.  Measured on
2026-10-17: the inner Gerbers of the unfilled board are 9.7 KB of pad rings;
after the fill they are 275 KB, and each GND zone reports one filled polygon of
~4100 mm^2 of the ~4320 mm^2 of outline the pour draws.

The gate is run twice on purpose - once on the unfilled board (negative
control: zero filled copper, so the measurement below cannot be vacuous) and
once on the filled copy.  In1.Cu must come out as ONE piece: a second piece
means a slot or an island in the one reference plane the signals depend on.
"""
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kicadfmt

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
BOARD = os.path.join(HW, "dashboard-plusplus-carrier.kicad_pcb")
PRJ = os.path.splitext(BOARD)[0] + ".kicad_pro"
PRL = os.path.splitext(BOARD)[0] + ".kicad_prl"
SCRATCH = os.path.join(HW, "out", "plane-fill-gate")

KICAD_CLI = kicadfmt.KICAD_CLI
KICAD_PY = kicadfmt.KICAD_PY

ZONE = re.compile(r'^\(zone \(net (\d+)\) \(net_name "([^"]*)"\) \(layer "([^"]+)"\)')


def zones_of(path):
    """(net name, layer number) -> layer name for every zone in the board.

    The layer NUMBER is the key because that is what pcbnew reports back
    (kicadfmt.layer_table explains why the names do not come from pcbnew).
    """
    txt = open(path, encoding="utf-8").read()
    num = kicadfmt.layer_table(txt)
    out = {}
    for line in txt.splitlines():
        m = ZONE.match(line)
        if m:
            lay, net = m.group(3), m.group(2)
            if (net, num[lay]) in out:
                raise SystemExit("two zones share net %r on %s; the gate cannot "
                                 "tell them apart" % (net, lay))
            out[(net, num[lay])] = lay
    return out


def measure(board):
    """Filled copper per zone: (net, layer number) -> (mm^2, outline count).

    Needs KiCad's own python: the numbers have to come from the same code that
    will plot the planes for the fab.
    """
    code = (
        "import pcbnew,sys;"
        "b=pcbnew.LoadBoard(sys.argv[1]);"
        "print('|'.join('%s~%d~%.1f~%d' % (z.GetNetname(), z.GetLayer(),"
        " z.GetFilledArea()/1e12, z.GetFilledPolysList(z.GetLayer()).OutlineCount())"
        " for z in b.Zones()))")
    r = subprocess.run([KICAD_PY, "-c", code, board], capture_output=True,
                       text=True)
    if r.returncode or "|" not in r.stdout:
        raise SystemExit("KiCad python failed (%s): %s"
                         % (r.returncode, (r.stderr or "").strip()[-300:]))
    line = [l for l in r.stdout.splitlines() if "|" in l][-1]
    out = {}
    for tok in line.strip().split("|"):
        net, lay, area, cnt = tok.split("~")
        out[(net, int(lay))] = (float(area), int(cnt))
    return out


def drc(board):
    rep = os.path.join(SCRATCH, os.path.basename(board) + "_drc.txt")
    subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all", "-o", rep, board],
                   capture_output=True, text=True)
    txt = open(rep, encoding="utf-8", errors="replace").read()
    g = lambda pat: int(re.search(pat, txt).group(1))
    return (g(r"Found (\d+) DRC violation"), g(r"Found (\d+) unconnected"),
            g(r"Found (\d+) Footprint errors"))


def main():
    for p in (BOARD, PRJ, KICAD_PY):
        if not os.path.isfile(p):
            raise SystemExit("missing " + p)
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH)
    # A DRC run needs the project beside the board (kicad-cli otherwise invents
    # its own design settings) and the footprint library reachable from it.
    shutil.copy(BOARD, os.path.join(SCRATCH, "gate.kicad_pcb"))
    shutil.copy(PRJ, os.path.join(SCRATCH, "gate.kicad_pro"))
    shutil.copy(PRL, os.path.join(SCRATCH, "gate.kicad_prl"))
    shutil.copy(os.path.join(HW, "fp-lib-table"), SCRATCH)
    shutil.copytree(os.path.join(HW, "footprints"),
                    os.path.join(SCRATCH, "footprints"), dirs_exist_ok=True)
    lay = zones_of(BOARD)
    bad = 0

    # negative control: the committed file must carry NO filled copper
    before = measure(os.path.join(SCRATCH, "gate.kicad_pcb"))
    print("unfilled    " + "  ".join(
        "%s %s: %.0f mm2 / %d piece(s)"
        % (lay.get(k, "?"), k[0] or "keepout", a, c)
        for k, (a, c) in before.items()))
    if any(a > 0 for a, _ in before.values()):
        print("FAIL: the committed board is supposed to ship its zones unfilled")
        bad = 1

    # the fill itself
    r = subprocess.run([KICAD_PY, os.path.join(HERE, "fill_zones.py"),
                        os.path.join(SCRATCH, "gate.kicad_pcb"),
                        os.path.join(SCRATCH, "gate-filled.kicad_pcb")],
                       capture_output=True, text=True)
    # pcbnew's image-handler chatter is noise, everything else is the record
    sys.stdout.write("".join("            " + l + "\n"
                             for l in (r.stdout + r.stderr).splitlines()
                             if not l.startswith("Adding duplicate")))
    if r.returncode:
        print("FAIL: fill_zones.py exited %d" % r.returncode)
        return 1
    after = measure(os.path.join(SCRATCH, "gate-filled.kicad_pcb"))
    # the filled copy is judged as its own board, so it needs its own project
    # file under the matching basename (kicad-cli pairs project and board by name)
    for ext in (".kicad_pro", ".kicad_prl"):
        shutil.copy(os.path.join(SCRATCH, "gate" + ext),
                    os.path.join(SCRATCH, "gate-filled" + ext))

    # every GND pour must have become real copper, and In1 must stay one piece
    for key, layer in lay.items():
        if key[0] != "GND":
            continue
        area, pieces = after[key]
        print("pour        %s %s: %.0f mm2 filled in %d piece(s) (was %.0f)"
              % (layer, key[0], area, pieces, before[key][0]))
        if area < 1000.0:
            print("FAIL: %s on %s poured %.0f mm2 - the plane is not there"
                  % (key[0], layer, area))
            bad = 1
        if layer == "In1.Cu" and pieces != 1:
            print("FAIL: In1.Cu fill came out in %d pieces - the reference plane "
                  "is cut (plan rule: In1 is never routed, never moated)" % pieces)
            bad = 1

    v, u, f = drc(os.path.join(SCRATCH, "gate-filled.kicad_pcb"))
    print("drc filled  %d violations, %d unconnected, %d footprint errors" % (v, u, f))
    if (v, u, f) != (0, 0, 0):
        txt = open(os.path.join(SCRATCH, "gate-filled.kicad_pcb_drc.txt"),
                   encoding="utf-8", errors="replace").read()
        for l in txt.splitlines():
            if l.startswith("["):
                print("   ", l)
        bad = 1

    print("plane fill gate: %s" % ("PASS" if not bad else "FAIL"))
    return bad


if __name__ == "__main__":
    sys.exit(main())
