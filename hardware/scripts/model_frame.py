"""How a 3D package file has to be written so it lands on its own footprint.

Shared by make_3d_models.py (which writes the .step files) and make_pcb.py
(which places the footprints and checks that it still places them the way those
files were written for).  Pure math - importing it costs nothing.

All coordinates in this file are the .kicad_pcb frame: x east, y SOUTH.

The rules below were measured on this KiCad 10 install, not assumed.
scripts/probe_frame.py --place drops a blue L (x 4..6, y 2..4 with a foot at
x 4..5, y 4..8: asymmetric in both axes, so a mirror and a turn cannot be
confused with each other) at known coordinates into one-part probe boards, once
per placement, pins the frame of the rendered picture with an off-centre marker
pad, and scores the rendered silhouette against every candidate rule.  Both
rules hold at 0/90/180/270 on both layers:

    F.Cu   drawn = M_y . R(rot) . file          B.Cu   drawn = R(-rot) . file

`file` is a point as written in the .step, `drawn` is where that point shows up
in design coordinates relative to the footprint origin.  Read the F.Cu rule the
other way round - M_y . R(rot) is the same map as R(-rot) . M_y - and it says
something much more natural: the viewer rotates a package exactly the way it
rotates the copper, and additionally mirrors it in y, because a package file's
own frame has y pointing north while the board's points south.

A footprint's own copper, Fab and courtyard draw at R(-rot) . C . library, where
C is "mirror y" for every footprint kicadfmt mirrored when it moved it to the
bottom side (bcu_flip in kicadfmt.board_footprint - bcu_keep_pads only re-picks
the placement angle afterwards, it does not leave the children unmirrored), and
identity for anything that stays on the top side.  The minus sign is not a
convention: a positive placement angle turns a footprint counter-clockwise on
screen, and in a frame whose y points south that is R(-rot).  Measured on the
shipped board (scripts/check_models_on_board.py, kicad-cli render of the real
board): F1 sits at (121.25, 124.65) rot 90, and its pad-2 pair, stored at local
(17,0) and (17,5), renders at (121.25, 107.65) and (126.25, 107.65) - north of
the origin, at R(-90) . stored - with its Fab outline at x 121.25..126.25,
y 105.65..126.65 (R(+90) would have put it at y 122.65..143.65, off the south
edge of the board).

Setting drawn_body == drawn_copper and solving for the file frame gives
required_matrix().  The rotations cancel on both sides (M_y.R(rot) is the same map
as R(-rot).M_y), so the frame a package file needs does not depend on the placement
angle at all: a top-side part wants a plain mirror in y whatever angle it sits at,
a bottom-side part wants whatever mirror its stored children already carry.  What
the angle decides is whether a wrong rotation *sign* is visible.  R(+rot) and
R(-rot) agree at 0 and 180, so an earlier reading of these rules (copper drawn at
R(+rot).C.body) was harmless for the four packages placed at 0 or 180 degrees and
wrong only for F1 (F.Cu, rot 90): it asked for a flip in x where the 521 clip wants
a flip in y, and put its clips 17 mm south-west of the pads they solder into,
hanging off the south edge of the board.  scripts/check_models_on_board.py is the
counter-test - it measures the bodies KiCad actually draws on the shipped board, so
nothing in the chain is taken on trust.
"""

import math

# Every footprint on this board that has a hand-made package file next to it:
# ref -> (.step name, library footprint it was copied from, layer, rot, are its
# children stored y-mirrored relative to that library - true for every part that
# was flipped to B.Cu, whatever its angle ended up being).
#
# make_3d_models.py writes each .step in the frame this table says the placement
# needs; make_pcb.py checks its real placements against it, so moving a part to
# another layer or rotation fails the build instead of quietly shipping a body
# that sits across its own pads.  Re-derive the rules with
# scripts/probe_frame.py --place if KiCad's behaviour ever changes.
MODEL_REFS = {
    # The XT30's shell and its footprint are both symmetric in y, so this flip
    # changes nothing you can see; the placement is recorded as it really is,
    # not as it happens to look.
    "J1": ("AMASS_XT30U-M_1x02_P5.0mm_Vertical",
           "Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical", "B.Cu", 0, True),
    "J6": ("JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
           "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
           "B.Cu", 180, True),
    "J9": ("JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal",
           "Connector_JST:JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal",
           "B.Cu", 180, True),
    "J14": ("JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
            "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
            "B.Cu", 0, True),
    "J17": ("JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
            "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal",
            "B.Cu", 0, True),
    "F1": ("Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
           "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
           "F.Cu", 90, False),
    "OK1": ("SO-4_4.4x3.9mm_P2.54mm", "Package_SO:SO-4_4.4x3.9mm_P2.54mm",
            "F.Cu", 0, False),
    "OK2": ("SO-4_4.4x3.9mm_P2.54mm", "Package_SO:SO-4_4.4x3.9mm_P2.54mm",
            "F.Cu", 0, False),
}


def rot2(x, y, deg):
    """Rotate in the board plane (KiCad's sense: y grows downwards)."""
    a = math.radians(deg)
    c, s = round(math.cos(a), 9), round(math.sin(a), 9)
    return (c * x - s * y, s * x + c * y)


def drawn_from_file(layer, rot, x, y):
    """Where a point written in the .step file appears on the board.

    Measured, not assumed (probe_frame.py --place): on F.Cu the placement
    rotation is applied and then y is negated; on B.Cu the model turns the OTHER
    way, by -rot and with no y negation at all.  At rot 0 the two sides agree,
    which is why a single sign error here is invisible until something is placed
    at 90 or 270 degrees.
    """
    if layer == "F.Cu":
        px, py = rot2(x, y, rot)
        return (px, -py)
    return rot2(x, y, -rot)


def file_from_drawn(layer, rot, x, y):
    """Inverse of drawn_from_file: a point on the board back into the file."""
    if layer == "F.Cu":
        return rot2(x, -y, -rot)
    return rot2(x, y, rot)


def copper_from_stored(x, y, rot):
    """Where a point as stored in the .kicad_mod draws on the board.

    Just the placement rotation, negated: a positive angle turns a footprint
    counter-clockwise on screen, and in a frame whose y points south that is
    R(-rot).  Same law on both sides - the y-mirror a bottom-side footprint
    carries is already in its stored children, so it is not applied again here.
    """
    return rot2(x, y, -rot)


def copper_from_body(rot, children_mirrored, x, y):
    """Where a point of the library footprint draws on the board.

    C first (the y-mirror kicadfmt mirrors into a footprint it moves to the
    bottom side - bake_rot runs after mirror_y and only touches angles, never
    positions, so the stored children are already mirrored before pcbnew
    rotates them), then the placement rotation.
    """
    return copper_from_stored(x, -y if children_mirrored else y, rot)


def required_matrix(layer, rot, children_mirrored):
    """2x2 matrix taking a body authored in the library frame into the file.

    The copper draws at R(-rot).C.body and the body at M_y.R(rot).file on F.Cu or
    R(-rot).file on B.Cu; setting those equal and solving for the file gives
    file = drawn_inverse.R(-rot).C.body.  The placement rotation cancels on both
    sides, so a top-side package wants the plain y-mirror at any angle and a
    bottom-side one wants the mirror its stored children already carry.  C is
    applied first because kicadfmt mirrors a footprint it moves to the bottom side
    in the footprint's own frame, and bake_rot (which only touches angles, never
    positions) runs after that - so the stored children are already mirrored
    before pcbnew rotates them.
    """
    cols = []
    for basis in ((1.0, 0.0), (0.0, 1.0)):
        x, y = copper_from_body(rot, children_mirrored, *basis)
        cols.append(file_from_drawn(layer, rot, x, y))
    return ((cols[0][0], cols[1][0]), (cols[0][1], cols[1][1]))


CANONICAL = (
    ("as authored (no flip)", ((1, 0), (0, 1))),
    ("mirror in y",           ((1, 0), (0, -1))),
    ("mirror in x",           ((-1, 0), (0, 1))),
    ("turn 180 deg about z",  ((-1, 0), (0, -1))),
)


def describe(matrix):
    """Name of the frame, or the matrix itself if it is not one of the four square ones."""
    got = tuple(round(v, 6) + 0.0 for row in matrix for v in row)
    for tag, m in CANONICAL:
        if got == tuple(float(v) for row in m for v in row):
            return tag
    return ("needs a general transform, rows [%+5.2f %+5.2f] [%+5.2f %+5.2f]"
            " - no single package file can serve this placement" % got)


def frames_by_package():
    """{.step name: (matrix, tag, refs)} - one file per package, all refs agreed.

    Raises when two placements of the same package want different frames: then
    one .step cannot serve both and the situation needs a decision, not a guess.
    """
    out = {}
    for ref, (pkg, _lib, layer, rot, mir) in MODEL_REFS.items():
        m = required_matrix(layer, rot, mir)
        if pkg in out:
            if out[pkg][0] != m:
                raise SystemExit(
                    "%s is placed at both %s and the two placements want "
                    "different file frames (%s vs %s) - one .step cannot serve "
                    "both" % (pkg, " / ".join(out[pkg][2] + [ref]),
                              describe(out[pkg][0]), describe(m)))
            out[pkg][2].append(ref)
        else:
            out[pkg] = (m, describe(m), [ref])
    return out


def check(placed):
    """Do the board's real placements still match MODEL_REFS?

    `placed` is {ref: (layer, rot, children_mirrored)} as make_pcb.py emits.
    Returns problem messages; empty means every shipped .step is still being
    asked to do exactly what it was written to do.
    """
    bad = []
    for ref, (pkg, _lib, layer, rot, mir) in MODEL_REFS.items():
        got = placed.get(ref)
        if got is None:
            bad.append("%s: not on the board, but %s.step was made for it"
                       % (ref, pkg))
            continue
        glayer, groot, gmir = got[0], int(round(got[1])) % 360, got[2]
        if (glayer, groot, gmir) == (layer, rot % 360, mir):
            continue
        # the two cases that are not really a problem: a part recorded on the
        # wrong side of a symmetric pattern, or an angle whose frame comes out
        # the same anyway.  Everything else is a body about to be drawn across
        # its own pads.
        if required_matrix(glayer, groot, gmir) == required_matrix(layer, rot, mir):
            continue
        want = describe(required_matrix(glayer, groot, gmir))
        bad.append(
            "%s is now %s at rot %d%s, but %s.step is written %s for it - this "
            "placement wants the file %s. Update MODEL_REFS, run "
            "scripts/make_3d_models.py, and re-check with "
            "scripts/probe_frame.py --place"
            % (ref, glayer, groot,
               " with children as published" if gmir is False and glayer == "B.Cu" else "",
               pkg, describe(required_matrix(layer, rot, mir)), want))
    return bad


def children_frame(official, stored):
    """How a footprint copy is stored relative to the library's own file.

    `official` and `stored` are pad boxes [(x, y, w, h), ...] of the same
    footprint - the library's own, and this repo's copy.  Returns "I" (stored as
    published), "M_y" (y-mirrored for the bottom side), "both" when the pad
    pattern is symmetric enough that the two cannot be told apart (the XT30:
    every pad sits at y=0), or None when it is neither, which means the copy is
    not that footprint any more.

    "both" (the XT30, the SO-4) is harmless when the moulding is symmetric in y
    as well - a y-mirror then leaves the body exactly where it is, and
    make_3d_models.verify() says so after measuring the body.  If the body is
    not symmetric in y, the pads alone cannot say which way it faces and that has
    to be settled against the vendor drawing.
    """
    def key(pts, mirror):
        return sorted((round(x, 3), round(-y if mirror else y, 3),
                       round(w, 3), round(h, 3)) for x, y, w, h in pts)
    a = [(x, y, w, h) for x, y, w, h in official]
    b = [(x, y, w, h) for x, y, w, h in stored]
    plain = key(a, False) == key(b, False)
    flipped = key(a, False) == key(b, True)
    if plain and flipped:
        return "both"
    if plain:
        return "I"
    if flipped:
        return "M_y"
    return None
