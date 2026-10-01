#!/usr/bin/env python3
"""Design tool for the app icon (issue #51): computes the gauge geometry, renders
a PNG preview of it, and verifies the committed Android vectors against it.

The launcher foreground, its monochrome layer and the splash glyph all carry the
path data this script prints, so one set of numbers owns the whole mark.

Usage:
  python scripts/preview_icon.py [out.png]   # render the preview (66 dp / 72 dp guides drawn)
  python scripts/preview_icon.py --paths     # print the path data the XML was written from
  python scripts/preview_icon.py --check     # measure the design (safe zone, joins, anchoring)
  python scripts/preview_icon.py --xml FILE  # assert an Android vector matches the design
"""
import math

SCALE = 4                      # px per dp of the 108x108 adaptive canvas
BG = (5, 8, 13)                # hud_bg
CYAN = (34, 211, 238)          # hud_cyan
DIM = (14, 92, 102)            # hud_cyan_dim
AMBER = (255, 176, 32)         # hud_amber
RED = (255, 61, 94)            # hud_red

CX = CY = 54.0                 # gauge centre in the 108 dp viewport
R_ARC = 30.0                   # dial arc radius
SWEEP = 130.0                  # half sweep: arc runs -130 deg .. +130 deg
REDLINE = (95.0, 130.0)        # redline segment of the dial
N_ARC = 5.0                    # arc stroke width
TICK_OUT = 26.0                # ticks start just inside the arc
TICK_MAJOR_LEN = 7.0
TICK_MINOR_LEN = 4.0
TICK_MAJOR_W = 2.5
TICK_MINOR_W = 1.5
NEEDLE_PHI = 50.0              # needle bearing, deg clockwise from 12
NEEDLE_TIP = 21.0
NEEDLE_BASE = 6.0              # pivot offset of the wide end
NEEDLE_HALF_W = 2.0
HUB_R = 6.5
HUB_W = 2.5


def pt(r, phi_deg):
    """Point on the gauge: bearing measured clockwise from 12 o'clock."""
    phi = math.radians(phi_deg)
    return (CX + r * math.sin(phi), CY - r * math.cos(phi))


def arc_path(r, phi_from, phi_to):
    """Arc between two bearings; large-arc flag set when the span exceeds 180."""
    x1, y1 = pt(r, phi_from)
    x2, y2 = pt(r, phi_to)
    large = 1 if abs(phi_to - phi_from) > 180 else 0
    sweep = 1 if phi_to > phi_from else 0
    return "M%.2f,%.2f A%.1f,%.1f 0 %d %d %.2f,%.2f" % (x1, y1, r, r, large, sweep, x2, y2)


def line_path(r1, phi, r2):
    """Radial tick at a bearing, from r1 to r2."""
    x1, y1 = pt(r1, phi)
    x2, y2 = pt(r2, phi)
    return "M%.2f,%.2f L%.2f,%.2f" % (x1, y1, x2, y2)


def ring_path(r):
    """Full circle as two half arcs - the idiom Android vectors use for rings."""
    x1, y1 = pt(r, 0.0)
    x2, y2 = pt(r, 180.0)
    return "M%.2f,%.2f A%.1f,%.1f 0 1 1 %.2f,%.2f A%.1f,%.1f 0 1 1 %.2f,%.2f Z" % (
        x1, y1, r, r, x2, y2, r, r, x1, y1)


def needle_points():
    """Tapered needle: wide end on the pivot, point at NEEDLE_TIP, short tail.
    Returns (corner1, tip, corner2, tail)."""
    u = (math.sin(math.radians(NEEDLE_PHI)), -math.cos(math.radians(NEEDLE_PHI)))
    p = (u[1], -u[0])                      # perpendicular
    bx, by = pt(NEEDLE_BASE, NEEDLE_PHI)
    c1 = (bx + NEEDLE_HALF_W * p[0], by + NEEDLE_HALF_W * p[1])
    c2 = (bx - NEEDLE_HALF_W * p[0], by - NEEDLE_HALF_W * p[1])
    tip = pt(NEEDLE_TIP, NEEDLE_PHI)
    tail = pt(NEEDLE_BASE - 4.0, NEEDLE_PHI)
    return (c1, tip, c2, tail)


def needle_path():
    c1, tip, c2, tail = needle_points()
    return ("M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z"
            % (c1[0], c1[1], tip[0], tip[1], c2[0], c2[1], tail[0], tail[1]))


def ticks():
    """(bearing, is_major) for the dial's scale: majors every quarter sweep."""
    out = []
    n = 9
    for i in range(n):
        phi = -SWEEP + (2 * SWEEP) * i / (n - 1)
        out.append((round(phi, 1), i % 2 == 0))
    return out


def draw_stroke(draw, d, width, color):
    """Render one open path 'd' with round caps (PIL has no stroke API)."""
    w = width * SCALE
    for seg in d.replace('Z', '').split('M')[1:]:
        pts = parse_path(seg)
        for a, b in zip(pts, pts[1:]):
            draw.line([(a[0] * SCALE, a[1] * SCALE), (b[0] * SCALE, b[1] * SCALE)],
                      fill=color, width=int(w))
        for (px, py) in pts:
            r = w / 2.0
            draw.ellipse([px * SCALE - r, py * SCALE - r, px * SCALE + r, py * SCALE + r],
                         fill=color)


def parse_path(seg):
    """Flatten an 'x,y A r,r 0 f f x,y [L x,y]' fragment into polyline points."""
    for c in 'MLAZ':                      # 'A30.0' -> ' A 30.0'
        seg = seg.replace(c, ' %s ' % c)
    toks = [t for t in seg.replace(',', ' ').split() if t]
    i = 1 if toks[:1] == ['M'] else 0
    pts = [(float(toks[i]), float(toks[i + 1]))]
    i += 2
    while i < len(toks):
        cmd = toks[i]
        if cmd == 'A':
            r = float(toks[i + 1])
            large = int(toks[i + 4])
            sweep = int(toks[i + 5])
            x2, y2 = float(toks[i + 6]), float(toks[i + 7])
            x1, y1 = pts[-1]
            # Pick the candidate centre that reproduces the flags: the arc must
            # travel in the sweep direction and cover more than half a turn only
            # when large-arc-flag is set.
            mx, my = (x1 + x2) / 2, (y1 + y2) / 2
            dx, dy = x2 - x1, y2 - y1
            dist = math.hypot(dx, dy) or 1e-9
            h = math.sqrt(max(0.0, r * r - (dist / 2) ** 2))
            nx, ny = -dy / dist, dx / dist
            chosen = None
            for cx, cy in ((mx + h * nx, my + h * ny), (mx - h * nx, my - h * ny)):
                a1 = math.atan2(y1 - cy, x1 - cx)
                a2 = math.atan2(y2 - cy, x2 - cx)
                if sweep and a2 < a1:
                    a2 += 2 * math.pi
                if not sweep and a2 > a1:
                    a2 -= 2 * math.pi
                if (abs(a2 - a1) > math.pi) == (large == 1):
                    chosen = (cx, cy, a1, a2)
                    break
            if chosen is None:
                raise ValueError("no centre matches the arc flags: %s" % seg)
            cx, cy, a1, a2 = chosen
            steps = max(8, int(abs(a2 - a1) / 0.03))
            for k in range(1, steps + 1):
                a = a1 + (a2 - a1) * k / steps
                pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
            i += 8
        elif cmd == 'L':
            pts.append((float(toks[i + 1]), float(toks[i + 2])))
            i += 3
        else:
            i += 1
    return pts


def build():
    """The icon's paths: (name, path data, width, color, kind).

    kind: 'stroke' = stroked open path, 'fill' = filled polygon,
    'ring' = circle drawn at radius `width` with a `color` stroke.
    """
    out = []
    # Dial: cyan working range, red redline zone, joined at REDLINE[0].
    out.append(("arc-cyan", arc_path(R_ARC, -SWEEP, REDLINE[0]), N_ARC, CYAN, "stroke"))
    out.append(("arc-red", arc_path(R_ARC, REDLINE[0], SWEEP), N_ARC, RED, "stroke"))
    for phi, major in ticks():
        inner = TICK_OUT - (TICK_MAJOR_LEN if major else TICK_MINOR_LEN)
        color = RED if phi >= REDLINE[0] else CYAN
        out.append(("tick-%s" % phi, line_path(TICK_OUT, phi, inner),
                    TICK_MAJOR_W if major else TICK_MINOR_W, color, "stroke"))
    out.append(("needle", needle_path(), 0, AMBER, "fill"))
    out.append(("hub", ring_path(HUB_R), HUB_W, CYAN, "ring"))
    return out


def extents():
    """(path name, furthest reach from the gauge centre in dp) for every path."""
    res = []
    for name, d, w, _col, kind in build():
        if kind == "ring":
            res.append((name, HUB_R + w / 2.0))
        else:
            reach = max(math.hypot(px - CX, py - CY) for (px, py) in parse_path(d))
            res.append((name, reach + (w / 2.0 if kind == "stroke" else 0.0)))
    return res


def check():
    """Numeric self-test of the geometry. No vision model on this bench, so the
    design is verified by measurement: safe-zone fit, arc continuity, hub and
    needle anchoring, tick placement, dial symmetry (issue #51)."""
    problems = []
    SAFE_R = 33.0                             # 66 dp guaranteed-visible circle
    for name, reach in extents():
        if reach > SAFE_R + 0.01:
            problems.append("%s leaves the 66 dp safe zone (%.2f dp)" % (name, reach))
    paths = build()
    # The redline must join the cyan arc with no seam: same endpoint.
    end_cyan = parse_path(paths[0][1])[-1]
    start_red = parse_path(paths[1][1])[0]
    if math.hypot(end_cyan[0] - start_red[0], end_cyan[1] - start_red[1]) > 0.02:
        problems.append("redline does not start where the cyan arc ends")
    # Needle anchored in the hub: the pivot-side corners and the tail sit inside
    # the ring, the tip reaches NEEDLE_TIP out on the dial.
    hub_outer = HUB_R + HUB_W / 2.0
    c1, tip, c2, tail = needle_points()
    for tag, p in (("corner1", c1), ("corner2", c2), ("tail", tail)):
        if math.hypot(p[0] - CX, p[1] - CY) > hub_outer + 0.01:
            problems.append("needle %s is outside the hub ring" % tag)
    if abs(math.hypot(tip[0] - CX, tip[1] - CY) - NEEDLE_TIP) > 0.01:
        problems.append("needle tip is not at its designed radius")
    # Ticks live between the arc and the hub, never over the hub or past the sweep.
    for phi, major in ticks():
        inner = TICK_OUT - (TICK_MAJOR_LEN if major else TICK_MINOR_LEN)
        if inner <= hub_outer:
            problems.append("tick at %s deg overlaps the hub" % phi)
        if abs(phi) > SWEEP + 0.01:
            problems.append("tick at %s deg falls outside the dial sweep" % phi)
    # A dial hand, not a random arrow: inside the sweep, short of the redline.
    if not 0 < NEEDLE_PHI < REDLINE[0]:
        problems.append("needle bearing is not in the working part of the dial")
    # Dial symmetry about the vertical axis.
    ax, ay = pt(R_ARC, -SWEEP)
    bx, by = pt(R_ARC, SWEEP)
    if abs((ax + bx) / 2 - CX) > 0.01 or abs(ay - by) > 0.01:
        problems.append("dial is not symmetric about the vertical axis")
    # The needle must actually differ from a tick: longer than the deepest tick.
    if NEEDLE_TIP <= TICK_OUT - TICK_MAJOR_LEN:
        problems.append("needle is not longer than the scale ticks")
    return problems


def check_xml(path):
    """Assert a committed Android vector carries exactly the designed geometry:
    same set of path data, nothing extra, nothing drifted. The splash glyph is
    allowed to sit inside a scaled <group>; the path data itself must match."""
    import io
    import re
    src = io.open(path, encoding='utf-8').read()
    found = re.findall(r'pathData="([^"]+)"', src)

    def norm(d):
        for c in 'MLAZ':
            d = d.replace(c, ' %s ' % c)
        toks = d.replace(',', ' ').split()
        return tuple(round(float(t), 2) if re.fullmatch(r'-?[\d.]+', t) else t
                     for t in toks)

    want = sorted(norm(d) for _n, d, _w, _c, _k in build())
    got = sorted(norm(d) for d in found)
    problems = []
    if len(want) != len(got):
        problems.append("%s has %d paths, design has %d" % (path, len(got), len(want)))
    for w, g in zip(want, got):
        if w != g:
            problems.append("%s drifts from the design (want %s, got %s)"
                            % (path, ' '.join(map(str, w)), ' '.join(map(str, g))))
    return problems


def render(out_path):
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (int(108 * SCALE), int(108 * SCALE)), BG)
    draw = ImageDraw.Draw(img)
    # Mask guides: 66 dp guaranteed-visible circle and the 72 dp safe zone.
    for r, col in ((33, DIM), (36, (30, 40, 48))):
        bb = [(CX - r) * SCALE, (CY - r) * SCALE, (CX + r) * SCALE, (CY + r) * SCALE]
        draw.ellipse(bb, outline=col, width=2)
    for name, d, w, col, kind in build():
        if kind == "ring":
            bb = [(CX - HUB_R) * SCALE, (CY - HUB_R) * SCALE,
                  (CX + HUB_R) * SCALE, (CY + HUB_R) * SCALE]
            draw.ellipse(bb, outline=col, width=int(w * SCALE))
        elif kind == "fill":
            draw.polygon([(p[0] * SCALE, p[1] * SCALE) for p in parse_path(d)], fill=col)
        else:
            draw_stroke(draw, d, w, col)
    img.save(out_path)
    return out_path


if __name__ == "__main__":
    import sys
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    out = args[0] if args else "icon-preview.png"
    if '--check' in sys.argv:
        problems = check()
        for p in problems:
            print("FAIL  %s" % p)
        print("geometry check: %s" % ("OK" if not problems else "%d problem(s)" % len(problems)))
        sys.exit(1 if problems else 0)
    if '--xml' in sys.argv:
        target = args[0] if args else ''
        problems = check_xml(target)
        for pr in problems:
            print("FAIL  %s" % pr)
        print("xml check %s: %s" % (target, "OK" if not problems else "%d problem(s)" % len(problems)))
        sys.exit(1 if problems else 0)
    if '--paths' in sys.argv:
        for name, d, w, col, kind in build():
            print("# %s (width %s, kind %s)\n  %s" % (name, w, kind, d))
    render(out)
    print("wrote %s" % out)
