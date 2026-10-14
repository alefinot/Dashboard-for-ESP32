#!/usr/bin/env python3
"""make_3d_models.py - the 3D packages for the five footprints KiCad does not ship.

Five footprints on this board point at a package that KiCad does not ship, so
those parts render as nothing in the 3D viewer.  Checked twice: nothing of that
name anywhere under this install's 3dmodels/, and github.com/KiCad/kicad-
packages3D@master answers 404 for all five file names while control names in the
same folders answer 200 - the library's own model links are dangling here:

  Package_SO:SO-4_4.4x3.9mm_P2.54mm                              OK1, OK2 (EL357N-B)
  Connector_JST:JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal J9
  Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal J6, J14, J17
  Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical             J1  (12 V in)
  Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_...         F1

Four of the five are the real vendor package: footprints/vendor_step/ holds those
STEP files exactly as fetched and footprints/vendor_step/SOURCES.md records where
each came from, the measured bounding box and the licensing caveat.  The vendor
files use their own origin convention, so each is re-anchored to its footprint's
frame by a rigid turn and move (VENDOR below spells out the measurement that
justifies each one).

The hand-drawn EL357N optocoupler is gone, replaced by Lite-On's own LTV-357T
package: LTV-357T is Lite-On's part number for the same 4 pin SOP 4.4x3.9
optocoupler form (the footprint's pads, Fab and courtyard are that form's), so
the vendor moulding and the vendor leads go in instead of a reading of the
Everlight drawing.

The Littelfuse 521 clip stays parametric.  Its package is not in the KiCad
release, and Littelfuse's own design center - like every component-CAD portal
that carries it - needs a browser session and an account, so it is drawn from the
520/521 datasheet numbers instead.  It stays on the board as F1: every closed
(captive, shrouded) 5x20 holder in the stock library needs 33 mm of run or more,
and the west column this fuse lives in has 32.05 mm between Q1's courtyard and
the board edge, with no sideways escape past Q1 and R1.  Measured 2026-10-14.

Everything is written into footprints/dpp.3dshapes/ in this repo, one .step per
footprint named exactly like it, and kicadfmt.py repoints those five model
links at ${KIPRJMOD}/footprints/dpp.3dshapes/<name>.step.  The board therefore
renders the same on every machine instead of depending on files this script
drops into somebody's KiCad install.  Run this after touching a model; run
make_pcb.py afterwards if a footprint changed.

Why STEP and not VRML: KiCad 10 reads STEP only.  Tested on this install by
rendering the board and diffing pixels - a real STEP dropped into the XT30 slot
changes 7898 px at its pads, while a .wrl (even a 30 mm test box, also tried
as .x3d and with an absolute path) changes nothing at all.  Upstream confirms
it: packages3D MR !1327 deleted every .wrl, the KiCad 10 release notes ship
"STEP files only", and 10.0.3+ offers to migrate leftover .wrl references.

All dimensions come either from the vendor package itself or from the
 datasheets (cited per model below), and always agree with the Fab outline +
pads of the footprint the model belongs to, so a body always lands on its own
copper: verify() at the end of this run re-measures every package against the
copper it is supposed to sit on and fails the build if a pin, a solder tail or
a body is off.  Coordinates are the footprint library's own convention - the
frame each .step is then written in is not: KiCad draws a model through the
placement, and the rule differs between the two board sides.  scripts/probe_frame.py
measured both halves on this install (scripts/model_frame.py, which the generators
share): a point in the file draws at M_y.R(rot) on F.Cu and at R(-rot) on B.Cu,
while the footprint's own copper draws at R(-rot) applied to the stored children.
Solved against each other that comes out a mirror in y for all five packages here,
and the placement angle cancels out, so F1 sitting at 90 degrees changes nothing.
An earlier reading of the same probe put the copper at R(+rot) instead, which asked
for a flip in x for the 521 clip and is the misalignment that was visible on the
board: the clips landed 17 mm from the pads they solder into.  The counter-test is
scripts/check_models_on_board.py, which measures the answer on the rendered board
instead of on paper.
The XT30 keeps a flip it cannot show: both the flip and its own shell are symmetric
in y.

Needs  pip install --no-deps cadquery-ocp      (the OCCT kernel; ~48 MB wheel)
Usage:  python scripts/make_3d_models.py [--out <dir>] [--dry-run] [--no-verify]
"""

import argparse
import math
import os
import re

from OCP.BRepAlgoAPI import BRepAlgoAPI_Common
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import (BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon,
                                BRepBuilderAPI_Transform)
from OCP.BRepGProp import BRepGProp
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder,
                             BRepPrimAPI_MakePrism)
from OCP.Bnd import Bnd_Box
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_ReturnStatus
from OCP.Interface import Interface_Static
from OCP.Quantity import Quantity_Color, Quantity_TOC_RGB
from OCP.STEPControl import STEPControl_Reader
from OCP.STEPCAFControl import STEPCAFControl_Writer
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDocStd import TDocStd_Document
from OCP.XCAFApp import XCAFApp_Application
from OCP.XCAFDoc import XCAFDoc_ColorGen, XCAFDoc_ColorSurf, XCAFDoc_DocumentTool
from OCP.gp import gp_Ax1, gp_Ax2, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec

import kicadfmt as kf
import model_frame as MF

# ---------------------------------------------------------------------------
# where things live: the packages and their vendor sources are inside the repo,
# which is what lets the footprints link them with ${KIPRJMOD} and make the
# board's 3D self-contained
# ---------------------------------------------------------------------------

HERE = os.path.dirname(os.path.abspath(__file__))
HARDWARE = os.path.dirname(HERE)
OUT_DIR = os.path.join(HARDWARE, "footprints", "dpp.3dshapes")
VENDOR_DIR = os.path.join(HARDWARE, "footprints", "vendor_step")
PRETTY = os.path.join(HARDWARE, "footprints", "dpp.pretty")


PLASTIC_BLACK = (0.16, 0.16, 0.17)
PLASTIC_JST = (0.90, 0.88, 0.82)          # JST PH natural nylon
PLASTIC_XT = (0.95, 0.42, 0.05)          # Amass XT orange
METAL_TIN = (0.68, 0.70, 0.72)
METAL_GOLD = (0.83, 0.66, 0.21)          # XT30 contacts are gold plated


class quiet_cout:
    """Hush OCC's own chatter for the duration of a STEP write.

    STEPCAFControl prints a multi-line "Statistics on Transfer" banner per file,
    straight to the C-level stdout, which buries the measurements this script is
    there to print.  Redirecting fd 1 covers C++ std::cout as well; errors still go
    to stderr, and a failed write is reported by the caller just after the block.
    """

    def __enter__(self):
        self.fd = os.dup(1)
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, 1)
        os.close(devnull)
        return self

    def __exit__(self, *exc):
        os.dup2(self.fd, 1)
        os.close(self.fd)
        return False


class Model:
    """One STEP package: a list of solids, each with a colour."""

    def __init__(self, name):
        self.name = name
        self.parts = []                   # (shape, (r, g, b))

    # ---- primitives ---------------------------------------------------
    def prism(self, profile, axis, a0, a1, color):
        """Extrude a flat profile along one axis.

        profile is (u, v) in the plane of that axis: 'z' -> (x, y) at z = a,
        'x' -> (y, z) at x = a, 'y' -> (x, z) at y = a.  Everything on this
        board is a prism extruded square to its own footprint.
        """
        if axis == "z":
            pts = [(u, v, a0) for (u, v) in profile]
            d = (0, 0, a1 - a0)
        elif axis == "x":
            pts = [(a0, u, v) for (u, v) in profile]
            d = (a1 - a0, 0, 0)
        else:
            pts = [(u, a0, v) for (u, v) in profile]
            d = (0, a1 - a0, 0)
        wire = BRepBuilderAPI_MakePolygon()
        for p in pts:
            wire.Add(gp_Pnt(*p))
        wire.Close()
        face = BRepBuilderAPI_MakeFace(wire.Wire())
        if not face.IsDone():
            raise SystemExit("%s: profile is not planar/valid" % self.name)
        shape = BRepPrimAPI_MakePrism(face.Shape(), gp_Vec(*d)).Shape()
        self.parts.append((shape, color))

    def box(self, x0, x1, y0, y1, z0, z1, color):
        shape = BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), x1 - x0, y1 - y0,
                                    z1 - z0).Shape()
        self.parts.append((shape, color))

    def cyl(self, cx, cy, z0, z1, r, color):
        shape = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(cx, cy, z0), gp_Dir(0, 0, 1)), r, z1 - z0).Shape()
        self.parts.append((shape, color))

    def ring(self, cy, cz, r_in, r_out, a_from, a_to, x0, x1, color, seg=36):
        """C-shaped band around an axis parallel to x - the fuse cradle.

        Angles in degrees in the (y, z) plane, math convention.
        """
        n = max(2, int(round(abs(a_to - a_from) / 360.0 * seg)))
        outer, inner = [], []
        for i in range(n + 1):
            a = math.radians(a_from + (a_to - a_from) * i / n)
            outer.append((cy + r_out * math.cos(a), cz + r_out * math.sin(a)))
            inner.append((cy + r_in * math.cos(a), cz + r_in * math.sin(a)))
        self.prism(outer + inner[::-1], "x", x0, x1, color)

    # ---- writing ------------------------------------------------------
    def write(self, path, frame=None):
        """Write the package, one STEP product per solid.

        `frame` is a gp_Trsf applied to every solid on the way out - the frame
        this file has to be written in.  model_frame.required_matrix() works it
        out from the placement the package is used at; None writes the body
        exactly as authored, which scripts/probe_frame.py uses to ask KiCad what
        it does with a raw file frame in the first place.

        KiCad's own drawing rule, measured by scripts/probe_frame.py --place
        (a blue L with a foot, asymmetric in both axes, at known coordinates in
        one-part probe boards at every 90 deg on both sides, scored against the
        rendered picture with the frame pinned by an off-centre marker pad):

            F.Cu   drawn = M_y . R(rot) . file     B.Cu  drawn = R(-rot) . file

        so a body authored in the footprint's own frame needs a flip that
        depends on the placement, not on the package.  With the copper drawn at
        R(-rot).stored (measured on the shipped board by
        scripts/check_models_on_board.py, and model_frame.copper_from_body),
        every placement on this board comes out as a mirror in y - the 521 clip
        (F.Cu, rot 90) included, which is the one part whose angle could have
        told the two rotation signs apart.  The XT30 keeps a flip it cannot show
        - both its shell and its footprint are symmetric in y.  What was wrong
        here before was a mirror in x for the clip, which put its clips 17 mm off
        the pads they are soldered to, hanging off the south edge of the board.

        Everything else in this file - the parametric clip, the vendor
        re-anchoring, bbox() - is written in footprint coordinates, so the
        numbers read the same as the .kicad_mod pads.  The frame change happens
        once, here.
        """
        doc = TDocStd_Document(TCollection_ExtendedString("XmlOcaf"))
        app = XCAFApp_Application.GetApplication_s()
        app.NewDocument(TCollection_ExtendedString("XmlOcaf"), doc)
        shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
        colors = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
        for shape, rgb in self.parts:
            out = BRepBuilderAPI_Transform(shape, frame, True).Shape() if frame else shape
            label = shapes.AddShape(out)
            col = Quantity_Color(rgb[0], rgb[1], rgb[2], Quantity_TOC_RGB)
            colors.AddColor(col)
            colors.SetColor(label, col, XCAFDoc_ColorGen)
            colors.SetColor(label, col, XCAFDoc_ColorSurf)
        Interface_Static.SetCVal_s("write.step.unit", "MM")
        w = STEPCAFControl_Writer()
        w.SetColorMode(True)
        w.SetLayerMode(False)
        w.SetNameMode(True)
        with quiet_cout():
            ok = w.Transfer(doc)
            done = w.Write(path) if ok else None
        if not ok:
            raise SystemExit("%s: STEP transfer failed" % self.name)
        if done != IFSelect_ReturnStatus.IFSelect_RetDone:
            raise SystemExit("%s: STEP write failed" % self.name)

    def bbox(self):
        b = Bnd_Box()
        for shape, _ in self.parts:
            BRepBndLib.Add_s(shape, b)
        lo, hi = b.CornerMin(), b.CornerMax()
        return ((lo.X(), lo.Y(), lo.Z()), (hi.X(), hi.Y(), hi.Z()))


# ---------------------------------------------------------------------------
# the four square frames a package file can be written in
# ---------------------------------------------------------------------------


def trsf_for(tag):
    """gp_Trsf for one of the frames model_frame.describe() can name.

    Built by composing primitives rather than from the raw 2x2: gp_Trsf keeps
    its matrix transposed and SetValues follows Value(i, j), which is a quiet way
    to transpose a flip.  Every placement on this board is a multiple of 90 deg
    so the required frame is always one of these four; anything else is a real
    problem (no single .step can serve it) and gets reported as one.
    """
    if tag in (None, "as authored (no flip)"):
        return None
    t = gp_Trsf()
    if tag == "mirror in y":
        t.SetMirror(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)))     # y -> -y
    elif tag == "mirror in x":
        t.SetMirror(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)))     # x -> -x
    elif tag == "turn 180 deg about z":
        t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), math.pi)
    else:
        raise SystemExit(
            "this placement wants %s, which is not one of the four square "
            "frames - a .step cannot serve it. Either place the part at a "
            "multiple of 90 deg or give it its own model file" % tag)
    return t


# ---------------------------------------------------------------------------
# the parametric package - F1's clip, no vendor MCAD reachable
# ---------------------------------------------------------------------------


def littelfuse_521():
    """F1 - Littelfuse 521 clip for a 5 x 20 mm cartridge fuse.

    520/521 datasheet: PC-board 5 mm pin spacing, formed from 0.4 mm stock,
    pin section 1.3 +-0.1 (the footprint drills 1.3), fuse axis 3.0 mm above
    the board, about 8 mm of clip over the leads.  Pins are at (0,0), (0,5),
    (17,0), (17,5): each clip's two legs stand 2.5 either side of the fuse axis
    at y = 2.5, and a 5 mm fuse (r 2.5) beds in a half shell around that axis -
    the legs meet that shell exactly at its own height, which is how the part
    is drawn in the datasheet.  Open clip: the fuse snaps in by hand.
    """
    m = Model("Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm"
              "_D1.30mm_Horizontal")
    for cx in (0.0, 17.0):
        for py in (0.0, 5.0):                   # legs, 1.15 square in a 1.3 drill
            m.box(cx - 0.575, cx + 0.575, py - 0.575, py + 0.575,
                  -1.6, 3.0, METAL_TIN)
        m.ring(2.5, 3.0, 2.5, 2.9, 180.0, 360.0, cx - 0.4, cx + 0.4, METAL_TIN)
    return m, ("Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm"
               "_D1.30mm_Horizontal")


# ---------------------------------------------------------------------------
# the four real vendor packages, re-anchored to the footprint frame
# ---------------------------------------------------------------------------
#
# A vendor MCAD file never shares KiCad's origin convention, so each one needs a
# rigid turn and move before it can be dropped on its footprint.  The numbers below are
# measured off the package (cross-sections sliced out of the STEP at a height
# where only the solder tails / pins are present) and compared with the copper
# of the footprint - they are not fitted after the fact.  A rotation rather than
# a mirror is used wherever a flip is needed, so the solid keeps its handedness.
#
#   AMASS XT30U-M                                       J1 (12 V in, B.Cu)
#     Sliced at z = -1.50..-0.50 (i.e. inside the board) the package is exactly
#     two 2.50 x 2.50 pins centred at x = +-2.50, y = 0, nothing else.  The
#     footprint's pads are 3.80 x 3.80 with a 2.70 drill at (0, 0) and (5.00, 0),
#     so +2.50 in x drops every pin into its own hole with 0.10 mm to spare.
#     The housing then spans x -2.58..7.58 against the Fab's -2.60..7.60, and
#     y +-2.80 against the Fab's +-2.60 but inside the courtyard's +-3.10: the
#     real moulding is 0.4 wider than the library's outline drawing.  Overall
#     height above the seating plane 10.75, and the contacts run from z -3.00 up
#     into the moulding: the drawing's "3.00" pin protrusion is *below* the
#     seating plane (the contacts stand that far above the board on the display
#     side), and nothing is proud of the housing top at all - the parametric model
#     this replaces had its contacts sticking 3.00 up past the moulding instead.
#
#   JST S3B-PH-SM4-TB / S4B-PH-SM4-TB                   J9 / J6, J14, J17 (B.Cu)
#     These packages are stored mirrored in y relative to the library footprint:
#     their contact tails are at +y and the mounting brackets at -y, the pads
#     are the other way round.  The outline is symmetric in x, so a 180 turn
#     about z performs that flip without inverting the solid.  Sliced at
#     z = 0.05..0.35 the S4B gives 4 contact tails 0.50 x 2.55 at x = -3, -1, 1,
#     3 (2.00 pitch) spanning y 3.05..5.60, plus 2 bracket tails 0.40 x 2.91 at
#     x = +-5.25, y -3.01..-0.10; the S3B is the same with contacts at x = -2, 0,
#     2 and brackets at +-4.25.  After the turn and +1.475 in y the contacts sit
#     dead-centred on the 1.00 x 3.50 signal pads (y -4.60..-1.10, 0.48/0.58
#     clearance at the ends), the brackets land 0.10 off centre in the
#     1.50 x 3.40 MP pads (x +-5.35 / x +-4.35, y 1.20..4.60), and the housing
#     spans x exactly the Fab's +-4.95 / +-5.95 and y -3.13..4.49 against the
#     Fab's -3.20..4.40: 0.07 inside its north end, 0.09 past its south, i.e. the
#     housing is 7.61 deep where the library draws 7.60.  With the soldered tails
#     it reaches y -4.13..4.49: 0.93 past the Fab at the contact end (the tails
#     run out under the cable entry), 0.09 at the bracket end, and 0.61 inside the
#     courtyard's +-5.10.  A Fab outline is the body, not the feet - every SMD
#     footprint on this board has legs outside its Fab - so the courtyard is the
#     outline that has to contain them.  Housing height 5.60 - the parametric
#     model this replaces was 6.60, a full millimetre too tall.
#
#   Lite-On LTV-357T                                    OK1, OK2 (EL357N-B)
#     The package arrives lying on its side: 3.85 (the short body dimension) in
#     x, the 2.10 body height in y, and the 7.02 "across leads" span in z.  A
#     quarter turn about y and then one about x stand it up, and the vendor
#     origin turns out to be the footprint origin already: sliced at z 0.00..0.30
#     the package is four lead toes spanning x 2.62..3.51 and y 1.07..1.47, i.e.
#     dead centred on the 1.80 x 0.60 pads at (+-3.15, +-1.27), toe ends 0.89 long
#     inside pads that end at x 4.05.  The moulding then spans x 4.26..4.38 by
#     y 3.72..3.85 (draft angle: narrower at the top), against the Fab's
#     4.62 x 4.12, and its top is at z 2.10 - the datasheet maximum.  The toes
#     reach x +-3.51 and the body y +-1.93, all inside the courtyard's
#     +-4.30 x +-2.20.  Nothing needs moving, and the moulding carries no pin-1
#     mark at all (its top face is a plain rectangle at every height probed), so
#     the corner it lands in is a library question, not a model one.
VENDOR = {
    "AMASS_XT30U-M_1x02_P5.0mm_Vertical": dict(
        src="AMASS_XT30U-M.step", colour=PLASTIC_XT, move=(2.50, 0.0, 0.0)),
    "SO-4_4.4x3.9mm_P2.54mm": dict(
        src="LiteOn_LTV-357T.step", colour=PLASTIC_BLACK,
        rot_y=-90.0, rot_x=90.0),
    "JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal": dict(
        src="JST_S3B-PH-SM4-TB.step", colour=PLASTIC_JST, rot_z=180.0,
        move=(0.0, 1.475, 0.0)),
    "JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal": dict(
        src="JST_S4B-PH-SM4-TB.step", colour=PLASTIC_JST, rot_z=180.0,
        move=(0.0, 1.475, 0.0)),
}


def _solids(shape):
    out, ex = [], TopExp_Explorer(shape, TopAbs_SOLID)
    while ex.More():
        out.append(ex.Current())
        ex.Next()
    return out or [shape]


def vendor_model(name, spec):
    """One vendor STEP, turned/moved into the footprint's own frame."""
    src = os.path.join(VENDOR_DIR, spec["src"])
    if not os.path.isfile(src):
        raise SystemExit("%s: vendor package missing, expected %s" % (name, src))
    rd = STEPControl_Reader()
    with quiet_cout():                            # OCC is just as chatty on reads
        done = rd.ReadFile(src)
        if done == IFSelect_ReturnStatus.IFSelect_RetDone:
            rd.TransferRoots()
    if done != IFSelect_ReturnStatus.IFSelect_RetDone:
        raise SystemExit("%s: could not read %s" % (name, src))
    # rot_z / rot_y / rot_x compose so the shape is turned about z first, then y,
    # then x (gp_Trsf multiplies right to left), which is the order the LTV-357T
    # measurement above is written in.
    trsf = gp_Trsf()
    for key, axis in (("rot_z", (0, 0, 1)), ("rot_y", (0, 1, 0)), ("rot_x", (1, 0, 0))):
        if spec.get(key):
            spin = gp_Trsf()
            spin.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(*axis)),
                             math.radians(spec[key]))
            trsf = spin * trsf
    if spec.get("move"):
        shift = gp_Trsf()
        shift.SetTranslation(gp_Vec(*spec["move"]))
        trsf = shift * trsf                      # turn first, then slide
    shape = BRepBuilderAPI_Transform(rd.OneShape(), trsf, True).Shape()
    # one part per solid: keeps the STEP assembly (and its colours) simple
    m = Model(name)
    for s in _solids(shape):
        m.parts.append((s, spec["colour"]))
    return m


def all_models():
    """The five packages KiCad lacks: vendor MCAD where a package exists, parametric where not."""
    yield littelfuse_521()
    for name in sorted(VENDOR):
        yield vendor_model(name, VENDOR[name]), name


# ---------------------------------------------------------------------------
# verification: does every package land on the copper it is meant to land on?
# ---------------------------------------------------------------------------


def sexp(text):
    """Minimal KiCad sexp parser -> nested lists of str / floats."""
    toks = re.findall(r'\(|\)|"[^"]*"|-?[\w.$%+/:{}-]+', text)
    root = []
    stack = [root]
    for t in toks:
        if t == "(":
            n = []
            stack[-1].append(n)
            stack.append(n)
        elif t == ")":
            stack.pop()
        else:
            stack[-1].append(t[1:-1] if t.startswith('"') else t)
    return root


def footprint_geom(name):
    """Pads (name/x/y/w/h/drill) + Fab and courtyard boxes of one dpp footprint.

    `back` says whether that footprint is a bottom-side one, read from the
    footprint's own (layer ...) token - not from the pads: a through-hole part
    says "*.Cu" and never names a side.  A B.Cu footprint carries its children
    y-mirrored (mirror_y in kicadfmt.py), a package stored for the top side does
    not, and the caller has to know which frame these numbers are in.
    """
    path = os.path.join(PRETTY, name + ".kicad_mod")
    if not os.path.isfile(path):
        raise SystemExit("%s: no footprint %s to check against" % (name, path))
    tree = sexp(open(path, encoding="utf-8").read())[0]
    pads, fab, cy = [], [], []
    back = (next((n[1] for n in tree[1:] if n and n[0] == "layer"), "F.Cu")
            == "B.Cu")

    def pts(node, out):
        for k in node[1:]:
            if k and k[0] in ("start", "end", "xy"):
                out.append((float(k[1]), float(k[2])))

    for n in tree[1:]:
        if not n:
            continue
        if n[0] == "pad":
            at, size, dr, lay = None, None, 0.0, ""
            for k in n[3:]:
                if not k:
                    continue
                if k[0] == "at":
                    at = (float(k[1]), float(k[2]))
                elif k[0] == "size":
                    size = (float(k[1]), float(k[2]))
                elif k[0] == "drill":
                    dr = float(k[1])
                elif k[0] == "layers":
                    lay = " ".join(k[1:])
            if at and size:
                pads.append(dict(name=n[1], x=at[0], y=at[1], w=size[0],
                                 h=size[1], drill=dr))
        elif n[0] in ("fp_line", "fp_poly", "fp_rect"):
            for k in n[1:]:
                if k and k[0] == "layer" and k[1].endswith((".Fab", ".CrtYd")):
                    p = []
                    pts(n, p)
                    (fab if k[1].endswith(".Fab") else cy).extend(p)

    def box(p):
        return ((min(q[0] for q in p), min(q[1] for q in p),
                 max(q[0] for q in p), max(q[1] for q in p)) if p else None)

    return pads, box(fab), box(cy), back


def _material_at(shape, x, y, z0, z1, r):
    """Centre of the package's material inside a probe box over one pad."""
    probe = BRepPrimAPI_MakeBox(gp_Pnt(x - r, y - r, z0), 2 * r, 2 * r,
                                z1 - z0).Shape()
    cut = BRepAlgoAPI_Common(shape, probe).Shape()
    best = None
    for s in _solids(cut):
        b = Bnd_Box()
        BRepBndLib.Add_s(s, b)
        if b.IsVoid():
            continue                      # boolean left a degenerate solid behind
        lo, hi = b.CornerMin(), b.CornerMax()
        cx, cyy = (lo.X() + hi.X()) / 2.0, (lo.Y() + hi.Y()) / 2.0
        d = math.hypot(cx - x, cyy - y)
        if best is None or d < best[0]:
            best = (d, cx, cyy)
    return best


def verify(names):
    """Every package must sit on its own copper, inside its own courtyard.

    Through-hole parts need material in the pad hole below the seating plane
    (z < 0 in the library frame), SMT parts need it on the pad surface above it.
    The body is checked against the courtyard, since that is the outline the
    board's keepouts are built from; the Fab outline is only printed - a real
    moulding is allowed to differ from a library's outline drawing by a fraction
    of a millimetre, and here the XT30 body is 0.4 wider than its Fab.

    Everything is compared in the frame of the file that was actually written,
    through the measured drawing rules (model_frame): the copper of a footprint
    draws at R(-rot) . stored, and the body draws at M_y.R(rot) . file on F.Cu /
    R(-rot) . file on B.Cu, so the place to probe inside the file is
    drawn_inverse . R(-rot) . stored.  That works at any rotation and on either
    side, which the hand-written sign flips it replaced did not.

    Probing through required_matrix() would prove nothing - it is the same map,
    so a check that reads the body back through the rule that wrote it passes
    whatever that rule was.  The copper half comes from the separately measured
    copper rule, and that makes this check bite: a mirror in x for F1 puts its
    clip legs 17 mm from every pad it is supposed to sit on.
    """
    where = {}
    for ref, (pkg, lib, layer, rot, mir) in MF.MODEL_REFS.items():
        where.setdefault(pkg, (ref, lib, layer, rot, mir))
    bad = 0
    for name in names:
        if name not in where:
            print("  %-58s  no placement recorded in model_frame.MODEL_REFS" % name)
            bad += 1
            continue
        ref, lib, layer, rot, mir = where[name]
        pads, fab, cy, back = footprint_geom(name)
        if back != (layer == "B.Cu"):
            print("      %s: its pads sit on %s, MODEL_REFS says %s"
                  % (ref, "B.Cu" if back else "F.Cu", layer))
            bad += 1
        stored = MF.children_frame(
            [(x, y, w, h) for _n, x, y, w, h, _d in kf.pads_of(lib)],
            [(p["x"], p["y"], p["w"], p["h"]) for p in pads])
        if stored is None:
            print("      %s: %s does not carry %s's pad pattern any more, so "
                  "nobody can tell how its children are stored"
                  % (ref, name, lib.split(":")[-1]))
            bad += 1
        elif stored in ("I", "M_y") and \
                MF.required_matrix(layer, rot, stored == "M_y") != \
                MF.required_matrix(layer, rot, mir):
            print("      %s: %s stores its children %s, which MODEL_REFS does not "
                  "expect - this placement would want the file written %s, and "
                  "%s.step is written %s"
                  % (ref, name, stored,
                     MF.describe(MF.required_matrix(layer, rot, stored == "M_y")),
                     name, MF.describe(MF.required_matrix(layer, rot, mir))))
            bad += 1

        def to_file(x, y, layer=layer, rot=rot):
            """A point as stored in the .kicad_mod, in the frame of the .step.

            The pad / Fab / courtyard tables read by footprint_geom are the
            footprint's own stored numbers - already y-mirrored for a bottom-side
            part - so only the placement rotation goes on top of them.
            """
            return MF.file_from_drawn(
                layer, rot, *MF.copper_from_stored(x, y, rot))

        path = os.path.join(OUT_DIR, name + ".step")
        rd = STEPControl_Reader()
        with quiet_cout():                        # one banner per read, too
            done = rd.ReadFile(path)
            if done == 1:                         # IFSelect_RetDone
                rd.TransferRoots()
                shape = rd.OneShape()
        if done != 1:
            print("  %-58s  CANNOT READ %s" % (name, path))
            bad += 1
            continue
        b = Bnd_Box()
        for s in _solids(shape):
            BRepBndLib.Add_s(s, b)
        lo, hi = b.CornerMin(), b.CornerMax()
        print("  %s  (%s at %s rot %d, file written %s)"
              % (name, ref, layer, rot,
                 MF.describe(MF.required_matrix(layer, rot, mir))))
        print("      body %6.2f x %6.2f mm, z %6.2f .. %6.2f (%d solids)"
              % (hi.X() - lo.X(), hi.Y() - lo.Y(), lo.Z(), hi.Z(),
                 len(_solids(shape))))
        if stored == "both":
            # every pad sits at y = 0, so the footprint cannot say whether it was
            # stored mirrored or not.  That is only harmless if the moulding is
            # symmetric in y too, and then it is provably harmless: a y-mirror
            # leaves the body where it is.
            if abs(lo.Y() + hi.Y()) > 0.05:
                print("      %s: FAIL - its pads cannot tell a y-mirror from no "
                      "mirror (all at y=0) and its body is not symmetric in y "
                      "either (y %.2f..%.2f), so nothing here says which way it "
                      "faces; settle it against the vendor drawing"
                      % (ref, lo.Y(), hi.Y()))
                bad += 1
            else:
                print("      pads and body are both symmetric in y: which of the "
                      "two this footprint is stored cannot be seen, and does not "
                      "matter")
        worst = 0.0
        for p in pads:
            px, py = to_file(p["x"], p["y"])
            if p["drill"]:
                how = "pin"
                got = _material_at(shape, px, py, -1.2, -0.6,
                                   max(0.30, p["drill"] / 2.0))
            else:
                how = "tail"
                got = _material_at(shape, px, py, 0.02, 0.45, 0.30)
            if got is None:
                print("      pad %-3s  NO %s OVER THE PAD (%.2f x %.2f at %.2f, %.2f)"
                      % (p["name"], how, p["w"], p["h"], px, py))
                bad += 1
                continue
            worst = max(worst, got[0])
            print("      pad %-3s  %-4s %5.2f mm off its centre" % (p["name"], how, got[0]))
        if worst > 0.35:
            print("      FAIL: a pin or tail is %.2f mm off its pad" % worst)
            bad += 1
        for tag2, ext in (("Fab", fab), ("courtyard", cy)):
            if not ext:
                continue
            x0, y0, x1, y1 = ext
            corners = [to_file(x, y) for x, y in ((x0, y0), (x1, y0),
                                                  (x0, y1), (x1, y1))]
            xs, ys = [c[0] for c in corners], [c[1] for c in corners]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            mx = min(lo.X() - x0, x1 - hi.X())
            my = min(lo.Y() - y0, y1 - hi.Y())
            if tag2 == "Fab":
                # informational: a real moulding (and every solder leg) is
                # allowed to cross the library's outline drawing
                print("      vs %-9s x %+5.2f  y %+5.2f" % (tag2 + ":", mx, my))
            elif min(mx, my) < -0.15:
                print("      FAIL: body is %.2f mm outside the courtyard"
                      % -min(mx, my))
                bad += 1
            else:
                print("      inside courtyard       x %+5.2f  y %+5.2f" % (mx, my))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None,
                    help="output dir (default footprints/dpp.3dshapes)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the check against the footprints")
    ap.add_argument("--check", action="store_true",
                    help="check the .step files already on disk, write nothing")
    a = ap.parse_args()
    global OUT_DIR
    if a.out:
        OUT_DIR = a.out
    print("3dshapes dir: %s" % OUT_DIR)
    made = []
    frames = MF.frames_by_package()
    if a.check:
        made = sorted({pkg for pkg, _lib, _l, _r, _m in MF.MODEL_REFS.values()})
        print("\nchecking the files already on disk (nothing written):")
        bad = verify(made)
        if bad:
            raise SystemExit("%d package/pad problem(s) - see above" % bad)
        print("\nall packages land on their own copper, inside their own courtyard.")
        return
    for model, name in all_models():
        (x0, y0, z0), (x1, y1, z1) = model.bbox()
        tag = frames[name][1]
        print("  %-58s %2d solids  %6.2f x %6.2f x %6.2f mm  z %6.2f..%6.2f  file %s"
              % (name, len(model.parts), x1 - x0, y1 - y0, z1 - z0, z0, z1, tag))
        if a.dry_run:
            continue
        os.makedirs(OUT_DIR, exist_ok=True)
        model.write(os.path.join(OUT_DIR, name + ".step"), frame=trsf_for(tag))
        made.append(name)
    if a.dry_run:
        return
    print("\nwrote %d packages to %s" % (len(made), OUT_DIR))
    if a.no_verify:
        return
    print("\nchecking every package against its own footprint:")
    bad = verify(made)
    if bad:
        raise SystemExit("%d package/pad problem(s) - see above" % bad)
    print("\nall packages land on their own copper, inside their own courtyard.")


if __name__ == "__main__":
    main()
