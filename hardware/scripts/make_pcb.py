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
import pcbheader

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
# "Display header 14 pin", so J3 is a **1x14** socket, placed exactly as the CAD
# mapping row says: pin 1 (VCC) y 17.99 -> pin 14 (T_DO) y 51.01, 33.02 = 13 x
# 2.54, the header centred in the drawn slot.  Its 35.56 mm body stands 0.28 mm
# past each end of the 35.00 mm slot - the earlier 1x16 reading of the module's
# silks put 40.64 mm of socket 5.6 mm into the drawing's south gap, which is
# what this corrects.  Only pins 1..8 are wired; 9..14 (SDO + the touch lines)
# stay open - no MISO in the firmware, touch removed in 1.3.6.
TFT = {"x": 92.51, "y1": 17.99, "pitch": 2.54, "pins": 14}  # pin 1 north
TFT["y_end"] = TFT["y1"] + (TFT["pins"] - 1) * TFT["pitch"]  # 51.01

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
SILK_ON = {"F1", "Q1", "D1", "D2", "D3", "OK1", "OK2", "C2", "C4", "C11", "C13",
           "TP1", "TP2", "TP3", "TP4", "TP5"}
SILK_ON |= {"R%d" % i for i in range(1, 14)}
SILK_ON |= {"C%d" % i for i in range(5, 11)}
SILK_BIG = {"F1", "Q1", "D1", "D2", "D3", "OK1", "OK2"}
SILK_AT = {}                    # ref: (dx, dy) local offset override, from DRC


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
        for (num, px, py, w, h, dr, cu) in kf.pads_full(fp_id):
            net = netmap.get(num)
            if not net or net == "NC":
                continue
            if layer == "B.Cu":
                cu = {("B.Cu" if c == "F.Cu" else c) for c in cu}
            out.append((ref, num, nm(net), cu,
                        _padbox(px, py, w, h, rot, x, y)))
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
    tft = {str(i + 1): n for i, n in enumerate(TFT_MAP)}
    P.append(("J3", "Connector_PinSocket_2.54mm:PinSocket_1x14_P2.54mm_Vertical",
              TFT["x"], TFT["y1"], 0.0, tft, "TFT-ILI9488", "F.Cu", 0))

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
    # F1 stands with its 5x20 axis north-south, dead centre on the DC-DC module.
    # This clip's *origin* is the pad-1 corner (pad 1 at local (0,0), pad 2 at
    # (17,0), the second column at local y +5.0), and rot 90 turns pad 2 to the
    # north end, facing the module's IN row.  Origin (20.25, 48.64) puts the
    # courtyard on (18.85,29.39)..(26.65,50.89) - centre (22.75, 40.14), which is
    # U4's centre to 0.01 mm, so the clip straddles the module: fuse and glass
    # tube on the display face, module underneath, opposite faces, which is what
    # makes centring over the module possible at all.  Nearest clearances: 7.86 mm
    # to the drawn west zone's west edge, 12.30 mm to J1's courtyard, 7.50 mm to
    # the module's OUT pads, 7.35 mm to C3's courtyard (other face), 11.22 mm to
    # the mid small-parts zone, 15.20 mm to the H4 ring.
    # DC_IN now arrives at the SOUTH end (y 48.64: the long 12 V run down from the
    # XT30 - that net's rats nest grows from 9.6 mm to 39.7 mm, the one real cost
    # of centring the clip, and free to drop to an inner layer at the THT pad) and
    # leaves DC_F at the NORTH end (y 31.64): 9.14 mm from IN+, 4.20 mm from Q1's
    # source pad, and DC_F's rats nest halves (39.3 mm -> 19.4 mm).
    P.append(("F1", "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
              20.25, 48.64, 90.0, {"1": N("DC_IN"), "2": N("DC_F")}, "5x20 2A",
              "F.Cu", 0))
    # Q1 steps north of the fuse: its 16.65 x 11.30 courtyard (the D2PAK courtyard
    # carries the copper tab) lands in the band y 17.55..28.85, 0.54 mm clear of
    # F1, with its pin column at x 18.20 right above the module's IN+ pad
    # (12.50..16.00, 19.39..21.89) on the far face.  0.70 mm west of D2.
    P.append(("Q1", "Package_TO_SOT_SMD:TO-263-3_TabPin2",
              25.85, 23.20, 0.0, {"1": N("MOS_G"), "2": N("DC_12"), "3": N("DC_F")},
              "IRFR9540N", "F.Cu", 0))
    # D1 (gate zener) and R1 (gate pull-down) stand in the 4.4 mm strip west of
    # that pin column, one above the other, both rot 270 so their centres land on
    # Q1's own pad rows: D1 at y 20.66 (gate row, pads (13.30,19.01)/(13.30,22.31))
    # and R1 at y 25.74 (source row, pads (13.30,24.83)/(13.30,26.65)) - 1.05 mm
    # apart, 1.20 / 1.40 mm west of Q1's courtyard, 1.97 mm (in y) north of the
    # fuse, 0.65 / 0.85 mm from C1's courtyard on the other face.  Q1's own pad
    # rows are y 20.66 (gate) / 23.20 (drain + tab) / 25.74 (source), which is what
    # the two centres are pinned to.  MOS_G's rats nest: 17.8 -> 12.4 -> 11.0 mm.
    P.append(("D1", "Diode_SMD:D_SOD-123", 13.30, 20.66, 270.0,
              {"1": N("MOS_G"), "2": N("DC_F")}, "BZT52C15V", "F.Cu", 0))
    P.append(("D2", "Diode_SMD:D_SMB",
              35.25, 30.00, 270.0, {"1": N("DC_12"), "2": N("GND")}, "P6SMB33A",
              "F.Cu", 0))
    P.append(("R1", R805, 13.30, 25.74, 270.0,
              {"1": N("MOS_G"), "2": N("DC_F")}, "10k", "F.Cu", 0))
    # The DC_12 parts form the matching column on the east side of Q1, x 35.25:
    # C2 in the tab's row, D2 the TVS below it, C4 far south at the module's OUT
    # end (see below).  C2 rot 270 - courtyard x 34.27..36.23 / y 21.50..24.90,
    # 1.97 mm east of Q1's courtyard, 1.45 mm above D2's, 2.25 mm clear of the TP
    # row, 0.93 mm inside the x 37.16 small-parts limit.  Its GND pad (2) sits
    # north at (35.25,22.25), 1.52 mm off the module's IN- pad (x 29.50..33.00 /
    # y 19.39..21.89, other face); the DC_12 pad (1) south at (35.25,24.15), 2.47
    # mm east of Q1's drain tab (x 22.65..32.05 / y 17.80..28.60).  That is the HF
    # loop this 100 n is meant to short - MOSFET tab to module input - instead of
    # the detour it took from its old seat at (30.00,31.50), 15 mm from the node
    # it decouples.  D2 rot 270 runs the same pad order: GND north at (35.25,27.85)
    # 2.05 mm off the tab, cathode (DC_12) south at (35.25,32.15); courtyard
    # x 33.00..37.50 / y 26.35..33.65, 0.70 mm east of Q1's.
    P.append(("C2", C805, 35.25, 23.20, 270.0,
              {"1": N("DC_12"), "2": N("GND")}, "100n", "F.Cu", 0))
    # Input / output bulk: the two 10 x 12.5 mm cans, moved to the BOTTOM
    # face in the 10 mm west extension (plan "carrier stage 3").  Inside the
    # drawn 85 x 55 envelope there is no B.Cu room for a can near the power
    # section at all (U4 fills x 12.25..33.25, J1 fills y <= 17.09), and on
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
    # turns the 13.3 mm courtyard across the 12.25 mm strip and into U4.
    P.append(("C1", "Capacitor_SMD:CP_Elec_10x12.5", 6.0, 29.6, 90.0,
              {"1": N("DC_12"), "2": N("GND")}, "220u/50V", "B.Cu", 0))
    P.append(("C3", "Capacitor_SMD:CP_Elec_10x12.5", 6.0, 45.4, 270.0,
              {"1": N("V5"), "2": N("GND")}, "470u/35V", "B.Cu", 0))
    # C4 (the 5 V HF bypass) finishes the x 35.25 column at the OTHER end of the
    # current path: it sits beside the module's OUT pads instead of stranded in
    # the middle of the west zone.  rot 90 puts its V5 pad north at (35.25,57.95),
    # level with OUT+ (y 58.39..60.89) and 1.02 mm east of U4's courtyard (which
    # it may cross - opposite faces).  8.80 mm from the fuse courtyard, which is
    # now its nearest F.Cu neighbour.  V5's rats nest drops 1.7 mm and the column
    # reads C2 / D2 / C4 top to bottom.
    P.append(("C4", C805, 35.25, 57.00, 90.0,
              {"1": N("V5"), "2": N("GND")}, "100n", "F.Cu", 0))
    # C13 moves east into the mid small-parts zone: it now sits at the head of the
    # 3V3 pin (J10b pad 1, 6.0 mm away) instead of 25 mm away in the west column,
    # which the vertical fuse courtyard owns.  1.04 mm south of C7, 2.95 mm north
    # of the DevKit row (opposite face), inside the drawn mid zone; V3V3's rats
    # nest drops from 62.2 mm to 40.1 mm.
    P.append(("C13", C805, 48.60, 44.50, 0.0,
              {"1": N("V3V3"), "2": N("GND")}, "100n", "F.Cu", 0))

    # ---- signal conditioning, F.Cu, in the drawn mid small-parts zone
    # (43.38 x 18.91, under the dev board that hangs below).
    # ---- signal conditioning, F.Cu, in the drawn mid small-parts zone
    # (43.38 x 18.91, under the dev board that hangs below).  Optocouplers are
    # the SMD twin of the PC817 (Everlight EL357N, 4-pin SOP, 2.54 mm leads);
    # both move onto the RX/CX column grid below (x 44.10 and 57.60) so the whole
    # mid band reads as one lattice: an opto standing over its own column.
    OPTO = "Package_SO:SO-4_4.4x3.9mm_P2.54mm"
    P.append(("OK1", OPTO, 44.10, 30.50, 0.0,
              {"1": N("IGN_LED"), "2": N("GND"),
               "3": N("IGN_SENSE"), "4": N("V3V3")}, "EL357N-B", "F.Cu", 0))
    P.append(("OK2", OPTO, 57.60, 30.50, 0.0,
              {"1": N("HALL_LED"), "2": N("GND"),
               "3": N("HALL_OUT"), "4": N("V3V3")}, "EL357N-B", "F.Cu", 0))

    RX = [39.6, 44.1, 48.6, 53.1, 57.6, 62.1]
    # the grid's 7th column, x 66.60, is used by C11 and D3 (and starts the spare
    # header); the test-point row below sits on columns 1..5 of the same lattice
    res = [("R2", "47k", "DC_12", "BAT_DIV", 35.5), ("R3", "10k", "BAT_DIV", "GND", 35.5),
           ("R4", "1k", "BAT_DIV", "BAT_SNS", 35.5), ("R5", "220R", "V3V3", "FUEL_SNS", 35.5),
           ("R6", "10k", "V3V3", "COOL_SNS", 35.5), ("R7", "10k", "V3V3", "LDR_SNS", 35.5),
           ("R8", "1k", "IGN_12", "IGN_LED", 38.5), ("R9", "10k", "IGN_SENSE", "GND", 38.5),
           ("R10", "1k", "HALL_SIG", "HALL_LED", 38.5), ("R11", "10k", "HALL_OUT", "GND", 38.5),
           ("R12", "10k", "TFT_BL", "GND", 38.5), ("R13", "1k", "HALL_OUT", "HALL", 38.5)]
    for i, (ref, val, a, b, y) in enumerate(res):
        P.append((ref, R805, RX[i % 6], y, 0.0,
                  {"1": N(a), "2": N(b)}, val, "F.Cu", 0))

    CX = RX
    caps = [("C5", "BAT_SNS"), ("C6", "FUEL_SNS"), ("C7", "COOL_SNS"),
            ("C8", "LDR_SNS"), ("C9", "IGN_SENSE"), ("C10", "HALL")]
    for i, (ref, net) in enumerate(caps):
        P.append((ref, C805, CX[i], 41.5, 0.0, {"1": N(net), "2": N("GND")},
                  "100n", "F.Cu", 0))
    # TRIP_BTN filter, right behind the north-edge J14 shell (which hangs below),
    # on the grid's 7th column - same x as D3 under it.
    P.append(("C11", C805, 66.60, 21.60, 0.0, {"1": N("TRIP_BTN"), "2": N("GND")},
              "100n", "F.Cu", 0))
    P.append(("D3", "Diode_SMD:D_SMB", 66.60, 30.50, 0.0,
              {"1": N("HALL"), "2": N("GND")}, "P6SMB6V2A", "F.Cu", 0))

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

    P.append(padgroup("J13", "DEBUG", 40.0, 22.0,
                      PIN_SILK["J13"], ["DBG_TX", "DBG_RX", "GND", "V5"]))
    P.append(padgroup("J15", "SPARE", 62.10, 56.50,
                      PIN_SILK["J15"], ["SPARE_D26", "SPARE_D21", "SPARE_D22",
                                        "SPARE_D19", "SPARE_D15", "GND"]))

    # ---- test points (north spare zone, below the GNSS header).  The row sits on
    # the mid band's column lattice - x 39.60 / 44.10 / 48.60 / 53.10 / 57.60, the
    # same five columns that carry R2-R6 / R8-R12 / C5-C9 - so a probe point lines
    # up with the part it probes instead of drifting off on a 2.5 mm pitch.
    for i, (ref, net) in enumerate([("TP1", "IGN_SENSE"), ("TP2", "HALL"),
                                    ("TP3", "BAT_SNS"), ("TP4", "FUEL_SNS"),
                                    ("TP5", "GND")]):
        P.append((ref, "TestPoint:TestPoint_Pad_D1.5mm", 39.6 + i * 4.5, 19.5,
                  0.0, {"1": N(net)}, net, "F.Cu", 0))

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
# 14 pin" block), pin 1..14 as read off the display's own silkscreen (plan
# section 1). Only the LCD lines are connected: SDO and the touch pads stay
# open (no MISO in firmware, touch removed in 1.3.6).
TFT_MAP = ["V5", "GND", "TFT_CS", "TFT_RST", "TFT_DC", "TFT_MOSI", "TFT_SCK",
           "TFT_BL", "NC", "NC", "NC", "NC", "NC", "NC"]
TFT_LABEL = ["VCC", "GND", "CS", "RST", "DC", "SDI", "SCK", "LED", "SDO",
             "T_CLK", "T_CS", "T_DIN", "TPEN", "T_DO"]

# silkscreen next to the vehicle / accessory headers: (ref, [labels per pin])
# Per-pin silks for the 2.54 mm groups. The 2.00 mm JST-PH shells carry a
# single two-circuit label instead (see silks()); pin order is fixed by the
# footprint and documented in the README / PINOUT.
PIN_SILK = {
    "J13": ["TX", "RX", "GND", "5V"],
    "J15": ["D26", "SDA", "SCL", "D19", "D15", "GND"],
    "J8": ["SDA", "SCL", "5V", "GND", "RX", "TX"],
    "JP1": ["5V", "SEL", "12V"],
}


def build():
    nets = NETS

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
    for ent in parts():
        ref, fp_id, x, y, rot, netmap, value, layer, dnp = ent[:9]
        bb = box_of(ent)
        if bb:
            boxes.append((ref, bb))
        holes += [(ref, h) for h in holes_of(ent)]
        pads += pads_abs(ent)

        if fp_id is None:                      # custom footprint (pads pre-resolved)
            out.append(kf.custom_footprint(ref, value, _onsheet(x, y), rot, netmap,
                                           layer=layer, outline=ent[9],
                                           courtyard_box=(None if ref != "U4"
                                                          else (-0.2, -13.2, 0.2, -12.8))))
            continue
        def _res(v):
            if isinstance(v, tuple):
                return v
            if isinstance(v, int):
                return (v, NETS[v - 1])
            return (NETS.index(v) + 1, v)
        netmap = {k: _res(v) for k, v in netmap.items() if v and v != "NC"}
        silk = ref in SILK_ON
        fp_txt = kf.board_footprint(fp_id, _onsheet(x, y), rot, ref, value, netmap,
                                    layer=layer, ref_to_fab=not silk,
                                    bcu_flip=ref in BCU_FLIP,
                                    bcu_keep_pads=ref in BCU_KEEP_PADS)
        if silk:
            fp_txt = kf.silk_reference(fp_txt, "F.SilkS",
                                       size=1.0 if ref in SILK_BIG else 0.8,
                                       at=SILK_AT.get(ref))
        out.append(fp_txt)

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

    out.append(")")
    return "\n".join(out) + "\n"


# ------------------------------------------------------- silks / drawings --
def _txt(x, y, text, layer="F.SilkS", size=0.8, thick=0.12, rot=0.0,
         mirror=False):
    # anchor = top-left corner of the text (KiCad centres fields by default)
    x, y = _onsheet(x, y)
    eff = '(effects (font (size %.2f %.2f) (thickness %.2f)) (justify left top%s))' % (
        size, size, thick, " mirror" if mirror else "")
    return ['(gr_text "%s" (at %.2f %.2f %.1f) (layer "%s") (uuid "%s") %s)'
            % (text, x, y, rot, layer, kf.uid(), eff)]


def silks():
    """Silkscreen + documentation marks, placed from the CAD geometry.

    Text height 0.8 mm minimum (fab limit). Per-pin text is only used where the
    2.54 mm pitch leaves room for it; the 2.00 mm JST-PH shells carry one
    two-circuit label instead.
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
    # under the west end of the north row, 0.35 mm east of J13's pads and
    # 0.30 mm north of OK1's printed designator.
    out += _txt(41.20, 26.00, "PIN 1 = USB END", size=0.8)
    out += _txt(KEEP[0] + 0.8, KEEP[1] + 0.8, "ANTENNA - NO COPPER", size=0.8)

    # TFT socket, standing at the east edge (14-way, pin 1 = VCC to the north)
    out += _txt(TFT["x"] - 4.2, TFT["y1"] - 0.5, "1", size=0.9)
    out += _txt(TFT["x"] - 4.4, TFT["y_end"] - 0.5, "14", size=0.9)
    out += _txt(TFT["x"] - 4.4, (TFT["y1"] + TFT["y_end"]) / 2.0 + 1.05,
                "TFT", size=0.9, rot=90.0)

    # 12 V entry polarity, east of the XT30 (B.Cu part -> mirrored silks, so the
    # anchor is the text's RIGHT edge).  Right-aligned at x 34.60: glyphs land at
    # x 31.0..34.6, clear of the shifted body (courtyard east edge 28.35, pad 2
    # edge 27.05) and of the module's "IN-" label (x 28.25..31.15, y 14.35..15.55)
    out += _txt(34.60, 12.4, "+12V", layer="B.SilkS", size=0.9, mirror=True)
    out += _txt(34.60, 14.4, "GND", layer="B.SilkS", size=0.9, mirror=True)

    # DC-DC module (under the board): labels outside its 21 x 43 outline
    inx, iny = LM["x"] - LM["span_x"] / 2, LM["y"] - LM["span_y"] / 2
    outx, outy = LM["x"] - LM["span_x"] / 2, LM["y"] + LM["span_y"] / 2
    # B.SilkS text is mirrored, so the anchor is the text's RIGHT edge
    for lab, x, y in [("IN+", inx + 0.9, iny - 4.3), ("IN-", inx + 17.9, iny - 4.3),
                      ("OUT+", outx + 1.2, outy + 1.6), ("OUT-", outx + 18.2, outy + 1.6)]:
        out += _txt(x, y, lab, layer="B.SilkS", size=0.9, mirror=True)

    # JST-PH shells hang under the board, so every pin name is printed on the
    # shell's own side - B.SilkS, mirrored, one word per named pin, centred on
    # that pin's column and set just inside the pad row (B.SilkS anchor is the
    # text's RIGHT edge).  Pads reach y 19.60 (north pair) and y 55.40 (south
    # pair); the 10.2 mm-deep courtyards stop at y 20.60 / y 54.40; the DevKit
    # rows run y 23.96..25.66 (J10a) and y 49.35..51.06 (J10b) and the DevKit
    # body hanging underneath spans y 23.49..51.49 - so both text bands sit
    # clear of plug, body, courtyard and copper.  C11 (the TRIP filter) is on the
    # TOP side above the TRIP/EN words: opposite faces, so the silks still read
    # from below.  Columns: J14 TRIP 64.00 / EN 68.00, J17 IGN_12 78.40 /
    # COOL_SNS 82.40, J6 LDR_SNS 84.40 / FUEL_SNS 80.40, J9 HALL_SIG 67.00.
    # Widths measured from the exported PDF at size 0.8 (KiCad stroke font).
    W8 = {"TRIP": 2.59, "EN": 1.56, "IGN": 2.02, "COOL": 3.12,
          "LDR": 2.25, "FUEL": 2.89, "HALL": 2.82}
    for lab, cx, y in [("TRIP", 64.00, 21.00), ("EN", 68.00, 21.00),
                       ("IGN", 78.40, 21.00), ("COOL", 82.40, 21.00),
                       ("LDR", 84.40, 52.90), ("FUEL", 80.40, 52.90),
                       ("HALL", 67.00, 52.90)]:
        out += _txt(cx + W8[lab] / 2.0, y, lab, layer="B.SilkS", size=0.8,
                    mirror=True)

    # GNSS pads sit under the board, so their label is mirrored silks too
    out += _txt(57.6 + 1.4, 12.9, "GNSS", layer="B.SilkS", size=0.8,
                mirror=True)

    # per-pin text where there is room (2.54 mm pitch groups).  J8 / JP1 are
    # pads on the underside, so their labels go on B.SilkS, mirrored.
    SIDE = {"J13": -1.4, "J15": 1.2}
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
    docs = ["TFT 1..14: VCC GND CS RST DC SDI SCK LED SDO T_CLK T_CS",
            "TFT cont: T_DIN TPEN T_DO",
            "ESP near row 1..15 (USB end first): VIN GND 13 12 14 27 26 25",
            "  33 32 35 34 39 36 EN",
            "ESP far row 16..30 (USB end first): 3V3 GND 15 2 4 16 17 5 18",
            "  19 21 3 1 22 23",
            "J17 IGN GND COOL GND   J14 TRIP GND EN GND",
            "J6 LDR GND FUEL GND    J9 +V SIG GND",
            "JST = right-angle SMD, bodies under the board; pin 1 is at the",
            "west end of the north pair, east end of the south pair (top view)",
            "no trip button on the carrier - trip comes in on J14 (pin 1)",
            "SPARE / DEBUG PADS - see J13 (TX RX GND 5V) and J15",
            "J13 DEBUG  J15 SPARE (D26 SDA SCL D19 D15 GND)",
            "TP1 IGN_SENSE  TP2 HALL  TP3 BAT_SNS  TP4 FUEL_SNS  TP5 GND"]
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
    txt = build()
    open(OUT, "w", encoding="utf-8").write(txt)
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
