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

Three of the five are now the real vendor package instead of something drawn
here: footprints/vendor_step/ holds those STEP files exactly as fetched and
footprints/vendor_step/SOURCES.md records where each came from, the measured
bounding box and the licensing caveat.  The vendor files use their own origin
convention, so each is re-anchored to its footprint's origin by a rigid move
(VENDOR below spells out the measurement that justifies each one).  The two
that stay parametric - the EL357N in the SO-4 4.4x3.9 variant and the
Littelfuse 521 clip - have no package to be found anywhere: not in the KiCad
release, and the component CAD portals (SnapEDA, Digi-Key Models, Ultra
Librarian, Littelfuse's own design center) all need a browser session and an
account, so they are drawn from their datasheet numbers instead.

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
a body is off.  Coordinates are the footprint library's own convention; for the
parts mounted on B.Cu that is the un-mirrored library frame, because mirror_y()
in kicadfmt.py leaves the model block alone and the viewer mirrors bottom-side
parts itself.

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
    def write(self, path):
        doc = TDocStd_Document(TCollection_ExtendedString("XmlOcaf"))
        app = XCAFApp_Application.GetApplication_s()
        app.NewDocument(TCollection_ExtendedString("XmlOcaf"), doc)
        shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
        colors = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
        for shape, rgb in self.parts:
            label = shapes.AddShape(shape)
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
# the five packages
# ---------------------------------------------------------------------------


def so4_el357n():
    """D1 - EL357N-B photocoupler, "4 PIN ULTRA SMALL SOP".

    Everlight EL357 datasheet (DPC-0000014): compact 4 pin SOP, 2.0 mm max
    profile.  The footprint Package_SO:SO-4_4.4x3.9mm_P2.54mm draws the body
    4.4 x 3.9 with a chamfer on the pin-1 corner, and pads 1.8 x 0.6 at
    x = +-3.15, so the body edge is at x = 2.2 and the leads run out over the
    pad to x = 4.05.
    Body 4.4 x 3.9 in plan, 0.15 off the board, top at 1.85 (<= 2.0 max).
    Leads gull-wing: 0.6 wide (pad width), 0.22 thick, out to x = 3.25, which is
    the drawing's 6.50 mm "across leads" dimension - an earlier version reached
    to 3.85 (7.70 across), wider than the real part, and the feet then hung past
    the pads, which stop at x = 4.05.
    """
    m = Model("SO-4_4.4x3.9mm_P2.54mm")
    body = [(-1.225, -1.95), (2.2, -1.95), (2.2, 1.95),
            (-2.2, 1.95), (-2.2, -0.975)]          # = the footprint Fab, pin-1 chamfer
    m.prism(body, "z", 0.15, 1.85, PLASTIC_BLACK)
    lead = [(2.0, 1.20), (2.75, 0.22), (3.25, 0.22), (3.25, 0.0),
            (2.55, 0.0), (2.0, 0.70)]              # gull wing, profile in x/z
    for sx in (1, -1):
        prof = [(sx * x, z) for (x, z) in lead]
        for py in (-1.27, 1.27):                   # pad rows at y = +-1.27
            m.prism(prof, "y", py - 0.3, py + 0.3, METAL_TIN)
    return m, "SO-4_4.4x3.9mm_P2.54mm"


# ---------------------------------------------------------------------------
# the three real vendor packages, re-anchored to the footprint origin
# ---------------------------------------------------------------------------
#
# A vendor MCAD file never shares KiCad's origin convention, so each one needs a
# rigid move before it can be dropped on its footprint.  The numbers below are
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
VENDOR = {
    "AMASS_XT30U-M_1x02_P5.0mm_Vertical": dict(
        src="AMASS_XT30U-M.step", colour=PLASTIC_XT, move=(2.50, 0.0, 0.0)),
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
    trsf = gp_Trsf()
    if spec.get("rot_z"):
        spin = gp_Trsf()
        spin.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                         math.radians(spec["rot_z"]))
        trsf = spin
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

def all_models():
    """The five packages: vendor MCAD where a package exists, parametric where not."""
    yield so4_el357n()
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

    A B.Cu footprint keeps its pads and courtyard y-mirrored (mirror_y in
    kicadfmt.py) while a package is stored in the library's un-mirrored frame, so
    `back` reports which frame the file is in and the caller flips the sign back
    before comparing anything.
    """
    path = os.path.join(PRETTY, name + ".kicad_mod")
    if not os.path.isfile(path):
        raise SystemExit("%s: no footprint %s to check against" % (name, path))
    tree = sexp(open(path, encoding="utf-8").read())[0]
    pads, fab, cy, back = [], [], [], False

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
                back = back or "B.Cu" in lay
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
    """
    bad = 0
    for name in names:
        pads, fab, cy, back = footprint_geom(name)
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
        print("  %s" % name)
        print("      body %6.2f x %6.2f mm, z %6.2f .. %6.2f (%d solids)"
              % (hi.X() - lo.X(), hi.Y() - lo.Y(), lo.Z(), hi.Z(),
                 len(_solids(shape))))
        worst = 0.0
        for p in pads:
            y = -p["y"] if back else p["y"]
            if p["drill"]:
                how = "pin"
                got = _material_at(shape, p["x"], y, -1.2, -0.6,
                                   max(0.30, p["drill"] / 2.0))
            else:
                how = "tail"
                got = _material_at(shape, p["x"], y, 0.02, 0.45, 0.30)
            if got is None:
                print("      pad %-3s  NO %s OVER THE PAD (%.2f x %.2f at %.2f, %.2f)"
                      % (p["name"], how, p["w"], p["h"], p["x"], y))
                bad += 1
                continue
            worst = max(worst, got[0])
            print("      pad %-3s  %-4s %5.2f mm off its centre" % (p["name"], how, got[0]))
        if worst > 0.35:
            print("      FAIL: a pin or tail is %.2f mm off its pad" % worst)
            bad += 1
        for tag, ext in (("Fab", fab), ("courtyard", cy)):
            if not ext:
                continue
            x0, y0, x1, y1 = ext
            if back:
                y0, y1 = -y1, -y0
            mx = min(lo.X() - x0, x1 - hi.X())
            my = min(lo.Y() - y0, y1 - hi.Y())
            if tag == "Fab":
                # informational: a real moulding (and every solder leg) is
                # allowed to cross the library's outline drawing
                print("      vs %-9s x %+5.2f  y %+5.2f" % (tag + ":", mx, my))
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
    a = ap.parse_args()
    global OUT_DIR
    if a.out:
        OUT_DIR = a.out
    print("3dshapes dir: %s" % OUT_DIR)
    made = []
    for model, name in all_models():
        (x0, y0, z0), (x1, y1, z1) = model.bbox()
        print("  %-58s %2d solids  %6.2f x %6.2f x %6.2f mm  z %6.2f..%6.2f"
              % (name, len(model.parts), x1 - x0, y1 - y0, z1 - z0, z0, z1))
        if a.dry_run:
            continue
        os.makedirs(OUT_DIR, exist_ok=True)
        model.write(os.path.join(OUT_DIR, name + ".step"))
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
