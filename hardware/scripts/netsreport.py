"""Net connectivity report for the carrier board - a review sheet, no wiring.

  python netsreport.py [board] [out.md]

Reads the .kicad_pcb, works out the real (world) position of every pad, and
writes a markdown report: net summary sorted by wiring span, per-net member
lists, per-part pin tables, flags (nets with no pads, pads left unbound, nets
that stretch across the board) and a placement table.
"""
import math
import os
import re
import sys
import time
from collections import defaultdict

import kicadfmt as kf
import make_pcb as mp

BOARD = sys.argv[1] if len(sys.argv) > 1 else mp.OUT
OUTMD = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(mp.OUT)), "nets-report.md")


# ---------------------------------------------------------------- board parse
def parse(path):
    txt = open(path, encoding="utf-8").read()
    netnames = {int(n): name for n, name in
                re.findall(r'\(net (\d+) "([^"]*)"\)', txt)}
    pads = []            # dict rows
    parts = []           # dict rows, one per footprint
    i = 0
    while True:
        m = re.compile(r'\(footprint "([^"]+)"').search(txt, i)
        if not m:
            break
        j = kf._skip_sexp(txt, m.start())
        blk = txt[m.start():j]
        i = j
        fx, fy, frot = [float(v) for v in re.search(
            r'\t\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', blk).groups()]
        layer = re.search(r'\t\(layer "([^"]+)"\)', blk).group(1)
        ref = re.search(r'\(property "Reference" "([^"]*)"', blk)
        val = re.search(r'\(property "Value" "([^"]*)"', blk)
        rot = math.radians(-frot)     # KiCad angles are CW in a Y-down frame

        def place(px, py):
            return (fx + px * math.cos(rot) - py * math.sin(rot),
                    fy + px * math.sin(rot) + py * math.cos(rot))

        row = {"ref": ref.group(1) if ref else "?",
               "value": val.group(1) if val else "",
               "fp": m.group(1), "x": fx, "y": fy, "rot": frot,
               "layer": layer, "pads": []}
        for g in kf._children(blk[blk.index("\n"):len(blk) - 1]):
            if not g.lstrip().startswith("(pad"):
                continue
            num = re.search(r'\(pad "([^"]*)"', g).group(1)
            px, py = [float(v) for v in re.search(
                r'\(at ([-\d.]+) ([-\d.]+)(?: [-\d.]+)?\)', g).groups()[0:2]]
            x, y = place(px, py)
            nm = re.search(r'\(net (\d+) "([^"]*)"\)', g)
            size = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', g)
            pr = {"pad": num, "x": x, "y": y,
                  "net": nm.group(2) if nm else "",
                  "netnum": int(nm.group(1)) if nm else 0,
                  "w": float(size.group(1)) if size else 0.0,
                  "h": float(size.group(2)) if size else 0.0,
                  "shape": g.split()[1] + " " + g.split()[2]}
            row["pads"].append(pr)
            pads.append(pr | {"ref": row["ref"], "layer": layer})
        parts.append(row)
    return netnames, parts, pads


# ------------------------------------------------------------------- geometry
def mst_len(points):
    """Minimum spanning tree length (Prim) - proxy for the wiring work."""
    if len(points) < 2:
        return 0.0
    pts = list(points)
    seen = [0]
    rest = list(range(1, len(pts)))
    total = 0.0
    while rest:
        best = min(((math.dist(pts[a], pts[b]), b) for a in seen for b in rest))
        total += best[0]
        seen.append(best[1])
        rest.remove(best[1])
    return total


def span(points):
    if len(points) < 2:
        return 0.0
    return max(math.dist(a, b) for i, a in enumerate(points)
               for b in points[i + 1:])


# --------------------------------------------------------------------- report
def main():
    netnames, parts, pads = parse(BOARD)
    bynet = defaultdict(list)
    for p in pads:
        bynet[p["net"]].append(p)

    lines = []
    W = lines.append
    W("# Dashboard++ carrier - net connectivity report")
    W("")
    W("Board: `%s`  |  generated %s  |  **no copper routed yet** (0 tracks, "
      "0 vias)" % (os.path.basename(BOARD), time.strftime("%Y-%m-%d %H:%M")))
    W("")
    W("Board outline 10.0-95.0 x 10.0-65.0 mm (85 x 55 mm on the 100 x 65 mm "
      "carrier plate). %d parts, %d pads. Coordinates below are the real pad "
      "positions on the board (footprint placement + rotation applied), X right, "
      "Y down, origin at the outline's bottom-left corner +10 mm margin." %
      (len(parts), len(pads)))
    W("")
    W("`span` = straight-line distance between the two farthest pads of the "
      "net; `MST` = minimum spanning tree over all its pads, i.e. the bare "
      "minimum of wiring the net needs if parts could overlap. Both are "
      "placement quality indicators: a big MST means the parts of that net are "
      "scattered.")
    W("")

    # ---- 1. summary table
    W("## 1. Net summary (longest wiring first)")
    W("")
    W("| # | net | pads | parts | span mm | MST mm | notes |")
    W("|---|-----|------|-------|---------|--------|-------|")
    rows = []
    for name, members in bynet.items():
        pts = [(m["x"], m["y"]) for m in members]
        refs = sorted({m["ref"] for m in members}, key=lambda r: (r[0], len(r), r))
        rows.append((mst_len(pts), span(pts), name, members, refs))
    netnum = {v: k for k, v in netnames.items()}
    for mst, sp, name, members, refs in sorted(rows, reverse=True):
        note = ""
        if not name:
            note = "*unbound pads*"
        elif len(members) == 1:
            note = "**single pad - check**"
        elif mst > 60:
            note = "scattered"
        W("| %d | %s | %d | %d | %.1f | %.1f | %s |"
          % (netnum.get(name, 0), name or "*(no net)*", len(members), len(refs),
             sp, mst, note))
    W("")
    W("Totals: %d pads - %d bound to one of the %d nets in use, %d left as "
      "no-connect (see section 4)." % (len(pads),
                                       sum(1 for p in pads if p["net"]),
                                       len({p["net"] for p in pads if p["net"]}),
                                       sum(1 for p in pads if not p["net"])))
    W("")

    # ---- 2. per-net detail
    W("## 2. Nets, member by member")
    W("")
    for mst, sp, name, members, refs in sorted(rows, reverse=True):
        W("### %s - %d pads over %d parts (MST %.1f mm)"
          % (name or "**no net assigned**", len(members), len(refs), mst))
        W("")
        W("| part | pad | x mm | y mm | side | pad size mm |")
        W("|------|-----|------|------|------|-------------|")
        for m in sorted(members, key=lambda m: (m["ref"], int(m["pad"])
                                                if m["pad"].isdigit() else 0)):
            W("| %s | %s | %.2f | %.2f | %s | %.2f x %.2f |"
              % (m["ref"], m["pad"], m["x"], m["y"],
                 "bottom" if m["layer"] == "B.Cu" else "top",
                 m["w"], m["h"]))
        W("")

    # ---- 3. per-part pinout
    W("## 3. Parts and their nets (current placement)")
    W("")
    for p in sorted(parts, key=lambda p: p["ref"]):
        W("### %s - `%s`" % (p["ref"], p["fp"]))
        W("")
        W("value `%s` | at (%.1f, %.1f) rot %.0f | %s | %d pads"
          % (p["value"], p["x"], p["y"], p["rot"],
             "bottom (B.Cu)" if p["layer"] == "B.Cu" else "top (F.Cu)",
             len(p["pads"])))
        W("")
        W("| pad | net | x mm | y mm |")
        W("|-----|-----|------|------|")
        for m in sorted(p["pads"], key=lambda m: (int(m["pad"])
                                                  if m["pad"].isdigit() else 0)):
            W("| %s | %s | %.2f | %.2f |" % (m["pad"], m["net"] or "-none-",
                                             m["x"], m["y"]))
        W("")

    # ---- 4. sanity flags for the placement review
    W("## 4. Flags to check before routing")
    W("")
    bound = {n for n in bynet if n}
    empty = [n for n in mp.NETS if n not in bound]
    W("**Declared nets with no pads:** %s" % (", ".join(empty) or "none"))
    W("")
    W("**Pads left with no net** (a no-connect is fine here, an accidental one "
      "is not):")
    W("")
    W("| part | pin | label on silk | value | x mm | y mm |")
    W("|------|-----|---------------|-------|------|------|")
    lbl = {}
    for ref, names in mp.PIN_SILK.items():
        for i, nm in enumerate(names):
            lbl[(ref, str(i + 1))] = nm
    for i in range(1, 16):
        lbl[("J10a", str(i))] = mp.ESP_LABEL.get(i, "")
        lbl[("J10b", str(i))] = mp.ESP_LABEL.get(15 + i, "")
    for i, nm in enumerate(mp.TFT_LABEL):
        lbl[("J3", str(i + 1))] = nm
    for p in sorted((p for p in pads if not p["net"]),
                    key=lambda p: (p["ref"], int(p["pad"]) if p["pad"].isdigit() else 0)):
        W("| %s | %s | %s | %s | %.2f | %.2f |"
          % (p["ref"], p["pad"], lbl.get((p["ref"], p["pad"]), ""),
             next((q["value"] for q in parts if q["ref"] == p["ref"]), ""),
             p["x"], p["y"]))
    W("")

    # ---- 5. parts and their placement, most scattered part first
    W("## 5. Placement table (for rearranging)")
    W("")
    W("| part | value | x mm | y mm | rot | side | pads | footprint |")
    W("|------|-------|------|------|-----|------|------|-----------|")
    for p in sorted(parts, key=lambda p: p["ref"]):
        W("| %s | %s | %.1f | %.1f | %.0f | %s | %d | %s |"
          % (p["ref"], p["value"], p["x"], p["y"], p["rot"],
             "B" if p["layer"] == "B.Cu" else "F", len(p["pads"]), p["fp"]))
    W("")

    open(OUTMD, "w", encoding="utf-8").write("\n".join(lines) + "\n")

    # ---- console summary
    print("wrote %s (%d lines)" % (OUTMD, len(lines)))
    print("%-14s %5s %6s %7s" % ("net", "pads", "MST", "span"))
    for mst, sp, name, members, refs in sorted(rows, reverse=True)[:15]:
        print("%-14s %5d %6.1f %7.1f" % (name, len(members), mst, sp))
    single = [n for n, v in bynet.items() if v and len(v) == 1]
    nonet = [p for p in pads if not p["net"]]
    empty = [n for n in mp.NETS if n not in {p["net"] for p in pads}]
    print("single-pad nets: %s" % (", ".join(sorted(single)) or "none"))
    print("nets with no pads: %s" % (", ".join(empty) or "none"))
    print("pads with no net: %d" % len(nonet))


if __name__ == "__main__":
    main()
