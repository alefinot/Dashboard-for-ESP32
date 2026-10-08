"""Top-view placement map of the carrier board (PNG, drawn with PIL).

  python placementmap.py [board] [out.png]

Shows the board outline with a mm grid, every part's courtyard + reference,
its pads, and the ratsnest of each net drawn as the minimum spanning tree of
its pads - i.e. how much wire each net costs from where the parts sit today.
That is the picture you want when deciding where parts should move.
"""
import math
import os
import re
import sys
from collections import defaultdict

from PIL import Image, ImageDraw, ImageFont

import kicadfmt as kf
import make_pcb as mp

BOARD = sys.argv[1] if len(sys.argv) > 1 else mp.OUT
OUTPNG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(mp.OUT)), "out", "placement-map.png")

SCALE = 14                 # px per mm
MARGIN = 46                # px reserved for the grid labels
HUE_NETS = 12              # distinct rat colours


# ------------------------------------------------------------------ parsing
def tf(fx, fy, rot_deg, px, py):
    r = math.radians(-rot_deg)          # KiCad angles are CW, Y is down
    return (fx + px * math.cos(r) - py * math.sin(r),
            fy + px * math.sin(r) + py * math.cos(r))


def parse(path):
    txt = open(path, encoding="utf-8").read()
    outline = [tuple(map(float, g)) for g in re.findall(
        r'\(gr_line \(start ([-\d.]+) ([-\d.]+)\) \(end ([-\d.]+) ([-\d.]+)\)'
        r'[^\n]*"Edge.Cuts"', txt)]
    parts = []
    i = 0
    while True:
        m = re.compile(r'\(footprint "([^"]+)"').search(txt, i)
        if not m:
            break
        j = kf._skip_sexp(txt, m.start())
        blk = txt[m.start():j]
        i = j
        fx, fy, rot = [float(v) for v in re.search(
            r'\t\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', blk).groups()]
        layer = re.search(r'\t\(layer "([^"]+)"\)', blk).group(1)
        ref = re.search(r'\(property "Reference" "([^"]*)"', blk)
        p = {"ref": ref.group(1) if ref else "?", "x": fx, "y": fy,
             "rot": rot, "layer": layer, "pads": [], "crt": []}
        body = blk[blk.index("\n"):len(blk) - 1]
        for g in kf._children(body):
            t = g.lstrip()
            if t.startswith("(pad"):
                num = re.search(r'\(pad "([^"]*)"', g).group(1)
                px, py = [float(v) for v in re.search(
                    r'\(at ([-\d.]+) ([-\d.]+)(?: [-\d.]+)?\)',
                    g).groups()[0:2]]
                x, y = tf(fx, fy, rot, px, py)
                nm = re.search(r'\(net (\d+) "([^"]*)"\)', g)
                shp = "round" if "round" in g.split()[:5] else "rect"
                p["pads"].append((x, y, nm.group(2) if nm else "", shp))
            elif "CrtYd" in g:
                for a in re.findall(r'\(start ([-\d.]+) ([-\d.]+)\)', g):
                    p["crt"].append(tf(fx, fy, rot, float(a[0]), float(a[1])))
                for a in re.findall(r'\(end ([-\d.]+) ([-\d.]+)\)', g):
                    p["crt"].append(tf(fx, fy, rot, float(a[0]), float(a[1])))
                for a in re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', g):
                    p["crt"].append(tf(fx, fy, rot, float(a[0]), float(a[1])))
        parts.append(p)
    return outline, parts


# ------------------------------------------------------------------ geometry
def mst_edges(pts):
    """Minimum spanning tree, returned as (a, b) index pairs (Prim)."""
    if len(pts) < 2:
        return []
    seen, rest, edges = [0], list(range(1, len(pts))), []
    while rest:
        d, a, b = min((math.dist(pts[u], pts[v]), u, v)
                      for u in seen for v in rest)
        edges.append((a, b))
        seen.append(b)
        rest.remove(b)
    return edges


def colour(idx):
    """Evenly spaced hues, reasonably readable on white."""
    import colorsys
    h = (idx * 1.0 / HUE_NETS) % 1.0
    r, g, b = colorsys.hsv_to_rgb(h, 0.85, 0.75)
    return (int(r * 255), int(g * 255), int(b * 255))


# --------------------------------------------------------------------- draw
def main():
    outline, parts = parse(BOARD)
    xs = [q[0] for q in outline] + [q[2] for q in outline]
    ys = [q[1] for q in outline] + [q[3] for q in outline]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    W = int((x1 - x0) * SCALE) + 2 * MARGIN + 260
    H = int((y1 - y0) * SCALE) + 2 * MARGIN
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype("arial.ttf", 13)
    fb = ImageFont.truetype("arial.ttf", 15)

    def P(x, y):
        return (int(round(MARGIN + (x - x0) * SCALE)),
                int(round(MARGIN + (y - y0) * SCALE)))

    # grid
    for gx in range(int(x0) - int(x0) % 10, int(x1) + 1, 10):
        d.line([P(gx, y0 - 2), P(gx, y1 + 2)], fill=(225, 225, 225))
        d.text((P(gx, y0)[0] - 6, MARGIN - 20), str(gx), fill=(120, 120, 120),
               font=f)
    for gy in range(int(y0) - int(y0) % 10, int(y1) + 1, 10):
        d.line([P(x0 - 2, gy), P(x1 + 2, gy)], fill=(225, 225, 225))
        d.text((MARGIN - 32, P(x0, gy)[1] - 7), str(gy), fill=(120, 120, 120),
               font=f)
    # DevKit body hanging under the carrier, and the antenna keep-out
    ex0, ey0, ex1, ey1 = mp.ESP_BODY
    d.rectangle([P(ex0, ey0), P(ex1, ey1)], outline=(120, 0, 160), width=2)
    d.text(P((ex0 + ex1) / 2, ey1 + 1.6), "DevKit body (hangs under)",
           fill=(120, 0, 160), font=f, anchor="mm")
    kx0, ky0, kx1, ky1 = mp.KEEP
    ka, kb = P(kx0, ky0), P(kx1, ky1)
    hatch = Image.new("RGB", (kb[0] - ka[0], kb[1] - ka[1]), "white")
    hd = ImageDraw.Draw(hatch)
    for t in range(-kb[1], kb[0] + kb[1], 9):
        hd.line([(t, 0), (t - kb[1], kb[1])], fill=(255, 205, 205))
    img.paste(hatch, ka)
    d = ImageDraw.Draw(img)
    d.rectangle([ka, kb], outline=(220, 0, 0), width=2)
    d.text(((ka[0] + kb[0]) // 2, (ka[1] + kb[1]) // 2),
           "antenna keep-out\n(no copper)", fill=(200, 0, 0), font=f,
           anchor="mm")
    # board outline
    d.line([P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1), P(x0, y0)],
           fill=(0, 0, 0), width=2)

    # rats first, so parts sit on top
    bynet = defaultdict(list)
    for p in parts:
        for (x, y, net, _s) in p["pads"]:
            if net:
                bynet[net].append((x, y))
    ranked = sorted(bynet, key=lambda n: -sum(
        math.dist(bynet[n][a], bynet[n][b]) for a, b in mst_edges(bynet[n])))
    for k, net in enumerate(ranked):
        pts = bynet[net]
        c = colour(k)
        for a, b in mst_edges(pts):
            d.line([P(*pts[a]), P(*pts[b])], fill=c, width=1)

    # parts
    for p in parts:
        pts = p["crt"]
        bottom = p["layer"] == "B.Cu"
        if len(pts) >= 2:
            cx0 = min(q[0] for q in pts)
            cy0 = min(q[1] for q in pts)
            cx1 = max(q[0] for q in pts)
            cy1 = max(q[1] for q in pts)
            d.rectangle([P(cx0, cy0), P(cx1, cy1)],
                        fill=(245, 235, 225) if bottom else (235, 240, 250),
                        outline=(150, 90, 0) if bottom else (40, 80, 160))
        for (x, y, _net, shp) in p["pads"]:
            r = 2.2
            if shp == "round":
                d.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=(90, 90, 90))
            else:
                d.rectangle([P(x - r, y - r), P(x + r, y + r)],
                            fill=(90, 90, 90))
        cx = (min(q[0] for q in pts) + max(q[0] for q in pts)) / 2 if len(
            pts) >= 2 else p["x"]
        cy = (min(q[1] for q in pts) + max(q[1] for q in pts)) / 2 if len(
            pts) >= 2 else p["y"]
        d.text(P(cx, cy), p["ref"], fill=(0, 0, 0), font=fb, anchor="mm")

    # legend: nets ranked by wiring cost
    lx = MARGIN + (x1 - x0) * SCALE + 30
    d.text((lx, 12), "rats, most expensive first", fill=(0, 0, 0), font=fb)
    yy = 34
    for k, net in enumerate(ranked):
        c = colour(k)
        d.rectangle([lx, yy, lx + 14, yy + 10], fill=c)
        cost = sum(math.dist(bynet[net][a], bynet[net][b])
                   for a, b in mst_edges(bynet[net]))
        d.text((lx + 20, yy - 2), "%s  %.0f mm  (%d)" % (net, cost,
                                                          len(bynet[net])),
               fill=(0, 0, 0), font=f)
        yy += 17
    d.text((lx, yy + 6), "orange courtyard = bottom side", fill=(120, 60, 0),
           font=f)

    os.makedirs(os.path.dirname(OUTPNG), exist_ok=True)
    img.save(OUTPNG)
    print("wrote %s (%dx%d)" % (OUTPNG, W, H))


if __name__ == "__main__":
    main()
