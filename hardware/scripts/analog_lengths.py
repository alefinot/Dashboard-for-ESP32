"""Sensed-net length audit: routed tree vs the MST floor, off live artifacts.

The sensor-silence plan (A5) claims the sensed nets sit X mm over the bare
minimum their pad placement allows (the MST of the pad centres).  This probe
re-measures that claim from things that cannot go stale:

  * pad boxes come from make_pcb.route_pads() - the same pad model the router
    wires, so the MST here is the same floor the router is judged against;
  * routed lengths are summed from the track segments of the .kicad_pcb
    itself, not from a route log.

  python analog_lengths.py [board]

Prints one row per net: pads, MST mm, routed mm, ratio, excess mm - the six A5
sensed nets first, then the surrounding reference nets.  Exit code 0 always:
this is a probe (like freespace.py), not a gate.  The gates that must stay
green are scripts/test_crosstalk.py and KiCad's DRC on the routed board.
"""
import math
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pcb                                            # noqa: E402

SENSED = ("BAT_SNS", "FUEL_SNS", "COOL_SNS", "LDR_SNS", "HALL_SIG",
          "IGN_SENSE")
CONTEXT = ("BAT_DIV", "HALL_OUT", "HALL_VCC", "V3V3")

BOARD = sys.argv[1] if len(sys.argv) > 1 else make_pcb.OUT


def routed_lengths(path):
    """Per-net routed track length (mm), summed from the board's segments."""
    txt = open(path, encoding="utf-8").read()
    names = {int(n): name for n, name in re.findall(r'\(net (\d+) "([^"]+)"\)',
                                                    txt)}
    out = defaultdict(float)
    pat = re.compile(r'\(segment \(net (\d+)\) \(start ([-\d.eE+]+) '
                     r'([-\d.eE+]+)\) \(end ([-\d.eE+]+) ([-\d.eE+]+)\) '
                     r'\(width [\d.]+\) \(layer "[^"]+"\)')
    for m in pat.finditer(txt):
        x0, y0, x1, y1 = (float(m.group(k)) for k in (2, 3, 4, 5))
        out[names[int(m.group(1))]] += math.hypot(x1 - x0, y1 - y0)
    return out


def mst_len(points):
    """Minimum spanning tree over pad centres (Prim), the wiring floor."""
    if len(points) < 2:
        return 0.0
    seen, rest, total = [points[0]], list(points[1:]), 0.0
    while rest:
        d, k = min((math.dist(a, b), i)
                   for a in seen for i, b in enumerate(rest))
        total += d
        seen.append(rest.pop(k))
    return total


def main():
    pads = make_pcb.route_pads()
    bynet = defaultdict(list)
    for p in pads:
        if p["net"]:
            bynet[p["net"]].append(p)
    routed = routed_lengths(BOARD)

    print("board: %s" % os.path.relpath(BOARD))
    print("%-1s %-10s %5s %9s %9s %7s %9s  %s"
          % ("*", "net", "pads", "MST mm", "routed", "ratio", "excess",
             "members"))
    sums = {"mst": 0.0, "routed": 0.0}
    for mark, net in ([("*", n) for n in SENSED] +
                      [(" ", n) for n in CONTEXT]):
        ps = bynet.get(net, [])
        pts = [(0.5 * (p["box"][0] + p["box"][2]),
                0.5 * (p["box"][1] + p["box"][3])) for p in ps]
        if len(pts) < 2:
            continue
        mst = mst_len(pts)
        r = routed.get(net)
        if mark == "*":
            sums["mst"] += mst
            sums["routed"] += r or 0.0
        members = ",".join(sorted({p["ref"] for p in ps}))[:58]
        print("%-1s %-10s %5d %9.1f %9s %7s %9s  %s"
              % (mark, net, len(ps), mst,
                 "%.1f" % r if r else "-",
                 "%.2f" % (r / mst) if r else "-",
                 "%.1f" % (r - mst) if r else "-", members))
    print("  %-10s       %9.1f %9.1f %7s %9s  six sensed nets"
          % ("TOTAL", sums["mst"], sums["routed"],
             "%.2f" % (sums["routed"] / sums["mst"]) if sums["mst"] else "-",
             "%.1f" % (sums["routed"] - sums["mst"]) if sums["mst"] else "-"))


if __name__ == "__main__":
    main()
