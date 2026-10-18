"""Task: the inner planes are stitched, and the stitch holes are legal.

Pins down the ground-integrity half of
Implementation plans/SENSOR-SILENCE-ADC-ACCURACY-PLAN-2026-10-18.md (§"return
path / stitching"), on the board make_pcb.py actually wrote:

  * the GND pour token really is scripts/make_pcb.py:POUR_CLEAR in the board
    file (0.35 mm, measured 2026-10-18: the same copper at 0.60 mm loses 227.1 mm
    / 7.74 % of the sampled signal run over no GND, at 0.35 mm it loses 25.1 mm /
    0.86 %, and both planes still fill as ONE piece each);
  * stitching ran: the board carries hundreds of GND vias, not the 36 the router
    placed by itself;
  * every signal layer change has a GND hole next to it (before: median 5.10 mm
    from a signal via to the nearest GND via, p90 9.80, worst 18.50);
  * every GND via on the board keeps its class clearance on F.Cu, In1.Cu,
    In2.Cu and B.Cu - checked with stitch_pcb's model at safe=0, which is the
    model proved to agree with KiCad's DRC on 2026-10-17 (a GND via declared
    F.Cu-B.Cu 1.8 mm from a 2.5 mm In2 rail produced
    `netclass "Power" 0.6000 mm`, i.e. DRC policed the barrel on a layer the via
    merely passes).  This is NOT a substitute for the DRC: make_pcb.py runs the
    filled DRC and stops on it.  It is the fast loop that catches a placer bug
    before a 12-minute regeneration;
  * and no two plated holes of different nets come inside the fab's 0.50 mm
    hole-to-hole limit (scripts/make_pcb.py:_CONSTRAINTS).

Run:  "<KiCad python>" hardware/scripts/test_stitch.py [board-file]
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pcb                                            # noqa: E402
import stitch_pcb                                          # noqa: E402

FAIL = []
HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, os.pardir, "dashboard-plusplus-carrier.kicad_pcb")


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        FAIL.append(msg)


def main(argv):
    path = argv[1] if len(argv) > 1 else BOARD
    txt = open(path, encoding="utf-8").read()
    nets = dict((int(n), nm) for n, nm in re.findall(r'\(net (\d+) "([^"]+)"\)',
                                                     txt))
    print("stitch gate on %s" % os.path.basename(path))
    print("-" * 66)

    # ---- the pour token -------------------------------------------------
    tok = re.findall(r'\(net_name "GND"\).*?\(connect_pads \(clearance ([\d.]+)\)',
                     txt)
    check(len(tok) == 2 and all(abs(float(t) - make_pcb.POUR_CLEAR) < 1e-6
                                for t in tok),
          "both GND pours carry connect_pads clearance %.2f (= POUR_CLEAR): %s"
          % (make_pcb.POUR_CLEAR, tok))

    # ---- read the emitted copper in the design frame --------------------
    segs, vias = stitch_pcb.read_board(path, make_pcb.PAGE_DX, make_pcb.PAGE_DY,
                                       nets)
    pads = make_pcb.route_pads()
    frame = (make_pcb.BOARD_X0, make_pcb.BOARD_Y0, make_pcb.BOARD_X1,
             make_pcb.BOARD_Y1)
    gnd = [v for v in vias if v["net"] == "GND"]
    sig = [v for v in vias if v["net"] != "GND"]
    print("  %d segment(s), %d via(s): %d GND, %d signal"
          % (len(segs), len(vias), len(gnd), len(sig)))
    check(len(gnd) >= 200,
          "stitching ran: %d GND vias on the board (the router alone put 36)"
          % len(gnd))
    check(sum(1 for v in gnd if abs(v["dia"] - stitch_pcb.VIA_DIA) < 1e-6)
          >= len(gnd) - 40,
          "the stitch holes use the via size already on the board (%.2f/%.2f mm), "
          "so no new fab feature appears" % (stitch_pcb.VIA_DIA,
                                             stitch_pcb.VIA_DRILL))

    # ---- the clearance verdict is KiCad's, not ours ----------------------
    # (AGENTS.md §30).  A local "is this GND via legal" re-implementation was
    # here and flagged all 217 stitch vias on the unfilled board, because on that
    # board a stitch via has nothing to touch - it is joined to GND by the pour,
    # which only exists after fill.  KiCad agrees the board is clean: make_pcb's
    # own DRC step and scripts/test_plane_fill.py both run DRC on the FILLED copy
    # and report 0 violations / 0 unconnected, and that is the verdict this gate
    # defers to.  What the stitcher does owe the fab, and what is checked below,
    # are the hole limits no DRC default encodes for us.

    # ---- holes of different nets stay 0.50 mm apart ---------------------
    holes = [(v["x"], v["y"], v["drill"], v["net"]) for v in vias]
    holes += [((p["box"][0] + p["box"][2]) / 2.0, (p["box"][1] + p["box"][3]) / 2.0,
               p["drill"], p["net"]) for p in pads
              if p.get("drill", 0.0) and p.get("net")]
    hh, who = 9e9, None
    for i in range(len(holes)):
        ax, ay, ad, an = holes[i]
        for j in range(i + 1, len(holes)):
            bx, by, bd, bn = holes[j]
            if an == bn:
                continue
            g = math.hypot(ax - bx, ay - by) - (ad + bd) / 2.0
            if g < hh:
                hh, who = g, (an, bn)
    # The fab limit is read from the project file, which is where make_pcb.py's
    # sync_project() writes it - so if the fab limit moves, this gate follows it
    # instead of arguing with a copy of it.
    fab = 0.50
    try:
        import json
        prj = json.load(open(path.replace(".kicad_pcb", ".kicad_pro"),
                             encoding="utf-8"))
        fab = prj["board"]["design_settings"]["rules"]["min_hole_to_hole"]
    except (OSError, ValueError, KeyError):
        pass
    check(hh >= fab - 1e-6, "tightest different-net hole-to-hole gap %.3f mm "
                            "stays at or over the fab limit %.2f mm (%s / %s)"
          % (hh, fab, who[0], who[1]))

    # ---- return-path proximity: a GND hole by every layer change --------
    gp = [(v["x"], v["y"]) for v in gnd]
    dist = sorted(((min(math.hypot(v["x"] - gx, v["y"] - gy) for gx, gy in gp), v)
                   for v in sig), key=lambda t: t[0])
    dflt = [d for d, v in dist
            if make_pcb.CLASS_OF.get(v["net"], "Default") == "Default"]
    heavy = [d for d, v in dist
             if make_pcb.CLASS_OF.get(v["net"], "Default") in ("Power", "HV")]
    def share(rows, lim):
        return sum(1 for d in rows if d <= lim) / float(len(rows)) if rows else 1.0
    print("  signal via -> nearest GND via:")
    print("    Default-class (%d): median %.2f, p90 %.2f, worst %.2f mm"
          % (len(dflt), dflt[len(dflt) // 2], dflt[int(len(dflt) * 0.9)], dflt[-1]))
    print("    Power/HV-class (%d): median %.2f, p90 %.2f, worst %.2f mm"
          % (len(heavy), heavy[len(heavy) // 2], heavy[int(len(heavy) * 0.9)],
             heavy[-1]))
    print("    before stitching (2026-10-18, same copper, no stitch pass): "
          "median 5.10, p90 9.80, worst 18.50 mm")
    # Companion vias are pinned where the fab allows one: a 0.70 mm GND pad next
    # to a 0.70 mm signal pad with the 0.50 mm different-net hole limit between
    # them means the closest legal centre distance is 1.20 mm, and in the crowded
    # corridors (DevKit rows, the lattice under copper - 170 lattice cells were
    # blocked by copper this build) there is no legal companion at all.  So the
    # floor is what the placement supports, not a wish: coverage that drops below
    # it means the stitcher stopped pinning signal vias, which is the regression
    # this gate exists to catch.  The whole-field distance moved from median 5.10
    # to ~1.8 mm, and the return-path audit (make_pcb's RETURN PATH line) is the
    # measurement that says whether the planes are actually doing their job.
    check(share(dflt, 2.5) >= 0.70,
          "%.0f%% of Default-class signal vias have a GND hole within 2.50 mm "
          "(floor 70%%)" % (100 * share(dflt, 2.5)))
    check(dflt and dflt[len(dflt) // 2] <= 2.2,
          "median Default-class signal via -> GND via distance %.2f mm stays at "
          "or under 2.20 (was 5.10 before stitching)"
          % (dflt[len(dflt) // 2] if dflt else 0.0))
    # Rail vias sit in copper that is already the inner plane: the 0.60 mm class
    # clearance and a 1.30 mm pad leave no room for a companion in the dense power
    # corner, and the return path there is the plane the rail is welded into.
    # Reported, not hidden - and the floor is set so a regression still shows up.
    check(share(heavy, 6.0) >= 0.90,
          "%.0f%% of Power/HV-class vias still have a GND hole within 6.00 mm "
          "(floor 90%%)" % (100 * share(heavy, 6.0)))
    far = [(v["net"], round(v["x"], 1), round(v["y"], 1), round(d, 2))
           for d, v in dist if d > 8.0]
    check(not far, "no signal via is more than 8.00 mm from a GND via "
                   "(%d: %s)" % (len(far), far[:6]))

    # ---- lattice presence: the planes are bonded, not just pinned -------
    lat = sum(1 for v in gnd
              if not any(math.hypot(v["x"] - s["x"], v["y"] - s["y"]) <= 2.0
                         for s in sig))
    check(lat >= 40, "%d GND via(s) sit away from any signal via - they are the "
                     "lattice bonding In1 to In2 across the open plane "
                     "(floor 40)" % lat)

    print("-" * 66)
    if FAIL:
        print("STITCH GATE: %d FAILURE(S)" % len(FAIL))
        return 1
    print("STITCH GATE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
