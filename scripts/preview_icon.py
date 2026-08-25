# Preview renderer for the adaptive icon foreground (108x108 dp vector).
# Draws the same geometry as the XML vector to a PNG so the design can be
# eyeballed before committing to the APK.
import math
from PIL import Image, ImageDraw

SCALE = 4  # px per dp
BG = (5, 8, 13)       # hud_bg
CYAN = (34, 211, 238) # hud_cyan
AMBER = (255, 176, 32)
DIM = (14, 92, 102)   # hud_cyan_dim

def pt(cx, cy, r, phi_deg):
    """Point on circle: angle measured clockwise from 12 o'clock."""
    phi = math.radians(phi_deg)
    return (cx + r * math.sin(phi), cy - r * math.cos(phi))

def draw_stroke(draw, p1, p2, width, color, radius=0):
    w = width * SCALE
    draw.line([p1, p2], fill=color, width=w)
    if radius:
        r = (width / 2) * SCALE
        for (px, py) in (p1, p2):
            draw.ellipse([px - r, py - r, px + r, py + r], fill=color)

def gauge_arc_points(cx, cy, r, phi_start, phi_end, steps=120):
    pts = []
    n = steps
    for i in range(n + 1):
        t = i / n
        phi = phi_start + (phi_end - phi_start) * t
        pts.append(pt(cx, cy, r, phi))
    return pts

def draw_arc(draw, cx, cy, r, phi_start, phi_end, width, color, steps=120):
    pts = gauge_arc_points(cx, cy, r, phi_start, phi_end, steps)
    w = width * SCALE
    # draw as polyline segments with round caps
    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i + 1]], fill=color, width=w)
    r_cap = (width / 2) * SCALE
    for p in (pts[0], pts[-1]):
        px, py = p[0] * SCALE, p[1] * SCALE
        draw.ellipse([px - r_cap, py - r_cap, px + r_cap, py + r_cap], fill=color)

def draw_tick(draw, cx, cy, r_in, r_out, phi, width, color):
    p1 = (cx, cy)
    a = pt(cx, cy, r_in, phi)
    b = pt(cx, cy, r_out, phi)
    draw_stroke(draw, a, b, width, color)

def render(name, cx, cy, needle_phi, ticks=(), extra=None):
    s = SCALE
    img = Image.new("RGB", (108 * s, 108 * s), BG)
    d = ImageDraw.Draw(img)
    # visible-area circle guide (72dp diameter)
    cxp, cyp, rvis = (54 * s, 54 * s, 36 * s)
    d.ellipse([cxp - rvis, cyp - rvis, cxp + rvis, cyp + rvis], outline=(40, 55, 66), width=1)

    # gauge arc: 240-degree sweep, gap at bottom
    draw_arc(d, 54 * s, 54 * s, 22 * s, -120, 120, 3, CYAN)
    # ticks
    for phi in ticks:
        draw_tick(d, 54 * s, 54 * s, 16 * s, 19.5 * s, phi, 2, CYAN)
    # needle
    tip = pt(54 * s, 54 * s, 17 * s, needle_phi)
    draw_stroke(d, (54 * s, 54 * s), tip, 3, CYAN)
    # hub
    hr = 3.5 * s
    d.ellipse([54 * s - hr, 54 * s - hr, 54 * s + hr, 54 * s + hr], fill=CYAN)
    if extra:
        extra(img)
    img.save(f"scripts/preview_{name}.png")
    print(f"scripts/preview_{name}.png")

# Variant A: pure minimal — arc + needle + hub
render("a", 54, 54, 60)
# Variant B: add 9 / 12 / 3 o'clock scale ticks
render("b", 54, 54, 60, ticks=(0,))
# Variant C: needle straight up (gauge at zero) + top tick
render("c", 54, 54, 0)
# Variant D: old icon for reference (as currently shipped)
def old_icon(img):
    pass
img = Image.new("RGB", (108 * SCALE, 108 * SCALE), BG)
d = ImageDraw.Draw(img)
cxp, cyp, rvis = (54 * SCALE, 54 * SCALE, 36 * SCALE)
d.ellipse([cxp - rvis, cyp - rvis, cxp + rvis, cyp + rvis], outline=(40, 55, 66), width=1)
# old arc: center (54,50) r=20, (34,50) to (64,32.68) through top (240 deg)
draw_arc(d, 54 * SCALE, 50 * SCALE, 20 * SCALE, -150, 30, 3, CYAN)
# old ticks (from XML)
for (x1, y1, x2, y2) in [
    (34, 50, 36.5, 50),
    (41, 67.32, 43.06, 65.56),
    (67, 67.32, 64.94, 65.56),
    (74, 50, 71.5, 50),
    (64, 32.68, 62.94, 34.44),
]:
    draw_stroke(d, (x1 * SCALE, y1 * SCALE), (x2 * SCALE, y2 * SCALE), 2, CYAN)
# old needle: (54,50) -> (70,50)
draw_stroke(d, (54 * SCALE, 50 * SCALE), (70 * SCALE, 50 * SCALE), 3, CYAN)
hr = 3.5 * SCALE
d.ellipse([54 * SCALE - hr, 50 * SCALE - hr, 54 * SCALE + hr, 50 * SCALE + hr], fill=CYAN)
img.save("scripts/preview_old.png")
print("scripts/preview_old.png")
