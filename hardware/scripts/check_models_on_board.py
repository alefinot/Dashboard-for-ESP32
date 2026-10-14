#!/usr/bin/env python3
"""check_models_on_board.py - do the 3D bodies KiCad draws actually land on pads?

scripts/make_3d_models.py --check proves that a body sits on its own copper
*inside the .step file*, which still trusts model_frame's drawing rules for both
halves of the comparison.  This is the end-to-end version, done the one way that
cannot be talked into agreeing with itself: render the shipped board twice with
kicad-cli, once with the hand-made package links in place and once with them
stripped out, and measure what the viewer added.

    python scripts/check_models_on_board.py            # 4 renders, ~2 min

Both renders come from copies of the same board in hardware/ (so ${KIPRJMOD}
still finds footprints/dpp.3dshapes/); only the (model ...) blocks of the
footprints listed in model_frame.MODEL_REFS differ between them.  Everything the
second render draws that the first one does not is a 3D body: diff them, take the
pixels that moved by more than SHADOW (soft shadows and antialiasing stay well
under it - measured on this board), split them into blobs and ask KiCad's own
geometry where each blob is allowed to be: a body's silhouette must fall inside
its footprint's courtyard, which is drawn from the same placement and is therefore
immune to whatever sign convention the viewer uses for the model.

That is the test that was missing when F1 shipped wrong: a body written in the
wrong frame still passed every in-process check, and on the board its clips sat
17 mm from the pads they solder into, hanging off the south edge.

Prints, per part: the courtyard it should be inside, the box the viewer actually
drew, and how many pixels it drew.  Exits non-zero if a body shows up outside its
courtyard, if a body is missing entirely (a model link that quietly stopped
resolving draws nothing), or if the two renders are not registered to each other.
"""

import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import kicadfmt as kf                                        # noqa: E402
import make_3d_models as MM                                  # noqa: E402
import model_frame as MF                                     # noqa: E402
import probe_model as P                                      # noqa: E402

SRC = os.path.join(HW, "dashboard-plusplus-carrier.kicad_pcb")
BOARD_WITH = os.path.join(HW, "probe_onboard_models.kicad_pcb")
BOARD_WITHOUT = os.path.join(HW, "probe_onboard_nomodels.kicad_pcb")
WIDTH, HEIGHT = 1600, 1200
SHADOW = 150          # sum over channels; measured: AA/shadow changes < 60 here
MARGIN = 1.0          # mm of slack around the courtyard for antialiasing
MIN_BLOB = 40         # px: a body is ~500 px at this zoom, noise is a handful


# ---------------------------------------------------------------- sexp reads

def edge_box(text):
    """The board outline as drawn by Edge.Cuts: (x0, y0, x1, y1) in design mm."""
    pts = []
    for m in re.finditer(r'\(gr_line \(start ([-\d.]+) ([-\d.]+)\) '
                         r'\(end ([-\d.]+) ([-\d.]+)\)[^\n]*\(layer "Edge\.Cuts"\)',
                         text):
        pts += [(float(m.group(1)), float(m.group(2))),
                (float(m.group(3)), float(m.group(4)))]
    if not pts:
        raise SystemExit("no Edge.Cuts lines found in %s" % SRC)
    return (min(p[0] for p in pts), min(p[1] for p in pts),
            max(p[0] for p in pts), max(p[1] for p in pts))


def placements(text, refs):
    """ref -> (x, y, rot, layer) for every footprint named in `refs`.

    The (at ...) of a footprint comes before its first (property ...); a
    property's own (at ...) is the text's position, not the part's, which is how
    a probe once read 90 degrees for every placement on the board.
    """
    out, i = {}, 0
    while True:
        i = text.find('(footprint "', i + 1)
        if i < 0:
            break
        blk = text[i:kf._skip_sexp(text, i)]
        head = blk[:blk.find("(property ")] if "(property " in blk else blk
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
        at = re.search(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", head)
        lay = re.search(r'\(layer "([FB]\.Cu)"', head)
        if ref and at and ref.group(1) in refs:
            out[ref.group(1)] = (float(at.group(1)), float(at.group(2)),
                                 float(at.group(3) or 0),
                                 lay.group(1) if lay else "F.Cu")
    return out


def drop_models(blk):
    """Remove every (model ...) child of one footprint block, one nesting level."""
    out, i, n = [], 0, len(blk)
    while i < n:
        j = blk.find("(model ", i)
        if j < 0:
            out.append(blk[i:])
            break
        out.append(blk[i:j])
        i = kf._skip_sexp(blk, j)
    return "".join(out)


def strip_models(text, refs):
    """The same board with the hand-made model links cut out of `refs`."""
    out, i, cut = [], 0, 0
    while True:
        j = text.find('(footprint "', i)
        if j < 0:
            out.append(text[i:])
            break
        end = kf._skip_sexp(text, j)
        blk = text[j:end]
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
        if ref and ref.group(1) in refs and "(model " in blk:
            blk2 = drop_models(blk)
            cut += blk.count("(model ")
            out.append(text[i:j] + blk2)
        else:
            out.append(text[i:end])
        i = end
    return "".join(out), cut


# ---------------------------------------------------------------- rendering

def render(board, side, png):
    r = subprocess.run([P.KCLI, "pcb", "render", "--side", side, "--quality",
                        "high", "--width", str(WIDTH), "--height", str(HEIGHT),
                        "-o", png, board], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("render failed (%s):\n%s" % (png, (r.stderr or "")[-500:]))
    return np.array(Image.open(png).convert("RGB")).astype(np.int16)


def board_pixels(img):
    """The green board region: pixels whose green channel leads red and blue."""
    g = img[:, :, 1] - np.minimum(img[:, :, 0], img[:, :, 2]) > 6
    ys, xs = np.nonzero(g)
    if not len(xs):
        raise SystemExit("no board in the render - nothing green to frame on")
    return xs.min(), xs.max(), ys.min(), ys.max()


def blobs(mask):
    """Connected components (8-neighbours) of a boolean mask, as pixel arrays."""
    seen = np.zeros(mask.shape, bool)
    out = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        stack, comp, seen[y0, x0] = [(y0, x0)], [], True
        while stack:
            y, x = stack.pop()
            comp.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1] \
                            and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        out.append(comp)
    return out


# ---------------------------------------------------------------- the check

def main():
    text = open(SRC, encoding="utf-8").read()
    refs = set(MF.MODEL_REFS)
    box = edge_box(text)
    placed = placements(text, refs)
    missing = refs - set(placed)
    if missing:
        raise SystemExit("%s not on the board any more" % ", ".join(sorted(missing)))
    # the check only means something if the placements are the ones the .step
    # files were written for; make_pcb.py enforces the same thing at build time
    for ref, (_pkg, _lib, layer, rot, _mir) in MF.MODEL_REFS.items():
        if (placed[ref][3], placed[ref][2]) != (layer, rot):
            raise SystemExit("%s is placed on %s rot %g, model_frame.MODEL_REFS "
                             "says %s rot %g - settle that first"
                             % (ref, placed[ref][3], placed[ref][2], layer, rot))

    without, cut = strip_models(text, refs)
    if cut != len(refs):                # one model link per placement
        print("note: stripped %d model links from %d placements - look at the "
              "copies in hardware/probe_onboard_*.kicad_pcb before trusting below"
              % (cut, len(refs)))
    open(BOARD_WITH, "w", encoding="utf-8").write(text)
    open(BOARD_WITHOUT, "w", encoding="utf-8").write(without)

    # courtyard of each part, in design mm, from the footprint's own stored
    # numbers through the measured copper rule (never through the model rule)
    zones = {}
    for ref, (pkg, _lib, _layer, rot, _mir) in MF.MODEL_REFS.items():
        _pads, _fab, cy, _back = MM.footprint_geom(pkg)
        if not cy:
            raise SystemExit("%s: %s has no courtyard to check against" % (ref, pkg))
        x, y, _rot, _l = placed[ref]
        pts = [MF.copper_from_stored(px, py, rot)
               for px, py in ((cy[0], cy[1]), (cy[2], cy[3]))]
        zones[ref] = (x + min(p[0] for p in pts), y + min(p[1] for p in pts),
                      x + max(p[0] for p in pts), y + max(p[1] for p in pts))
    print("\ncourtyards the bodies have to stay inside (design mm, +%.1f mm slack):"
          % MARGIN)
    for ref in sorted(zones, key=lambda r: (zones[r][1], zones[r][0])):
        z = zones[ref]
        print("  %-4s x %7.2f..%7.2f  y %7.2f..%7.2f"
              % (ref, z[0], z[2], z[1], z[3]))

    print("board %s  outline x %.2f..%.2f y %.2f..%.2f mm"
          % (os.path.basename(SRC), box[0], box[2], box[1], box[3]))
    print("rendering with and without the %d package placements "
          "(top and bottom, quality high)..." % len(refs))
    bad = 0
    for side in ("top", "bottom"):
        on = render(BOARD_WITH, side, os.path.join(HW, "check_models_%s_with.png" % side))
        off = render(BOARD_WITHOUT, side, os.path.join(HW, "check_models_%s_no.png" % side))
        a, b = board_pixels(on), board_pixels(off)
        if max(abs(p - q) for p, q in zip(a, b)) > 2:
            raise SystemExit("the two %s renders are not registered (%s vs %s) - "
                             "a diff between them would prove nothing"
                             % (side, a, b))
        scale = ((b[1] - b[0] + 1) / (box[2] - box[0]),
                 (b[3] - b[2] + 1) / (box[3] - box[1]))
        if abs(scale[0] - scale[1]) > 0.02 * scale[0]:
            raise SystemExit("render scale differs on the two axes (%.3f vs %.3f "
                             "px/mm) - the picture is not a plain top view"
                             % scale)
        d = np.abs(on - off).sum(axis=2)
        mask = d > SHADOW
        n = int(mask.sum())
        print("\n  %s: %d px drawn by the models (%.2f px/mm, board px %d..%d / %d..%d)"
              % (side, n, scale[0], b[0], b[1], b[2], b[3]))
        if not n:
            print("    FAIL: the models changed nothing on this side")
            bad += 1
            continue
        def to_mm(px, py):
            """Image pixel -> design mm.

            The renderer frames the board, so the Edge.Cuts box fixes both the
            scale and the origin.  A bottom view is the board seen from under it:
            it comes out mirrored left to right, which is why the two sides need
            different x here (and the same y - flipping the board over about its
            X axis leaves north and south where they were).
            """
            x = (box[0] + (px - b[0]) / scale[0] if side == "top"
                 else box[2] - (px - b[0]) / scale[0])
            return (x, box[1] + (py - b[2]) / scale[1])
        stray = 0
        for comp in blobs(mask):
            if len(comp) < MIN_BLOB:
                continue
            ys = [c[0] for c in comp]
            xs = [c[1] for c in comp]
            ax, ay = to_mm(min(xs), min(ys))
            bx2, by2 = to_mm(max(xs), max(ys))
            x0, x1 = min(ax, bx2), max(ax, bx2)
            y0, y1 = min(ay, by2), max(ay, by2)
            owner = [r for r, z in zones.items()
                     if x0 >= z[0] - MARGIN and x1 <= z[2] + MARGIN
                     and y0 >= z[1] - MARGIN and y1 <= z[3] + MARGIN]
            tag = ", ".join(sorted(owner)) if owner else "NO COURTYARD"
            print("    %7.2f x %6.2f mm at x %7.2f..%7.2f y %7.2f..%7.2f  "
                  "%6d px  -> %s"
                  % (x1 - x0, y1 - y0, x0, x1, y0, y1, len(comp), tag))
            if not owner:
                near = min(((abs(x0 - z[0]) + abs(x1 - z[2]) + abs(y0 - z[1])
                             + abs(y1 - z[3])) / 4.0, r)
                           for r, z in zones.items())
                print("      FAIL: this body is %.1f mm from %s's courtyard - it "
                      "is drawn where no part of that footprint is"
                      % (near[0], near[1]))
                stray += 1
        if stray:
            bad += stray

    print("\n%s" % ("PASS: every body lands inside its own courtyard"
                    if not bad else "FAIL: %d blob(s) drawn outside every "
                    "courtyard - see above" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
