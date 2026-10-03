#!/usr/bin/env python3
"""Design tool for the app icon (issue #51): computes the gauge geometry, renders
launch-size previews (masked, as a launcher would show them), verifies the design
by measurement, and emits the Android vector XML that ships.

The launcher foreground, its monochrome layer, the background layer and the splash
glyph all carry the path data this script prints, so one set of numbers owns the
whole mark.

Usage:
  python scripts/preview_icon.py [--variant NAME] [out.png]   # render one preview
  python scripts/preview_icon.py --variants                   # list variant names
  python scripts/preview_icon.py --paths [--variant NAME]     # print the path data
  python scripts/preview_icon.py --check [--variant NAME]     # measure the design
  python scripts/preview_icon.py --xml FILE [--variant NAME]  # assert an XML matches
  python scripts/preview_icon.py --write [--variant NAME]     # write the Android vectors
  python scripts/preview_icon.py --sheet [out.png] [NAME..]   # contact sheet at the
        # real launcher sizes (162 / 96 / 48 / 28 px), masked as a launcher masks them

Design rules the check enforces - the eye is a poor instrument at 28 dp, so the
design has to be provable by numbers as well as by eye:
  * every stroke/cap stays inside the 66 dp guaranteed-visible circle, and stops
    1.25 dp short of it so the mark has air inside the mask;
  * nothing on the mark is thinner than 3 dp - 1.5 dp is under one physical pixel
    on a 48 dp launcher tile;
  * the air above the mark and the air below it stay within a factor of two: a
    dial open at the bottom carries its ink high, so the gauge centre sits below
    the canvas centre or the tile reads high;
  * one dominant weight (the dial) and one secondary (the needle);
  * the needle is symmetric about its own axis, its shoulders and tail sit inside
    the hub, and its tip clears the dial's inner edge;
  * the dial is symmetric about the vertical axis and the redline joins it at one
    shared endpoint, with no seam.
"""
import math

# ---------------------------------------------------------------- palette ----
# Mirrors res/values/colors.xml / ui/theme/Color.kt - the HUD's own palette.
BG        = (5, 8, 13, 255)       # hud_bg
BG_GLOW   = (12, 27, 38, 255)     # hud_icon_glow - hud_bg lifted with cyan
ICON_MID  = (8, 17, 24, 255)      # hud_icon_mid - the glow's 0.55 stop
CYAN      = (34, 211, 238, 255)   # hud_cyan
CYAN_DIM  = (14, 92, 102, 255)    # hud_cyan_dim
AMBER     = (255, 176, 32, 255)   # hud_amber
RED       = (255, 61, 94, 255)    # hud_red
WHITE     = (216, 240, 245, 255)  # hud_text_primary

# The palette lives in res/values/colors.xml; the emitted XML references it by
# name so there is one place to change a colour. Anything not in this map ships
# as a literal #AARRGGBB.
COLOR_NAMES = {
    BG: "@color/hud_bg",
    BG_GLOW: "@color/hud_icon_glow",
    ICON_MID: "@color/hud_icon_mid",
    CYAN: "@color/hud_cyan",
    CYAN_DIM: "@color/hud_cyan_dim",
    AMBER: "@color/hud_amber",
    RED: "@color/hud_red",
    WHITE: "@color/hud_text_primary",
}


def color_ref(paint):
    return COLOR_NAMES.get(tuple(paint))

CX = CY = 54.0        # gauge centre in the 108 dp adaptive viewport
SAFE_R = 33.0         # 66 dp guaranteed-visible circle
GUIDE_R = 36.0        # 72 dp zone - everything outside may be clipped

W_MAJOR = 9.0         # the dial: one dominant weight
W_SECOND = 3.6        # secondary weight (ticks), a multiple-free 40 % of the dial


def rgba(c, a=None):
    return (c[0], c[1], c[2], c[3] if a is None else a)


def mix(c1, c2, t):
    return tuple(round(c1[i] + (c2[i] - c1[i]) * t) for i in range(4))


# ------------------------------------------------------------ geometry ------
def pt(cx, cy, r, phi_deg):
    """Point on the gauge: bearing measured clockwise from 12 o'clock."""
    phi = math.radians(phi_deg)
    return (cx + r * math.sin(phi), cy - r * math.cos(phi))


def arc_path(cx, cy, r, phi_from, phi_to):
    """Arc between two bearings; large-arc flag set when the span exceeds 180."""
    x1, y1 = pt(cx, cy, r, phi_from)
    x2, y2 = pt(cx, cy, r, phi_to)
    large = 1 if abs(phi_to - phi_from) > 180 else 0
    sweep = 1 if phi_to > phi_from else 0
    return "M%.2f,%.2f A%.1f,%.1f 0 %d %d %.2f,%.2f" % (x1, y1, r, r, large, sweep, x2, y2)


def line_path(r1, phi, r2, cx=CX, cy=CY):
    """Radial segment at a bearing, from r1 to r2."""
    x1, y1 = pt(cx, cy, r1, phi)
    x2, y2 = pt(cx, cy, r2, phi)
    return "M%.2f,%.2f L%.2f,%.2f" % (x1, y1, x2, y2)


def ring_path(r, cx=CX, cy=CY):
    """Full circle as two half arcs - the idiom Android vectors use for rings."""
    x1, y1 = pt(cx, cy, r, 0.0)
    x2, y2 = pt(cx, cy, r, 180.0)
    return ("M%.2f,%.2f A%.1f,%.1f 0 1 1 %.2f,%.2f A%.1f,%.1f 0 1 1 %.2f,%.2f Z"
            % (x1, y1, r, r, x2, y2, r, r, x1, y1))


def needle_path(phi, tip, half_w, tail, cx=CX, cy=CY):
    """Symmetric kite needle: point at `tip`, shoulders `half_w` either side of the
    pivot, counterweight tail at `tail` on the far side. Symmetric about its own
    axis so it reads as a turned hand, not a stray arrow."""
    u = (math.sin(math.radians(phi)), -math.cos(math.radians(phi)))
    p = (u[1], -u[0])                       # perpendicular
    tx, ty = pt(cx, cy, tip, phi)
    sx, sy = pt(cx, cy, 0.0, phi)
    kx, ky = pt(cx, cy, -tail, phi)
    a = (sx + half_w * p[0], sy + half_w * p[1])
    b = (sx - half_w * p[0], sy - half_w * p[1])
    return ("M%.2f,%.2f L%.2f,%.2f L%.2f,%.2f L%.2f,%.2f Z"
            % (tx, ty, a[0], a[1], kx, ky, b[0], b[1]))


def needle_points(phi, tip, half_w, tail, cx=CX, cy=CY):
    u = (math.sin(math.radians(phi)), -math.cos(math.radians(phi)))
    p = (u[1], -u[0])
    sx, sy = pt(cx, cy, 0.0, phi)
    return ((sx + half_w * p[0], sy + half_w * p[1]),
            pt(cx, cy, tip, phi),
            (sx - half_w * p[0], sy - half_w * p[1]),
            pt(cx, cy, -tail, phi))


# ------------------------------------------------------------- variants -----
# Each variant is the full parameter set of the mark. `arc` is a list of
# (phi_from, phi_to, colour) segments, `ticks` a list of (phi, length, width),
# `needle` a (phi, tip, half_w, tail) tuple.
VARIANTS = {
    # Baseline: the mark currently shipped, kept so the sheet shows the before.
    "old": dict(
        literal=[
            ("arc-cyan", "M31.02,73.28 A30,30 0 1 1 83.89,56.61", 5, CYAN, "stroke"),
            ("arc-red", "M83.89,56.61 A30,30 0 0 1 76.98,73.28", 5, RED, "stroke"),
            ("tick1", "M34.08,70.71 L39.45,66.21", 2.5, CYAN, "stroke"),
            ("tick2", "M28.22,57.39 L32.19,56.87", 1.5, CYAN, "stroke"),
            ("tick3", "M30.44,43.01 L36.78,45.97", 2.5, CYAN, "stroke"),
            ("tick4", "M40.03,32.07 L42.18,35.45", 1.5, CYAN, "stroke"),
            ("tick5", "M54,28 L54,35", 2.5, CYAN, "stroke"),
            ("tick6", "M67.97,32.07 L65.82,35.45", 1.5, CYAN, "stroke"),
            ("tick7", "M77.56,43.01 L71.22,45.97", 2.5, CYAN, "stroke"),
            ("tick8", "M79.78,57.39 L75.81,56.87", 1.5, RED, "stroke"),
            ("tick9", "M73.92,70.71 L68.55,66.21", 2.5, RED, "stroke"),
            ("needle", "M57.31,48.61 L70.09,40.50 L59.88,51.68 L55.53,52.71 Z", 0, AMBER, "fill"),
            ("hub", "M54,47.5 A6.5,6.5 0 1 1 54,60.5 A6.5,6.5 0 1 1 54,47.5 Z", 2.5, CYAN, "ring"),
        ],
        glow=None,
    ),
    # The shipped mark. A 260-degree dial in one bold weight, its last 26 degrees
    # painted red as the redline, and a tapered needle parked mid-swing on a hub big
    # enough to hold it. No tick ring: at 48 dp the ticks blurred into a fringe, and
    # the dial alone already says "instrument". The gauge centre sits 2.5 dp below
    # the canvas centre - the open bottom carries no ink, so a centred dial reads
    # high in the mask (the check measures the air above and below).
    "gauge": dict(
        r_arc=25.5, sweep=130.0, w=8.5, cy=56.5,
        arc=[(-130.0, 104.0, CYAN), (104.0, 130.0, RED)],
        ticks=[],
        needle=(40.0, 20.0, 3.4, 5.4), needle_color=AMBER,
        hub=(7.0, AMBER, 2.5), core_color=BG,
        glow=(54.0, 52.0, 60.0, BG_GLOW, ICON_MID, BG),
    ),
}
DEFAULT_VARIANT = "gauge"


def build(spec, mono=False):
    """The mark's paths: (name, path data, width, colour, kind).

    kind: 'stroke' = stroked open path, 'fill' = filled polygon,
    'ring' = circle stroke, 'disc' = filled circle.
    `mono` repaints everything in one colour for the themed-icon layer.
    """
    if "literal" in spec:
        src = spec["literal"]
        if mono:
            return [(n, d, w, (255, 255, 255, 255), k) for (n, d, w, _c, k) in src]
        return list(src)

    cx, cy = spec.get("cx", CX), spec.get("cy", CY)
    r, w = spec["r_arc"], spec["w"]
    ink = (255, 255, 255, 255) if mono else None
    out = []
    if spec.get("track"):
        tr, tw, tc = spec["track"]
        out.append(("track", ring_path(tr, cx, cy), tw, ink if mono else tc, "ring"))
    for i, (p1, p2, paint) in enumerate(spec["arc"]):
        col = ink if mono else paint
        kind = "stroke" if spec.get("cap", "round") == "round" else "stroke-butt"
        out.append(("arc-%d" % i, arc_path(cx, cy, r, p1, p2), w, col, kind))
    tick_w = spec.get("tick_w", W_SECOND)
    tick_c = ink if mono else spec.get("tick_color", CYAN)
    for i, (phi, ln, tw) in enumerate(spec["ticks"]):
        out.append(("tick-%d" % i, line_path(r - w * 0.55, phi, r - w * 0.55 - ln, cx, cy),
                    tw if tw else tick_w, tick_c, "stroke"))
    nphi, ntip, nhw, ntail = spec["needle"]
    out.append(("needle", needle_path(nphi, ntip, nhw, ntail, cx, cy), 0,
                ink if mono else spec["needle_color"], "fill"))
    hub_r, hub_c, core_r = spec["hub"]
    out.append(("hub", ring_path(hub_r, cx, cy), 0, ink if mono else hub_c, "disc"))
    if core_r:
        out.append(("hub-core", ring_path(core_r, cx, cy), 0,
                    ink if mono else spec.get("core_color", BG), "disc"))
    return out


# ------------------------------------------------------------- parsing ------
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
            for ccx, ccy in ((mx + h * nx, my + h * ny), (mx - h * nx, my - h * ny)):
                a1 = math.atan2(y1 - ccy, x1 - ccx)
                a2 = math.atan2(y2 - ccy, x2 - ccx)
                if sweep and a2 < a1:
                    a2 += 2 * math.pi
                if not sweep and a2 > a1:
                    a2 -= 2 * math.pi
                if (abs(a2 - a1) > math.pi - 1e-9) == (large == 1):
                    chosen = (ccx, ccy, a1, a2)
                    break
            if chosen is None:
                raise ValueError("no centre matches the arc flags: %s" % seg)
            ccx, ccy, a1, a2 = chosen
            steps = max(8, int(abs(a2 - a1) / 0.02))
            for k in range(1, steps + 1):
                a = a1 + (a2 - a1) * k / steps
                pts.append((ccx + r * math.cos(a), ccy + r * math.sin(a)))
            i += 8
        elif cmd == 'L':
            pts.append((float(toks[i + 1]), float(toks[i + 2])))
            i += 3
        else:
            i += 1
    return pts


# ------------------------------------------------------------ rendering -----
SS = 3                  # supersample factor for anti-aliasing


def draw_paths(img, paths, scale, cx=CX, cy=CY):
    """Paint path specs onto an RGBA image; `scale` is px per dp."""
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img, "RGBA")
    for name, d, w, paint, kind in paths:
        pts = parse_path(d)
        if kind in ("fill", "disc"):
            draw.polygon([(p[0] * scale, p[1] * scale) for p in pts], fill=paint)
            continue
        px = [(p[0] * scale, p[1] * scale) for p in pts]
        half = w * scale / 2.0
        round_cap = kind == "stroke"
        for a, b in zip(px, px[1:]):
            col = paint
            draw.line([a, b], fill=col, width=int(round(w * scale)))
            if round_cap:
                draw.ellipse([b[0] - half, b[1] - half, b[0] + half, b[1] + half], fill=col)
        if round_cap and len(px) > 1:
            a = px[0]
            draw.ellipse([a[0] - half, a[1] - half, a[0] + half, a[1] + half], fill=paint)
    return img


def render_mark(size_px, spec, mono=False):
    """The mark alone, transparent background, at `size_px` for the 108 dp canvas."""
    from PIL import Image
    s = size_px * SS / 108.0
    img = Image.new("RGBA", (size_px * SS, size_px * SS), (0, 0, 0, 0))
    draw_paths(img, build(spec, mono), s)
    return img.resize((size_px, size_px), Image.LANCZOS)


def render_bg(size_px, spec):
    """The adaptive background layer: flat hud_bg lifted by a radial cyan glow."""
    import numpy as np
    from PIL import Image
    img = Image.new("RGBA", (size_px, size_px), BG)
    glow = spec.get("glow")
    if not glow:
        return img
    gx, gy, gr, inner, mid, outer = glow
    ss = 2
    n = size_px * ss
    y, x = np.mgrid[0:n, 0:n].astype(float) / ss
    # Same three stops the vector ships: glow -> mid at 0.55 -> hud_bg at 1.0.
    t = np.clip(np.hypot(x - gx, y - gy) / gr, 0.0, 1.0)
    chan = [np.where(t < 0.55,
                     inner[i] + (mid[i] - inner[i]) * np.clip(t / 0.55, 0, 1),
                     mid[i] + (outer[i] - mid[i]) * np.clip((t - 0.55) / 0.45, 0, 1))
            for i in range(4)]
    arr = np.dstack(chan).astype('uint8')
    return Image.fromarray(arr, "RGBA").resize((size_px, size_px), Image.LANCZOS)


def mask_img(size_px, shape):
    """Launcher mask as an L image: 'circle' or Android's squircle."""
    from PIL import Image, ImageDraw
    ss = 4
    m = Image.new("L", (size_px * ss, size_px * ss), 0)
    d = ImageDraw.Draw(m)
    n = size_px * ss
    if shape == "circle":
        d.ellipse([0, 0, n - 1, n - 1], fill=255)
    else:               # superellipse stand-in for the adaptive squircle
        pts = []
        for i in range(720):
            a = 2 * math.pi * i / 720
            ca, sa = math.cos(a), math.sin(a)
            x = 0.5 + 0.5 * math.copysign(abs(ca) ** (2 / 3.2), ca)
            y = 0.5 + 0.5 * math.copysign(abs(sa) ** (2 / 3.2), sa)
            pts.append((x * n, y * n))
        d.polygon(pts, fill=255)
    return m.resize((size_px, size_px), Image.LANCZOS)


def tile(size_px, spec, shape, wallpaper):
    """How a launcher would show it: background + mark, clipped by the mask."""
    from PIL import Image
    base = Image.new("RGBA", (size_px, size_px), wallpaper)
    base.paste(render_bg(size_px, spec), (0, 0), mask_img(size_px, shape))
    base.alpha_composite(render_mark(size_px, spec))
    return base


def optical_center(spec, mono=False):
    """Alpha-weighted centroid of the drawn mark, in dp. A dial that is open at the
    bottom carries more ink above the middle, so a mark centred on the canvas can
    still sit high in the mask; this is how we measure that instead of eyeballing."""
    import numpy as np
    n = 216                                   # 2 px per dp
    a = np.asarray(render_mark(n, spec, mono), dtype=float)[:, :, 3]
    if a.sum() == 0:
        return (CX, CY, 0.0)
    y, x = np.mgrid[0:n, 0:n]
    cx = float((x * a).sum() / a.sum()) * 108.0 / n
    cy = float((y * a).sum() / a.sum()) * 108.0 / n
    return (cx, cy, math.hypot(cx - CX, cy - CY))


# column label -> (render size px, mask shape, display upscale)
SHEET_COLS = [("guides 432", 432, None, 1), ("masked 162", 162, "circle", 1),
              ("masked 96", 96, "squircle", 1), ("masked 48 (shown 3x)", 48, "circle", 3),
              ("masked 28 (shown 4x)", 28, "circle", 4)]
WALLPAPER_LIGHT = (151, 157, 165, 255)


def sheet(variant_names, out_path):
    """Contact sheet: the geometry with the mask guides, then the same mark the way a
    launcher draws it (clipped, and small enough to judge on a home screen)."""
    from PIL import Image, ImageDraw
    rows = []
    for name in variant_names:
        spec = VARIANTS[name]
        cells = []
        for _lbl, size, shape, up in SHEET_COLS:
            if shape is None:
                img = Image.new("RGBA", (size, size), BG)
                img.paste(render_bg(size, spec), (0, 0))
                d = ImageDraw.Draw(img, "RGBA")
                for rr, col in ((SAFE_R, (14, 92, 102, 255)), (GUIDE_R, (30, 40, 48, 255))):
                    bb = [(CX - rr) * size / 108.0, (CY - rr) * size / 108.0,
                          (CX + rr) * size / 108.0, (CY + rr) * size / 108.0]
                    d.ellipse(bb, outline=col, width=2)
                img.alpha_composite(render_mark(size, spec))
            else:
                img = tile(size, spec, shape, WALLPAPER_LIGHT)
            if up > 1:
                img = img.resize((size * up, size * up), Image.NEAREST)
            cells.append(img)
        rows.append((name, cells))
    colw = [max(cells[i].size[0] for _n, cells in rows) for i in range(len(SHEET_COLS))]
    rowh = [max(im.size[1] for im in cells) for _n, cells in rows]
    pad, label = 20, 18
    out = Image.new("RGB", (sum(w + pad for w in colw) + pad,
                            sum(h + label + pad for h in rowh) + pad), (26, 28, 32))
    d = ImageDraw.Draw(out)
    y = pad
    for (_n, cells), h in zip(rows, rowh):
        d.text((pad, y), _n, fill=(200, 210, 220))
        x = pad
        for (lbl, _size, _shape, _up), im, w in zip(SHEET_COLS, cells, colw):
            out.paste(im.convert("RGB"), (x, y + label))
            d.text((x, y + label + im.size[1] + 2), lbl, fill=(150, 160, 170))
            x += w + pad
        y += h + label + pad * 2
    out.save(out_path)
    return out_path


# --------------------------------------------------------------- checks -----
def extents(spec):
    """(path name, furthest reach from the canvas centre in dp) for every path."""
    res = []
    for name, d, w, _paint, kind in build(spec):
        pts = parse_path(d)
        reach = max(math.hypot(px - CX, py - CY) for (px, py) in pts)
        if kind in ("stroke", "stroke-butt", "ring"):
            reach += w / 2.0
        res.append((name, reach))
    return res


def bounds(spec):
    """(min x, min y, max x, max y) of the ink, in dp, counting stroke widths."""
    lo = [1e9, 1e9]
    hi = [-1e9, -1e9]
    for name, d, w, _paint, kind in build(spec):
        pad = w / 2.0 if kind in ("stroke", "stroke-butt", "ring") else 0.0
        for (px, py) in parse_path(d):
            lo[0], lo[1] = min(lo[0], px - pad), min(lo[1], py - pad)
            hi[0], hi[1] = max(hi[0], px + pad), max(hi[1], py + pad)
    return (lo[0], lo[1], hi[0], hi[1])


def check(spec):
    """Numeric self-test of the design: safe-zone fit and air, weight discipline,
    arc joins, needle anchoring, tick placement, dial symmetry, centring."""
    problems = []
    for name, reach in extents(spec):
        if reach > SAFE_R + 0.01:
            problems.append("%s leaves the 66 dp safe zone (%.2f dp)" % (name, reach))
    paths = build(spec)
    if "literal" in spec:
        return problems
    gcx, gcy = spec.get("cx", CX), spec.get("cy", CY)
    # One dominant weight, no hairlines: nothing on the mark thinner than 3 dp,
    # because 1.5 dp is under a physical pixel on a 48 dp launcher tile.
    for name, d, w, _p, kind in paths:
        if kind in ("stroke", "stroke-butt", "ring") and w < 3.0:
            problems.append("%s is a hairline (%.1f dp) - it will not survive 48 dp" % (name, w))
    # Air inside the mask: the mark must not run to the edge of the visible circle.
    tight = max(r for _n, r in extents(spec))
    if tight > SAFE_R - 1.25:
        problems.append("mark crowds the mask (furthest reach %.2f dp, want <= %.2f)"
                        % (tight, SAFE_R - 1.25))
    # Balance inside the mask: the air above the mark and the air below it must not
    # diverge. A dial open at the bottom carries its mass high, so the gauge centre
    # has to sit a little below the mask centre for the tile to look level.
    x0, y0, x1, y1 = bounds(spec)
    above = y0 - (CY - SAFE_R)
    below = (CY + SAFE_R) - y1
    if max(above, below) > 2.0 * min(above, below):
        problems.append("mark sits off-level in the mask (%.1f dp of air above, %.1f below)"
                        % (above, below))
    left = x0 - (CX - SAFE_R)
    right = (CX + SAFE_R) - x1
    if abs(left - right) > 0.5:
        problems.append("mark is not centred horizontally (%.1f dp left, %.1f right)"
                        % (left, right))
    # Dial segments must join with no seam: consecutive arcs share an endpoint,
    # unless the design asks for a deliberate gap (segments).
    gaps = spec.get("gaps_ok", False)
    arcs = [p for p in paths if p[0].startswith("arc-")]
    for a, b in zip(arcs, arcs[1:]):
        e = parse_path(a[1])[-1]
        s = parse_path(b[1])[0]
        d = math.hypot(e[0] - s[0], e[1] - s[1])
        if not gaps and d > 0.02:
            problems.append("arc %s does not continue %s (gap %.2f dp)" % (b[0], a[0], d))
        if gaps and d < 0.02:
            problems.append("arcs %s/%s touch - segments need a gap" % (a[0], b[0]))
    # Needle: symmetric hand, anchored in the hub, tip out on the dial.
    hub_r, _hc, core_r = spec["hub"]
    nphi, ntip, nhw, ntail = spec["needle"]
    c1, tip, c2, tail = needle_points(nphi, ntip, nhw, ntail, gcx, gcy)
    for tag, p, q in (("shoulders", c1, c2),):
        if abs(math.hypot(p[0] - gcx, p[1] - gcy) - math.hypot(q[0] - gcx, q[1] - gcy)) > 1e-6:
            problems.append("needle is not symmetric about its axis")
        if math.hypot(p[0] - gcx, p[1] - gcy) > hub_r + 0.01:
            problems.append("needle shoulders are outside the hub")
    if math.hypot(tail[0] - gcx, tail[1] - gcy) > hub_r + 0.01:
        problems.append("needle tail is outside the hub")
    if abs(math.hypot(tip[0] - gcx, tip[1] - gcy) - ntip) > 0.01:
        problems.append("needle tip is not at its designed radius")
    if ntip <= spec["r_arc"] - spec["w"]:
        problems.append("needle does not reach clear of the dial's inner edge")
    if not 0 < nphi < spec["sweep"]:
        problems.append("needle bearing falls outside the dial sweep")
    # Ticks live between the dial and the hub, never over the hub, never under
    # the needle.
    for i, (phi, ln, tw) in enumerate(spec["ticks"]):
        inner = spec["r_arc"] - spec["w"] * 0.55 - ln
        if inner <= hub_r:
            problems.append("tick %d overlaps the hub" % i)
        if abs(phi) > spec["sweep"]:
            problems.append("tick %d falls outside the dial sweep" % i)
        if abs(phi - nphi) < 12:
            problems.append("tick %d sits under the needle" % i)
    # Dial symmetric about the vertical axis.
    ax, ay = pt(gcx, gcy, spec["r_arc"], -spec["sweep"])
    bx, by = pt(gcx, gcy, spec["r_arc"], spec["sweep"])
    if abs((ax + bx) / 2 - gcx) > 0.01 or abs(ay - by) > 0.01:
        problems.append("dial is not symmetric about the vertical axis")
    return problems


def check_xml(path, spec):
    """Assert a committed Android vector carries exactly the designed geometry."""
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

    want = sorted(norm(d) for _n, d, _w, _c, _k in build(spec))
    got = sorted(norm(d) for d in found)
    problems = []
    if len(want) != len(got):
        problems.append("%s has %d paths, design has %d" % (path, len(got), len(want)))
    for w, g in zip(want, got):
        if w != g:
            problems.append("%s drifts from the design (want %s, got %s)"
                            % (path, ' '.join(map(str, w)), ' '.join(map(str, g))))
    return problems


# ----------------------------------------------------------- XML emission ---
def xml_path(indent, name, d, w, paint, kind):
    i = " " * indent
    col = color_ref(paint) or "#FF%02X%02X%02X" % paint[:3]
    if kind in ("stroke", "stroke-butt", "ring"):
        cap = "butt" if kind == "stroke-butt" else "round"
        return ('%s<path android:strokeColor="%s" android:strokeWidth="%s"'
                ' android:strokeLineCap="%s" android:fillColor="#00000000"'
                ' android:pathData="%s"/>' % (i, col, w, cap, d))
    return '%s<path android:fillColor="%s" android:pathData="%s"/>' % (i, col, d)


def emit_variant(spec, mono=False, group=None):
    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<vector xmlns:android="http://schemas.android.com/apk/res/android"'
             ' xmlns:aapt="http://schemas.android.com/aapt"'
             ' android:width="108dp" android:height="108dp"'
             ' android:viewportWidth="108" android:viewportHeight="108">']
    indent = 4
    if group:
        # Scale about the mark's own centre, not the canvas centre: the dial sits a
        # little low in the mask, and scaling about 54,54 would push it further down.
        lines.append('    <group android:name="%s" android:scaleX="%s" android:scaleY="%s"'
                     ' android:pivotX="%s" android:pivotY="%s">'
                     % (group[0], group[1], group[1],
                        group[2] if len(group) > 2 else 54,
                        group[3] if len(group) > 3 else 54))
        indent = 8
    for name, d, w, paint, kind in build(spec, mono):
        lines.append(xml_path(indent, name, d, w, paint, kind))
    if group:
        lines.append('    </group>')
    lines.append('</vector>')
    return "\n".join(lines) + "\n"


def emit_background(spec):
    gx, gy, gr, inner, mid, outer = spec["glow"]
    def ref(c):
        return color_ref(c) or "#FF%02X%02X%02X" % c[:3]
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    xmlns:aapt="http://schemas.android.com/aapt"\n'
            '    android:width="108dp" android:height="108dp"\n'
            '    android:viewportWidth="108" android:viewportHeight="108">\n'
            '    <path android:pathData="M0,0 H108 V108 H0 Z">\n'
            '        <aapt:attr name="android:fillColor">\n'
            '            <gradient android:type="radial" android:gradientRadius="%s"\n'
            '                android:centerX="%s" android:centerY="%s">\n'
            '                <item android:offset="0" android:color="%s"/>\n'
            '                <item android:offset="0.55" android:color="%s"/>\n'
            '                <item android:offset="1" android:color="%s"/>\n'
            '            </gradient>\n'
            '        </aapt:attr>\n'
            '    </path>\n'
            '</vector>\n'
            % (gr, gx, gy, ref(inner), ref(mid), ref(outer)))


def write_all(spec, name, res_dir):
    import io
    import os
    def head(what):
        # XML forbids '--' inside a comment, so the header never spells a flag name.
        return ('<?xml version="1.0" encoding="utf-8"?>\n'
                '<!-- Generated by scripts/preview_icon.py (variant "%s"): %s\n'
                '     Do not hand-edit the path data: change the design in that script\n'
                '     and regenerate the layers with its write flag. -->\n' % (name, what))
    def body(text):
        return text.split("\n", 1)[1]      # drop the file's own <?xml?> line
    files = {
        "drawable/ic_launcher_foreground.xml":
            head("the launcher foreground: one bold dial, the redline that closes it,\n"
                 '     the needle, and the hub it pivots on.')
            + body(emit_variant(spec)),
        "drawable/ic_launcher_monochrome.xml":
            head("the launcher mark in one colour, for themed (Android 13+) icon packs.\n"
                 '     Same paths as ic_launcher_foreground.xml - keep them in step.')
            + body(emit_variant(spec, mono=True)),
        "drawable/ic_launcher_background.xml":
            head("the background layer: hud_bg lifted by a radial cyan glow behind the dial.")
            + body(emit_background(spec)),
        "drawable/ic_splash.xml":
            head("the splash glyph: the same gauge scaled up, since the splash has no\n"
                 '     adaptive mask. Painted by SplashScreen() in ui/screens/DashboardRoot.kt.')
            + body(emit_variant(spec, group=("gauge", "1.55",
                                             spec.get("cx", CX), spec.get("cy", CY)))),
    }
    for rel, text in files.items():
        p = os.path.join(res_dir, rel)
        io.open(p, "w", encoding='utf-8', newline="\n").write(text)
        print("wrote %s" % p)


if __name__ == "__main__":
    import sys
    argv = sys.argv[1:]
    flags, args, out = [], [], None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--out':
            out = argv[i + 1]
            i += 2
            continue
        if a.startswith('--'):
            flags.append(a)
        else:
            args.append(a)
        i += 1
    vname = DEFAULT_VARIANT
    if '--variant' in flags:
        vname = args[0]
        args = args[1:]
    if out is None:
        out = "icon-sheet.png" if '--sheet' in flags else "icon-preview.png"
    spec = VARIANTS[vname]
    if '--variants' in flags:
        print(", ".join("%s%s" % (k, " (default)" if k == DEFAULT_VARIANT else "")
                        for k in VARIANTS))
        sys.exit(0)
    if '--check' in flags:
        problems = check(spec)
        for p in problems:
            print("FAIL  %s" % p)
        print("%s: geometry check %s" % (vname, "OK" if not problems else "%d problem(s)" % len(problems)))
        for n, r in extents(spec):
            print("   %-10s reach %.2f dp (safe %.1f)" % (n, r, SAFE_R))
        ox, oy, off = optical_center(spec)
        x0, y0, x1, y1 = bounds(spec)
        print("   ink box %.1f..%.1f x %.1f..%.1f, optical centre (%.2f, %.2f), %.2f dp high"
              % (x0, x1, y0, y1, ox, oy, CY - oy))
        sys.exit(1 if problems else 0)
    if '--xml' in flags:
        path = args[0] if args else out
        problems = check_xml(path, spec)
        for pr in problems:
            print("FAIL  %s" % pr)
        print("xml check %s: %s" % (path, "OK" if not problems else "%d problem(s)" % len(problems)))
        sys.exit(1 if problems else 0)
    if '--write' in flags:
        problems = check(spec)
        if problems:
            for p in problems:
                print("FAIL  %s" % p)
            print("refusing to write: the design fails its own geometry check")
            sys.exit(1)
        write_all(spec, vname, args[0] if args else "android/app/src/main/res")
        sys.exit(0)
    if '--paths' in flags:
        for nm, d, w, col, kind in build(spec):
            print("# %s (width %s, kind %s)\n  %s" % (nm, w, kind, d))
    if '--sheet' in flags:
        names = args or list(VARIANTS)
        print("wrote %s" % sheet(names, out))
    else:
        render_mark(432, spec).convert("RGB").save(out)
        print("wrote %s" % out)
