"""Task: a sensed line does not share its layer with someone else's switching.

This is A3 / D4 of Implementation plans/SENSOR-SILENCE-ADC-ACCURACY-PLAN-2026-
10-18.md, promoted out of scratch/crosstalk.py.  For every routed item (segment,
and via pad modelled as a disc) of a sensed net - make_pcb.QUIET, the same set
the router is handed as cfg["quiet"] - it measures, against every FOREIGN routed
item on a shared layer:

  * the closest copper-edge approach, which must stay at or over
    route_pcb.QUIET_EDGE (0.5 mm): the class clearance alone is legal and still
    couples;
  * the length of parallel run inside 1.0 mm, which must stay at or under
    PAR_MAX (2.0 mm), because two parallel runs exchange charge in proportion to
    how long they lie side by side.

Baseline - the same copper before the quiet band existed (board as committed at
00fc7175, measured 2026-10-18): BAT_SNS ran 20 mm parallel to DBG_RX at 0.55 mm
and 9 mm parallel to TFT_DC at 0.45 mm; FUEL_SNS 6 mm parallel to EN at 0.35 mm;
COOL_SNS sat 0.35 mm from the 1 kHz backlight PWM TFT_BL; LDR_SNS came within
0.25 mm of DBG_RX across layers, which this gate deliberately does not count.
§1c of the plan carries the full table.  Those parallel runs are the injection
path into nodes of 10 kOhm.

Geometry note: this measures the written board; it is not a clone of KiCad's
checker.  Distances come from sampled point-segment maths (the same sampling
rule scratch/crosstalk.py used, so the plan's numbers stay comparable), and a
via counts as a fat point on the outer layers the generator emits it on.  The
DRC stays the electrical referee; this gate polices the one rule DRC cannot see,
namely that 0.25 mm is legal and still too close.

What the verdict means, measured 2026-10-18: the gate is a RATCHET on this count
(OFFENDER_MAX in main()), not a claim that the spacing target is met.  33 pairs
broke the band before any of A2/A3 existed, and neither enforcement attempt got
the number down - wall: three nets in pieces and 100+ pairs; priced band: 32 pairs
and more parallel run (route_pcb.QUIET_EDGE carries both).  The corridors out of
J10a/J10b are one lane wide, so on this placement spacing is not the mechanism
that buys silence; guarding and filtering (§6 A4, §4 C7) are.  Re-measured
2026-10-19: re-routing with the sensed nets first (A5, out/a5-e1.log and
out/a5-e2.log) either cut nets or multiplied the hug count, so the routing is
unchanged and the ceiling was lowered to the measured 33.  The A4 guards
(guard_pcb.py) are GND and exempt here - PASS still never means the sensors are
quiet.

Run:  "<KiCad python>" hardware/scripts/test_crosstalk.py [board-file]
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pcb                                            # noqa: E402
import route_pcb                                           # noqa: E402

FAIL = []
HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, os.pardir, "dashboard-plusplus-carrier.kicad_pcb")
SENS = set(make_pcb.QUIET)   # the high-impedance measured nodes; see make_pcb
                             # QUIET for why V3V3 (a low-impedance reference) and
                             # HALL (a glitch-filtered digital speed line) are not
                             # in the band, and the same set the router prices on
PAR_BAND = 1.0      # mm, close enough to exchange charge
PAR_MAX = 2.0       # mm of parallel run tolerated inside that band
SKIP = {"GND"}      # ground copper is the reference, not an aggressor
SEG_RE = re.compile(r'\s*\(segment \(net (\d+)\) \(start ([\d.]+) ([\d.]+)\) '
                    r'\(end ([\d.]+) ([\d.]+)\) \(width ([\d.]+)\) '
                    r'\(layer "([^"]+)"\)')
VIA_RE = re.compile(r'\s*\(via \(net (\d+)\) \(at ([\d.]+) ([\d.]+)\) '
                    r'\(size ([\d.]+)\)')


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        FAIL.append(msg)


def pt_seg(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    if L2 == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def seg_seg(a, b):
    """Rough min distance: point-seg from all four ends, plus samples along."""
    d = min(pt_seg(a[0], a[1], b[0], b[1], b[2], b[3]),
            pt_seg(a[2], a[3], b[0], b[1], b[2], b[3]),
            pt_seg(b[0], b[1], a[0], a[1], a[2], a[3]),
            pt_seg(b[2], b[3], a[0], a[1], a[2], a[3]))
    for f in (0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9):
        d = min(d, pt_seg(a[0] + (a[2] - a[0]) * f, a[1] + (a[3] - a[1]) * f,
                          b[0], b[1], b[2], b[3]))
        d = min(d, pt_seg(b[0] + (b[2] - b[0]) * f, b[1] + (b[3] - b[1]) * f,
                          a[0], a[1], a[2], a[3]))
    return d


def angle_between(a, b):
    a1 = math.atan2(a[3] - a[1], a[2] - a[0])
    b1 = math.atan2(b[3] - b[1], b[2] - b[0])
    d = abs((a1 - b1 + math.pi) % (2 * math.pi) - math.pi)
    return min(d, math.pi - d)


def read_copper(path, page_dx, page_dy, nets):
    """Routed copper per net as (x0, y0, x1, y1, half_width, layer).

    The board file is written in the drawing-sheet frame, so every coordinate is
    shifted back into the design frame the generators use (see make_pcb's note
    on PAGE_DX / PAGE_DY).
    """
    out = {}
    for ln in open(path, encoding="utf-8"):
        m = SEG_RE.match(ln)
        if m:
            n = nets.get(int(m.group(1)), "?")
            out.setdefault(n, []).append(
                (float(m.group(2)) - page_dx, float(m.group(3)) - page_dy,
                 float(m.group(4)) - page_dx, float(m.group(5)) - page_dy,
                 float(m.group(6)) / 2.0, m.group(7)))
            continue
        m = VIA_RE.match(ln)
        if m:
            n = nets.get(int(m.group(1)), "?")
            x = float(m.group(2)) - page_dx
            y = float(m.group(3)) - page_dy
            r = float(m.group(4)) / 2.0
            # a via pad is a disc: a degenerate segment.  fmt_copper emits every
            # via on F.Cu and B.Cu, so those are the faces its pad couples on.
            for lay in ("F.Cu", "B.Cu"):
                out.setdefault(n, []).append((x, y, x, y, r, lay))
    return out


def main(argv):
    path = argv[1] if len(argv) > 1 else BOARD
    txt = open(path, encoding="utf-8").read()
    nets = dict((int(n), nm)
                for n, nm in re.findall(r'\(net (\d+) "([^"]+)"\)', txt))
    segs = read_copper(path, make_pcb.PAGE_DX, make_pcb.PAGE_DY, nets)
    print("crosstalk gate on %s" % os.path.basename(path))
    print("  sensed set (%d): %s" % (len(SENS), ", ".join(sorted(SENS))))
    print("-" * 66)

    offenders, below = [], []
    for sn in sorted(SENS):
        A = segs.get(sn, [])
        if not A:
            print("  %-10s (no routed copper)" % sn)
            continue
        per = {}
        for other, B in segs.items():
            if other in SKIP or other in SENS or other == sn:
                continue
            dmin, par = 9e9, 0.0
            for a in A:
                for b in B:
                    if a[5] != b[5]:
                        continue          # the rule is same-layer; a cross-layer
                    d = seg_seg(a[:4], b[:4]) - (a[4] + b[4])
                    if d < dmin:
                        dmin = d
                    if d <= PAR_BAND and \
                            angle_between(a[:4], b[:4]) < math.radians(25.0):
                        par += min(math.hypot(a[2] - a[0], a[3] - a[1]),
                                   math.hypot(b[2] - b[0], b[3] - b[1]))
            if dmin < 9e8:
                per[other] = (dmin, par)
                if dmin < route_pcb.QUIET_EDGE - 1e-9 or par > PAR_MAX + 1e-9:
                    offenders.append((sn, other, dmin, par))
                if dmin < make_pcb.net_clr(other) - 1e-9:
                    below.append((sn, other, round(dmin, 3)))
        top = sorted(per.items(), key=lambda kv: kv[1][0])[:3]
        print("  %-10s len %6.1f mm   %s"
              % (sn, sum(math.hypot(a[2] - a[0], a[3] - a[1]) for a in A),
                 "; ".join("%s %.2fmm par%.0f" % (k, v[0], v[1]) for k, v in top)))
    print("-" * 66)
    print("  rule: sensed routed copper keeps %.2f mm edge clearance from foreign "
          "routed copper on a shared layer, and no more than %.1f mm of parallel "
          "run inside %.1f mm" % (route_pcb.QUIET_EDGE, PAR_MAX, PAR_BAND))
    print("  before the quiet band (00fc7175): BAT_SNS~DBG_RX 0.55 mm over 20 mm "
          "parallel, FUEL_SNS~EN 0.35 mm over 6 mm, COOL_SNS~TFT_BL 0.35 mm")
    # OFFENDER_MAX is a ratchet, not a target.  33 pair(s) hugged before A2/A3
    # existed (board at 00fc7175); routing with the band priced into the A* left
    # 32 and longer parallel runs; routing with the band as a wall left
    # TFT_DC/TFT_RST/TFT_SCK in pieces and 100+ pairs still hugging
    # (route_pcb.QUIET_EDGE holds both measurements).  The reason is the layout:
    # the corridors out of J10a/J10b are one lane wide.  On 2026-10-19 the A5
    # experiments re-ordered the route (sensed nets first) and measured it
    # instead of assuming: out/a5-e1.log got BAT_SNS from 180.4 to 91.3 mm but
    # left GND and DBG_RX cut and took the route log's quiet count from 40 to
    # 277 item pairs; out/a5-e2.log both missed the length target and took it to
    # 430.  So the shipped routing is unchanged and the ceiling is pinned at the
    # measured 33 - the guards (guard_pcb.py, §6 A4) run beside 15 % of the
    # sensed runs where a legal, grounded slot exists and are GND, i.e. exempt
    # here, so they cannot move this number.  It fails if it grows, and is meant
    # to be lowered when the layout moves - the only place the remaining pairs
    # can be bought back (the sensed runs need shorter, straighter corridors).
    OFFENDER_MAX = 33
    check(len(offenders) <= OFFENDER_MAX,
          "%d sensed/foreign pair(s) inside the %.2f mm band (ceiling %d = the "
          "measured count; 33 at 00fc7175). This is NOT the plan's target met - "
          "the spacing rule holds only where the corridors leave room, and the "
          "2026-10-19 A5 re-route experiments made it worse, not better: %s"
          % (len(offenders), route_pcb.QUIET_EDGE, OFFENDER_MAX,
             ["%s~%s %.2fmm par%.0f" % o for o in
              sorted(offenders, key=lambda o: o[2])[:8]]))
    # Anything below the class clearance is also a DRC violation, and make_pcb.py
    # stops the pipeline on the filled DRC - so a hit here means the pipeline gate
    # was bypassed, not that crosstalk regressed.
    check(not below, "no sensed pair is closer than the foreign net's class "
                     "clearance (%d): %s" % (len(below), below[:6]))
    print("-" * 66)
    if FAIL:
        print("CROSSTALK GATE: %d FAILURE(S)" % len(FAIL))
        return 1
    print("CROSSTALK GATE: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
