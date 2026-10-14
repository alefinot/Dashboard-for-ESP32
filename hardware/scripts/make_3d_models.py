#!/usr/bin/env python3
"""make_3d_models.py - generate the 3D packages KiCad does not ship for us.

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

Third-party STEP candidates were checked first and none was usable: the free
JST STEP files are the THT BxxB drawing package with its own origin convention
(and Datakit licensing is unclear), the free XT30 STEP is the XT30UPB variant
with a base, and there is no EL357N or Littelfuse 521 package to be found.
So the models are generated here, into STEP, under exactly the file name the
footprint already asks for - the footprint and the board need no edit at all.

Why STEP and not VRML: KiCad 10 reads STEP only.  Tested on this install by
rendering the board and diffing pixels - a real STEP dropped into the XT30 slot
changes 7898 px at its pads, while a .wrl (even a 30 mm test box, also tried
as .x3d and with an absolute path) changes nothing at all.  Upstream confirms
it: packages3D MR !1327 deleted every .wrl, the KiCad 10 release notes ship
"STEP files only", and 10.0.3+ offers to migrate leftover .wrl references.

All dimensions come from the datasheets (cited per model below) and always
agree with the Fab outline + pads of the footprint the model belongs to, so the
body always lands on its own copper.  Coordinates are the footprint library's
own convention; for the parts mounted on B.Cu that is the un-mirrored library
frame, because mirror_y() in kicadfmt.py leaves the model block alone and the
viewer mirrors bottom-side parts itself.

Needs  pip install --no-deps cadquery-ocp      (the OCCT kernel; ~48 MB wheel)
Usage:  python scripts/make_3d_models.py [--out <3dmodels dir>] [--dry-run]
"""

import argparse
import math
import os

from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepGProp import BRepGProp
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder,
                             BRepPrimAPI_MakePrism)
from OCP.Bnd import Bnd_Box
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_ReturnStatus
from OCP.Interface import Interface_Static
from OCP.Quantity import Quantity_Color, Quantity_TOC_RGB
from OCP.STEPCAFControl import STEPCAFControl_Writer
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDocStd import TDocStd_Document
from OCP.XCAFApp import XCAFApp_Application
from OCP.XCAFDoc import XCAFDoc_ColorGen, XCAFDoc_ColorSurf, XCAFDoc_DocumentTool
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec

# ---------------------------------------------------------------------------
# where to write: KiCad's shared 3dmodels tree, found the way KiCad finds
# KICAD10_3DMODEL_DIR (the env var is unset on this machine, KiCad resolves it
# from its own install location)
# ---------------------------------------------------------------------------


def default_out():
    env = os.environ.get("KICAD10_3DMODEL_DIR")
    if env and os.path.isdir(env):
        return env
    cands = []
    local = os.environ.get("LOCALAPPDATA")
    if local:
        cands.append(os.path.join(local, "Programs", "KiCad", "10.0", "share",
                                  "kicad", "3dmodels"))
    for root in ("C:/Program Files", os.path.expanduser("~")):
        for v in ("10.0", "10", "9.0"):
            cands.append(os.path.join(root, "KiCad", v, "share", "kicad", "3dmodels"))
    cands += ["/usr/share/kicad/3dmodels",
              "/usr/local/share/kicad/3dmodels",
              "/Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels"]
    for c in cands:
        if os.path.isdir(c):
            return c
    raise SystemExit("could not locate KiCad's 3dmodels dir - pass --out")


PLASTIC_BLACK = (0.16, 0.16, 0.17)
PLASTIC_JST = (0.90, 0.88, 0.82)          # JST PH natural nylon
PLASTIC_XT = (0.95, 0.42, 0.05)          # Amass XT orange
METAL_TIN = (0.68, 0.70, 0.72)
METAL_GOLD = (0.83, 0.66, 0.21)          # XT30 contacts are gold plated


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
        if not w.Transfer(doc):
            raise SystemExit("%s: STEP transfer failed" % self.name)
        if w.Write(path) != IFSelect_ReturnStatus.IFSelect_RetDone:
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
    Leads gull-wing: 0.6 wide (pad width), 0.22 thick foot out to x = 3.85.
    """
    m = Model("SO-4_4.4x3.9mm_P2.54mm")
    body = [(-1.225, -1.95), (2.2, -1.95), (2.2, 1.95),
            (-2.2, 1.95), (-2.2, -0.975)]          # = the footprint Fab, pin-1 chamfer
    m.prism(body, "z", 0.15, 1.85, PLASTIC_BLACK)
    lead = [(2.0, 1.20), (2.75, 0.22), (3.85, 0.22), (3.85, 0.0),
            (2.55, 0.0), (2.0, 0.70)]              # gull wing, profile in x/z
    for sx in (1, -1):
        prof = [(sx * x, z) for (x, z) in lead]
        for py in (-1.27, 1.27):                   # pad rows at y = +-1.27
            m.prism(prof, "y", py - 0.3, py + 0.3, METAL_TIN)
    return m, "Package_SO.3dshapes/SO-4_4.4x3.9mm_P2.54mm.step"


def jst_ph(n):
    """JST PH SxxB-PH-SM4-TB - surface mount, horizontal (top entry).

    Built to match this board's footprint (dpp:JST_PH_SxB-PH-SM4-TB..., copied
    from the ePH land pattern) exactly:
      Fab outline  x = +-half, y = -4.4..1.6 full width, then a step in to
                   +-(half-0.8) from y = 1.6 to 3.2 (the wire-entry face), with
                   half = 1.95 + n  (9.90 wide at n = 3, 11.90 at n = 4)
      signal pads  1 x 3.5 at y = +2.85, pitch 2.00, x = -(n-1) .. +(n-1)
                   -> they run from under the body out past its wire-entry face
      MP pads      1.5 x 3.4 at x = +-(half-0.6), y = -2.9 (front corners)
    So the cable comes in at +y and the lock brackets are at the -y corners;
    body height 6.6 (ePH catalog overall).
    """
    half = 1.95 + n
    step = half - 0.8                       # Fab step for the wire-entry face
    m = Model("JST_PH_S%dB-PH-SM4-TB_1x0%d-1MP_P2.00mm_Horizontal" % (n, n))
    outline = [(-half, -4.4), (half, -4.4), (half, 3.2), (step, 3.2),
               (step, 1.6), (-step, 1.6), (-step, 3.2), (-half, 3.2)]
    m.prism(outline, "z", 0.0, 6.6, PLASTIC_JST)          # housing, = Fab
    xs = [(2 * i - (n - 1)) for i in range(n)]            # pad centres, 2.00 pitch
    for x in xs:                                         # contact feet on the pads
        m.box(x - 0.35, x + 0.35, 1.2, 4.5, 0.0, 0.35, METAL_TIN)
        m.box(x - 0.35, x + 0.35, 1.2, 2.0, 0.0, 1.0, METAL_TIN)   # riser under body
    for sx in (1, -1):                                   # lock brackets on the MP pads
        x0, x1 = (sx * (half - 1.1), sx * (half - 0.1))
        m.box(min(x0, x1), max(x0, x1), -4.5, -1.3, 0.0, 0.5, METAL_TIN)
    return m, ("Connector_JST.3dshapes/JST_PH_S%dB-PH-SM4-TB_1x0%d-1MP_P2.00mm"
               "_Horizontal.step" % (n, n))


def xt30u_m():
    """J1 - AMASS XT30U-M: vertical cable male, no base.

    Amass XT30U-M drawing V1.2 dimensions (its text layer, not the picture):
    5.20 across, 13.70 overall above the seating plane, 3.00 as a short
    dimension next to the top of the long one.  Every model of the family puts
    the top of the plastic at 10.70, and 13.70 - 3.00 = 10.70, so 13.70 is
    overall and 3.00 is how far the gold pins stand out past the plastic.
    The footprint's Fab is a 10.20 x 5.20 outline with the chamfered key end at
    -x, and both pads are 3.8 x 3.8 with a 2.7 drill - so the contacts are r
    1.35 (the XT30 pin diameter) running from 1.6 below the board (the solder
    tail; its length is not dimensioned) up to 3.00 above the plastic.
    """
    m = Model("AMASS_XT30U-M_1x02_P5.0mm_Vertical")
    shell = [(-2.6, -1.3), (-2.6, 1.3), (-0.9, 2.6), (7.6, 2.6),
             (7.6, -2.6), (-0.9, -2.6)]                       # = footprint Fab
    m.prism(shell, "z", 0.0, 10.70, PLASTIC_XT)
    for x in (0.0, 5.0):             # contacts: tail, body, 3.00 proud of it
        m.cyl(x, 0.0, -1.6, 13.70, 1.35, METAL_GOLD)
    return m, ("Connector_AMASS.3dshapes/AMASS_XT30U-M_1x02_P5.0mm_Vertical.step")


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
    return m, ("Fuse.3dshapes/Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_"
               "P17.00x5.00mm_D1.30mm_Horizontal.step")


def all_models():
    yield so4_el357n()
    for n in (3, 4):
        yield jst_ph(n)
    yield xt30u_m()
    yield littelfuse_521()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None, help="KiCad 3dmodels dir")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    out = a.out or default_out()
    print("3dmodels dir: %s" % out)
    for model, rel in all_models():
        path = os.path.join(out, rel.replace("/", os.sep))
        (x0, y0, z0), (x1, y1, z1) = model.bbox()
        print("  %-72s %d solids  %6.2f x %6.2f x %6.2f mm  z %6.2f..%6.2f"
              % (rel, len(model.parts), x1 - x0, y1 - y0, z1 - z0, z0, z1))
        if a.dry_run:
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        model.write(path)
    if not a.dry_run:
        print("done - the footprints already ask for exactly these names, so no "
              "footprint or board edit is needed.")


if __name__ == "__main__":
    main()
