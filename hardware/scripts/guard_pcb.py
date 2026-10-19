"""A4 grounded guards: run GND beside the sensed nets where the board allows.

    python scripts/guard_pcb.py [board-file]      # audit/report only, writes nothing

Why this exists (2026-10-19): the crosstalk gate (scripts/test_crosstalk.py)
counts 33 sensed/foreign net pairs hugging inside the 0.5 mm band on the
shipped routing, and re-routing cannot buy the spacing back - the A5
experiments (out/a5-e1.log, out/a5-e2.log) either left nets CUT or took the
band count from 40 item-pairs to 277/430.  The corridors out of J10a/J10b are
one lane wide.  So the plan's other mechanism is applied instead: put grounded
copper between a sensed line and whatever crosses it.

The shape (plan A4): a 0.20 mm GND track running beside the sensed run, with
its centre line OFFSET + half the sensed track width (0.55 + 0.125 = 0.675 mm)
from the sensed centre line - the closest a 0.20 mm GND track may sit while
keeping the 0.25 mm Default-class gap.  The probe
(hardware/scratch/guardprobe.py, same rule) measured the room on the shipped
board: one side is free for ~12 % of the BAT_SNS run length, ~16 % for FUEL_SNS,
up to ~44 % for HALL_OUT; 0 % at 0.30 mm, which is why the offset is fixed.

What is emitted, and what is refused:

* a stretch is emitted only where a 0.20 mm disc sampled every 0.25 mm along
  the guard line is legal ON THE SENSED LAYER (stitch_pcb.Placer + 0.03 mm
  slack).  The probe checks all four layers; that over-states the blockage for
  a track - a F.Cu guard over B.Cu copper is legal, and KiCad's DRC agrees;
* every piece is BONDED to the GND net at both ends, and a piece whose ends
  cannot be bonded is NOT emitted - floating copper next to a high-impedance
  ADC line would be an antenna, not a guard.  Ends are bonded the cheap way
  first: a straight 0.20 mm spur to the nearest existing GND copper on the
  sensed layer (track or via pad, <= 2.5 mm away; GND is the dominant copper in
  these corridors, and the measured 2026-10-19 run showed the one-lane slots
  cannot host a 0.70 mm via anywhere along the sensed runs).  Only when no GND
  copper is reachable does it place a GND via (0.70/0.35) outward from the
  sensed line, on a spot legal on all four layers.  Mid ties every <= 5 mm are
  best-effort;
* pieces shorter than 2.0 mm are dropped - a stub is noise, not a fence;
* the sensed run is never touched (same routing as make_pcb.py), and no
  clearance rule is relaxed for the guard either.

This module is geometry only: GND tracks + GND vias are appended to the routed
copper before the board is written.  scripts/test_crosstalk.py does not count
GND as an aggressor, so guards cannot move the ratchet; KiCad's DRC on the
filled board stays the referee (AGENTS rule 30).
"""
import math
import re
import sys

import stitch_pcb

GUARD_W = 0.20           # mm, the fab minimum; plan A4's "grounded guard"
OFFSET = 0.55            # mm from the sensed EDGE to the guard CENTRE minus
                         # half the guard width; guard centre = OFFSET + w/2
STEP = 0.25              # mm sampling of the guard line and the spurs
MIN_RUN = 2.00           # mm, shorter legal stretches are not worth a fence
VIA_EVERY = 5.00         # mm, best-effort stitch through the middle of a piece
STITCH_INSET = 0.05      # mm, stitch points sit just inside the piece ends
VIA_R = (0.90, 1.05, 1.20, 1.40, 1.65, 1.95, 2.30, 2.70, 3.10)   # centre
                         # distance from the sensed centre line, tried in this
                         # order: >= 0.85 keeps the 0.70 mm pad 0.25 mm off the
                         # sensed track even at its nearest point
                         # (0.125 + 0.25 + 0.35)
VIA_ANG = (0.0, 20.0, -20.0, 40.0, -40.0, 60.0, -60.0)   # degrees around the
                         # outward normal: in a one-lane corridor the copper is
                         # rarely free exactly perpendicular
SAFE = stitch_pcb.SAFE
VIA_DIA = stitch_pcb.VIA_DIA
VIA_DRILL = stitch_pcb.VIA_DRILL
EPS = 1e-9


def _pt(a, u, nrm, t, side, d):
    """Point `t` mm along the run, `d` mm to `side` of it (design frame)."""
    return (a[0] + u[0] * t + nrm[0] * side * d,
            a[1] + u[1] * t + nrm[1] * side * d)


def _spur_clear(pl, layer, g, v):
    """A 0.20 mm spur from the guard point g to the via centre v is legal."""
    n = max(1, int(math.hypot(v[0] - g[0], v[1] - g[1]) / STEP))
    for i in range(n + 1):
        f = i / float(n)
        x = g[0] + (v[0] - g[0]) * f
        y = g[1] + (v[1] - g[1]) * f
        if not pl.legal(x, y, GUARD_W, "GND", 0.0, safe=SAFE, layers=(layer,)):
            return False
    return True


def _gnd_bond(pl, layer, cands, g, reach=2.50):
    """Nearest existing GND copper point (same layer) reachable from g.

    `cands` are the REAL GND shapes on the layer ("seg" with a/b/w, "via" with
    x/y/dia) - not obstacle boxes.  A box of a diagonal track contains corners
    that are not copper: the 2026-10-19 DRC pre-check on the box model put two
    spur ends in such corners and got `track_dangling` twice.  So the contact
    point is the closest point on a track's CENTRELINE (always inside the
    stroke) or a via's centre.  Merely overlapping the stroke is NOT enough for
    KiCad - the second pre-check left two ends 0.05..0.10 mm inside a GND track
    with no spur and got `track_dangling` again, so a spur is always drawn
    unless the two rounded points are identical (the end truly sits on the
    centreline, a clean T-joint).

    Returns (point, spur length, is_spur) or None.
    """
    best = []
    for kind, o in cands:
        if kind == "seg":
            (ax, ay), (bx, by) = o["a"], o["b"]
            l2 = (bx - ax) ** 2 + (by - ay) ** 2
            if l2 <= 0.0:
                px, py = ax, ay
            else:
                t = ((g[0] - ax) * (bx - ax) + (g[1] - ay) * (by - ay)) / l2
                t = min(1.0, max(0.0, t))
                px, py = ax + (bx - ax) * t, ay + (by - ay) * t
        else:
            px, py = o["x"], o["y"]
        d = math.hypot(px - g[0], py - g[1])
        if d <= reach:
            best.append((d, px, py))
    best.sort(key=lambda c: c[0])
    gr = (round(g[0], 3), round(g[1], 3))
    for d, px, py in best[:8]:
        p = (round(px, 3), round(py, 3))
        if p == gr:
            return p, 0.0, False
        if _spur_clear(pl, layer, g, p):
            return p, d, True
    return None


def _find_via(pl, layer, a, u, nrm, t, side, d, avoid=()):
    """First legal GND via centre outward from the sensed line at `t`.

    `avoid` holds holes chosen but not yet handed to pl.add() (both ends of a
    piece are resolved before either is emitted): the pre-check board got two
    `hole_to_hole ... 0.0000 mm` pairs because two ends could pick one spot.
    """
    base = (a[0] + u[0] * t, a[1] + u[1] * t)
    ox, oy = nrm[0] * side, nrm[1] * side
    for r in VIA_R:
        if r <= d + 0.10:
            continue
        for ang in VIA_ANG:
            c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
            v = (base[0] + (ox * c - oy * s) * r,
                 base[1] + (ox * s + oy * c) * r)
            if any(math.hypot(v[0] - px, v[1] - py) < stitch_pcb.MIN_SEP
                   for px, py in avoid):
                continue
            if pl.legal(*v) and pl._free_of_stitch(*v) and \
                    _spur_clear(pl, layer, _pt(a, u, nrm, t, side, d), v):
                return (round(v[0], 3), round(v[1], 3))
    return None


def _tie(pl, layer, gnd, a, u, nrm, t, side, d, avoid=()):
    """Bond the guard point at `t` to GND: existing copper first, via second."""
    g = _pt(a, u, nrm, t, side, d)
    b = _gnd_bond(pl, layer, gnd, g)
    if b:
        p, _d, is_spur = b
        return ("bond", p, is_spur)
    v = _find_via(pl, layer, a, u, nrm, t, side, d, avoid)
    if v:
        return ("via", v, True)
    return None


def _emit_tie(pl, out_s, out_v, layer, g, tie):
    kind, p, is_spur = tie
    gp = (round(g[0], 3), round(g[1], 3))
    if kind == "via":
        out_s.append({"net": "GND", "layer": layer, "a": gp, "b": p,
                      "w": GUARD_W})
        out_v.append(pl.add(*p))
    elif is_spur:
        out_s.append({"net": "GND", "layer": layer, "a": gp, "b": p,
                      "w": GUARD_W})


def _segment(pl, s, gnd, out_s, out_v):
    """Guard one routed segment on both sides.

    Returns (covered mm, pieces emitted, stretches refused for lack of a
    stitch).  Covered length is the union of the emitted stretches over the
    two sides, so a stretch guarded on both sides still counts once.
    """
    (ax, ay), (bx, by) = s["a"], s["b"]
    L = math.hypot(bx - ax, by - ay)
    if L < MIN_RUN:
        return 0.0, 0, 0
    u = ((bx - ax) / L, (by - ay) / L)
    nrm = (-u[1], u[0])
    d = OFFSET + s["w"] / 2.0
    lay = s["layer"]
    n = max(1, int(L / STEP))
    step_t = L / n
    covered, pieces, blocked = [], 0, 0
    for side in (1, -1):
        flags = []
        for i in range(n + 1):
            p = _pt((ax, ay), u, nrm, i * step_t, side, d)
            flags.append(pl.legal(p[0], p[1], GUARD_W, "GND", 0.0,
                                  safe=SAFE, layers=(lay,)))
        i = 0
        while i <= n:
            if not flags[i]:
                i += 1
                continue
            j = i
            while j + 1 <= n and flags[j + 1]:
                j += 1
            t0, t1 = i * step_t, j * step_t
            i = j + 1
            if t1 - t0 < MIN_RUN - EPS:
                continue
            t_lo, t_hi = t0 + STITCH_INSET, t1 - STITCH_INSET
            if t_hi - t_lo < 0.10:
                blocked += 1
                continue
            tie0 = _tie(pl, lay, gnd, (ax, ay), u, nrm, t_lo, side, d)
            avoid = []
            if tie0 and tie0[0] == "via":
                avoid.append(tie0[1])
            tie1 = _tie(pl, lay, gnd, (ax, ay), u, nrm, t_hi, side, d, avoid)
            if not (tie0 and tie1):
                blocked += 1          # floating copper is not a guard
                continue
            g0 = _pt((ax, ay), u, nrm, t_lo, side, d)
            g1 = _pt((ax, ay), u, nrm, t_hi, side, d)
            out_s.append({"net": "GND", "layer": lay,
                          "a": (round(g0[0], 3), round(g0[1], 3)),
                          "b": (round(g1[0], 3), round(g1[1], 3)),
                          "w": GUARD_W})
            _emit_tie(pl, out_s, out_v, lay, g0, tie0)
            _emit_tie(pl, out_s, out_v, lay, g1, tie1)
            # best-effort mid ties: a long piece must not rely on its two ends
            # alone for the return current, but a blocked mid point is not a
            # reason to drop a piece whose ends are bonded
            t = t_lo + VIA_EVERY
            while t < t_hi - 0.10:
                tie = _tie(pl, lay, gnd, (ax, ay), u, nrm, t, side, d)
                if tie:
                    _emit_tie(pl, out_s, out_v, lay,
                              _pt((ax, ay), u, nrm, t, side, d), tie)
                t += VIA_EVERY
            covered.append((t0, t1))
            pieces += 1
    covered.sort()
    cov, c0, c1 = 0.0, None, None
    for a, b in covered:
        if c0 is None:
            c0, c1 = a, b
        elif a <= c1 + EPS:
            c1 = max(c1, b)
        else:
            cov += c1 - c0
            c0, c1 = a, b
    if c0 is not None:
        cov += c1 - c0
    return cov, pieces, blocked


def place(frame, keep, pads, segs, vias, clr, quiet):
    """Return (new GND segments, new GND vias, report) for the `quiet` nets.

    `vias` must already contain every stitch hole the board will have - the
    guard placer snapshots it as obstacles and hole partners.
    """
    pl = stitch_pcb.Placer(frame, keep, pads, segs, vias, clr)
    gnd = {}
    for s in segs:
        if s["net"] == "GND" and s["w"] > 0:
            gnd.setdefault(s["layer"], []).append(("seg", s))
    for v in vias:
        if v["net"] == "GND":
            for lay in ("F.Cu", "B.Cu"):
                gnd.setdefault(lay, []).append(("via", v))
    out_s, out_v, nets = [], [], {}
    for net in sorted(quiet):
        mine = [s for s in segs if s["net"] == net and s["w"] > 0]
        ln = sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
                 for s in mine)
        cov = 0.0
        pcs = blk = 0
        for s in mine:
            c, p, b = _segment(pl, s, gnd.get(s["layer"], ()), out_s, out_v)
            cov += c
            pcs += p
            blk += b
        nets[net] = {"len": ln, "cov": cov, "pieces": pcs, "blocked": blk}
    rep = {"nets": nets,
           "pieces": sum(r["pieces"] for r in nets.values()),
           "blocked": sum(r["blocked"] for r in nets.values()),
           "guard_mm": rep_guard_mm(out_s, out_v),
           "spur_mm": rep_spur_mm(out_s, out_v),
           "vias": len(out_v)}
    return out_s, out_v, rep


def _is_spur(s, vias):
    return any(abs(s["b"][0] - v["x"]) < EPS and abs(s["b"][1] - v["y"]) < EPS
               for v in vias)


def rep_guard_mm(segs, vias):
    return sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
               for s in segs if not _is_spur(s, vias))


def rep_spur_mm(segs, vias):
    return sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
               for s in segs if _is_spur(s, vias))


# ------------------------------------------------------- audit mode (file) ----
def main(argv):
    sys.path.insert(0, __file__.rsplit("/", 1)[0].replace("\\", "/"))
    import make_pcb as mp
    here = __file__.replace("\\", "/").rsplit("/", 1)[0]
    path = (argv[1] if len(argv) > 1 else
            here + "/../dashboard-plusplus-carrier.kicad_pcb").replace("\\", "/")
    nets = dict((int(n), nm) for n, nm in
                re.findall(r'\(net (\d+) "([^"]+)"\)',
                           open(path, encoding="utf-8").read()))
    segs, vias = stitch_pcb.read_board(path, mp.PAGE_DX, mp.PAGE_DY, nets)
    pads = mp.route_pads()
    frame = (mp.BOARD_X0, mp.BOARD_Y0, mp.BOARD_X1, mp.BOARD_Y1)
    gs, gv, rep = place(frame, mp.KEEP, pads, segs, vias, mp.net_clr, mp.QUIET)
    print("guard audit on %s" % path)
    print("  sensed runs, 0.20 mm GND at edge %.2f mm (layer-aware, +%.2f slack):"
          % (OFFSET, SAFE))
    for net in sorted(rep["nets"]):
        r = rep["nets"][net]
        pct = 100.0 * r["cov"] / r["len"] if r["len"] else 0.0
        print("    %-10s %7.1f of %7.1f mm  %3.0f%%  (%d piece(s), "
              "%d stretch(es) refused: no ground tie)"
              % (net, r["cov"], r["len"], pct, r["pieces"], r["blocked"]))
    tot = sum(r["len"] for r in rep["nets"].values())
    cov = sum(r["cov"] for r in rep["nets"].values())
    print("  total: %.1f of %.1f mm guarded (%.0f%%); %d piece(s), "
          "%.1f mm guard + %.1f mm spur, %d stitch via(s)"
          % (cov, tot, 100.0 * cov / tot if tot else 0.0, rep["pieces"],
             rep["guard_mm"], rep["spur_mm"], rep["vias"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
