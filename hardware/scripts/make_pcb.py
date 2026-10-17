"""Generate the Dashboard++ for ESP32 carrier board (hardware/dashboard-plusplus-carrier.kicad_pcb).

Everything mechanical/electrical lives in the tables below: NETS, PARTS, OUTLINE,
KEEPOUT.  Re-run after every change:

    python make_pcb.py            # writes the board, runs DRC, prints the report

Coordinate system: KiCad coordinates, millimetres, Y increasing downwards
(looking at the TOP / display side of the carrier).  Board outline spans
BOARD_X0..BOARD_X1 x BOARD_Y0..BOARD_Y1.

Sides:
  F.Cu  = display side.  The 4.0" TFT module plugs in here and lies flat.
  B.Cu  = ESP32 side.    The DevKit plugs in here and lies flat.
"""
import os
import re
import subprocess
import sys

import kicadfmt as kf
import model_frame as MF
import pcbheader
import route_pcb

KICAD_CLI = os.environ.get(
    "KICAD_CLI",
    r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                   "dashboard-plusplus-carrier.kicad_pcb")

# ------------------------------------------------------------- geometry ----
# The drawing's envelope is 85.0 x 55.0 (x 10.0 -> 95.0).  BOARD_X0 is pulled
# 10 mm further west so the two bulk electrolytics fit on B.Cu beside the
# DC-DC module - see plan "carrier stage 3 - board extension 95x55, bulk caps
# to B.Cu".  Every part keeps its drawn distance from the east edge (uxv()
# maps the CAD at x = 95 - u), so the extra 10 mm is a tail at the west end.
BOARD_X0, BOARD_Y0 = 0.0, 10.0           # board outline corners
BOARD_X1, BOARD_Y1 = 95.0, 65.0          # 95.0 x 55.0 mm

# ------------------------------------------------------- sheet placement ----
# KiCad 10 pins the printed sheet to (0,0) of the board coordinate system, so a
# board drawn at x 0..95 / y 10..65 lands in the corner of the A4 landscape page
# (297 x 210 mm) instead of the middle of it.  PAGE_DX / PAGE_DY shift what is
# WRITTEN TO THE FILE so the outline is dead centre on that page:
#   outline -> x 101.0..196.0, y 77.5..132.5   (page centre is 148.5, 105.0)
#   margins -> 91 mm left/right, 67.5 mm top/bottom, inside the drawing border
#              (the border the plotter draws sits at x 10..287, y 10..200)
# Design coordinates stay in the old frame - the tables in this file,
# cad/*.geo.json and every number in PINOUT.md still read the outline as
# BOARD_X0..BOARD_X1 x BOARD_Y0..BOARD_Y1.  The offset is applied only by the
# emitters (through _onsheet), so the advisory hints, the internal checks and
# the footprint library keep their familiar numbers.  Gerbers and drills DO move
# with the drawing: the KiCad datum is the coordinate origin, which now sits
# 101 mm west / 67.5 mm north of the board's north-west corner.
PAGE_DX, PAGE_DY = 101.0, 67.5


def _onsheet(x, y):
    """Design-frame (x, y) -> position on the A4 sheet (PAGE_DX/PAGE_DY shift)."""
    return x + PAGE_DX, y + PAGE_DY

# ---------------------------------------------------------------- CAD frame --
# Every position below comes from the CAD drawing
# (hardware/screenshots/Dashboard++ PCB with dimensions.pdf), extracted as
# vectors by hardware/scripts/extract_cad.py into
# hardware/cad/Dashboard++ PCB with dimensions.geo.json; the reading of each
# view is written up in hardware/cad/layout_notes.md.
# The drawing's own frame is (u = mm right, v = mm down, origin = view outline
# corner); it maps to KiCad as
#       x = 95.00 - u      y = 10.00 + v
# i.e. the whole board mirrored left-right, which puts the DevKit's antenna
# keep-out in the east column exactly as the built board does.
def cad(u, v):
    """Drawing coordinates -> KiCad board coordinates."""
    return 95.0 - u, 10.0 + v


def cadbox(u0, v0, u1, v1):
    a, b = cad(u0, v1), cad(u1, v0)
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]))


# ESP32 DevKit V1 clone (30 pin, 2x15) hanging under the carrier (B.Cu).
# Body drawn u 8.80..58.80 x v 13.49..41.49 (50.00 x 28.00); the two header
# strips are drawn 36.99 x 1.00 (u 12.81..49.81) at v 13.81..14.80 and
# v 40.21..41.20, which puts their centre lines 26.40 apart - a drafting
# error: the real DevKit V1 rows are 25.40 apart, and 28.00 - 25.40 = 2 x
# ~1.3 mm of body margin is exactly what the drawn body shows.  The rows are
# kept on the drawn mid-line and spaced 25.40; pin 1 keeps the drawn west end
# (inset half a pad from the strip end so the 15 pads sit inside the strip).
STRIP_N = cadbox(12.81, 13.81, 49.81, 14.80)    # 45.19,23.81 .. 82.19,24.80
STRIP_S = cadbox(12.81, 40.21, 49.81, 41.20)    # 45.19,50.21 .. 82.19,51.20
ESP_MID = (STRIP_N[1] + STRIP_N[3] + STRIP_S[1] + STRIP_S[3]) / 4.0    # 37.505
ESP_X0 = STRIP_N[0] + (STRIP_N[2] - STRIP_N[0] - 14 * 2.54) / 2.0        # 45.905
ESP_ROWS = {
    "row_spacing": 25.40,               # centre-to-centre of the two header rows
    "pitch": 2.54,
    "pins": 15,
    "y_near": ESP_MID - 12.70,          # 24.805 north row (J10a), VIN at the west
    "x_start": ESP_X0,                  # pin 1 (USB end of the dev board)
    "x_end": ESP_X0 + 14 * 2.54,        # pin 15 (EN) = 81.465
    "y_far": ESP_MID + 12.70,           # 50.205 south row (J10b), 3V3 at the west
}

# DevKit board outline under the carrier (drawn 50.00 x 28.00)
ESP_BODY = cadbox(8.80, 13.49, 58.80, 41.49)     # 36.20,23.49 .. 86.20,51.49

# TFT display socket J3: drawn as a 1.00 x 35.00 slab standing 10.02 mm above
# F.Cu at the board edge (u 1.99..2.99, v 7.00..42.00).  The drawing's block is
# "Display header 14 pin", so J3 is a **1x14** socket, centred in that 35.00 mm
# slot: the pad row spans y 17.99..51.01 (33.02 = 13 x 2.54) and the 35.56 mm
# body stands 0.28 mm past each end of the slot - the earlier 1x16 reading of
# the module's silks put 40.64 mm of socket 5.6 mm into the drawing's south gap,
# which is what that correction was about.
#
# Which END of the row is pad 1 is what matters, and **pad 1 = VCC sits at the
# SOUTH end, y 51.01**; the stack runs north from there (VCC GND CS RST DC SDI
# SCK LED | SDO T_CLK T_CS T_DIN TPEN T_DO), so the far north pad (y 17.99) is
# pad 14 = T_DO.  The stock 1x14 strip grows toward +y (south) from its pad-1
# origin, so that origin is pinned to (92.51, 51.01) and the footprint is turned
# 180 degrees about it: square pad, courtyard chamfer and the "1" all land on the
# VCC end.  The board used to sit the other way round (V5 on the pad at y 17.99);
# the 180-degree experiment that preceded this only renumbered the same holes and
# left VCC at the north, which was still wrong - this is the change that actually
# moves the nets, and with no track on the board yet it moves nothing but pad
# nets and the ratsnest.  See TFT_MAP below: all 14 pads are named, six of them
# deliberately dead-ends.
TFT = {"x": 92.51, "y1": 51.01, "pitch": 2.54, "pins": 14}   # pad 1 (VCC) south
TFT["y_end"] = TFT["y1"] - (TFT["pins"] - 1) * TFT["pitch"]  # 17.99 (pad 14)

# LM2596 step-down module, soldered to the BOTTOM face.
# Drawn 21.00 x 43.00 (u 61.75..82.75, v 8.64..51.64) with silks `in +` / `-`
# on the u-small (north) end and `out +` / `-` on the south end, `+` on the
# u-large side -> in KiCad: IN pair to the north, OUT pair to the south, `+`
# pads to the west.  Pad pairs read off the silks: y 21.91 / 60.90, x 17.0 apart.
LM = {"x": 22.75, "y": 41.405, "span_x": 17.00, "span_y": 38.99}
LM_BODY = cadbox(61.75, 8.64, 82.75, 51.64)      # 12.25,18.64 .. 33.25,61.64

# GNSS J8: drawn as a 22.99 x 7.00 "6 solder pads" zone (u 35.81..58.80,
# v 0.00..7.00) - built as six bare SMD pads, no header, no drill.
GNSS_ZONE = cadbox(35.81, 0.00, 58.80, 7.00)     # 36.20,10.00 .. 59.19,17.00

# Test-point row (TP1..TP5), on J1's pad row.  Shared by parts() and silks():
# the silks carry a short form of the NET each probe sits on, not its
# designator - full net names (IGN_SENSE & co) are 6 mm wide and cannot share
# the 4.50 mm pitch, the words below can.
#
# TP_X0 52.15 puts the middle probe (TP3) on x 61.15, the midpoint of the free
# run along that row: Q1's pad-2 east edge (x 27.30) to the board's east edge
# (x 95.00).  The row is therefore centred in the north strip east of the 12 V
# column instead of riding the first five columns of the small-parts lattice.
# What that costs and buys, all measured off the board (KiCad DRC is clean
# through the whole move: 0 violations, 0 warnings, 115 unconnected as before):
#   - the row leaves the 4.50 mm lattice (39.60..57.60 -> 52.15..70.15), so the
#     "one grid top to bottom" reading of the mid band goes; the pitch stays 4.50.
#   - the five probe nets get shorter, not longer - their circuitry sits east of
#     the old row (nets-report.md MSTs): HALL 42.3 -> 32.1, FUEL_SNS 83.8 -> 76.0,
#     BAT_SNS 58.2 -> 51.5, IGN_SENSE 49.5 -> 48.6, GND 262.2 -> 260.6.
#   - the far-face neighbours: J8's GNSS pads (x 40.44..54.94) pass under the
#     west end - TP1 sits over J8.5, 0.24 mm off J8.6 (as before, the row was
#     never clear of them); TP3 lands on J14's left mounting tab (x 60.90..
#     62.40 / y 10.40..13.80) with a 1.00 x 0.50 mm cross-face overlap, TP4/TP5
#     stand 1.36 mm north of J14's signal pads and TP5 0.70 mm from that
#     connector's right tab.  Different faces, 1.6 mm of board in between, and
#     the row stays 12.06 mm north of the antenna keep-out (y 26.80), so the
#     keep-out never sees a probe.
#   - the ink follows the row, so BAT / FUEL / GND (x 59.96..70.95, y 11.77..
#     12.57) and the TEST PROBES caption (x 56.00..65.76, y 15.84..16.84) now
#     sit in that same J14 band on the far face - no DRC hit (those custom JST
#     footprints carry no courtyard, and the silk is on the other side), but it
#     is crowded there.
TPS = [("TP1", "IGN_SENSE"), ("TP2", "HALL"), ("TP3", "BAT_SNS"),
       ("TP4", "FUEL_SNS"), ("TP5", "GND")]
TP_SHORT = {"IGN_SENSE": "IGN", "HALL": "HALL", "BAT_SNS": "BAT",
            "FUEL_SNS": "FUEL", "GND": "GND"}
TP_X0, TP_PITCH, TP_Y = 52.15, 4.50, 13.99
# Ink width of each word (stroke centre-lines), measured off the plotted
# F.SilkS (KiCad's stroke font, 0.80 mm glyphs, 0.12 mm stroke) so the labels
# sit centred on their pad instead of on a 0.70 mm-per-glyph guess, which ran
# ~0.2 mm wide.  At this pitch the tightest pair (FUEL | GND) keeps 2.12 mm.
TP_INKW = {"IGN": 1.64, "HALL": 2.55, "BAT": 1.83, "FUEL": 2.63, "GND": 2.13}
TP_BEAR = 0.27        # ink starts this far right of the anchor (left bearing)

# Four JST-PH zones, 12.01 x 7.00 each, two designators per zone (the drawing
# writes both); one right-angle SMD shell per zone, two circuits each.
# ELEV view: the shells hang 5.01 mm below B.Cu, flush with the board edge.
JST_ZONES = {
    "J14": (cadbox(20.81, 0.00, 32.82, 7.00), ("J14", "J16")),   # TRIP + EN
    "J17": (cadbox(8.80, 0.00, 20.81, 7.00), ("J17", "J5")),     # IGN + coolant
    "J6": (cadbox(8.80, 48.01, 20.81, 55.01), ("J6", "J7")),     # LDR + fuel
    "J9": (cadbox(20.81, 48.01, 32.82, 55.01), ("J9",)),         # hall (+V SIG G)
}

# JP1 (hall +V selector): drawn 6.00 x 3.50 box, silks `JP1` / `DIP SWITCH`.
# The pads deliberately do NOT sit in that box any more - see parts() for the
# 5.00 mm shift west (JP1_OFF) away from the hall plug.
JP1_BOX = cadbox(34.65, 49.74, 40.66, 53.24)     # 54.34,59.74 .. 60.35,63.24
JP1_OFF = (-5.00, 0.00)

# Mounting holes - the drawing has none; these four sit 3.50 mm inside the four
# board corners (M3/M4 clearance): the Ø3.2 drill keeps 1.90 mm to the edge and
# the Ø5.0 keep-out ring keeps 1.00 mm.  The west pair lands in the 10 mm tail,
# north and south of the bulk-can pair; the east pair is unchanged.
MOUNTS = [(3.50, 13.50), (91.50, 13.50), (91.50, 61.50), (3.50, 61.50)]
MOUNT_DRILL, MOUNT_RING = 3.2, 5.0

# Annotation zones from the CAD drawing (F.Cu work areas).  They used to be
# dashed rectangles on Dwgs.User; they are kept here as the drawing's record and
# in PINOUT.md section 1 - nothing but the outline, copper, courtyards and silks
# is drawn on the board now.
ZONE_SPARE_N = cadbox(13.74, 0.80, 57.13, 13.33)    # 37.87,10.80 .. 81.26,23.33
ZONE_SPARE_S = cadbox(13.74, 41.17, 57.13, 53.73)   # 37.87,51.17 .. 81.26,63.73
ZONE_SMD_MID = cadbox(13.74, 17.89, 57.13, 36.80)   # 37.87,27.89 .. 81.26,46.80
ZONE_SMD_WEST = cadbox(57.84, 0.80, 84.01, 53.73)   # 10.99,10.80 .. 37.16,63.73

# copper keep-out under the DevKit PCB antenna (between EN and IO23, all layers)
KEEP = (ESP_ROWS["x_end"] - 8.6, ESP_ROWS["y_near"] + 2.0,
        ESP_BODY[2] + 0.6,       ESP_ROWS["y_far"] - 2.0)


# ------------------------------------------------ routing: classes, widths --
# Four copper classes:
#
#   Power   DC_IN DC_F DC_12 V5 - the 1 A rails.  Outer layers are 1 oz:
#           IPC-2221 external, 10 C rise -> 0.80 mm carries about 2.0 A, so
#           0.80 mm is both the drawn width and the class minimum.  Clearance
#           0.60 mm to any other class.
#   Ref     V3V3 - the 3V3 that comes OUT of the DevKit's own AMS1117, loaded
#           only by sensor references: OK1/OK2 input LEDs, the fuel excitation
#           R5 (220R), the two 10k reference legs R6 R7, the C13 bypass.
#           Worst case under 0.1 A, so 0.40 mm (about 1.2 A on the same IPC
#           table), 0.30 mm clearance and a normal 0.70/0.35 via is the right
#           size for it.  Not a cosmetic call: as a Power net its 1.30 mm via
#           and 0.60 mm keep-away sat in the BAT_DIV column and walled C5.1 in
#           on all four sides, and BAT_SNS refused to route at any resolution.
#   HV      IGN_12 HALL_VCC MOS_G - 12 V referenced, milliamps.  Thin traces are
#           fine (0.25 mm) but the 0.60 mm creepage to everything else is not
#           negotiable.
#   Default everything else, GND included: 0.25 / 0.25.  GND is drawn 0.60 mm
#           anyway because it is the tree, not because it is power.
#   HV      IGN_12 HALL_VCC MOS_G - 12 V referenced, milliamps.  Thin traces are
#           fine (0.25 mm) but the 0.60 mm creepage to everything else is not
#           negotiable.
#   Default everything else, GND included: 0.25 / 0.25.  GND is drawn 0.60 mm
#           anyway because it is the tree, not because it is power.
#
# Fab limits (JLCPCB 4-layer JLC04161H-7628): min track/space 0.127 mm (0.09 mm
# extreme), PTH-to-track 0.33 mm, hole-to-hole 0.5 mm, via 0.2 mm drill on a
# 0.45 mm pad.  They go into the project file (sync_project below).
#
# KiCad 10 no longer accepts a net_class block in the .kicad_pcb - measured:
# loading a board that carries one fails with "Failed to load board: ...
# Expected ''symbol'' in ...[130]" - so the classes and their net membership
# live in the PROJECT: sync_project() writes net_settings.classes plus the
# netclass_patterns that attach each net to a class, and `kicad-cli pcb drc`
# does police them (a V5 track pulled to 0.10 mm from a GND track reports
# `netclass "Power" clearance 0.6000 mm; reale 0.1000 mm`).  An earlier note
# here claimed the project patterns were ignored; that test appended the test
# segments AFTER the board's closing paren, so KiCad never read them.  The gate
# that keeps this honest is scripts/test_drc_classes.py.
# A class in the project format carries a default track_width but no MINIMUM
# width (a 0.25 mm V5 track under the Power class at 0.8 mm raises no width
# violation), so the width half of the separation is still enforced by the
# router (every net is inflated by max(its own clearance, the neighbour's)) and
# cross-checked on the emitted copper by classcheck() below.
POWER = ("DC_IN", "DC_F", "DC_12", "V5")
HV = ("IGN_12", "HALL_VCC", "MOS_G")
REF = ("V3V3",)   # DevKit 3V3 out -> sensor references, under 0.1 A
CLASS_OF = {n: "Power" for n in POWER}
CLASS_OF.update({n: "HV" for n in HV})
CLASS_OF.update({n: "Ref" for n in REF})
CLASS_CLEAR = {"Default": 0.25, "Power": 0.60, "HV": 0.60, "Ref": 0.30}
CLASS_MINW = {"Default": 0.25, "Power": 0.80, "HV": 0.25, "Ref": 0.40}
CLASS_VIA = {"Default": (0.70, 0.35), "Power": (1.30, 0.60), "HV": (0.70, 0.35),
             "Ref": (0.70, 0.35)}
#   0.25 mm -> ~0.9 A, 0.50 -> ~1.5 A, 0.80 -> ~2.0 A (1 oz outer, 10 C rise)
NET_W = {n: 0.80 for n in POWER}
NET_W["GND"] = 0.60
NET_W["V3V3"] = 0.40
TRACE_W = 0.25                       # class Default
MIN_W = 0.20                         # board rule, fab limited

# route order: the rails get the widest first bite, then the ground tree, then
# the 12 V sensing legs, then the analogue chain, then the DevKit signals, then
# the spares and the debug pair - they have room to spare, so they go last.
ROUTE_ORDER = [("power", list(POWER) + list(REF)), ("gnd", ["GND"]), ("hv", list(HV)),
               ("analog", ["IGN_LED", "IGN_SENSE", "HALL_LED", "HALL_SIG",
                           "HALL_OUT", "HALL", "BAT_DIV", "BAT_SNS",
                           "FUEL_SNS", "COOL_SNS", "LDR_SNS", "TRIP_BTN",
                           "EN"]),
               ("tft+gps", ["TFT_CS", "TFT_DC", "TFT_SCK", "TFT_MOSI",
                            "TFT_RST", "TFT_BL", "GPS_RX", "GPS_TX"]),
               ("spares", ["BOOT0", "SPARE_D26", "SPARE_D21", "SPARE_D22",
                           "SPARE_D19", "SPARE_D15", "SPARE_D13", "SPARE_D2"]),
               ("debug", ["DBG_TX", "DBG_RX"])]


def net_w(net):
    return NET_W.get(net, TRACE_W)


def net_span(pads):
    """Bounding-box perimeter of a net's pad centres, in mm - how much territory
    the net needs.  Inside a route group the short local nets are routed first:
    a 6 mm RC hop has to claim its own patch of copper before a 40 mm run of
    the same group cuts through that patch - COOL_SNS (R6.2 to C7.1) ended up
    in a pocket walled in by copper routed before it, 14 unconnected items."""
    xs = [0.5 * (p["box"][0] + p["box"][2]) for p in pads]
    ys = [0.5 * (p["box"][1] + p["box"][3]) for p in pads]
    return (max(xs) - min(xs)) + (max(ys) - min(ys))


def net_clr(net):
    return CLASS_CLEAR.get(CLASS_OF.get(net, "Default"), 0.25)


def net_via(net):
    return CLASS_VIA.get(CLASS_OF.get(net, "Default"), CLASS_VIA["Default"])


def route_pads():
    """Every copper pad as the router wants it: box, layers, drill.

    Mirrors pads_abs() (which build() already calls) but keeps the drill and the
    layer list, and reports a THT pad on both outer layers: a drilled pad is
    copper on every layer it passes through, which is what makes it the cheapest
    way onto the inner ground planes (and the router's hole rules - hole to hole
    0.5 mm, drilled hole to foreign copper 0.33 mm - are read off the same list).
    """
    out = []
    for ent in parts():
        ref, fp_id, x, y, rot, netmap, _value, layer = ent[:8]

        def nm(v):
            if isinstance(v, int):
                return NETS[v - 1]
            if isinstance(v, tuple):
                return v[1]
            return v

        if fp_id is None:
            raw = [(num, px, py, w, h, dr,
                    nm(net) if net != "npth" else None) for (num, px, py, w, h,
                                                              dr, net) in ent[5]]
        else:
            raw = []
            for (num, px, py, w, h, dr, cu) in kf.pads_full(fp_id):
                net = netmap.get(num)
                if net == "NC":
                    net = None
                # an unbound pad is still copper.  J9 / J14 / J17's mounting
                # tabs (Pad MP, no net) were skipped here, invisible to the
                # router, and tracks and vias landed on them (DRC
                # shorting_items "coll GND and <no net>").  Keep them with
                # net None: add_static turns those into "keep"-class obstacles.
                if layer == "B.Cu":
                    cu = tuple("B.Cu" if c == "F.Cu" else c for c in cu)
                raw.append((num, px, py, w, h, dr, nm(net) if net else None))
        mir = pad_mirrored(ref, layer, fp_id)
        for (num, px, py, w, h, dr, net) in raw:
            bx1, by1, bx2, by2 = _padbox(px, -py if mir else py, w, h, rot, x, y)
            out.append(dict(ref=ref, pad=str(num), net=net, drill=dr or 0.0,
                            layers=("F.Cu", "B.Cu") if dr else (layer,),
                            box=(bx1, by1, bx2, by2)))

    # Sub-shapes of a pad are not separate obstacles.  TO-263-3_TabPin2 tiles its
    # 9.4 x 10.8 tab (pad "2") with four unnumbered 4.55 x 5.25 paste sub-pads;
    # an unnumbered pad carries no net, so the router saw them as four foreign
    # "keep" obstacles covering the very tab it had to reach, and the DC_12 A* had
    # nowhere to land (Q1's tab came out unconnected in DRC).  A pad that lies
    # inside a bigger pad of the same footprint is a subdivision of that pad's
    # copper, not something a track has to keep away from.
    by_ref = {}
    for p in out:
        by_ref.setdefault(p["ref"], []).append(p)
    drop = set()
    for ref2, ps in by_ref.items():
        big = [p["box"] for p in ps if p["net"] and p["drill"] == 0.0
               and p["pad"] != ""]
        for p in ps:
            if p["net"]:
                continue
            x0, y0, x1, y1 = p["box"]
            for (bx0, by0, bx1, by1) in big:
                if (bx0 - 1e-6 <= x0 and x1 <= bx1 + 1e-6 and
                        by0 - 1e-6 <= y0 and y1 <= by1 + 1e-6):
                    drop.add(id(p))
                    break
    return [p for p in out if id(p) not in drop]


def classcheck(segs, vias):
    """Advisory check of the copper the router emitted, by class.

    Not a clone of KiCad's DRC: it only looks at what this generator drew
    (segment vs segment, segment vs pad) with axis-aligned gaps, so it can only
    over-report.  Its job is the half KiCad cannot police from a generated
    project - the per-class MINIMUM WIDTH (a project net class carries a
    default track_width, no minimum) - so it catches a mistake in the routing
    tables above: a 0.25 mm track on a 1 A rail, a 12 V leg that crept up to a
    ground trace.  The clearances are policed by DRC itself now that the nets
    are attached to classes (sync_project); scripts/test_drc_classes.py proves
    that referee fires.
    """
    items = []
    for s in segs:
        (ax, ay), (bx, by) = s["a"], s["b"]
        w = s["w"]
        items.append(("tr", s["net"], s["layer"], min(ax, bx) - w / 2.0,
                      min(ay, by) - w / 2.0, max(ax, bx) + w / 2.0,
                      max(ay, by) + w / 2.0))
    for v in vias:
        items.append(("tr", v["net"], "F.Cu", v["x"] - v["dia"] / 2.0,
                      v["y"] - v["dia"] / 2.0, v["x"] + v["dia"] / 2.0,
                      v["y"] + v["dia"] / 2.0))
        items.append(("tr", v["net"], "B.Cu", v["x"] - v["dia"] / 2.0,
                      v["y"] - v["dia"] / 2.0, v["x"] + v["dia"] / 2.0,
                      v["y"] + v["dia"] / 2.0))
    for p in route_pads():
        if not p["net"]:
            continue
        for lay in p["layers"]:
            items.append(("pad", p["net"], lay, p["box"][0], p["box"][1],
                          p["box"][2], p["box"][3]))
    bad = 0
    for i in range(len(items)):
        ka, na, la, ax1, ay1, ax2, ay2 = items[i]
        for j in range(i + 1, len(items)):
            kb, nb, lb, bx1, by1, bx2, by2 = items[j]
            if la != lb or na == nb or (ka == "pad" and kb == "pad"):
                # pad-to-pad spacing is placement, and placement is what KiCad's
                # courtyard/clearance DRC already polices; land patterns put
                # their own neighbouring pads 0.3 mm apart on different nets by
                # design (the DC-DC module's IN+ next to IN-), so comparing pads
                # to pads only buries the routing faults this check exists for
                continue
            gx = max(bx1 - ax2, ax1 - bx2, 0.0)
            gy = max(by1 - ay2, ay1 - by2, 0.0)
            gap = max(gx, gy) if (gx == 0 or gy == 0) else (gx * gx + gy * gy) ** 0.5
            need = max(net_clr(na), net_clr(nb))
            if gap < need - 1e-3:
                print("  CLASS GAP    %-10s %s %s / %-10s %s: gap %.2f < %.2f"
                      % (na, la, (round(ax1, 2), round(ay1, 2), round(ax2, 2),
                                  round(ay2, 2)), nb,
                         (round(bx1, 2), round(by1, 2), round(bx2, 2),
                          round(by2, 2)), gap, need))
                bad += 1
                if bad >= 12:
                    print("  ... (more suppressed)")
                    break
        if bad >= 12:
            break
    print("  CLASS CHECK %s" % ("%d gaps under class clearance" % bad if bad
                                else "ok - every class gap on the emitted copper"
                                         " clears its class clearance"))
    return bad


def fmt_copper(segs, vias):
    """KiCad 10 tokens for the routed copper.

    KiCad 10 reads a flat (start / end / width / layer) segment; the older
    (stroke (width ...)) form is rejected outright ("Expected start, end, width,
    layer, ...; got 'stroke'"), and a via is (at / size / drill / layers).
    """
    out = []
    for s in segs:
        # the router works in the design frame (the numbers in PINOUT.md); the
        # board file is drawn in the sheet frame - footprints, outlines, zones
        # and silks all go through _onsheet(), and so must the copper, or every
        # track floats 101 mm west / 67.5 mm north of the pads it was routed to
        (ax, ay), (bx, by) = _onsheet(*s["a"]), _onsheet(*s["b"])
        out.append('(segment (net %d) (start %.3f %.3f) (end %.3f %.3f)'
                   ' (width %.3f) (layer "%s") (tstamp "%s"))'
                   % (NETS.index(s["net"]) + 1, ax, ay, bx, by, s["w"],
                      s["layer"], kf.uid()))
    for v in vias:
        (vx, vy) = _onsheet(v["x"], v["y"])
        out.append('(via (net %d) (at %.3f %.3f) (size %.3f) (drill %.3f)'
                   ' (layers "F.Cu" "B.Cu") (tstamp "%s"))'
                   % (NETS.index(v["net"]) + 1, vx, vy, v["dia"],
                      v["drill"], kf.uid()))
    return out


def sync_project(path):
    """Push the class defaults and the fab limits into the .kicad_pro.

    KiCad 9/10 keeps the net classes AND the design rules in the PROJECT file,
    not the board - proved by experiment: setting the project Default clearance
    to 5.0 mm reported 499 clearance violations on an untouched board, and the
    .kicad_pcb has no design_settings block at all.  The generator writes them
    here and leaves the rest of the file alone.
    """
    import json
    try:
        prj = json.load(open(path, encoding="utf-8"))
    except (OSError, ValueError):
        print("  SYNC SKIPPED %s (no project file)" % os.path.basename(path))
        return
    ns = prj.setdefault("net_settings", {})
    base = next((c for c in (ns.get("classes") or [])
                 if c.get("name") == "Default"), {})
    want = []
    for name in ("Default", "Power", "HV", "Ref"):
        c = dict(base)
        c["name"] = name
        c["clearance"] = CLASS_CLEAR[name]
        c["track_width"] = CLASS_MINW[name]
        c["via_diameter"], c["via_drill"] = CLASS_VIA[name]
        c["microvia_diameter"], c["microvia_drill"] = 0.45, 0.2
        c["priority"] = {"Default": 4, "Power": 1, "HV": 2, "Ref": 3}[name]
        want.append(c)
    ns["classes"] = want
    # A class is only a rule if a net belongs to one, and in KiCad 10 that
    # membership is carried here (there is no net_class block in a 10.x board
    # file - see the note at CLASS_OF).  A pattern matches the whole net name
    # top-down: a leading "/" would mean "net under sheet path X", and this
    # board is flat, so the bare name is the exact form.  With this list empty
    # - what used to be written - every net is graded against Default and the
    # numbers above are decoration; scripts/test_drc_classes.py fails if that
    # ever happens again.
    # What the project format cannot express is a per-class MINIMUM width: a
    # class has a default track_width only (measured - a 0.25 mm V5 track under
    # the Power class at 0.8 mm raises no width violation), so classcheck()
    # stays the width referee and rules.min_track_width below is the floor.
    ns["netclass_patterns"] = [{"netclass": CLASS_OF[n], "pattern": n}
                               for n in NETS if n in CLASS_OF]
    r = (prj.setdefault("board", {}).setdefault("design_settings", {})
         .setdefault("rules", {}))
    r.update({"min_clearance": 0.20,        # JLC standard outer is 0.127 mm
              "min_track_width": MIN_W,
              "min_hole_clearance": 0.33,   # drilled hole to foreign copper
              "min_hole_to_hole": 0.50,
              "min_via_annular_width": 0.10,
              "min_via_diameter": 0.45,
              "min_through_hole_diameter": 0.30,
              "min_copper_edge_clearance": 0.30})
    json.dump(prj, open(path, "w", encoding="utf-8"), indent=2)
    print("  PROJECT      %s: %d net classes, %d nets attached to them, "
          "fab limits" % (os.path.basename(path), len(want),
                          len(ns["netclass_patterns"])))


def keepout_clear(bb):
    kx0, ky0, kx1, ky1 = KEEP
    return not (bb[0] < kx1 and kx0 < bb[2] and bb[1] < ky1 and ky0 < bb[3])


# ----------------------------------------------------------------- nets ----
# Net names follow src/dashboard.h (README "Hardware Pinout Matrix" is the
# reference): GPIO4 POWER_SENSE, GPIO33 HALL, GPIO32 FUEL, GPIO35 BATTERY,
# GPIO36 TEMP/coolant, GPIO34 LIGHT (LDR), GPIO25 TRIP_RESET, GNSS 16/17,
# display 18/23/5/27/14 + backlight 12, console 1/3, BOOT 0.
NETS = [
    "GND",              # 0 V everywhere
    "DC_IN",            # XT30 + pin, before the fuse
    "DC_F",             # fused +12 (fuse out, MOSFET source)
    "MOS_G",            # reverse-polarity P-MOS gate node
    "DC_12",            # protected +12 -> LM2596 input, battery divider
    "V5",               # LM2596 output: DevKit VIN, TFT VCC, GPS 5V
    "V3V3",             # DevKit 3V3 pin (its own AMS1117) -> sensor refs
    "IGN_12",           # key-switched +12 pad (ignition sense input)
    "IGN_LED",          # opto-1 LED anode node
    "IGN_SENSE",        # GPIO4 POWER_SENSE (HIGH = ignition on, EXT0 wake)
    "HALL_VCC",         # hall sensor +V, selected 5V/12V by jumper JP1
    "HALL_SIG",         # hall signal pad
    "HALL_LED",         # opto-2 LED anode node
    "HALL_OUT",         # opto-2 emitter-follower output (non-inverting)
    "HALL",             # filtered hall line -> GPIO33 HALL_SENSOR_PIN
    "BAT_DIV",          # 47k/10k battery divider tap
    "BAT_SNS",          # GPIO35 BATTERY_SENSE (after 1k series)
    "FUEL_SNS",         # GPIO32 FUEL_TOUCH_PIN (3V3 - 220R - sender - GND)
    "COOL_SNS",         # GPIO36 TEMP_SENSE (NTC balance node)
    "LDR_SNS",          # GPIO34 LIGHT_SENSOR (LDR + 10k divider)
    "TRIP_BTN",         # GPIO25 TRIP_RESET (button to GND)
    "TFT_CS",           # GPIO5
    "TFT_DC",           # GPIO27
    "TFT_SCK",          # GPIO18
    "TFT_MOSI",         # GPIO23
    "TFT_RST",          # GPIO14
    "TFT_BL",           # GPIO12 backlight, MTDI: must be LOW at reset
    "TFT_SDO",          # display SDO (MISO), socket pad 6 - open, no read path
    "TFT_TCLK",         # touch SPI clock, pad 5 - open (touch removed 1.3.6)
    "TFT_TCS",          # touch chip select, pad 4 - open
    "TFT_TDIN",         # touch SPI data in, pad 3 - open
    "TFT_TDO",          # touch SPI data out, pad 2 - open
    "TFT_TPEN",         # touch T_IRQ / PEN, pad 1 - open
    "GPS_RX",           # GPIO16 <- GNSS module TX
    "GPS_TX",           # GPIO17 -> GNSS module RX (idle)
    "EN",               # DevKit EN (reset jumper)
    "BOOT0",            # GPIO0 boot jumper
    "DBG_TX",           # GPIO1 console TX
    "DBG_RX",           # GPIO3 console RX
    "SPARE_D26",        # spare GPIO pads
    "SPARE_D22",
    "SPARE_D21",
    "SPARE_D19",
    "SPARE_D13",
    "SPARE_D2",
    "SPARE_D15",        # strapping pin MTDO - header only, no button
    "NC",               # unused DevKit pins (IO39)
]


def N(name):
    return NETS.index(name) + 1




def _rot(p, rot):
    r = (360 - rot) % 360          # KiCad's (at x y rot) is clockwise-on-screen
    x, y = p
    if r == 90:
        return (-y, x)
    if r == 180:
        return (-x, -y)
    if r == 270:
        return (y, -x)
    return (x, y)


def box_of(ent):
    """Absolute courtyard bounding box of a part entry (None if unknown)."""
    ref, fp_id, x, y, rot = ent[0], ent[1], ent[2], ent[3], ent[4]
    if fp_id is None:
        # Custom part: ent[9] is the drawn body outline.  U4's real courtyard sits
        # 0.25 mm outside that box, so this hint box is 0.25 mm optimistic on that
        # part only - KiCad's DRC is the referee, not this number.
        pts = [_rot(pt, rot) for pt in ent[9]]
    else:
        c = kf.courtyard(fp_id)
        if not c:
            return None
        pts = [_rot((c[0], c[1]), rot), _rot((c[2], c[1]), rot),
               _rot((c[2], c[3]), rot), _rot((c[0], c[3]), rot)]
    xs = [p[0] + x for p in pts]
    ys = [p[1] + y for p in pts]
    bb = (min(xs), min(ys), max(xs), max(ys))
    if ref in BODY:                        # round bodies bigger than the courtyard
        d = BODY[ref]
        bb = (min(bb[0], x - d / 2), min(bb[1], y - d / 2),
              max(bb[2], x + d / 2), max(bb[3], y + d / 2))
    return bb


# Round / oversized bodies that the KiCad courtyard understates, by ref.
# (the SMD electrolytic cans carry a courtyard bigger than the can itself)
BODY = {}

# Footprints that physically sit on the BOTTOM face: their courtyard only
# clashes with other bottom-side parts, never with top-side ones.
BOTTOM_SIDE = {"J10a", "J10b", "U4", "J1", "J14", "J17", "J6",
               "J9", "J8", "JP1", "C1", "C3"}

# Of those, the ones stored the KiCad way (child geometry mirrored in y).  Without
# that mirroring KiCad's 3D viewer draws the part's body mirrored about the part
# origin: an underside part's shell lands beside its own pads instead of on them
# (a pad row ends up a whole row-length away), and an edge-mounted shell mates
# towards the middle of the board so no cable can reach it.
#   - J10a / J10b / J1 / C1 / C3 are also placed at the angle that puts every pad
#     - and with it every net - back on exactly the copper it sits on today.
#   - The four JST-PH shells are mirrored in place, no compensation, on purpose:
#     their solder tails sit 4.60 mm on one side of the origin and their mounting
#     ears 4.60 mm on the other, so mirroring swaps which group faces the board
#     edge.  That swap is the point - mirrored, the shells mate off the board edge
#     (what PINOUT.md calls "bodies hang outside the outline") instead of into the
#     middle of the board.  Their origins do not move, the ears keep the 0.40 mm
#     edge gap the tails had, and pin 1 stays at the west end of the north pair /
#     east end of the south pair, so no net changes ends.
BCU_FLIP = {"J10a", "J10b", "J1", "C1", "C3", "J17", "J14", "J6", "J9"}
BCU_KEEP_PADS = BCU_FLIP - {"J17", "J14", "J6", "J9"}


# Edge-mounted shells may let their courtyard hang over the outline; the
# RA JST-PH footprints are 10.2 deep against a 7.00 mm drawn zone.
EDGE_OK = {"J14", "J17", "J6", "J9"}


# --- silkscreen designators --------------------------------------------------
# Parts that print their reference ON the board (F.SilkS) rather than only on
# the fabrication layer.  Everything not listed keeps its designator on F.Fab:
# the 2.54 mm parts (DevKit socket, pin headers, debug/spare pad groups, the
# JST shells, XT30) have no room for silk text between their pads - at that
# pitch the text fails KiCad's silk-clearance checks, which is why this was
# originally switched off wholesale - and those parts are identified by their
# outlines and pin-1 marks anyway.  C1/C3 are on the underside, where the
# generator drops all library silkscreen (see kicadfmt.board_footprint), and
# U4 sits under the display, so those three stay on Fab too.
# Sizes: 0.80 mm glyphs for the 0805 lattice, 1.00 mm where there is room.
# The test points print their NET instead of their designator (see silks()),
# so TP1..TP5 stay off this set and their references move to F.Fab.
SILK_ON = {"F1", "Q1", "D1", "D2", "D3", "OK1", "OK2", "C2", "C4", "C11", "C13"}
SILK_ON |= {"R%d" % i for i in range(1, 14)}
SILK_ON |= {"C%d" % i for i in range(5, 11)}
SILK_BIG = {"F1", "Q1", "D1", "D2", "D3", "OK1", "OK2"}
SILK_AT = {"D2": (-5.90, 0.00)}   # ref: (dx, dy) local offset override, from DRC
# D2's designator has to move with Q1's move: the library seats "D2" at local
# (0,-3), which is design (32.25,30.00), and Q1's drain tab now runs to x 32.05 /
# y 21.90..32.70 - the text printed straight over that copper (DRC
# `silk_over_copper`).  Local (-5.90, 0.00) on a rot-90 part puts the anchor at
# design (35.25, 35.90), just outside the diode's own courtyard and clear of the
# tab; the DRC is clean with it there.  The ink measures x 34.70..35.70 /
# y 35.11..36.64 (off the F.SilkS gerber): 1.46 mm below D2's courtyard, 2.65 mm
# east of the tab, and it reads vertically like C2's and C4's labels.  North-west
# of the part is not an option: the module's IN- pad (x 29.50..33.00,
# y 19.39..21.89, far face) sits there.


def holes_of(ent):
    """Absolute (x, y, drill) of every drilled pad of a part."""
    fp_id, x, y, rot = ent[1], ent[2], ent[3], ent[4]
    pads = ([(p[0], p[1], p[2], p[3], p[4], p[5]) for p in ent[5]]
            if fp_id is None else kf.pads_of(fp_id))
    out = []
    for (_n, px, py, _w, _h, dr) in pads:
        if dr <= 0:
            continue
        dx, dy = _rot((px, py), rot)
        out.append((dx + x, dy + y, dr))
    return out


def _padbox(px, py, w, h, rot, ox, oy):
    """Absolute bounding box of a pad (rotated rect -> axis-aligned box)."""
    hw, hh = _rot((w / 2.0, 0.0), rot), _rot((0.0, h / 2.0), rot)
    ex, ey = max(abs(hw[0]), abs(hh[0])), max(abs(hw[1]), abs(hh[1]))
    cx, cy = _rot((px, py), rot)
    return (cx + ox - ex, cy + oy - ey, cx + ox + ex, cy + oy + ey)


def pad_mirrored(ref, layer, fp_id):
    """True when a footprint's pad offsets are stored y-negated on the board.

    kicadfmt.board_footprint mirrors a B.Cu part's children in y (bcu_flip) the
    way KiCad's GUI stores an underside footprint.  When the pad pattern can
    take a compensating placement angle (bcu_keep_pads, i.e. ref in
    BCU_KEEP_PADS) it re-picks rot so the copper stays where the placement math
    put it; for the four shells in BCU_FLIP - BCU_KEEP_PADS (J6, J9, J14, J17)
    it cannot, so their copper really is at rot(px, -py) about the origin.
    Anything that models those pads (the router's pad table, the sanity checks)
    has to mirror too, or it routes to 5.7 mm of empty board and parks copper on
    top of the connector - which is exactly what DRC reported as shorting_items
    and 20-odd unconnected pads.
    """
    return (fp_id is not None and layer == "B.Cu" and ref in BCU_FLIP
            and ref not in BCU_KEEP_PADS)


def pads_abs(ent):
    """(ref, pad, net, copper-layers, box) for every bound pad of a part entry.

    Used only for the build-time sanity checks (drc() is the authority).
    """
    ref, fp_id, x, y, rot, netmap, _value, layer = ent[:8]

    def nm(v):
        if isinstance(v, int):
            return NETS[v - 1]
        if isinstance(v, tuple):
            return v[1]
        return v

    out = []
    if fp_id is None:
        for (num, px, py, w, h, dr, net) in ent[5]:
            if net == "npth":
                continue
            cu = {"F.Cu", "B.Cu"} if dr else {layer}
            out.append((ref, num, nm(net) if net else None, cu,
                        _padbox(px, py, w, h, rot, x, y)))
    else:
        mir = pad_mirrored(ref, layer, fp_id)
        for (num, px, py, w, h, dr, cu) in kf.pads_full(fp_id):
            net = netmap.get(num)
            if not net or net == "NC":
                continue
            if layer == "B.Cu":
                cu = {("B.Cu" if c == "F.Cu" else c) for c in cu}
            out.append((ref, num, nm(net), cu,
                        _padbox(px, -py if mir else py, w, h, rot, x, y)))
    return out

# ------------------------------------------------------------- parts -------
# Entry:
#   (ref, footprint_id, x, y, rot, {pad: net}, value, layer, dnp[, outline])
# For headers at rot 90/270 pin 1 sits at (x, y) and the row runs along +X.
def parts():
    P = []

    # ---- ESP32 DevKit sockets, B.Cu (the dev board hangs under the carrier).
    # Rows re-centred on the drawn body (26.40 drawn pitch is a drafting error;
    # the real DevKit V1 is 25.40). Pin 1 = VIN at the west (USB) end.
    near = {str(i): NETMAP_ESP[i] for i in range(1, 16)}
    far = {str(i - 15): NETMAP_ESP[i] for i in range(16, 31)}
    P.append(("J10a", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_near"], 90.0, near, "ESP32-DEVKIT-A",
              "B.Cu", 0))
    P.append(("J10b", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_far"], 90.0, far, "ESP32-DEVKIT-B",
              "B.Cu", 0))

    # ---- TFT display socket J3, F.Cu, standing at the east board edge
    # (drawn 1.00 x 35.00 slab, 10.02 mm above F.Cu; the module is 14-way).
    # Pad 1 (VCC) on the SOUTH pad, y 51.01, turned 180 deg about that origin -
    # the row runs north, pad 14 (T_DO) at y 17.99.
    tft = {str(i + 1): n for i, n in enumerate(TFT_MAP)}
    P.append(("J3", "Connector_PinSocket_2.54mm:PinSocket_1x14_P2.54mm_Vertical",
              TFT["x"], TFT["y1"], 180.0, tft, "TFT-ILI9488", "F.Cu", 0))

    # ---- DC-DC step-down module, soldered to the BOTTOM face (custom fp).
    # Drawn 21 x 43 body, `in +`/`-` at the north end, `out +`/`-` at the south,
    # `+` on the west side -> IN pair north, OUT pair south, `+` pads west.
    hx, hy = LM["span_x"] / 2, LM["span_y"] / 2
    # pads carry no drill - the module lies flat on the underside with its pins
    # soldered straight onto back-side copper, so they emit as SMD rects
    pads = [("1", -hx, -hy, 3.5, 2.5, 0.0, (N("DC_12"), "DC_12")),      # IN +
            ("2", hx, -hy, 3.5, 2.5, 0.0, (N("GND"), "GND")),           # IN -
            ("3", -hx, hy, 3.5, 2.5, 0.0, (N("V5"), "V5")),             # OUT +
            ("4", hx, hy, 3.5, 2.5, 0.0, (N("GND"), "GND")),            # OUT -
            ]
    bx0, by0, bx1, by1 = LM_BODY
    cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
    outl = [(bx0 - cx, by0 - cy), (bx1 - cx, by0 - cy), (bx1 - cx, by1 - cy),
            (bx0 - cx, by1 - cy), (bx0 - cx, by0 - cy)]
    P.append(("U4", None, cx, cy, 0.0, pads, "DPP_DCDC_MODULE", "B.Cu", 0, outl))
    LM["x"], LM["y"] = cx, cy

    # ---- 12 V entry: XT30 on the BOTTOM face, pad pair centred on the DC-DC
    # module's centreline x 22.75 (pad 1 at 20.25, pad 2 at 25.25), shell face
    # 0.89 mm inside the north edge.  It used to sit 5.5 mm west, where the pad
    # pair centred on the module's IN-pair centreline (17.00) rather than on the
    # module itself - and it fell outside the drawn 9.99 x 5.01 zone anyway.
    P.append(("J1", "Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical",
              20.25, 13.99, 0.0, {"1": N("DC_IN"), "2": N("GND")}, "XT30U-M",
              "B.Cu", 0))

    # ---- JST-PH shells, one per drawn 12.01 x 7.00 zone, two circuits each.
    # Refs follow the drawing's first label of each zone; the second designator
    # is printed in the silks (README/PINOUT list both).
    # Right-angle SMD shells (S..-PH-SM4-TB: everything soldered, no drill),
    # mirrored (B.Cu) so they mate off the board edge: mounting ears 0.40 mm from
    # the edge, solder tails folded inward, shells hanging below B.Cu.
    # Their 13.2 courtyards cannot both fit the two 12.01 zones, so each pair
    # is shifted 1.2 mm apart from the zone centres.
    PH4 = "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal"
    PH3 = "Connector_JST:JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal"
    GAP = 0.40                                  # footprint feature -> board edge
    # tails and mounting ears both sit 4.60 mm from the origin, on opposite sides;
    # mirrored (B.Cu) it is the ears that face the edge and the tails fold inward
    NY = BOARD_Y0 + 4.60 + GAP                  # north origin: pads reach 10.40
    SY = BOARD_Y1 - 4.60 - GAP                  # south origin: pads reach 64.60
    for ref, fp, val, nm, x, y, rot in [
            ("J17", PH4, "IGN + COOLANT", {"1": "IGN_12", "2": "GND",
                                           "3": "COOL_SNS", "4": "GND"},
             81.40, NY, 0.0),
            ("J14", PH4, "TRIP + EN", {"1": "TRIP_BTN", "2": "GND",
                                       "3": "EN", "4": "GND"},
             67.00, NY, 0.0),
            ("J6", PH4, "LDR + FUEL", {"1": "LDR_SNS", "2": "GND",
                                       "3": "FUEL_SNS", "4": "GND"},
             81.40, SY, 180.0),
            ("J9", PH3, "HALL", {"1": "HALL_VCC", "2": "HALL_SIG",
                                 "3": "GND"}, 67.00, SY, 180.0)]:
        P.append((ref, fp, x, y, rot, {k: N(v) for k, v in nm.items()}, val,
                  "B.Cu", 0))

    # ---- GNSS J8: six bare solder pads (no header, no drill) inside the drawn
    # 22.99 x 7.00 zone - the module sits under the board and is soldered in.
    gx = (GNSS_ZONE[0] + GNSS_ZONE[2]) / 2 - 5 * 2.54 / 2      # 41.35
    gy = (GNSS_ZONE[1] + GNSS_ZONE[3]) / 2                    # 13.50
    P.append(("J8", None, gx, gy, 0.0,
              [(str(i + 1), i * 2.54, 0.0, 1.8, 1.2, 0.0, (N(net), net))
               for i, net in enumerate(["SPARE_D21", "SPARE_D22", "V5",
                                        "GND", "GPS_TX", "GPS_RX"])],
              "GPS_PADS", "B.Cu", 0,
              [(-1.40, -1.10), (14.10, -1.10), (14.10, 1.10), (-1.40, 1.10),
               (-1.40, -1.10)]))

    # ---- JP1: hall +V selector (5V / sel / 12V) - three bare pads filling a
    # 6.00 x 3.50 box, pin 1 (5V) at the west end.  The box is the drawing's
    # "DIP SWITCH" box shifted 5.00 mm WEST (JP1_OFF) - which is to the RIGHT as
    # read from the underside, the face these pads are on.  Reason: as drawn, the
    # box's east edge stood 0.06 mm off J9's courtyard, i.e. the jumper was
    # flush against the hall plug's shell with nothing to grip.  Moved, it keeps
    # 5.06 mm to that shell; the courtyard outline moves with the pads.
    bx0 = JP1_BOX[0] + JP1_OFF[0]                 # 49.34
    by0, by1 = JP1_BOX[1], JP1_BOX[3]             # y unchanged: 59.74 .. 63.24
    bx1 = JP1_BOX[2] + JP1_OFF[0]                 # 55.35
    jx = bx0 + (bx1 - bx0 - 2 * 2.54) / 2         # 49.805
    jy = (by0 + by1) / 2                          # 61.49
    P.append(("JP1", None, jx, jy, 0.0,
              [(str(i + 1), i * 2.54, 0.0, 1.7, 1.6, 0.0, (N(net), net))
               for i, net in enumerate(["V5", "HALL_VCC", "DC_12"])],
              "JP1_PADS_3", "B.Cu", 0,
              [(bx0 - jx, -1.75), (bx1 - jx, -1.75),
               (bx1 - jx, 1.75), (bx0 - jx, 1.75),
               (bx0 - jx, -1.75)]))

    # ---- mounting holes (the drawing has none; see plan section 6)
    for i, (x, y) in enumerate(MOUNTS, 1):
        P.append(("H%d" % i, None, x, y, 0.0,
                  [("1", 0.0, 0.0, MOUNT_RING, MOUNT_RING, MOUNT_DRILL, "npth")],
                  "MOUNT", "F.Cu", 0,
                  [(-MOUNT_RING / 2, -MOUNT_RING / 2), (MOUNT_RING / 2, -MOUNT_RING / 2),
                   (MOUNT_RING / 2, MOUNT_RING / 2), (-MOUNT_RING / 2, MOUNT_RING / 2),
                   (-MOUNT_RING / 2, -MOUNT_RING / 2)]))

    # ---- 12 V protection chain, F.Cu, in the drawn west small-parts zone
    # (26.17 x 52.92, the column above the module that hangs below).
    # XT30 -> F1 -> Q1 (P-MOS high side) -> module IN+; gate zener D1, R1.
    # All SMD now (no through-hole except the fuse holder itself).
    R805 = "Resistor_SMD:R_0805_2012Metric"
    C805 = "Capacitor_SMD:C_0805_2012Metric"
    # F1 stands with its 5x20 axis north-south, lined up under Q1 rather than
    # dead centre on the DC-DC module: the two protection parts now read as one
    # straight column down the west zone, with the MOSFET's body edge level with
    # D2's (see Q1 below).
    # This clip's *origin* is the pad-1 corner (pad 1 at local (0,0), pad 2 at
    # (17,0), the second column at local y +5.0), and rot 90 turns pad 2 to the
    # north end, facing the module's IN row.  Origin (20.25, 57.15) puts the
    # courtyard on (18.85,37.90)..(26.65,59.40) - centre (22.75, 48.65), the
    # midpoint of the free run down that column: Q1's body edge (y 32.30) to the
    # board's south edge (y 65.00).  The body outline, the courtyard and the pad
    # pair all share that centre, so it is the only sensible "fuse centre".
    # The clip still straddles the module (fuse and glass tube on the display
    # face, module underneath, opposite faces); its courtyard stays wholly inside
    # U4's (module courtyard y 18.39..61.89 against the fuse's 37.90..59.40).
    # Nearest clearances after the 4.41 mm move south (courtyard to courtyard,
    # measured off the board file): 4.95 mm to Q1 to the north, 7.35 mm to C3,
    # 7.53 mm to C1 (far-face courtyards), 7.62 mm to C4, 7.64 mm to D2, 7.87 mm
    # to R1, 11.65 mm to D1, 12.85 mm to the H4 ring, 20.81 mm to J1's courtyard;
    # 5.60 mm from the courtyard to the south board edge, and the courtyard stays
    # wholly inside U4's (module courtyard 12.00,18.39..33.50,61.89).
    # DC_IN arrives at the SOUTH end (y 57.15: that net's MST 43.8 -> 48.2 mm in
    # nets-report.md, and free to drop to an inner layer at the THT pad; its pads
    # now stand 3.10 mm from the module's OUT pads on the far face) and leaves
    # DC_F at the NORTH end (y 40.15): 17.39 mm from the module's IN+ pad, 8.61 mm
    # from Q1's source pad; DC_F's MST 20.6 -> 24.8 mm (the fuse's north pads move
    # 4.41 mm away from Q1's source row).
    P.append(("F1", "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
              20.25, 57.15, 90.0, {"1": N("DC_IN"), "2": N("DC_F")}, "5x20 2A",
              "F.Cu", 0))
    # Q1 steps north of the fuse: its 16.65 x 11.30 courtyard (the D2PAK courtyard
    # carries the copper tab) lands in the band y 21.65..32.95, 0.54 mm clear of
    # F1, with its 14.95 x 10.00 body edge at y 32.30 - level with D2's body edge,
    # which is what the two now share.  What the 4.10 mm costs: the gate pad
    # (15.90..20.50, 24.21..25.31) used to sit straight above the module's IN+ pad
    # (12.50..16.00, 19.39..21.89, far face) and now clears it by 2.32 mm in y,
    # while the drain tab (22.65..32.05, 21.90..32.70) instead ends level with the
    # module's IN- pad row, 0.01 mm short of it.  DC_12's MST 80.8 -> 82.6 mm.
    # 0.70 mm west of D2.
    P.append(("Q1", "Package_TO_SOT_SMD:TO-263-3_TabPin2",
              25.85, 27.30, 0.0, {"1": N("MOS_G"), "2": N("DC_12"), "3": N("DC_F")},
              "IRFR9540N", "F.Cu", 0))
    # D1 (gate zener) and R1 (gate pull-down) stand in the 4.4 mm strip west of
    # that pin column, one above the other, both rot 270 so their centres land on
    # Q1's own pad rows: D1 at y 24.76 (gate row, pads (13.30,23.11)/(13.30,26.41))
    # and R1 at y 29.84 (source row, pads (13.30,28.93)/(13.30,30.75)) - 1.05 mm
    # apart, 1.20 / 1.40 mm west of Q1's courtyard, 1.97 mm (in y) north of the
    # fuse, 0.65 / 0.85 mm from C1's courtyard on the other face.  Q1's own pad
    # rows are y 24.76 (gate) / 27.30 (drain + tab) / 29.84 (source), which is what
    # the two centres are pinned to; both parts ride along with Q1 so that stays
    # true.  MOS_G's MST is 11.0 mm, unchanged, because all four of its pads move
    # together.
    P.append(("D1", "Diode_SMD:D_SOD-123", 13.30, 24.76, 270.0,
              {"1": N("MOS_G"), "2": N("DC_F")}, "BZT52C15V", "F.Cu", 0))
    P.append(("D2", "Diode_SMD:D_SMB",
              35.25, 30.00, 90.0, {"1": N("DC_12"), "2": N("GND")}, "P6SMB33A",
              "F.Cu", 0))
    P.append(("R1", R805, 13.30, 29.84, 270.0,
              {"1": N("MOS_G"), "2": N("DC_F")}, "10k", "F.Cu", 0))
    # The DC_12 parts form the matching column on the east side of Q1, x 35.25:
    # C2 in the tab's row, D2 the TVS below it, C4 far south at the module's OUT
    # end (see below).  Both rot 90, which puts their GND pads north, facing the
    # module's input-GND pad (31.25,20.64, other face).
    # C2 - courtyard x 34.27..36.23 / y 21.50..24.90, measured against the real
    # CrtYd graphics: 1.97 mm west to Q1, 1.45 mm south to D2, 2.62 mm east to
    # J13 (3.81 mm to TP1's pad - test points carry no courtyard).  GND (pad 2)
    # north at (35.25,22.25), 1.75 mm off that module pad; DC_12 (pad 1) south at
    # (35.25,24.15), 2.70 mm east of Q1's drain tab (x 22.65..32.05 / y
    # 21.90..32.70).  That is the HF loop this 100 n is meant to short - MOSFET
    # tab to the module's input pair - rather than the detour from its old seat at
    # (30.00,31.50), 15 mm from the node it decouples.
    # D2 - courtyard x 33.00..37.50 / y 26.35..33.65, 0.70 mm east of Q1's.  Anode
    # (pad 2, GND) north at (35.25,27.85): 1.95 mm off the tab, 4.91 mm off that
    # module pad.  Cathode (pad 1, DC_12) south at (35.25,32.15), 1.95 mm off the
    # tab (since Q1 moved south the tab spans y 21.90..32.70, so it overlaps both
    # pads in y).  Flipping to rot 270 would now buy nothing on the tab side and
    # would throw the anode 5.40 mm further from the switcher's return (10.31 mm
    # against 4.91): the rail side is already a fat copper node (tab + module
    # IN+), the return side is not, so it is the return leg that is worth keeping
    # short.  DC_12's MST now reads 82.6 mm and GND's 262.2 mm (nets-report.md);
    # C2's old seat at (30.00,31.50) was 15 mm from the node it decouples.
    # Every number above comes from nets-report.md, the pad coordinates in
    # drc-report.txt and the CrtYd graphics in the board file - KiCad's own
    # transforms.  A hand-rolled rotation model is not trustworthy: in KiCad's
    # Y-down frame a guessed sign on rot 90/270 silently swaps pad 1 and pad 2,
    # and these courtyards are an fp_rect (0805/SMB), not four fp_lines.
    P.append(("C2", C805, 35.25, 23.20, 90.0,
              {"1": N("DC_12"), "2": N("GND")}, "100n", "F.Cu", 0))
    # Input / output bulk: the two 10 x 12.5 mm cans, moved to the BOTTOM
    # face in the 10 mm west extension (plan "carrier stage 3").  Inside the
    # drawn 85 x 55 envelope there is no B.Cu room for a can near the power
    # section at all (U4 fills x 12.25..33.25 body, 12.00..33.50 once its
    # courtyard is counted, J1 fills y <= 17.09), and on
    # F.Cu they stand 12.5 mm proud of the display side.  Under the board they
    # cost nothing: that face already carries 17 mm of module and a 10 mm
    # XT30.  They now stand together as a pair mid-tail - centres (6.00, 29.60)
    # and (6.00, 45.40), courtyards 2.50 mm apart, 6.95 mm clear of both west
    # corner holes - and each can is turned so pad 1 (+) faces the module pad it
    # belongs to: C1 (220u on DC_12) rot 90 puts + north, 5.25 mm in x / 1.31 mm
    # in y from IN+ (x 12.50..16.00, y 19.39..21.89); C3 (470u on V5) rot 270
    # puts + south, 5.25 x 6.59 mm from OUT+ (y 58.39..60.89), with the In2.Cu
    # V5 plane right there.  The two inner pads are both GND, 3.00 mm apart, so
    # one copper bridge can dress both cans.  Only rot 90/270 fits: rot 0/180
    # turns the 13.3 mm courtyard across the 12.00 mm strip (courtyard to
    # courtyard) and into U4.
    P.append(("C1", "Capacitor_SMD:CP_Elec_10x12.5", 6.0, 29.6, 90.0,
              {"1": N("DC_12"), "2": N("GND")}, "220u/50V", "B.Cu", 0))
    P.append(("C3", "Capacitor_SMD:CP_Elec_10x12.5", 6.0, 45.4, 270.0,
              {"1": N("V5"), "2": N("GND")}, "470u/35V", "B.Cu", 0))
    # C4 (the 5 V HF bypass) finishes the x 35.25 column at the OTHER end of the
    # current path: it sits beside the module's OUT pads instead of stranded in
    # the middle of the west zone.  rot 90 puts its V5 pad south at (35.25,57.95),
    # 1.75 mm from the module's OUT- pad (31.25,59.64, other face); OUT+ sits 17 mm
    # west of that pad, so V5 reaches C4 through the copper pour rather than a
    # short stub - the pair of output pins is too far apart for one 0805 to bridge.
    # GND (pad 2) north at (35.25,56.05).  Nearest courtyards: J10b 8.58 mm, the
    # fuse 8.80 mm.  V5's rats nest drops 1.7 mm and the column reads
    # C2 / D2 / C4 top to bottom.
    P.append(("C4", C805, 35.25, 57.00, 90.0,
              {"1": N("V5"), "2": N("GND")}, "100n", "F.Cu", 0))
    # C13 (the 3V3 decoupler) now sits ON the mid lattice, in the column that
    # runs under the DevKit's 3V3 pin (see the lattice table below); it used to
    # hang in the aisle 1.04 mm under C7, which was the tightest joint on the
    # board.  Its cell was picked for the pin, not for the chain: 7.59 mm from
    # J10b-1 against 6.30 mm for the old off-grid seat, and V3V3's rats nest only
    # pays 1.7 mm for it (47.9 -> 49.6).

    # ---- signal conditioning, F.Cu, in the drawn mid small-parts zone (43.38 x
    # 18.91, under the dev board that hangs below).  Optocouplers are the SMD
    # twin of the PC817 (Everlight EL357N, 4-pin SOP, 2.54 mm leads); they stand
    # at the north end of the band, over the column they serve.
    #
    # The lattice: 6 columns x 5.40 mm, 3 rows x 4.34 mm (x 39.60..66.60, y
    # 35.40..44.08), with a row of optos/TVS above it at y 30.50.  Rows 1-2 carry
    # one signal chain per column, top to bottom (battery divider, ignition, the
    # BAT_SNS hold and the fuel feed, the hall outputs, the coolant feed and the
    # hall LED drive, the ldr feed); row 3 is the filter row, where each cap
    # parks in the column under the DevKit pin it conditions.  Chosen by
    # measurement, not taste:
    #   - HAND SOLDER ROOM (the reason for the change).  The old grid was 4.50 x
    #     3.00, which leaves 1.60 mm pad-to-pad across a column step and 1.55 mm
    #     across a row step: an iron tip is 1.5-2 mm, so 51 of the 57 pad pairs
    #     in the band were under 2.5 mm and you could not get between two joints.
    #     At 5.40 x 4.34 the tightest pair in the band is 2.50 mm and nothing is
    #     below it (measured over pad rectangles, scripts in hardware/scratch).
    #   - RATS NEST.  Grouping by chain instead of by part type (the old grid was
    #     "all resistors in two rows, all caps in one row") shortens the wiring,
    #     measured as the Manhattan MST over every pad of a net (the same table
    #     scripts/netsreport.py prints): over the fifteen nets that live in this
    #     band, 671.1 -> 624.7 mm - LDR_SNS 81.3 -> 59.3, COOL_SNS 58.4 -> 44.0,
    #     HALL_SIG 38.0 -> 19.8, FUEL_SNS 100.2 -> 92.8, IGN_12 60.4 -> 51.9,
    #     HALL_OUT 16.2 -> 12.0.  It is not free: GND spreads 317.9 -> 335.2 and
    #     HALL/HALL_LED gain 13.8/10.1 mm (the caps moved under their pins, away
    #     from the opto side of their chain), BAT_DIV +3.3.  Whole board, all
    #     nets: 2024.9 -> 1995.7 mm.
    #   - R12 (TFT_BL) moves out of the band to the north strip beside C11 for
    #     access and grid symmetry, not for wiring - that net was already running
    #     north to J11 and its span only moves 55.4 -> 56.4 mm.  What it buys is
    #     the lattice cell that C13 (and the 3V3 pin) needed.
    #   - KiCad DRC stays at 0 violations / 115 unconnected / 0 footprint errors
    #     (nothing routed).  Candidate lattices were built and DRC'd as whole
    #     boards in hardware/scratch before this one was committed.
    # The 7th column (x 66.60) still carries C11 in the north strip and D3 here.
    OPTO = "Package_SO:SO-4_4.4x3.9mm_P2.54mm"
    COLS = [39.60, 45.00, 50.40, 55.80, 61.20, 66.60]      # 5.40 pitch
    ROWS = (35.40, 39.74, 44.08)                          # 4.34 pitch
    YBIG = 30.50                                         # opto / TVS row
    # (ref, footprint, value, pad1 net, pad2 net, column, row)
    LATTICE = [
        # col 0 (x 39.60) - battery divider, west end, next to Q1 / F1
        ("R2", R805, "47k",  "DC_12",    "BAT_DIV",  0, 0),
        ("R3", R805, "10k",  "BAT_DIV",  "GND",      0, 1),
        ("R4", R805, "1k",   "BAT_DIV",  "BAT_SNS",  0, 2),
        # col 1 (x 45.00) - ignition input: OK1 stands over it
        ("R8",  R805, "1k",  "IGN_12",    "IGN_LED",   1, 0),
        ("R9",  R805, "10k", "IGN_SENSE", "GND",       1, 1),
        ("C9",  C805, "100n", "IGN_SENSE", "GND",      1, 2),
        # col 2 (x 50.40) - BAT_SNS filter, the fuel excitation, and the 3V3
        # bypass.  C13 is the one cell not chosen by its chain: a bypass cap has
        # to sit next to the pin it protects, and the 3V3 pin (J10b-1) is at
        # (45.91, 50.20) - this cell is 7.59 mm away, against 6.30 mm for the
        # old off-grid seat.  Chosen by scanning every cell: the cheapest seat
        # for C13 (the far east column) is 25 mm from the pin and only 12 mm
        # cheaper on the rats nest, which is a bad trade for a decoupler.
        ("C5",  C805, "100n", "BAT_SNS",  "GND",       2, 0),
        ("R5",  R805, "220R", "V3V3",     "FUEL_SNS",  2, 1),
        ("C13", C805, "100n", "V3V3",    "GND",        2, 2),
        # col 3 (x 55.80) - hall outputs: OK2 stands over it.  The third row of
        # the grid is not part of the column's chain: each filter sits in the
        # column that runs under the DevKit pin it conditions (FUEL_SNS enters on
        # J10a-8, x 68.77, so its cap stops one column west of that, between the
        # pin and R5 on the next column over)
        ("R11", R805, "10k", "HALL_OUT", "GND",        3, 0),
        ("R13", R805, "1k",  "HALL_OUT", "HALL",       3, 1),
        ("C6",  C805, "100n", "FUEL_SNS", "GND",       3, 2),
        # col 4 (x 61.20) - coolant feed, then the hall LED drive
        ("R6",  R805, "10k", "V3V3",     "COOL_SNS",   4, 0),
        ("C7",  C805, "100n", "COOL_SNS", "GND",       4, 1),
        ("R10", R805, "1k",  "HALL_SIG", "HALL_LED",   4, 2),
        # col 5 (x 66.60) - ldr feed and the hall filter, which parks under the
        # HALL pin (J10a-7, x 66.23); D3 stands over this column
        ("R7",  R805, "10k", "V3V3",     "LDR_SNS",    5, 0),
        ("C8",  C805, "100n", "LDR_SNS", "GND",        5, 1),
        ("C10", C805, "100n", "HALL",     "GND",       5, 2),
    ]
    for ref, fp, val, a, b, ci, ri in LATTICE:
        P.append((ref, fp, COLS[ci], ROWS[ri], 0.0,
                  {"1": N(a), "2": N(b)}, val, "F.Cu", 0))

    P.append(("OK1", OPTO, COLS[1], YBIG, 0.0,
              {"1": N("IGN_LED"), "2": N("GND"),
               "3": N("IGN_SENSE"), "4": N("V3V3")}, "EL357N-B", "F.Cu", 0))
    P.append(("OK2", OPTO, COLS[3], YBIG, 0.0,
              {"1": N("HALL_LED"), "2": N("GND"),
               "3": N("HALL_OUT"), "4": N("V3V3")}, "EL357N-B", "F.Cu", 0))
    # The two SO-4 courtyards are 6.50 mm wide, so the optos can only sit two
    # columns apart (5.40 would overlap); D3 (P6SMB TVS on the HALL line) takes
    # the next free slot east of OK2 at the band's east column.
    P.append(("D3", "Diode_SMD:D_SMB", COLS[5], YBIG, 0.0,
              {"1": N("HALL"), "2": N("GND")}, "P6SMB6V2A", "F.Cu", 0))

    # TRIP_BTN filter, right behind the north-edge J14 shell (which hangs below),
    # on the grid's 7th column - same x as D3 under it.  R12 (the TFT_BL pull
    # down) joins it there: the backlight net runs north to J11 anyway, and that
    # freed the lattice cell C13 needed for the 3V3 pin.
    P.append(("C11", C805, 66.60, 21.60, 0.0, {"1": N("TRIP_BTN"), "2": N("GND")},
              "100n", "F.Cu", 0))
    P.append(("R12", R805, 58.98, 21.60, 0.0,
              {"1": N("TFT_BL"), "2": N("GND")}, "10k", "F.Cu", 0))

    # ---- debug / spare: solder pads, not headers (the drawing's two
    # "Area for Spare and debug pads" zones). Pads are 1.7 x 1.0, 2.54 pitch.
    def padgroup(ref, value, x, y, labels, nets):
        n = len(labels)
        pads = [(str(i + 1), i * 2.54, 0.0, 1.7, 1.0, 0.0,
                 (N(net), net) if net != "GND" else (N("GND"), "GND"))
                for i, net in enumerate(nets)]
        return (ref, None, x, y, 0.0, pads, value, "F.Cu", 0,
                [(-1.15, -0.85), ((n - 1) * 2.54 + 1.15, -0.85),
                 ((n - 1) * 2.54 + 1.15, 0.85), (-1.15, 0.85), (-1.15, -0.85)])

    # J13 (debug TX/RX/GND/5V) sits on the SOUTH row line, immediately west of
    # J15's D26 pad: pad 4 at x 59.56 is J15's pad 1 (62.10) one 2.54 mm step to
    # the west, so DEBUG + SPARE read as one 10-pad strip along y 61.50 and one
    # line of pin names prints under it (TX RX GND 5V D26 SDA SCL D19 D15 GND).
    # That row line is the MOUNTING-HOLE line: H3 (91.50, 61.50) and H4 (3.50,
    # 61.50) share it, so the strip, the west hole and the east hole read as one
    # line across the south edge (the row used to sit at y 56.50).  Pad bottom
    # edge y 62.00 = 3.00 mm from the outline; name-strip ink y 62.73..63.53 =
    # 1.47 mm from it; the row stays inside the drawn south spare zone
    # (y 51.17..63.73) and its courtyard stops 13.05 mm short of H3's Ø5.0
    # keep-out ring.  It crosses over J9's underside mounting tabs (B.Cu, y
    # 61.20..64.60 at x 61.90..63.40 and 70.60..72.10) - these DEBUG/SPARE pads
    # are bare SMD rects on the top face with no drill, so the overlap is across
    # the two faces and KiCad is happy (0 violations, 115 unconnected pads).
    # Courtyards 0.24 mm apart - J13 reaches x 60.71, J15 starts at 60.95.  The
    # 5.00 mm shift costs, from nets-report.md: DBG_TX 25.2 -> 26.9, DBG_RX 20.4
    # -> 22.4, SPARE_D26 31.7 -> 36.7, SPARE_D19 6.4 -> 11.3, SPARE_D21 MST 56.6
    # -> 60.5, SPARE_D22 MST 62.2 -> 67.0, GND MST 260.6 -> 268.1; V5 improves
    # instead (MST 159.3 -> 158.4) and V3V3 is untouched.  For the record, when
    # this pair sat at (40.00, 22.00) in the north spare zone the same two
    # console nets were 46.0 and 42.1 mm of rats nest to J10b pins 13/14.
    P.append(padgroup("J13", "DEBUG", 51.94, 61.50,
                      PIN_SILK["J13"], ["DBG_TX", "DBG_RX", "GND", "V5"]))
    P.append(padgroup("J15", "SPARE", 62.10, 61.50,
                      PIN_SILK["J15"], ["SPARE_D26", "SPARE_D21", "SPARE_D22",
                                        "SPARE_D19", "SPARE_D15", "GND"]))
    # D13 (J10a pin 3) and D2 (J10b pin 4) had nowhere to go: on the drawing they
    # are spare DevKit pins with no second pad anywhere, so they were single-pad
    # nets - unreachable, and DRC called them dangling.  They get their own 2-pad
    # solder groups (signal + GND, same 1.7 x 1.0 pads and 2.54 pitch as
    # J13/J15), each inside its own drawn spare zone and on the DevKit's own
    # 2.54 lattice: J16 one lattice row north of the near row (y 19.72), J18 one
    # row south of the far row (y 55.28).  Both clear: 2.46 mm to the socket
    # courtyards (y 23.03 / 51.97), 4.48 mm to the GNSS pads (J8 courtyard y
    # 14.60) and to the hall jumper (JP1 courtyard y 59.74), and the J16 labels
    # print north of the pads in the gap to J8.  That puts D13 6.4 mm and D2
    # 7.3 mm from their DevKit pad instead of 40.
    P.append(padgroup("J16", "SPARE N", 45.91, 19.72,
                      PIN_SILK["J16"], ["SPARE_D13", "GND"]))
    P.append(padgroup("J18", "SPARE S", 48.45, 55.28,
                      PIN_SILK["J18"], ["SPARE_D2", "GND"]))

    # ---- test points.  The row stands on the XT30's own pad line: y 13.99 is
    # J1's pad row, so the 12 V entry and the five probe points read as one row
    # across the north of the board (it used to sit at y 19.50, one lattice row
    # lower, with 5.5 mm of slack between them).  The row runs x 52.15..70.15 -
    # centred on the free run east of the 12 V column (TP_X0 up in the constants:
    # the midpoint of Q1's pad-2 east edge and the board's east edge), which takes
    # it off the lattice columns it used to share with R2-R6 / R8-R12 / C5-C9;
    # the pitch is still 4.50 mm.  The pads land over the GNSS group (J8, soldered
    # on the far face, its own pads y 12.90..14.10) and, at the east end, over
    # J14's mounting tab and signal pads - opposite faces, 1.6 mm of board
    # between them, and neither custom footprint carries a courtyard to collide
    # with.  Neither group is a keep-out; the antenna keep-out stays 12.06 mm
    # south of the row.
    # Two other readings of "align to the XT30" were probed and rejected by DRC:
    # centred on the connector's centreline (x 13.75..31.75 at y 16.30) puts TP2
    # against J1's plated through-hole - shorting_items plus a solder-mask
    # bridge, 8 items; pushed flush to the north edge (y 11.25) the TP designators
    # run off the board (silk_edge_clearance, 5 items).  Here: 0 violations.
    # The five nets they hang on are shorter after this move than before it
    # (MST from nets-report.md): IGN_SENSE 49.5 -> 48.6, HALL 42.3 -> 32.1,
    # BAT_SNS 58.2 -> 51.5, FUEL_SNS 83.8 -> 76.0, GND 262.2 -> 260.6 mm.
    # The silks above them read out a short form of the net (IGN / HALL / BAT /
    # FUEL / GND, captioned TEST PROBES) - see silks() - so the row can be read
    # off the board without a datasheet.
    for i, (ref, net) in enumerate(TPS):
        P.append((ref, "TestPoint:TestPoint_Pad_D1.5mm", TP_X0 + i * TP_PITCH,
                  TP_Y, 0.0, {"1": N(net)}, net, "F.Cu", 0))

    return P


# ESP32 DevKit V1 (DOIT 30-pin) header position -> net.
# Positions are counted from the USB end of the dev board:
#   pads 1..15  = near row (y_near), 1 = VIN at the USB end, 15 = EN at the
#                 antenna end
#   pads 16..30 = far row (y_far),  16 = 3V3 at the USB end, 30 = IO23 at the
#                 antenna end
# Verified against the DOIT DevKit V1 pin map (left col VIN..EN, right col
# 3V3..IO23). Clones vary - the silkscreen labels next to every pad are the
# check, and J10 silks says "CHECK SILK".
NETMAP_ESP = {
    1: "V5", 2: "GND", 3: "SPARE_D13", 4: "TFT_BL", 5: "TFT_RST",
    6: "TFT_DC", 7: "SPARE_D26", 8: "TRIP_BTN", 9: "HALL", 10: "FUEL_SNS",
    11: "BAT_SNS", 12: "LDR_SNS", 13: "NC", 14: "COOL_SNS", 15: "EN",
    16: "V3V3", 17: "GND", 18: "SPARE_D15", 19: "SPARE_D2", 20: "IGN_SENSE",
    21: "GPS_RX", 22: "GPS_TX", 23: "TFT_CS", 24: "TFT_SCK", 25: "SPARE_D19",
    26: "SPARE_D21", 27: "DBG_RX", 28: "DBG_TX", 29: "SPARE_D22",
    30: "TFT_MOSI",
}

# silkscreen label printed next to each ESP socket pad (verify against the board!)
ESP_LABEL = {
    1: "VIN", 2: "GND", 3: "IO13", 4: "IO12", 5: "IO14", 6: "IO27", 7: "IO26",
    8: "IO25", 9: "IO33", 10: "IO32", 11: "IO35", 12: "IO34", 13: "IO39",
    14: "IO36", 15: "EN",
    16: "3V3", 17: "GND", 18: "IO15", 19: "IO2", 20: "IO4", 21: "IO16",
    22: "IO17", 23: "IO5", 24: "IO18", 25: "IO19", 26: "IO21", 27: "IO3",
    28: "IO1", 29: "IO22", 30: "IO23",
}

# TFT 1x14 socket ("4.0'' TFT SPI 480X320 V1.1", the drawing's "Display header
# 14 pin" block).  TFT_MAP is indexed by KiCad **pad number 1..14**, and pad 1 =
# VCC is the SOUTH pad of the row (y 51.01), the stack running north to pad 14 =
# T_DO at y 17.99.  Every pad carries a name now.
# Pads 9..14 - the LCD's SDO and the XPT2046-style touch lines - are single-pad
# nets, unconnected on purpose (no MISO read path in the firmware, touch
# removed in 1.3.6); naming them is what records *why* those six holes are
# empty, where the old board left them as six nameless pads on no net at all.
TFT_MAP = ["V5", "GND", "TFT_CS", "TFT_RST", "TFT_DC", "TFT_MOSI", "TFT_SCK",
           "TFT_BL", "TFT_SDO", "TFT_TCLK", "TFT_TCS", "TFT_TDIN", "TFT_TPEN",
           "TFT_TDO"]
# the label as printed on the display's own silkscreen, same pad order; the
# carrier prints these very words beside the socket (see silks()) - horizontal,
# so there is room for the long forms and nothing is abbreviated.
TFT_LABEL = ["VCC", "GND", "CS", "RST", "DC", "SDI", "SCK", "LED", "SDO",
             "T_CLK", "T_CS", "T_DIN", "TPEN", "T_DO"]

# silkscreen next to the vehicle / accessory headers: (ref, [labels per pin])
# Per-pin silks for the 2.54 mm groups; the 2.00 mm JST-PH shells are named
# separately, one word standing up beside every pad (see JST_PIN_SILK below and
# silks()).  Pin order is fixed by the footprint and documented in PINOUT.
PIN_SILK = {
    "J13": ["TX", "RX", "GND", "5V"],
    "J15": ["D26", "SDA", "SCL", "D19", "D15", "GND"],
    "J16": ["D13", "GND"],
    "J18": ["D2", "GND"],
    "J8": ["SDA", "SCL", "5V", "GND", "RX", "TX"],
    "JP1": ["5V", "SEL", "12V"],
}

# Every pad of the four JST-PH shells gets its own word, in pad order, using the
# short forms PINOUT.md section 4 already prints (the eight GND pads used to be
# the only unnamed pads on the board).
JST_PIN_SILK = {
    "J17": ["IGN", "GND", "COOL", "GND"],
    "J14": ["TRIP", "GND", "EN", "GND"],
    "J6": ["LDR", "GND", "FUEL", "GND"],
    "J9": ["+V", "SIG", "G"],
}
# Distance from the inner edge of a shell's pad row to the centre of its words
# (measured off the B.Silkscreen gerber, not estimated): the worst word lands
# 0.43 mm clear of the shell's own courtyard and 0.53 mm off the DevKit pads.
JST_BAND = 2.42
# Ink length of each word along its own axis, measured off the plotted
# B.Silkscreen at 0.80 mm glyphs (KiCad stroke font, 0.12 mm stroke: the plot
# gives the stroke centre lines, so 0.12 mm is added).  The band the words stand
# in is 3.86 mm tall, so the longest (COOL, 3.02 mm) leaves 0.84 mm of slack;
# JST_INK is what the advisory note in silks() checks against.
JST_INK = {"TRIP": 2.48, "GND": 2.25, "EN": 1.30, "IGN": 1.76, "COOL": 3.02,
           "LDR": 2.02, "FUEL": 2.75, "+V": 1.53, "SIG": 1.76, "G": 0.62}
JST_BAND_H = 3.86


def build(copper=()):
    nets = NETS

    # no net_class block here: KiCad 10 rejects one ("Expected at, descr, ... or
    # component_classes") - the classes live in the project file, see
    # sync_project().
    out = [pcbheader.header([""] + nets)]

    # board outline
    for a, b in [((BOARD_X0, BOARD_Y0), (BOARD_X1, BOARD_Y0)),
                 ((BOARD_X1, BOARD_Y0), (BOARD_X1, BOARD_Y1)),
                 ((BOARD_X1, BOARD_Y1), (BOARD_X0, BOARD_Y1)),
                 ((BOARD_X0, BOARD_Y1), (BOARD_X0, BOARD_Y0))]:
        (ax, ay), (bx, by) = _onsheet(*a), _onsheet(*b)
        out.append('(gr_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid))'
                   ' (layer "Edge.Cuts") (uuid "%s"))'
                   % (ax, ay, bx, by, kf.uid()))

    boxes = []
    holes = []
    pads = []
    placed = {}                # ref -> (layer, rot, children-mirrored), for the
    # model_frame.MODEL_REFS check below: the hand-made package files are written
    # for the placements this table records, and a part that quietly moves to
    # another side or angle would land its body across its own pads.
    for ent in parts():
        ref, fp_id, x, y, rot, netmap, value, layer, dnp = ent[:9]
        bb = box_of(ent)
        if bb:
            boxes.append((ref, bb))
        holes += [(ref, h) for h in holes_of(ent)]
        pads += pads_abs(ent)

        if fp_id is None:                      # custom footprint (pads pre-resolved)
            ol = ent[9]
            # U4 used to carry a 0.4 mm CrtYd tick at the IN row, which left DRC
            # policing no keepout around the module at all - a part could sit on
            # top of its 21 x 43 body and nothing would complain.  Give it the
            # real courtyard: the drawn body outline plus the 0.25 mm margin.
            # Verified on the regenerated board - still 0 violations - and the
            # rule is genuinely live: widening the west edge by 1.25 mm lights up
            # courtyards_overlap against C1 / C3 on the same face.  KiCad only
            # compares footprints on the SAME side, which is what keeps F1 (F.Cu)
            # straddling the module legal, and Q1 / C2 / D2 / C4 beside it.
            # Same-face (B.Cu) clearances with the margin: 0.50 mm to C1 / C3,
            # 1.30 mm to J1's shell, 3.11 mm to the south edge.
            cbox = ((min(p[0] for p in ol) - 0.25, min(p[1] for p in ol) - 0.25,
                     max(p[0] for p in ol) + 0.25, max(p[1] for p in ol) + 0.25)
                    if ref == "U4" and ol else None)
            out.append(kf.custom_footprint(ref, value, _onsheet(x, y), rot, netmap,
                                           layer=layer, outline=ol, courtyard_box=cbox))
            continue
        def _res(v):
            if isinstance(v, tuple):
                return v
            if isinstance(v, int):
                return (v, NETS[v - 1])
            return (NETS.index(v) + 1, v)
        netmap = {k: _res(v) for k, v in netmap.items() if v and v != "NC"}
        silk = ref in SILK_ON
        if ref in MF.MODEL_REFS:
            # exactly what kicadfmt.board_footprint is about to do to this
            # footprint's children, read off the same two flags it takes
            placed[ref] = (layer, rot,
                           layer == "B.Cu" and ref in BCU_FLIP)
        fp_txt = kf.board_footprint(fp_id, _onsheet(x, y), rot, ref, value, netmap,
                                    layer=layer, ref_to_fab=not silk,
                                    bcu_flip=ref in BCU_FLIP,
                                    bcu_keep_pads=ref in BCU_KEEP_PADS)
        if silk:
            fp_txt = kf.silk_reference(fp_txt, "F.SilkS",
                                       size=1.0 if ref in SILK_BIG else 0.8,
                                       at=SILK_AT.get(ref))
        out.append(fp_txt)

    problems = MF.check(placed)
    for p in problems:
        print("MODEL FRAME: " + p)
    if problems:
        raise SystemExit(
            "the hand-made package files are written for the placements recorded "
            "in model_frame.MODEL_REFS, and this board no longer matches them. "
            "Either the placement moved by accident, or it moved on purpose, in "
            "which case update that table and rebuild the files "
            "(python scripts/make_3d_models.py).  A rotation that is not a "
            "multiple of 90 degrees cannot be served by one file at all - "
            "scripts/probe_frame.py --place shows why.")

    # --- ADVISORY HINTS ONLY -------------------------------------------------
    # KiCad's own DRC (run at the end of main()) is the single referee for
    # clearance/creepage/courtyard/edge rules.  These bounding-box notes exist
    # only to point at a suspect part before the DRC report scrolls past it.
    # They are deliberately approximate (rectangles around curved courtyards);
    # do NOT spend time making them match KiCad - read the DRC report instead.
    for ref, bb in boxes:
        lim = 0.40 if ref in EDGE_OK else 0.0   # edge shells hang over the line
        if not (BOARD_X0 - lim <= bb[0] and bb[2] <= BOARD_X1 + lim
                and BOARD_Y0 - lim <= bb[1] and bb[3] <= BOARD_Y1 + lim):
            print("  OUTSIDE BOARD  %-6s %s" % (ref, tuple(round(v, 2) for v in bb)))
        if not keepout_clear(bb):
            print("  IN ANTENNA AREA %-6s %s" % (ref, tuple(round(v, 2) for v in bb)))
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            ra, a = boxes[i]
            rb, b = boxes[j]
            if (ra in BOTTOM_SIDE) != (rb in BOTTOM_SIDE):
                continue        # opposite faces of the board: no clash
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                print("  OVERLAP %s / %s" % (ra, rb))
    for i in range(len(holes)):          # drill vs drill (both faces)
        for j in range(i + 1, len(holes)):
            (ra, (ax, ay, ad)), (rb, (bx, by, bd)) = holes[i], holes[j]
            if ra == rb:
                continue
            if (ax - bx) ** 2 + (ay - by) ** 2 < (ad / 2 + bd / 2 + 0.2) ** 2:
                print("  HOLES TOO CLOSE %s (%.2f,%.2f) / %s (%.2f,%.2f)"
                      % (ra, ax, ay, rb, bx, by))
    for i in range(len(pads)):        # pad copper vs another net's pad copper
        for j in range(i + 1, len(pads)):
            ra, pa, na, ca, ba = pads[i]
            rb, pb, nb, cb, bb = pads[j]
            if ra == rb or na == nb or not (ca & cb):
                continue
            if (ba[0] < bb[2] and bb[0] < ba[2] and ba[1] < bb[3]
                    and bb[1] < ba[3]):
                print("  SHORT %s / %s: %s.%s at (%.2f,%.2f) vs %s.%s at (%.2f,%.2f)"
                      % (na, nb, ra, pa, (ba[0] + ba[2]) / 2, (ba[1] + ba[3]) / 2,
                         rb, pb, (bb[0] + bb[2]) / 2, (bb[1] + bb[3]) / 2))

    out += silks()
    out += netcheck()

    # copper keep-out under the ESP32 module antenna (all copper layers)
    kx0, ky0, kx1, ky1 = KEEP
    keep = [(kx0, ky0), (kx1, ky0), (kx1, ky1), (kx0, ky1), (kx0, ky0)]

    def poly(pts):
        return "(polygon (pts %s))" % " ".join("(xy %.2f %.2f)" % _onsheet(*q)
                                               for q in pts)

    for lay in ("F.Cu", "B.Cu"):
        out.append('(zone (net 0) (net_name "") (layer "%s") (uuid "%s") (hatch edge 0.5)'
                   ' (keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed)'
                   ' (copperpour not_allowed)) %s)'
                   % (lay, kf.uid(), poly(keep)))

    # GND pours on the two inner planes (filled by KiCad at export time)
    gnd = NETS.index("GND") + 1
    for lay in ("In1.Cu", "In2.Cu"):
        out.append('(zone (net %d) (net_name "GND") (layer "%s") (uuid "%s") (hatch edge 0.5)'
                   ' (connect_pads (clearance 0.5)) (min_thickness 0.2)'
                   ' (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))'
                   ' %s %s)'
                   % (gnd, lay, kf.uid(),
                      poly([(BOARD_X0, BOARD_Y0), (BOARD_X1, BOARD_Y0),
                            (BOARD_X1, BOARD_Y1), (BOARD_X0, BOARD_Y1),
                            (BOARD_X0, BOARD_Y0)]), poly(keep)))

    # routed copper (scripts/route_pcb.py) - outer layers only.  In1.Cu and
    # In2.Cu stay the ground planes: their pours do their own connecting, and a
    # trace dropped between the planes would cut them into islands.
    out += list(copper)

    out.append(")")
    return "\n".join(out) + "\n"


# ------------------------------------------------------- silks / drawings --
def _txt(x, y, text, layer="F.SilkS", size=0.8, thick=0.12, rot=0.0,
         mirror=False, halign="left", valign="top"):
    # anchor = top-left corner of the text by default (KiCad centres fields by
    # default, so the two alignment tokens are spelled out).  KiCad's centred
    # case is the ABSENCE of a token - the parser rejects "center" in the
    # horizontal slot - so "center"/"middle" here simply leave the token out.
    # That is what centres a silk word on its pad without guessing the
    # stroke-font width of every label (guessing came out up to 0.4 mm off).
    x, y = _onsheet(x, y)
    toks = ([] if halign == "center" else [halign]) \
        + ([] if valign == "middle" else [valign])
    just = " (justify%s%s)" % ((" " + " ".join(toks)) if toks else "",
                               " mirror" if mirror else "")
    eff = '(effects (font (size %.2f %.2f) (thickness %.2f))%s)' % (
        size, size, thick, just)
    return ['(gr_text "%s" (at %.2f %.2f %.1f) (layer "%s") (uuid "%s") %s)'
            % (text, x, y, rot, layer, kf.uid(), eff)]


def silks():
    """Silkscreen + documentation marks, placed from the CAD geometry.

    Text height 0.8 mm minimum (fab limit). Per-pin text is used wherever the
    pitch takes it: flat words on the 2.54 mm groups, one word standing up
    beside every pad of the 2.00 mm JST-PH shells.
    """
    out = []

    # No documentation boxes any more: the DevKit body outline, the antenna
    # keep-out, the module body, the GNSS pad zone, the four JST-PH plug zones,
    # the spare/debug work zones and the drawn 85 x 55 envelope were all dashed
    # rectangles on Dwgs.User (13 of them, 52 gr_lines).  Their coordinates are
    # still listed in PINOUT.md section 1; the board is drawn clean - outline,
    # copper, courtyards and silks only.
    out += _txt(24.0, 8.9, "DRAWN 85 x 55 ENVELOPE - BOARD EXTENDED 10 mm WEST",
                layer="Dwgs.User", size=1.0)
    # the two bulk cans live underneath, mid-tail: label them in the west margin
    out += _txt(-12.60, 37.20, "C1 / C3 UNDER THE BOARD",
                layer="Dwgs.User", size=0.7)

    # dev board rows: pin 1 is VIN, at the dev board's USB end.  The note sits
    # under the west end of the north row, 0.30 mm north of OK1's printed
    # designator and clear of the C2 / D2 column (courtyard west edge 33.00).
    out += _txt(41.20, 26.00, "PIN 1 = USB END", size=0.8)
    # The antenna keep-out reminder is split over two lines: one line measured
    # ~17.4 mm of ink but the zone it describes is 13.94 mm wide (x 72.87->86.80),
    # so the single line ran 3.5 mm east of its own zone and into the TFT name
    # column at x 87.5 (DRC: silk_overlap against T_CLK).  Split, each word is
    # ~6.7 mm of ink and sits well inside the zone (ink x 73.83->80.64, y
    # 27.63->29.88); the 1.45 mm line pitch leaves 0.65 mm between the two lines
    # of ink, since 0.80 mm stroke glyphs draw 0.80 mm tall caps.
    out += _txt(KEEP[0] + 0.8, KEEP[1] + 0.8, "ANTENNA", size=0.8)
    out += _txt(KEEP[0] + 0.8, KEEP[1] + 2.25, "NO COPPER", size=0.8)

    # TFT socket, standing at the east edge (14-way), pad 1 = VCC at the SOUTH
    # end and the row running north to pad 14 = T_DO.  Every pad is named in the
    # clear west of the socket, **horizontally** (rot 0) so the words read
    # straight off the board, right-aligned on a common edge at anchor x 91.00
    # and centred on their own pad row by KiCad's own justify (halign="right",
    # valign="middle" - no stroke-font width guesswork, which came out up to 0.4
    # mm off when centring was computed by hand).
    # Horizontal costs board width rather than pitch, and there is width to
    # spare: the longest word (T_CLK) is ~3.2 mm of ink, so the words run from x
    # 91.00 west to ~87.5, still ~1.3 mm east of the DevKit's body outline at x
    # 86.20 - which is also why the module's long forms go on the board whole
    # (T_CLK / T_CS / T_DIN), no abbreviating.
    for i, nm in enumerate(TFT_LABEL):
        out += _txt(91.00, TFT["y1"] - i * TFT["pitch"], nm, size=0.8,
                    halign="right", valign="middle")
    # The pin numbers sit outboard of the row and 2.5 mm west of the name
    # column: "1" below the VCC (south) end, "14" above the T_DO (north) end.
    # Straight outboard of the pads they would sit over the NE mounting hole H2
    # at (91.50, 13.50) - its mask opening reaches y 15.15, which DRC flags as
    # silk_over_copper - and level with the pads they run into the outermost
    # words (DRC: silk_overlap).
    out += _txt(88.50, TFT["y1"] + 2.34, "1", size=0.9,
                halign="right", valign="middle")
    out += _txt(88.50, TFT["y_end"] - 2.34, "14", size=0.9,
                halign="right", valign="middle")
    # the socket's own name, horizontal, centred under the south (pad-1) end:
    # 2.1 mm clear of the socket's body silks, and clear of the GND fill's south
    # cap (the strip ends at y 54.27)
    out += _txt(TFT["x"], TFT["y1"] + 3.44, "TFT", size=0.9, halign="center")

    # 12 V entry polarity: one label outside each XT30 pad, both centred on the
    # connector's y centre (pads at y 13.99, courtyard y 10.89..17.09).  B.Cu part
    # -> mirrored silks, so the anchor is the text's RIGHT edge: "+12V" (3.30 mm of
    # ink) hangs west of x 16.15, "GND" (2.40 mm of ink) starts at x 29.35 - 1.00 mm
    # clear of J1's courtyard (x 17.15 / 28.35) and 2.20 mm clear of the pad it
    # names (pad 1 DC_IN reaches x 18.35, pad 2 GND reaches x 27.15).  Ink widths
    # are measured off the plotted B.SilkS gerber, not estimated.  Both lines land
    # at y 13.54..14.44, 1.94 mm north of the module's "IN+" / "IN-" row (ink
    # 12.80..14.86 and 29.80..31.86 at y 16.38..17.28), and the west slot is empty
    # board: no pad, courtyard or silk reaches x 12.85..16.15 at that height
    out += _txt(16.44, 13.50, "+12V", layer="B.SilkS", size=0.9, mirror=True)
    out += _txt(32.00, 13.50, "GND", layer="B.SilkS", size=0.9, mirror=True)

    # DC-DC module (under the board): labels outside its 21 x 43 outline
    inx, iny = LM["x"] - LM["span_x"] / 2, LM["y"] - LM["span_y"] / 2
    outx, outy = LM["x"] - LM["span_x"] / 2, LM["y"] + LM["span_y"] / 2
    # B.SilkS text is mirrored, so the anchor is the text's RIGHT edge
    for lab, x, y in [("IN+", inx + 0.9, iny - 4.3), ("IN-", inx + 17.9, iny - 4.3),
                      ("OUT+", outx + 1.2, outy + 1.6), ("OUT-", outx + 18.2, outy + 1.6)]:
        out += _txt(x, y, lab, layer="B.SilkS", size=0.9, mirror=True)

    # JST-PH shells hang under the board, so every pad is named on the shell's
    # own face - B.SilkS, mirrored - with its own word STANDING UP beside it.
    # rot 270 was checked, not guessed: the plotted ink of a standing word is a
    # pure rotation of the old flat ink of the same word (identical stroke sets
    # under (x,y)->(-y,x)), so nothing is mirrored, and the first letter lands at
    # the south end of its column - which reads bottom-to-top from underneath.  Flat words cannot do this job: at 2.00 mm pitch the signal
    # words are 1.30 mm (EN) to 2.48 mm (TRIP) of ink against a 2.00 mm column,
    # so a flat word only pays for every other pad - which is exactly what these
    # shells used to do, and it left 8 of their 15 pads (every GND among them,
    # and all three of J9) with no name at all.  A standing word is glyph-height
    # wide, 0.80 mm + 0.12 mm stroke = 0.92 mm, so neighbouring columns keep
    # 1.08 mm of air (measured 1.20 mm centre-line to centre-line) and each word
    # still stands on its own pad.
    # Columns and pad edges come from pads_abs(), which un-mirrors the local
    # library copy the way the board stores this copper - reading the dpp.pretty
    # pads straight (pads_of) double-mirrors them and parks every word on top of
    # the pad it names, 15 silk_over_copper hits, which is how this was caught.
    # Reality, off KiCad's own report: J14 x 64.00/66.00/68.00/70.00 and J17 x
    # 78.40/80.40/82.40/84.40 (pin 1 west), J6 x 84.40..78.40 and J9 x 69.00/
    # 67.00/65.00 (pin 1 east, rot 180); their copper spans y 16.10..19.60 (north
    # pair) and y 55.40..58.90 (south pair), the 10.2 mm courtyards stop at
    # y 20.10 / y 54.90, and the DevKit rows run y 23.96..25.66 (J10a) / y
    # 49.35..51.06 (J10b), the DevKit body hanging underneath over y 23.49..
    # 51.49.  So the two text bands, y 20.10..23.96 and y 51.06..54.90 (3.86 mm
    # each), sit clear of plug, body, courtyard and copper, and every word fits:
    # measured off the plot, the words reach y 20.53..23.43 (COOL, the worst on
    # the north band) and y 51.61..54.24 (FUEL, the worst on the south one):
    # 0.43..1.67 mm off the courtyards, 0.53..0.55 mm off the DevKit pads (the
    # DevKit's own underside edge at y 23.49 misses the longest word by 0.06 mm),
    # and 0.93..2.17 mm off the pad copper each word names.
    # C11 (the TRIP filter) and R12 stand over the north words on the TOP face:
    # opposite faces, so the words still read from below, and what DRC weighs
    # against them is the B.Cu copper under them (silk_over_copper).
    # KiCad's own justify centres each word on its pad (0.04 mm off, measured) and
    # in the middle of its band, so there is no stroke-font width maths here.
    for ent in parts():
        ref = ent[0]
        if ref not in JST_PIN_SILK:
            continue
        sig = sorted((int(p[1]), p[4]) for p in pads_abs(ent) if p[1].isdigit())
        for (num, box), lab in zip(sig, JST_PIN_SILK[ref]):
            cx, cy = (box[0] + box[2]) / 2.0, (box[1] + box[3]) / 2.0
            inb = 1.0 if cy < ESP_MID else -1.0      # toward the board's middle
            out += _txt(cx, cy + inb * ((box[3] - box[1]) / 2.0 + JST_BAND),
                        lab, layer="B.SilkS", size=0.8, rot=270.0, mirror=True,
                        halign="center", valign="middle")
            if JST_INK[lab] > JST_BAND_H:
                print("  SILK TOO LONG %s.%d '%s': %.2f mm of ink against a "
                      "%.2f mm band" % (ref, num, lab, JST_INK[lab],
                                        JST_BAND_H))

    # GNSS pads sit under the board, so their label is mirrored silks too
    out += _txt(57.6 + 1.4, 12.9, "GNSS", layer="B.SilkS", size=0.8,
                mirror=True)

    # test-point row: the five probes are labelled with a short form of the net
    # they sit on, not with TP1..TP5.  At 0.80 mm the full names are wider than
    # the 4.50 mm pitch (IGN_SENSE 6.10, FUEL_SNS 5.52 mm ink), which forced two
    # staggered lines; the short words (1.64..2.63 mm ink) leave 2.12..2.41 mm
    # of air between neighbours, so all five sit on ONE line at y 11.77->12.57,
    # each centred on its own pad: 0.47 mm clear of the pads' silks rings
    # (r 0.95 -> y 13.04) and 1.77 mm off the north edge.  Widths come off the
    # plotted layer (TP_INKW above), not a per-glyph guess, which is how each
    # word lands on its pad to within 0.04 mm.  TEST PROBES captions the row
    # from below, in the gap between the test-point courtyards (y 15.24) and the
    # DevKit's north socket (courtyard y 23.96).  What it hovers over is all
    # far-face copper now: the GNSS pads (J8, to x 54.94) at its west end and
    # J14's signal pads (x 63.50..70.50, y 16.10..19.60 - the ink crosses their
    # north edge by 0.74 mm) at its east end.
    for i, (ref, net) in enumerate(TPS):
        lab = TP_SHORT[net]
        out += _txt(TP_X0 + i * TP_PITCH - TP_INKW[lab] / 2.0 - TP_BEAR,
                    TP_Y - 2.25, lab, size=0.8)
    out += _txt(TP_X0 + 2 * TP_PITCH - 9.76 / 2.0 - TP_BEAR, 15.80,
                "TEST PROBES", size=1.0, thick=0.15)

    # per-pin text where there is room (2.54 mm pitch groups).  J8 / JP1 are
    # pads on the underside, so their labels go on B.SilkS, mirrored.
    # both 2.54 mm pad groups are labelled on their south side, one text strip
    # _txt hangs the glyph box BELOW its anchor (valign top), so a north-side
    # label has to clear the pad's north edge plus its mask relief: at -1.25 the
    # 'D13'/'GND' box reached y 19.27, i.e. 0.05 mm inside J16 pad 1/2's mask
    # window (pad edge 19.22 + 0.05 relief) - DRC silk_over_copper.  -1.60 puts
    # the box at y 18.12..18.92: 0.25 mm off the mask, still 3.5 mm clear of the
    # GNSS pads' courtyard to the north (y 14.60).
    SIDE = {"J13": 1.2, "J15": 1.2, "J16": -1.60, "J18": 1.25}
    BSIDE = {"J8": 1.9, "JP1": -2.6}
    for ent in parts():
        ref, x, y = ent[0], ent[2], ent[3]
        if ref not in PIN_SILK:
            continue
        if ref in SIDE:
            for i, lab in enumerate(PIN_SILK[ref]):
                out += _txt(x + i * 2.54 - 0.7 * len(lab) / 2.0, y + SIDE[ref],
                            lab, size=0.8)
        elif ref in BSIDE:
            for i, lab in enumerate(PIN_SILK[ref]):
                out += _txt(x + i * 2.54 + 0.7 * len(lab) / 2.0,
                            y + BSIDE[ref], lab, layer="B.SilkS", size=0.8,
                            mirror=True)

    # build documentation (not printed): zones, pin map, test points.  The block
    # sits south of the board outline (bottom edge y = 65), never over the pads -
    # it used to be laid across the north end of the board and got in the way of
    # the view while working.  Still inside the A4 page, still the same text.
    out += _txt(10.5, 66.4, "Dashboard++ for ESP32 carrier - CAD layout rev 2"
                            " - board 95 x 55 mm", layer="Dwgs.User", size=1.2)
    out += _txt(10.5, 68.8, "BUILD NOTES (Dwgs.User - not on the board)",
                layer="Dwgs.User", size=1.0)
    docs = ["TFT pad 1 = VCC at the SOUTH end; 1..14 north:",
            "  VCC GND CS RST DC SDI SCK LED SDO T_CLK T_CS T_DIN TPEN T_DO",
            "  (pads 9..14 named, left open)",
            "ESP near row 1..15 (USB end first): VIN GND 13 12 14 27 26 25",
            "  33 32 35 34 39 36 EN",
            "ESP far row 16..30 (USB end first): 3V3 GND 15 2 4 16 17 5 18",
            "  19 21 3 1 22 23",
            "J17 IGN GND COOL GND   J14 TRIP GND EN GND",
            "J6 LDR GND FUEL GND    J9 +V SIG G",
            "JST = right-angle SMD, bodies under the board; pin 1 is at the",
            "west end of the north pair, east end of the south pair (top view)",
            "no trip button on the carrier - trip comes in on J14 (pin 1)",
            "SPARE / DEBUG PADS - see J13 (TX RX GND 5V) and J15",
            "J13 DEBUG  J15 SPARE (D26 SDA SCL D19 D15 GND)",
            "J16 SPARE N (D13 GND) north of the near row,",
            "J18 SPARE S (D2 GND) south of the far row",
            "TP1 IGN_SENSE  TP2 HALL  TP3 BAT_SNS  TP4 FUEL_SNS  TP5 GND",
            "their silks read out short forms: IGN HALL BAT FUEL GND"]
    y = 70.4
    for txt in docs:
        out += _txt(10.5, y, txt, layer="Dwgs.User", size=1.0)
        y += 1.5
    return out


def netcheck():
    """Count pads per net so a typo or a missing connection shows up at once."""
    cnt = {}
    for ent in parts():
        netmap = ent[5]
        if ent[1] is None:                  # custom footprint: pads carry nets
            for p in netmap:
                if p[6] and p[6] != "npth":
                    cnt[p[6][1]] = cnt.get(p[6][1], 0) + 1
            continue
        for pad, v in netmap.items():
            if isinstance(v, int):
                v = NETS[v - 1]
            name = v[1] if isinstance(v, tuple) else v
            if name and name != "NC":
                cnt[name] = cnt.get(name, 0) + 1
    singles = sorted(n for n, c in cnt.items() if c < 2)
    for n in singles:
        print("  NET ONCE     %-12s (single pad - check wiring)" % n)
    print("  nets: %d   single-pad nets: %d" % (len(cnt), len(singles)))
    return []


def write_footprint_lib():
    """Emit footprints/carrier.pretty + fp-lib-table for the board.

    Only the bespoke footprints (the ones this generator draws from scratch)
    live in the local library; stock parts keep their normal KiCad nicknames so
    the board stays editable on any machine.
    """
    libdir = os.path.join(os.path.dirname(os.path.abspath(OUT)), "footprints",
                          kf.LOCAL_LIB + ".pretty")
    names = kf.write_local_lib(libdir)
    tbl = os.path.join(os.path.dirname(os.path.abspath(OUT)), "fp-lib-table")
    entry = ('  (lib (name "%s")(type "KiCad")'
             '(uri "${KIPRJMOD}/footprints/%s.pretty")(options "")'
             '(descr "Footprints generated by scripts/make_pcb.py"))'
             % (kf.LOCAL_LIB, kf.LOCAL_LIB))
    old = open(tbl, encoding="utf-8").read() if os.path.exists(tbl) else ""
    if "make_pcb.py" not in old:
        open(tbl, "w", encoding="utf-8").write(
            "(fp_lib_table\n  (version 7)\n%s\n)\n" % entry)
    return names


def main():
    rp = route_pads()
    have = {}
    for p in rp:
        if p["net"]:
            have.setdefault(p["net"], []).append(p)
    todo = [n for _, group in ROUTE_ORDER for n in group if len(have.get(n, [])) > 1]
    left = sorted(n for n in have if len(have[n]) > 1 and n not in todo)
    if left:
        print("  ROUTE UNPLANNED %s (routed last, in net order)" % ", ".join(left))
        todo += left
    single = sorted(n for n in have if len(have[n]) < 2)
    if single:
        print("  ROUTE SINGLE-PAD %s (nothing to route to)" % ", ".join(single))
    segs, vias, log = route_pcb.route(
        dict(pads=rp, frame=(BOARD_X0, BOARD_Y0, BOARD_X1, BOARD_Y1),
             keep=KEEP, classes=dict(CLASS_OF), clearance=CLASS_CLEAR,
             widths={n: net_w(n) for n in todo}, via=CLASS_VIA,
             route_layers=["F.Cu", "B.Cu"],
             order=[(g, sorted([n for n in group if n in todo],
                               key=lambda n: net_span(have[n])))
                    for g, group in ROUTE_ORDER]))
    for line in log:
        print("  ROUTE        " + line)
    print("  COPPER       %d segments, %d vias" % (len(segs), len(vias)))
    classcheck(segs, vias)

    txt = build(copper=fmt_copper(segs, vias))
    open(OUT, "w", encoding="utf-8").write(txt)
    sync_project(os.path.splitext(OUT)[0] + ".kicad_pro")
    names = write_footprint_lib()
    print("footprint library: %d footprints -> footprints/%s.pretty" % (len(names), kf.LOCAL_LIB))
    print("wrote", os.path.abspath(OUT), "(%d bytes)" % len(txt))
    r = subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all",
                        "-o", "drc-report.txt", OUT], capture_output=True, text=True)
    print("DRC exit", r.returncode, (r.stderr or "").strip()[:400])
    if os.path.exists("drc-report.txt"):
        rep = open("drc-report.txt", encoding="utf-8", errors="replace").read()
        viol = re.search(r"Found (\d+) DRC violation", rep)
        unc = re.search(r"Found (\d+) unconnected", rep)
        print("violations:", viol.group(1) if viol else "?",
              "| unconnected:", unc.group(1) if unc else "?")
        for line in rep.splitlines():
            if line.startswith("["):
                print("  ", line)
    print("report: drc-report.txt")


if __name__ == "__main__":
    main()
