"""Return-path audit - step 3 of the 4-layer stackup plan (rulebook rule 5).

A signal trace is only as clean as the copper underneath it.  On this board the
reference is a solid plane: F.Cu traces return through In1.Cu (GND), B.Cu traces
return through In2.Cu (GND), and an In2.Cu power rail returns through In1.Cu.
KiCad's DRC never asks that question - it checks that copper does not touch
copper, not that a trace has a plane under it.  So this audit does: sample every
track centreline, and ask whether the reference layer below is covered by our own
filled pour at that exact point.

It reads the geometry KiCad itself filled (the pour polygons from the FILLED
copy, arcs and all, through `SHAPE_POLY_SET.PointInside`), so it is not a second
implementation of DRC - it is KiCad's own copper asked about something KiCad does
not police.  The question it answers needs the fill, so it runs on
hardware/out/drc-check/<name>-filled.kicad_pcb, the same copy DRC judges.

A gap is only a problem when it is not the trace's own hole in the plane: the
pour carves an anti-pad around every pad and via that is not GND, including the
one the trace itself lands in.  Those are exempt, and the exemption is computed
from the same board - it is not a free-form "ignore small gaps" fudge.

Usage (KiCad's own python, which has pcbnew):
    python return_path.py <filled.kicad_pcb> [--json out.json] [--coarse 0.25]
Exit code: 0 always (the numbers are the product); the caller's test decides what
is acceptable, so a failing number cannot be argued away by editing this file.
"""

import json
import math
import sys

import pcbnew

# What a signal's reference layer is, by the stackup (§2 of the plan).  In1.Cu is
# never routed, so a track there would be a policy bug caught elsewhere; In1.Cu as
# a *reference* is what F.Cu looks at.
REF = {pcbnew.F_Cu: pcbnew.In1_Cu, pcbnew.B_Cu: pcbnew.In2_Cu,
       pcbnew.In2_Cu: pcbnew.In1_Cu}
NAME = {pcbnew.F_Cu: "F.Cu", pcbnew.B_Cu: "B.Cu",
        pcbnew.In1_Cu: "In1.Cu", pcbnew.In2_Cu: "In2.Cu"}

SCAN = 0.25          # mm, centreline sample pitch; a gap is reported to this
                     # resolution, which is enough to name the offender and its
                     # coordinates (the point of the audit), and fine enough that
                     # a slot in a plane cannot hide inside one sample step
OWN_PAD_TOL = 0.05   # mm slack on the "this hole belongs to the trace" test


def _mm(v):
    return v / 1e6            # internal units (nm) -> mm


def _iu(x_mm, y_mm):
    return pcbnew.VECTOR2I(int(round(x_mm * 1e6)), int(round(y_mm * 1e6)))


def pour_polys(board):
    """layer id -> list of filled SHAPE_POLY_SETs (the actual plane copper)."""
    out = {}
    for z in board.Zones():
        if z.GetNetname() != "GND":
            continue
        li = z.GetLayer()
        ps = z.GetFilledPolysList(li)
        if ps.OutlineCount():
            out.setdefault(li, []).append(ps)
    return out


def own_holes(board):
    """net code -> [(x, y, radius_mm)] drilled holes and pads that punch an
    anti-pad in the pour: the trace's own hole is not its own return-path fault."""
    holes = {}
    for p in board.GetPads():
        n = p.GetNetCode()
        if n == 0:
            continue
        sz = p.GetSize()
        r = max(sz.x, sz.y) / 2e6
        pos = p.GetPosition()
        holes.setdefault(n, []).append((_mm(pos.x), _mm(pos.y), r))
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            n = t.GetNetCode()
            if n == 0:
                continue
            # A via's copper is per layer (top pad and bottom pad may differ), so
            # KiCad ASSERTS on a bare GetWidth() - it even pops a modal wxWidgets
            # alert on the Windows build (that is the "PCB_VIA::GetWidth called
            # without a layer argument" dialog).  Ask for a face explicitly.
            pos = t.GetStart()
            holes.setdefault(n, []).append((_mm(pos.x), _mm(pos.y),
                                            t.GetWidth(pcbnew.F_Cu) / 2e6))
    return holes


def audit(path, scan=SCAN, json_path=None, verbose=True):
    board = pcbnew.LoadBoard(path)
    polys = pour_polys(board)
    holes = own_holes(board)
    pour_clr = {}
    for li, ps in polys.items():
        # how far the pour stands off foreign copper, straight from the zone
        # itself (`(connect_pads (clearance X))` in the board) - that is what sets
        # the anti-pad radius, so the exemption below is the board's own rule and
        # not a number invented here
        pour_clr[li] = max(z.GetOwnClearance(li, z.GetNetCode()) / 1e6
                           for z in board.Zones()
                           if z.GetLayer() == li and z.GetNetname() == "GND")

    def covered(li, x, y):
        for ps in polys.get(li, ()):
            if ps.PointInside(_iu(x, y)):
                return True
        return False

    def own(net, x, y, li):
        for (hx, hy, r) in holes.get(net, ()):
            if abs(hx - x) <= r + pour_clr[li] + OWN_PAD_TOL and \
                    abs(hy - y) <= r + pour_clr[li] + OWN_PAD_TOL:
                if math.hypot(hx - x, hy - y) <= r + pour_clr[li] + OWN_PAD_TOL:
                    return True
        return False

    ntrk = sum(1 for t in board.GetTracks()
               if not isinstance(t, pcbnew.PCB_VIA))
    per_net, offenders = {}, []
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA) or not isinstance(t, pcbnew.PCB_TRACK):
            continue
        li = t.GetLayer()
        ref = REF.get(li)
        if ref is None or ref not in polys:
            continue
        net, name = t.GetNetCode(), t.GetNetname()
        a, b = t.GetStart(), t.GetEnd()
        ax, ay, bx, by = _mm(a.x), _mm(a.y), _mm(b.x), _mm(b.y)
        seg = math.hypot(bx - ax, by - ay)
        if seg <= 0:
            continue
        d = per_net.setdefault("%s@%s" % (name, NAME[li]),
                               {"net": name, "layer": NAME[li],
                                "ref": NAME[ref], "mm": 0.0,
                                "gap_mm": 0.0, "gaps": 0,
                                "worst": 0.0, "wx": 0.0, "wy": 0.0})
        n = max(1, int(seg / scan))
        step = seg / n
        run0, run_len = None, 0.0
        for k in range(n + 1):
            f = k / n
            x, y = ax + (bx - ax) * f, ay + (by - ay) * f
            ok = covered(ref, x, y) or own(net, x, y, ref)
            d["mm"] += step * (1 if k else 0)     # length accounted per interval
            if not ok:
                if run0 is None:
                    run0, run_len = k, 0.0
                else:
                    run_len += step
            else:
                if run0 is not None:
                    run_len += step
                    # measure the gap properly, and name its middle point
                    i0 = max(0, run0 - 1)
                    i1 = min(n, run0 + int(round(run_len / step)))
                    g0 = ax + (bx - ax) * i0 / n
                    g1 = ax + (bx - ax) * i1 / n
                    gy0 = ay + (by - ay) * i0 / n
                    gy1 = ay + (by - ay) * i1 / n
                    mx, my = (g0 + g1) / 2.0, (gy0 + gy1) / 2.0
                    d["gaps"] += 1
                    d["gap_mm"] += run_len
                    if run_len > d["worst"]:
                        d["worst"], d["wx"], d["wy"] = run_len, mx, my
                    offenders.append({"net": name, "layer": NAME[li],
                                      "ref": NAME[ref], "w": t.GetWidth() / 1e6,
                                      "from": [round(g0, 2), round(gy0, 2)],
                                      "to": [round(g1, 2), round(gy1, 2)],
                                      "mm": round(run_len, 2)})
                    run0, run_len = None, 0.0
        if run0 is not None:
            run_len += step
            i0 = max(0, run0 - 1)
            g0 = ax + (bx - ax) * i0 / n
            gy0 = ay + (by - ay) * i0 / n
            d["gaps"] += 1
            d["gap_mm"] += run_len
            if run_len > d["worst"]:
                d["worst"], d["wx"], d["wy"] = run_len, g0, gy0
            offenders.append({"net": name, "layer": NAME[li], "ref": NAME[ref],
                              "w": t.GetWidth() / 1e6,
                              "from": [round(g0, 2), round(gy0, 2)],
                              "to": [round(bx, 2), round(by, 2)],
                              "mm": round(run_len, 2)})

    tot = sum(d["mm"] for d in per_net.values())
    gap = sum(d["gap_mm"] for d in per_net.values())
    if verbose:
        print("RETURN PATH  %d track(s) on %d net/face row(s), %.1f mm sampled, "
              "%.1f mm (%.1f%%) over no GND on their reference layer"
              % (ntrk, len(per_net), tot, gap,
                 100.0 * gap / tot if tot else 0.0))
        bad = sorted((d for d in per_net.values() if d["gap_mm"] > 0),
                     key=lambda d: -d["gap_mm"])
        for d in bad:
            print("  %-10s %-6s over %-6s  %7.1f mm of %7.1f mm  %d gap(s), "
                  "worst %.2f mm at (%.1f, %.1f)"
                  % (d["net"], d["layer"], d["ref"], d["gap_mm"], d["mm"],
                     d["gaps"], d["worst"], d["wx"], d["wy"]))
        print("  offenders (gap > 0.20 mm):")
        for o in sorted(offenders, key=lambda o: -o["mm"]):
            if o["mm"] > 0.20:
                print("    %-10s %-6s %5.2f mm  (%.2f, %.2f) -> (%.2f, %.2f)"
                      % (o["net"], o["layer"], o["mm"], o["from"][0],
                         o["from"][1], o["to"][0], o["to"][1]))
    if json_path:
        json.dump({"board": path.replace("\\", "/"), "sampled_mm": tot,
                   "gap_mm": gap, "per_net": per_net, "offenders": offenders},
                  open(json_path, "w", encoding="utf-8"), indent=1)
    return per_net, offenders, tot, gap


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    jp = None
    sc = SCAN
    if "--json" in sys.argv:
        jp = sys.argv[sys.argv.index("--json") + 1]
    if "--coarse" in sys.argv:
        sc = float(sys.argv[sys.argv.index("--coarse") + 1])
    audit(args[0], scan=sc, json_path=jp)
