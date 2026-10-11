"""Print the current placement table (courtyard box + drilled holes) per part.

Local helper for hand-tuning make_pcb.py; not part of the build.
"""
import sys
sys.path.insert(0, ".")
import make_pcb as m

rows = []
for ent in m.parts():
    ref, fp_id, x, y, rot = ent[0], ent[1], ent[2], ent[3], ent[4]
    bb = m.box_of(ent)
    side = "B" if ref in m.BOTTOM_SIDE else "F"
    rows.append((ref, side, x, y, rot, bb, [h for h in m.holes_of(ent)]))
for r in sorted(rows, key=lambda r: (r[1], r[2])):
    ref, side, x, y, rot, bb, holes = r
    print("%-5s %s at (%7.2f,%7.2f) rot %5.1f  crt %s" % (
        ref, side, x, y, rot,
        tuple(round(v, 2) for v in bb) if bb else None))
    if holes:
        print("        holes: " + " ".join("(%.2f,%.2f d%.1f)" % h for h in holes))
