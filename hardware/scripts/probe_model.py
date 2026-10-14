"""Where does a 3D model actually land relative to its own pads?

The board's five "KiCad does not ship this package" footprints carry STEP files
this repo writes itself, so every one of them has to be checked against its own
copper instead of trusted.  This probe answers the question the eye cannot: it
emits a one-part board through exactly the path make_pcb.py uses, renders it with
kicad-cli, then measures the picture back:

  * the green board body fixes the pixel -> design scale and offset;
  * one thick off-centre copper track (marker) fixes the *frame* - a plain
    rectangle cannot tell a view from its mirror image, and that ambiguity is
    exactly what makes a model look "5 mm off" or not;
  * the copper pads confirm the footprint landed where it was placed;
  * every other visible blob is the 3D model.

WHAT THIS ESTABLISHED (measured 2026-10-14, cases `ph_official` / `ph_roundtrip`
below, plus probe_z.py):

  * KiCad draws a STEP model at **(x, -y, z) of the file**, and a solid authored
    below the seating plane stays below it (bottom view only).  This script alone
    cannot prove it - a footprint's own Fab/courtyard/silkscreen lines land in the
    same "not copper" bucket as the body, which badly polluted the first blob
    measurements - so probe_frame.py repeats the test on a colour-coded model and
    scores every candidate rule: 100% mirror-y, 0% identity / mirror-x / turn-180.
  * The official KiCad 10 packages are stored in that same y-negated frame
    (probe_frame.py re-exports the stock 1x06 pin header with its coordinates
    untouched: file y -13.97..1.27, drawn at y -1.27..13.97), so models written
    here have to match it, which `make_3d_models.Model.write()` now does once on
    the way out.  Before that, every model this repo wrote was drawn mirrored in
    y: the 521 fuse clip sat on the far side of its own courtyard (6.16 mm of y),
    and the four JST PH headers rendered with their cable entry facing into the
    board.
  * Whether a rendered offset is a *shift* or a *mirror* is not answerable from a
    symmetric body alone - probe_z.py separates a mirror from a turn by occlusion.

  python scripts/probe_model.py                  # every case in CASES
  python scripts/probe_model.py fuse_rot90 ...   # selected cases
  python scripts/probe_model.py --synth          # synthetic box models too

Boards/PNGs are written as probe_* in hardware/ (not in _probe/) because the
model links say ${KIPRJMOD}/footprints/dpp.3dshapes/... and KIPRJMOD is the
folder the board file sits in.
"""
import math
import os
import re
import subprocess
import sys
from collections import deque

import numpy as np
from PIL import Image

import kicadfmt as kf
import pcbheader

KCLI = r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli"
HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(HW, "_probe")

FUSE = "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal"
JST4 = "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal"
XT = "Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical"

# probe board outline, design mm
BX0, BX1, BY0, BY1 = 15.0, 65.0, 10.0, 50.0
# a filled copper square, deliberately off-centre: it is what pins the frame of
# the render (a rectangle alone cannot tell a view from its mirror image, and
# that ambiguity is exactly what makes a body look "5 mm off" or not)
MARK_X, MARK_Y, MARK_R = 20.0, 15.0, 1.0

FN_FUSE = {"1": (1, "A"), "2": (2, "GND")}
FN_JST = {"1": (1, "A"), "2": (2, "B"), "3": (3, "C"), "4": (4, "D"), "MP": (5, "GND")}
FN_XT = {"1": (1, "A"), "2": (2, "GND")}

HDR6 = "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical"
FN_HDR6 = {str(i): (i, "A") for i in range(1, 7)}

CASES = {
    "fuse_rot0":   (FUSE, (40, 30), 0,   "F.Cu", FN_FUSE, None),
    "fuse_rot90":  (FUSE, (40, 30), 90,  "F.Cu", FN_FUSE, None),
    "fuse_rot180": (FUSE, (40, 30), 180, "F.Cu", FN_FUSE, None),
    "fuse_rot270": (FUSE, (40, 30), 270, "F.Cu", FN_FUSE, None),
    "jst_fcu":     (JST4, (40, 30), 0,   "F.Cu", FN_JST, None),
    "jst_bcu":     (JST4, (40, 30), 0,   "B.Cu", FN_JST, None),
    "xt30":        (XT,   (40, 30), 0,   "F.Cu", FN_XT, None),
    # an official package, drawn straight from the install - the reference that
    # tells this repo's files which frame they have to be written in
    "hdr_official": (HDR6, (40, 30), 0,  "F.Cu", FN_HDR6, None),
}


def emit(fp_id, at, rot, layer, netmap, model=None, name=None, bcu_flip=None,
         bcu_keep_pads=False):
    """Write a one-part probe board and render it; return the PNG path.

    The board is 50 x 40 mm with the footprint under test at `at`/`rot` and one
    off-centre copper square (MARK_X/MARK_Y) whose whole job is to pin the frame
    of the picture: a rectangle alone cannot tell a view from its mirror image,
    and that ambiguity is what makes a body look "5 mm off" or not.  `model`
    replaces the footprint's own 3D link - that is how a synthetic or re-exported
    package gets in front of KiCad.  Library footprints come in through
    kicadfmt.get_module, which already rewrites a dangling official link to this
    repo's dpp.3dshapes copy, so the probe boards show the same geometry the real
    board draws.
    """
    os.makedirs(TMP, exist_ok=True)
    nets = sorted({v[1] for v in netmap.values()} | {"GND"})
    out = [pcbheader.header([""] + nets)]
    for a, b in (((BX0, BY0), (BX1, BY0)), ((BX1, BY0), (BX1, BY1)),
                 ((BX1, BY1), (BX0, BY1)), ((BX0, BY1), (BX0, BY0))):
        out.append('(gr_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid))'
                   ' (layer "Edge.Cuts") (uuid "%s"))' % (a[0], a[1], b[0], b[1], kf.uid()))
    gnet = (nets.index("GND") + 1, "GND")
    out.append(kf.custom_footprint("MK", "marker", (MARK_X, MARK_Y), 0,
                                   [("1", 0, 0, 2 * MARK_R, 2 * MARK_R, 0.8, gnet)],
                                   courtyard=False))
    flip = (layer == "B.Cu") if bcu_flip is None else bcu_flip
    fp = kf.board_footprint(fp_id, at, rot, "FP", "probe", netmap,
                            layer=layer, bcu_flip=flip, bcu_keep_pads=bcu_keep_pads)
    if model:
        if '(model "' not in fp:
            print("   %s has no 3D model to override" % fp_id)
        else:
            fp = re.sub('\(model "[^"]*"', '(model "%s"' % model.replace("\\", "/"),
                        fp, count=1)
    out.append(fp)
    board = os.path.join(HW, "probe_%s.kicad_pcb" % (name or fp_id))
    with open(board, "w", encoding="utf-8") as fh:
        fh.write("\n".join(out + [")"]) + "\n")
    png = os.path.join(HW, "probe_%s.png" % (name or fp_id))
    r = subprocess.run([KCLI, "pcb", "render", "--side", "bottom" if layer == "B.Cu" else "top",
                        "--width", "1200", "--height", "800", "--quality", "high",
                        "-o", png, board], capture_output=True, text=True)
    if r.returncode:
        print("render failed for %s:\n%s" % (board, (r.stderr or "").strip()[-600:]))
        return None
    return png


def _blobs(mask, minn=25):
    H, W = mask.shape
    lab = np.zeros(mask.shape, int)
    out, cur = [], 0
    for y in range(H):
        for x in range(W):
            if mask[y, x] and lab[y, x] == 0:
                cur += 1
                q = deque([(y, x)])
                lab[y, x] = cur
                pts = []
                while q:
                    p = q.popleft()
                    pts.append(p)
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = p[0] + dy, p[1] + dx
                            if 0 <= ny < H and 0 <= nx < W and mask[ny, nx] and lab[ny, nx] == 0:
                                lab[ny, nx] = cur
                                q.append((ny, nx))
                if len(pts) >= minn:
                    xa = np.array([p[1] for p in pts])
                    ya = np.array([p[0] for p in pts])
                    out.append((len(pts), xa.min(), xa.max(), ya.min(), ya.max()))
    return out


def measure(png):
    a = np.array(Image.open(png).convert("RGB")).astype(int)
    H, W, _ = a.shape
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    copper = (R - B > 15) & (G - B > 10) & (R > 50) & (B < 130)
    green = (G > R + 8) & (G > B + 8)
    dark = (R < 40) & (G < 40) & (B < 40)
    ys, xs = np.nonzero(green)
    bx0, bx1, by0, by1 = xs.min(), xs.max(), ys.min(), ys.max()
    sx = (bx1 - bx0 + 1) / (BX1 - BX0)
    sy = (by1 - by0 + 1) / (BY1 - BY0)
    return (sx, sy), (bx0, bx1, by0, by1), _blobs(copper), _blobs(~(copper | green | dark))


def to_local(dx, dy, at, rot, back):
    """design point -> the footprint library's own frame (undo placement)."""
    dx, dy = dx - at[0], dy - at[1]
    th = math.radians(-rot)
    c, s = math.cos(th), math.sin(th)
    x, y = dx * c - dy * s, dx * s + dy * c
    return (x, -y) if back else (x, y)


def report(name, fp_id, at, rot, layer, png):
    (sx, sy), (bx0, bx1, by0, by1), copper, other = measure(png)
    back = layer == "B.Cu"
    print("\n== %s: %s at (%g,%g) rot %g on %s   px/mm %.3f,%.3f" %
          (name, fp_id.split(":")[-1], at[0], at[1], rot, layer, sx, sy))
    if abs(sx - sy) > 0.02 * min(sx, sy):
        print("   outline fit is not isotropic (%.3f vs %.3f) - outline misread"
              % (sx, sy))
    # which of the four pixel->design frames puts the copper marker where it was
    # drawn?  only one of them can, because the marker is off-centre
    frames = []
    for fx in (False, True):
        for fy in (False, True):
            def p2d(u, v, fx=fx, fy=fy):
                return ((BX1 - (u - bx0) / sx) if fx else (BX0 + (u - bx0) / sx),
                        (BY1 - (v - by0) / sy) if fy else (BY0 + (v - by0) / sy))
            best = min((math.hypot(*[a - b for a, b in zip(p2d(0.5*(x0+x1), 0.5*(y0+y1)),
                                                            (MARK_X, MARK_Y))])
                        for _, x0, x1, y0, y1 in copper), default=99.0)
            frames.append((best, fx, fy))
    frames.sort()
    if frames[0][0] > 0.5 or frames[1][0] < 0.5:
        print("   marker frame undetermined (best %.2f mm, next %.2f mm)"
              % (frames[0][0], frames[1][0]))
        return
    _, fx, fy = frames[0]
    print("   marker pins the frame: xflip=%d yflip=%d (off by %.2f mm)"
          % (fx, fy, frames[0][0]))

    def p2d(u, v):
        return ((BX1 - (u - bx0) / sx) if fx else (BX0 + (u - bx0) / sx),
                (BY1 - (v - by0) / sy) if fy else (BY0 + (v - by0) / sy))

    pts = [p2d(0.5 * (x0 + x1), 0.5 * (y0 + y1)) for _, x0, x1, y0, y1 in copper]
    mk = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - MARK_X, pts[i][1] - MARK_Y))
    got = [p for i, p in enumerate(pts) if i != mk]     # drop the marker itself
    err = []
    for num, px, py, w, h, dr in kf.pads_of(fp_id):
        th = math.radians(rot)
        c, s = math.cos(th), math.sin(th)
        yy = -py if back else py
        ex, ey = at[0] + px * c - yy * s, at[1] + px * s + yy * c
        if got:
            g = min(got, key=lambda q: (q[0] - ex) ** 2 + (q[1] - ey) ** 2)
            err.append(math.hypot(g[0] - ex, g[1] - ey))
    print("   copper: %d blobs (one is the marker), %d pads expected, max centre error %.2f mm"
          % (len(copper), len(err), max(err) if err else -1))
    print("   non-copper blobs, in the footprint's own frame:")
    for n, x0, x1, y0, y1 in other:
        lx0, ly0 = to_local(*p2d(x0, y1), at, rot, back)
        lx1, ly1 = to_local(*p2d(x1, y0), at, rot, back)
        lo, hi = min(lx0, lx1), max(lx0, lx1)
        lo2, hi2 = min(ly0, ly1), max(ly0, ly1)
        print("     n=%5d  x %6.2f..%6.2f   y %6.2f..%6.2f   size %5.2f x %5.2f"
              % (n, lo, hi, lo2, hi2, hi - lo, hi2 - lo2))


def synth_steps():
    """Two synthetic packages: the same four boxes, one as a single root and one
    as four free products - to see whether the STEP assembly structure matters."""
    import make_3d_models as MM
    boxes = [(-2, 0, -1, 1), (8, 10, -1, 1), (17, 19, -1, 1), (8, 10, 3, 5)]
    for name, compound in (("t_single", True), ("t_multi", False)):
        m = MM.Model(name)
        for (x0, x1, y0, y1) in boxes:
            m.box(x0, x1, y0, y1, 0, 1, MM.METAL_TIN)
        if compound:
            from OCP.BRep import BRep_Builder
            from OCP.TopoDS import TopoDS_Compound
            b = BRep_Builder()
            comp = TopoDS_Compound()
            b.MakeCompound(comp)
            for sh, _ in m.parts:
                b.Add(comp, sh)
            m = MM.Model(name)
            m.parts = [(comp, MM.METAL_TIN)]
        m.write(os.path.join(TMP, name + ".step"))
    print("wrote _probe/t_single.step and _probe/t_multi.step")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:]]
    if "--synth" in args:
        args.remove("--synth")
        synth_steps()
        for name in ("t_single", "t_multi"):
            png = emit(FUSE, (40, 30), 0, "F.Cu", FN_FUSE,
                       model="${KIPRJMOD}/_probe/%s.step" % name, name=name)
            if png:
                report(name, FUSE, (40, 30), 0, "F.Cu", png)
    for name in (args or CASES):
        fp_id, at, rot, layer, nets, model = CASES[name]
        png = emit(fp_id, at, rot, layer, nets, model, name=name)
        if png:
            report(name, fp_id, at, rot, layer, png)
