"""What does KiCad do to a STEP file's coordinates when it draws it?

Every package this repo writes has to be authored in the frame KiCad expects, so
this is answered by measurement, not assumption: hand KiCad a model whose file
frame is known exactly, and find where the body actually lands on the board.

Method - colour instead of geometry.  A footprint draws pads, Fab, courtyard and
silkscreen of its own, and those lines pollute any "everything that is not copper"
blob measurement (they did, badly, when probe_model.py first tried it).  So the
probe models are painted a colour nothing else on the board has (saturated blue)
and only blue pixels are counted.  The board is a one-part probe board from
probe_model.emit, whose off-centre drilled copper marker pins the frame of the
render from either side - which the earlier surface-only marker could not do.

  python scripts/probe_frame.py          # part 1: the rule, and the library
  python scripts/probe_frame.py --place  # part 2: what OUR placements need

Part 1 (`f_up`/`f_dn`: one blue box standing on the board and hanging under it;
`f_up_b`/`f_dn_b` the same on the other side; `lib_hdr`: the stock KiCad 1x06 pin
header re-exported blue with no coordinate touched).  Each candidate rule -
identity, mirror in y, mirror in x, turn 180 - is scored by how much of the
rectangle it predicts is actually blue.

Part 2 is the one that decides the shipped files.  A footprint is emitted exactly
the way make_pcb stores it (layer, rotation, B.Cu child-flip, keep-pads or not),
the blue body is an L authored in the DESIGN frame - chiral, so a mirror can never
be mistaken for a rotation - and the answer is read straight off in design
millimetres.  What part 2 measures is the drawing rule D (a point in the file ->
where it draws) at every rotation on both sides.  model_frame.py turns that into
the frame each package file has to be written in by also accounting for how that
footprint's children are stored (mirror_y for a flipped B.Cu part, untouched
otherwise), because the body has to land on copper that moved by the same amount.
Every placement prints whether the measured D agrees with model_frame's rule.
"""
import math
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kicadfmt as kf                                      # noqa: E402
import make_3d_models as MM                                # noqa: E402
import probe_model as P                                    # noqa: E402
import model_frame as MF                                   # noqa: E402

HW, TMP = P.HW, P.TMP
BLUE = (0.05, 0.10, 0.95)
RULES = {
    "identity  (x,  y)": lambda x, y: (x, y),
    "mirror y  (x, -y)": lambda x, y: (x, -y),
    "mirror x  (-x, y)": lambda x, y: (-x, y),
    "turn 180  (-x,-y)": lambda x, y: (-x, -y),
}


def _rgb(png):
    return np.array(Image.open(png).convert("RGB")).astype(int)


def _blue(png):
    a = _rgb(png)
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    return (B > 110) & (B - R > 55) & (B - G > 45)


def _frame(png):
    """Green body -> px/mm and offset; the drilled marker -> which way the view
    faces, which is what makes the same measurement readable from either side."""
    a = _rgb(png)
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    green = (G > R + 8) & (G > B + 8)
    copper = (R - B > 15) & (G - B > 10) & (R > 50) & (B < 130)
    ys, xs = np.nonzero(green)
    bx0, bx1, by0, by1 = xs.min(), xs.max(), ys.min(), ys.max()
    sx = (bx1 - bx0 + 1) / (P.BX1 - P.BX0)
    sy = (by1 - by0 + 1) / (P.BY1 - P.BY0)
    best = None
    for fx in (False, True):
        for fy in (False, True):
            d = min(math.hypot(((P.BX1 - (0.5 * (x0 + x1) - bx0) / sx) if fx
                                else (P.BX0 + (0.5 * (x0 + x1) - bx0) / sx)) - P.MARK_X,
                               ((P.BY1 - (0.5 * (y0 + y1) - by0) / sy) if fy
                                else (P.BY0 + (0.5 * (y0 + y1) - by0) / sy)) - P.MARK_Y)
                    for _, x0, x1, y0, y1 in P._blobs(copper, 25))
            if best is None or d < best[0]:
                best = (d, fx, fy)
    return best, (bx0, bx1, by0, by1, sx, sy)


def _d2p(fr, x, y):
    """design mm -> pixel of the render"""
    (_, fx, fy), (bx0, bx1, by0, by1, sx, sy) = fr
    u = (bx1 - (x - P.BX0) * sx) if fx else (bx0 + (x - P.BX0) * sx)
    v = (by1 - (y - P.BY0) * sy) if fy else (by0 + (y - P.BY0) * sy)
    return u, v


def _p2d(fr, u, v):
    """pixel -> design mm, the exact inverse of _d2p"""
    (_, fx, fy), (bx0, bx1, by0, by1, sx, sy) = fr
    x = (P.BX0 + (bx1 - u) / sx) if fx else (P.BX0 + (u - bx0) / sx)
    y = (P.BY0 + (by1 - v) / sy) if fy else (P.BY0 + (v - by0) / sy)
    return x, y


def _rot(x, y, deg):
    r = math.radians(deg)
    return x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)


def _bbox_at(at, deg, box):
    xs, ys = [], []
    for cx in (box[0][0], box[1][0]):
        for cy in (box[0][1], box[1][1]):
            px, py = _rot(cx, cy, deg)
            xs.append(at[0] + px)
            ys.append(at[1] + py)
    return (min(xs), max(xs), min(ys), max(ys))


def synth(name, build):
    """Write one probe model in the raw file frame - no flip at all."""
    os.makedirs(TMP, exist_ok=True)
    m = MM.Model(name)
    build(m)
    m.write(os.path.join(TMP, name + ".step"))
    bb = m.bbox()
    print("   file frame: x %6.2f..%6.2f  y %6.2f..%6.2f  z %6.2f..%6.2f"
          % (bb[0][0], bb[1][0], bb[0][1], bb[1][1], bb[0][2], bb[1][2]))
    return bb


def lib_blue(name, rel):
    from OCP.STEPControl import STEPControl_Reader
    os.makedirs(TMP, exist_ok=True)
    rd = STEPControl_Reader()
    with MM.quiet_cout():
        if rd.ReadFile(os.path.join(kf.MODEL_DIR, rel)) != 1:
            print("   cannot read", rel)
            return None
        rd.TransferRoots()
        sh = rd.OneShape()
    m = MM.Model(name)
    m.parts = [(sh, BLUE)]
    m.write(os.path.join(TMP, name + ".step"))
    bb = m.bbox()
    print("   file frame: x %6.2f..%6.2f  y %6.2f..%6.2f  z %6.2f..%6.2f"
          % (bb[0][0], bb[1][0], bb[0][1], bb[1][1], bb[0][2], bb[1][2]))
    return bb


def render(board, side, png):
    r = subprocess.run([P.KCLI, "pcb", "render", "--side", side, "--width", "1200",
                        "--height", "800", "--quality", "high", "-o", png, board],
                       capture_output=True, text=True)
    if r.returncode:
        print("   render failed:", (r.stderr or "")[-300:])
    return None if r.returncode else png


def stored(board):
    """The placement the footprint under test really carries: (x, y, rot).

    probe_model.emit puts the part under test in as reference "FP" and an
    off-centre copper marker in as "MK"; the marker is emitted first, and reading
    its placement instead of the part's is how a probe once reported "rot 0" for
    every case.  So walk the footprint blocks properly and take the (at ...) that
    belongs to FP - a footprint's own placement, not the placement the generator
    asked for, which is not always what ends up stored (a B.Cu part that keeps
    its copper where it was gets its angle re-picked on the way out).
    """
    text = open(board, encoding="utf-8").read()
    i = 0
    while True:
        i = text.find('(footprint "', i + 1)
        if i < 0:
            return None
        blk = text[i:kf._skip_sexp(text, i)]
        if '(property "Reference" "FP"' not in blk:
            continue
        a = re.search(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", blk)
        if a:
            return (float(a.group(1)), float(a.group(2)), float(a.group(3) or 0))


CASES = [
    ("f_up", False, lambda m: m.box(4, 6, 2, 4, 0, 3, BLUE),
     "one box authored x 4..6, y 2..4, z 0..3 (standing on the board)"),
    ("f_dn", False, lambda m: m.box(4, 6, 2, 4, -3, 0, BLUE),
     "same box at z -3..0 (hanging under the board)"),
    ("f_up_b", True, lambda m: m.box(4, 6, 2, 4, 0, 3, BLUE),
     "same box, footprint on B.Cu, z 0..3"),
    ("f_dn_b", True, lambda m: m.box(4, 6, 2, 4, -3, 0, BLUE),
     "same box, footprint on B.Cu, z -3..0"),
]
LIB = [("lib_hdr", "Connector_PinHeader_2.54mm.3dshapes/PinHeader_1x06_P2.54mm_Vertical.step",
        "stock KiCad 1x06 pin header, re-exported blue, coordinates untouched")]

# Part 2: the layer, rotation and storage each of our five packages really uses.
# J6, J9, J14, J17 (JST PH) and J1 (XT30) sit on B.Cu; F1 (the 521 clip) and
# OK1/OK2 (SO-4 opto) sit on F.Cu.
PLACEMENTS = [
    # name,     footprint, at,        rot, layer,  bcu_flip, keep_pads
    ("j_b0",    P.JST4, (40, 30),   0, "B.Cu", True,  False),   # like J6/J9/J14/J17
    ("j_b90",   P.JST4, (40, 30),  90, "B.Cu", True,  False),
    ("j_b180",  P.JST4, (40, 30), 180, "B.Cu", True,  False),
    ("j_b270",  P.JST4, (40, 30), 270, "B.Cu", True,  False),
    ("x_b90",   P.XT,   (40, 30),  90, "B.Cu", True,  True),    # like J1, keeps pads
    ("x_b270",  P.XT,   (40, 30), 270, "B.Cu", True,  True),
    ("f_f0",    P.FUSE, (40, 30),   0, "F.Cu", False, False),   # like F1
    ("f_f90",   P.FUSE, (40, 30),  90, "F.Cu", False, False),
    ("f_f180",  P.FUSE, (40, 30), 180, "F.Cu", False, False),
    ("f_f270",  P.FUSE, (40, 30), 270, "F.Cu", False, False),
]


def design_L(m):
    """An L authored in the design frame: x 4..6, y 2..4 plus a foot at x 4..5,
    y 4..8.  Chiral, so whichever way KiCad turns it, the picture says which."""
    m.box(4, 6, 2, 4, 0, 3, BLUE)
    m.box(4, 5, 4, 8, 0, 3, BLUE)


def _rule_bbox(bbox, fn):
    """The axis box of a file bbox after one of RULES."""
    pts = [fn(cx, cy) for cx in (bbox[0][0], bbox[1][0])
           for cy in (bbox[0][1], bbox[1][1])]
    return [(min(p[0] for p in pts), min(p[1] for p in pts)),
            (max(p[0] for p in pts), max(p[1] for p in pts))]


def part1():
    print("Part 1: which rule does KiCad apply to the file's coordinates?")
    for name, back, build, note in CASES:
        print("\n== %s: %s" % (name, note))
        bbox = synth(name, build)
        P.emit(P.FUSE, (40, 30), 0, "B.Cu" if back else "F.Cu", P.FN_FUSE,
               model="${KIPRJMOD}/_probe/%s.step" % name, name=name)
        board = os.path.join(HW, "probe_%s.kicad_pcb" % name)
        for side in (("bottom", "top") if back else ("top", "bottom")):
            png = render(board, side, os.path.join(HW, "probe_%s_%s.png" % (name, side)))
            if not png:
                continue
            print("   --side %s (footprint on %s)" % (side, "B.Cu" if back else "F.Cu"))
            fr = _frame(png)
            (d, fx, fy), _ = fr
            mask = _blue(png)
            n = int(mask.sum())
            print("   frame: xflip=%d yflip=%d (marker off %.2f mm); blue pixels %d"
                  % (fx, fy, d, n))
            if d > 0.6 or not n:
                print("   cannot score (frame unknown or no blue visible)")
                continue
            ys, xs = np.nonzero(mask)
            ax, ay = _p2d(fr, xs.min(), ys.min())
            bx, by = _p2d(fr, xs.max(), ys.max())
            want = (min(ax, bx), max(ax, bx), min(ay, by), max(ay, by))
            print("   blue drawn at x %6.2f..%6.2f  y %6.2f..%6.2f" % want)
            for label, fn in RULES.items():
                pred = _bbox_at((40, 30), 0, _rule_bbox(bbox, fn))
                err = max(abs(pred[0] - want[0]), abs(pred[1] - want[1]),
                          abs(pred[2] - want[2]), abs(pred[3] - want[3]))
                print("      %-18s predicts x %6.2f..%6.2f  y %6.2f..%6.2f   off %.2f mm"
                      % (label, pred[0], pred[1], pred[2], pred[3], err))
    for name, rel, note in LIB:
        print("\n== %s: %s" % (name, note))
        bbox = lib_blue(name, rel)
        if not bbox:
            continue
        P.emit(P.HDR6, (40, 30), 0, "F.Cu", P.FN_HDR6,
               model="${KIPRJMOD}/_probe/%s.step" % name, name=name)
        png = render(os.path.join(HW, "probe_%s.kicad_pcb" % name), "top",
                     os.path.join(HW, "probe_%s.png" % name))
        if not png:
            continue
        fr = _frame(png)
        (d, fx, fy), _ = fr
        mask = _blue(png)
        print("   frame: xflip=%d yflip=%d (marker off %.2f mm); blue pixels %d"
              % (fx, fy, d, int(mask.sum())))
        if d > 0.6 or not int(mask.sum()):
            continue
        ys, xs = np.nonzero(mask)
        ax, ay = _p2d(fr, xs.min(), ys.min())
        bx, by = _p2d(fr, xs.max(), ys.max())
        print("   the file's own bbox is x %6.2f..%6.2f  y %6.2f..%6.2f"
              % (bbox[0][0], bbox[1][0], bbox[0][1], bbox[1][1]))
        print("   drawn at            x %6.2f..%6.2f  y %6.2f..%6.2f"
              % (min(ax, bx), max(ax, bx), min(ay, by), max(ay, by)))
        for label, fn in RULES.items():
            want = _bbox_at((40, 30), 0, _rule_bbox(bbox, fn))
            err = max(abs(want[0] - min(ax, bx)), abs(want[1] - max(ax, bx)),
                      abs(want[2] - min(ay, by)), abs(want[3] - max(ay, by)))
            print("      %-18s predicts x %6.2f..%6.2f  y %6.2f..%6.2f   off %.2f mm"
                  % (label, want[0], want[1], want[2], want[3], err))


def _cols(f):
    """2x2 matrix of a linear map: columns are the images of (1,0) and (0,1)."""
    c1, c2 = f((1.0, 0.0)), f((0.0, 1.0))
    return ((c1[0], c2[0]), (c1[1], c2[1]))


def part2():
    print("Part 2: where does a body authored in the DESIGN frame land?")
    print("        (a blue L: x 4..6 y 2..4 with a foot at x 4..5 y 4..8, written "
          "with no flip at all)")
    agree = []
    for name, fp_id, at, rot, layer, flip, keep in PLACEMENTS:
        print("\n== %s: %s at %s rot %g on %s (bcu_flip=%s keep_pads=%s)"
              % (name, fp_id.split(":")[-1], at, rot, layer, flip, keep))
        bbox = synth(name, design_L)
        P.emit(fp_id, at, rot, layer,
               P.FN_JST if "JST" in fp_id else (P.FN_XT if "AMASS" in fp_id else P.FN_FUSE),
               model="${KIPRJMOD}/_probe/%s.step" % name, name=name,
               bcu_flip=flip, bcu_keep_pads=keep)
        board = os.path.join(HW, "probe_%s.kicad_pcb" % name)
        sp = stored(board) or (at[0], at[1], rot)
        print("   stored placement: at (%.2f, %.2f) rot %g" % (sp[0], sp[1], sp[2]))
        png = render(board, "bottom" if layer == "B.Cu" else "top",
                     os.path.join(HW, "probe_%s.png" % name))
        if not png:
            continue
        fr = _frame(png)
        (d, fx, fy), _ = fr
        mask = _blue(png)
        print("   frame: xflip=%d yflip=%d (marker off %.2f mm); blue pixels %d"
              % (fx, fy, d, int(mask.sum())))
        if d > 0.6 or not int(mask.sum()):
            print("   cannot measure")
            continue
        ys, xs = np.nonzero(mask)
        ax, ay = _p2d(fr, xs.min(), ys.min())
        bx, by = _p2d(fr, xs.max(), ys.max())
        want = (min(ax, bx), max(ax, bx), min(ay, by), max(ay, by))
        print("   blue drawn at x %6.2f..%6.2f  y %6.2f..%6.2f" % want)
        scored = []
        for label, fn in RULES.items():
            pred = _bbox_at(sp, sp[2], _rule_bbox(bbox, fn))
            err = max(abs(pred[0] - want[0]), abs(pred[1] - want[1]),
                      abs(pred[2] - want[2]), abs(pred[3] - want[3]))
            print("      %-18s predicts x %6.2f..%6.2f  y %6.2f..%6.2f   off %.2f mm"
                  % (label, pred[0], pred[1], pred[2], pred[3], err))
            scored.append((err, label, fn))
        scored.sort(key=lambda s: s[0])
        err, label, fn = scored[0]
        # the drawn body is R(rot) . rule(file): that composition IS the drawing
        # rule for this side, and model_frame has to agree with it
        meas = _cols(lambda b: _rot(*fn(*b), sp[2]))
        mine = _cols(lambda b: MF.drawn_from_file(layer, sp[2], *b))
        ok = err < 0.6 and all(abs(meas[r][c] - mine[r][c]) < 0.02
                               for r in range(2) for c in range(2))
        print("   VERDICT: the body draws as %s of the file, then rotated "
              "(off %.2f mm; next best %s at %.2f mm)"
              % (label, err, scored[1][1], scored[1][0])
              if scored[1][0] - err > 1.0 else
              "   VERDICT: the body draws as %s of the file (off %.2f mm), but no "
              "other rule separates from it (%s at %.2f mm)"
              % (label, err, scored[1][1], scored[1][0]))
        print("            that rule is %s; model_frame's rule for %s rot %g is %s -> %s"
              % (MF.describe(meas), layer, sp[2], MF.describe(mine),
                 "MATCHES" if ok else "DOES NOT MATCH"))
        print("            so a body authored in the library frame has to be written "
              "%s at this placement" % MF.describe(
                  MF.required_matrix(layer, sp[2], flip and not keep)))
        agree.append(ok)
    if not all(agree):
        raise SystemExit("model_frame's rule disagrees with the measurement - "
                         "fix the table before any package is rebuilt")
    print("\nall %d placements agree with model_frame: F.Cu draws M_y.R(rot), "
          "B.Cu draws R(-rot)." % len(agree))


if __name__ == "__main__":
    if "--place" in sys.argv:
        part2()
    else:
        part1()
