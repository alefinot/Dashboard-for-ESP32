"""Find free board slots for a part, using the same courtyard rules the
generator and KiCad use.

  python place.py <fp_id> <body_dia> [target_x target_y] [pin_pitch_to_avoid]

Prints the legal positions (0.5 mm grid) closest to the target first, plus the
free-slot bounding boxes so a whole region is visible at a glance.
"""
import sys
import make_pcb as mp


def boxes(skip=()):
    out = []
    for ent in mp.parts():
        if ent[0] in skip:
            continue
        bb = mp.box_of(ent)
        if bb:
            out.append((ent[0], bb, ent[7]))
    return out


def fits(bb, others, ref="NEW"):
    if not (mp.BOARD_X0 + 0.3 <= bb[0] and bb[2] <= mp.BOARD_X1 - 0.3
            and mp.BOARD_Y0 + 0.3 <= bb[1] and bb[3] <= mp.BOARD_Y1 - 0.3):
        return False
    if not mp.keepout_clear(bb):
        return False
    for oref, obb, olayer in others:
        if ref in mp.BOTTOM_SIDE or oref in mp.BOTTOM_SIDE:
            continue
        if bb[0] < obb[2] and obb[0] < bb[2] and bb[1] < obb[3] and obb[1] < bb[3]:
            return False
    return True


def main():
    fp_id = sys.argv[1]
    dia = float(sys.argv[2])
    tx, ty = (float(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 \
        else ((mp.BOARD_X0 + mp.BOARD_X1) / 2, (mp.BOARD_Y0 + mp.BOARD_Y1) / 2)
    rot = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0

    others = boxes()
    # the courtyard from the library, enlarged to the real round body
    c = mp.kf.courtyard(fp_id)
    ent = ("C3", fp_id, 0.0, 0.0, rot, {}, "", "F.Cu", 0)
    base = mp.box_of(ent)
    dx, dy = base[2] - base[0], base[3] - base[1]
    dx, dy = max(dx, dia), max(dy, dia)
    cx, cy = (base[0] + base[2]) / 2, (base[1] + base[3]) / 2

    good = []
    x = mp.BOARD_X0 + 0.3 + dx / 2
    while x <= mp.BOARD_X1 - 0.3 - dx / 2 + 1e-9:
        y = mp.BOARD_Y0 + 0.3 + dy / 2
        while y <= mp.BOARD_Y1 - 0.3 - dy / 2 + 1e-9:
            bb = (x - cx + base[0], y - cy + base[1], x - cx + base[2], y - cy + base[3])
            if fits(bb, others):
                good.append((x, y))
            y += 0.5
        x += 0.5
    print("free centre positions: %d (part box %.1f x %.1f mm, rot %g)"
          % (len(good), dx, dy, rot))
    if not good:
        return
    # cluster into rectangular regions for readability
    seen, regions = set(), []
    for x, y in good:
        key = (round(x * 2), round(y * 2))
        if key in seen:
            continue
        stack, blob = [key], []
        seen.add(key)
        while stack:
            k = stack.pop()
            blob.append(k)
            for nk in ((k[0] + 2, k[1]), (k[0] - 2, k[1]), (k[0], k[1] + 2), (k[0], k[1] - 2)):
                if nk in set((round(a * 2), round(b * 2)) for a, b in good) and nk not in seen:
                    seen.add(nk)
                    stack.append(nk)
        xs = [k[0] / 2 for k in blob]
        ys = [k[1] / 2 for k in blob]
        regions.append((min(xs), min(ys), max(xs), max(ys), len(blob)))
    for r in sorted(regions, key=lambda r: -r[4]):
        cxr, cyr = (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
        print("  slot x %.1f..%.1f  y %.1f..%.1f  (%d spots, centre %.1f %.1f, dist to target %.1f)"
              % (r[0], r[2], r[1], r[3], r[4], cxr, cyr,
                 ((cxr - tx) ** 2 + (cyr - ty) ** 2) ** 0.5))


if __name__ == "__main__":
    main()
