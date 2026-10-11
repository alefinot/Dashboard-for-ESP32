"""Generate the Dashboard++ for ESP32 carrier board (hardware/dashboard-plusplus-carrier.kicad_pcb).

Everything mechanical/electrical lives in the tables below: NETS, PARTS, OUTLINE,
KEEPOUT.  Re-run after every change:

    python make_pcb.py            # writes the board, runs DRC, prints the report

Coordinate system: KiCad coordinates, millimetres, Y increasing downwards
(looking at the TOP / display side of the carrier).  Board outline spans
BOARD_X0..BOARD_X1 x BOARD_Y0..BOARD_Y1.

Sides:
  F.Cu  = display side.  The 4.3" TFT module plugs in here and lies flat.
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
BOARD_X0, BOARD_Y0 = 10.0, 10.0          # board outline corners
BOARD_X1, BOARD_Y1 = 95.0, 65.0          # 85.0 x 55.0 mm

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
# Body drawn u 8.80..58.80 x v 13.49..41.49 (50.00 x 28.00); header strips
# 36.99 x 1.00 at v 13.81..14.80 / 40.21..41.20 - the drawing puts their
# centre lines 26.40 apart, the real DevKit is 25.40, so the rows are
# re-centred on the body centre line (v 27.49) -> v 14.79 / 40.19.
ESP_ROWS = {
    "row_spacing": 25.40,               # centre-to-centre of the two header rows
    "pitch": 2.54,
    "pins": 15,
    "y_near": 24.79,                    # north row (J10a), pin 1 = VIN at the west
    "x_start": 46.46,                   # pin 1 (USB end of the dev board)
    "x_end": 46.46 + 14 * 2.54,         # pin 15 (EN) = 82.02
    "y_far": 50.19,                     # south row (J10b), pin 16 = 3V3 at the west
}

# DevKit board outline under the carrier (drawn 50.00 x 28.00)
ESP_BODY = cadbox(8.80, 13.49, 58.80, 41.49)     # 36.20,23.49 .. 86.20,51.49

# TFT display socket J3: drawn as a 1.00 x 35.00 slab standing 10.02 mm above
# F.Cu at the board edge (u 1.99..2.99, v 7.00..42.00). The module's own
# silkscreen is 16-way (VCC GND CS RST DC SDI SCK LED SDO T_CLK T_CS T_DIN
# TPEN T_DO SD NC), so a 1x16 socket is used: its 40.64 body is centred on the
# drawn slot's north end, growing 5.6 mm into the drawing's 13 mm south gap.
TFT = {"x": 92.51, "y1": 18.27, "pitch": 2.54, "pins": 16}   # pin 1 north
TFT["y_end"] = TFT["y1"] + (TFT["pins"] - 1) * TFT["pitch"]  # 56.37

# LM2596 step-down module, soldered to the BOTTOM face.
# Drawn 21.00 x 43.00 (u 61.75..82.75, v 8.64..51.64) with silks `in +` / `-`
# on the u-small (north) end and `out +` / `-` on the south end, `+` on the
# u-large side -> in KiCad: IN pair to the north, OUT pair to the south, `+`
# pads to the west.  Pad pairs read off the silks: y 21.91 / 60.90, x 17.0 apart.
LM = {"x": 22.75, "y": 41.405, "span_x": 17.00, "span_y": 38.99}
LM_BODY = cadbox(61.75, 8.64, 82.75, 51.64)      # 12.25,18.64 .. 33.25,61.64

# GNSS J8: drawn as a 22.99 x 7.00 "6 solder pads" zone (u 35.81..58.80,
# v 0.00..7.00) - kept as a 6-pin 2.54 header inside it.
GNSS_ZONE = cadbox(35.81, 0.00, 58.80, 7.00)     # 36.20,10.00 .. 59.19,17.00

# Four JST-PH zones, 12.01 x 7.00 each, two designators per zone (the drawing
# writes both); one shell per zone, two circuits each (user decision).
JST_ZONES = {
    "J14": (cadbox(20.81, 0.00, 32.82, 7.00), ("J14", "J16")),   # TRIP + EN
    "J17": (cadbox(8.80, 0.00, 20.81, 7.00), ("J17", "J5")),     # IGN + coolant
    "J6": (cadbox(8.80, 48.01, 20.81, 55.01), ("J6", "J7")),     # LDR + fuel
    "J9": (cadbox(20.81, 48.01, 32.82, 55.01), ("J9",)),         # hall (+V SIG G)
}

# JP1 (hall +V selector): drawn 6.00 x 3.50 box, silks `JP1` / `DIP SWITCH`.
JP1_BOX = cadbox(34.65, 49.74, 40.66, 53.24)     # 54.34,59.74 .. 60.35,63.24

# Mounting holes - the drawing has none; these four clear everything drawn
# (H1/H2 sit in the free east corners, H3/H4 sit between the module's east
# edge and the JST / GNSS zones).
MOUNTS = [(91.50, 13.50), (91.50, 61.50), (44.00, 62.00), (34.50, 13.00)]
MOUNT_DRILL, MOUNT_RING = 3.2, 5.0

# Annotation zones drawn dashed on the TOP view (F.Cu work areas, Dwgs.User)
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
BODY = {"C1": 10.5, "C3": 10.5}

# Footprints that physically sit on the BOTTOM face (the off-board DC-DC module
# soldered to the back): their courtyard does not block top-side parts.
BOTTOM_SIDE = {"J10a", "J10b", "U4", "J1", "J14", "J17", "J6",
               "J9", "J8", "JP1"}

# ------------------------------------------------------------- parts -------
# All through-hole. Entry:
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
    # (drawn 1.00 x 35.00 slab, 10.02 mm above F.Cu; the module is 16-way).
    tft = {str(i + 1): n for i, n in enumerate(TFT_MAP)}
    P.append(("J3", "Connector_PinSocket_2.54mm:PinSocket_1x16_P2.54mm_Vertical",
              TFT["x"], TFT["y1"], 0.0, tft, "TFT-ILI9488", "F.Cu", 0))

    # ---- DC-DC step-down module, soldered to the BOTTOM face (custom fp).
    # Drawn 21 x 43 body, `in +`/`-` at the north end, `out +`/`-` at the south,
    # `+` on the west side -> IN pair north, OUT pair south, `+` pads west.
    hx, hy = LM["span_x"] / 2, LM["span_y"] / 2
    pads = [("1", -hx, -hy, 3.5, 2.5, 1.0, (N("DC_12"), "DC_12")),      # IN +
            ("2", hx, -hy, 3.5, 2.5, 1.0, (N("GND"), "GND")),           # IN -
            ("3", -hx, hy, 3.5, 2.5, 1.0, (N("V5"), "V5")),             # OUT +
            ("4", hx, hy, 3.5, 2.5, 1.0, (N("GND"), "GND")),            # OUT -
            ]
    bx0, by0, bx1, by1 = LM_BODY
    cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
    outl = [(bx0 - cx, by0 - cy), (bx1 - cx, by0 - cy), (bx1 - cx, by1 - cy),
            (bx0 - cx, by1 - cy), (bx0 - cx, by0 - cy)]
    P.append(("U4", None, cx, cy, 0.0, pads, "DPP_DCDC_MODULE", "B.Cu", 0, outl))
    LM["x"], LM["y"] = cx, cy

    # ---- 12 V entry: XT30 on the BOTTOM face, in the drawn 9.99 x 5.01 zone
    P.append(("J1", "Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical",
              14.75, 13.99, 0.0, {"1": N("DC_IN"), "2": N("GND")}, "XT30U-M",
              "B.Cu", 0))

    # ---- JST-PH shells, one per drawn 12.01 x 7.00 zone, two circuits each.
    # Refs follow the drawing's first label of each zone; the second designator
    # is printed in the silks (README/PINOUT list both).
    PH4 = "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical"
    PH3 = "Connector_JST:JST_PH_B3B-PH-K_1x03_P2.00mm_Vertical"
    for ref, fp, val, nm in [
            ("J17", PH4, "IGN + COOLANT", {"1": "IGN_12", "2": "GND",
                                           "3": "COOL_SNS", "4": "GND"}),
            ("J14", PH4, "TRIP + EN", {"1": "TRIP_BTN", "2": "GND",
                                       "3": "EN", "4": "GND"}),
            ("J6", PH4, "LDR + FUEL", {"1": "LDR_SNS", "2": "GND",
                                       "3": "FUEL_SNS", "4": "GND"}),
            ("J9", PH3, "HALL", {"1": "HALL_VCC", "2": "HALL_SIG",
                                 "3": "GND"})]:
        z = JST_ZONES[ref][0]
        n = 4 if fp is PH4 else 3
        x = (z[0] + z[2]) / 2 - (n - 1)          # pads at 2.00 pitch
        y = (z[1] + z[3]) / 2 - 0.55
        P.append((ref, fp, x, y, 0.0, {k: N(v) for k, v in nm.items()}, val,
                  "B.Cu", 0))

    # ---- GNSS J8: 6-pin header inside the drawn 22.99 x 7.00 pad zone
    P.append(("J8", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
              40.00, 13.50, 90.0,
              {"1": N("SPARE_D21"), "2": N("SPARE_D22"), "3": N("V5"),
               "4": N("GND"), "5": N("GPS_TX"), "6": N("GPS_RX")}, "GPS",
              "B.Cu", 0))

    # ---- JP1: hall +V selector (5V / sel / 12V) in the drawn 6.00 x 3.50 box
    P.append(("JP1", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
              55.50, 61.50, 90.0,
              {"1": N("V5"), "2": N("HALL_VCC"), "3": N("DC_12")}, "HALL +V",
              "B.Cu", 0))

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
    P.append(("F1", "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
              15.5, 26.0, 0.0, {"1": N("DC_IN"), "2": N("DC_F")}, "5x20 10A",
              "F.Cu", 0))
    P.append(("Q1", "Package_TO_SOT_THT:TO-220-3_Vertical",
              13.5, 36.0, 0.0, {"1": N("MOS_G"), "2": N("DC_12"), "3": N("DC_F")},
              "IRF9540N", "F.Cu", 0))
    P.append(("D1", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              24.0, 36.0, 0.0, {"1": N("MOS_G"), "2": N("DC_F")}, "BZX55C15V",
              "F.Cu", 0))
    P.append(("D2", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              32.0, 36.0, 0.0, {"1": N("DC_12"), "2": N("GND")}, "P6KE33A",
              "F.Cu", 0))
    P.append(("R1", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical",
              13.5, 42.0, 0.0, {"1": N("MOS_G"), "2": N("DC_F")}, "10k", "F.Cu", 0))
    P.append(("C2", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              30.5, 41.0, 0.0, {"1": N("DC_12"), "2": N("GND")}, "100n", "F.Cu", 0))
    # input / output bulk sit on F.Cu directly over the module
    P.append(("C1", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
              29.5, 48.5, 0.0, {"1": N("DC_12"), "2": N("GND")}, "220u/50V",
              "F.Cu", 0))
    P.append(("C3", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
              15.5, 56.5, 0.0, {"1": N("V5"), "2": N("GND")}, "470u/35V",
              "F.Cu", 0))
    P.append(("C4", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              13.5, 47.5, 0.0, {"1": N("V5"), "2": N("GND")}, "100n", "F.Cu", 0))

    # ---- signal conditioning, F.Cu, in the drawn mid small-parts zone
    # (43.38 x 18.91, under the dev board that hangs below).
    P.append(("OK1", "Package_DIP:DIP-4_W7.62mm", 39.5, 30.0, 0.0,
              {"1": N("IGN_LED"), "2": N("GND"),
               "3": N("IGN_SENSE"), "4": N("V3V3")}, "PC817B", "F.Cu", 0))
    P.append(("OK2", "Package_DIP:DIP-4_W7.62mm", 50.5, 30.0, 0.0,
              {"1": N("HALL_LED"), "2": N("GND"),
               "3": N("HALL_OUT"), "4": N("V3V3")}, "PC817B", "F.Cu", 0))

    RVERT = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical"
    RX = [39.0, 44.9, 50.8, 56.7, 62.6, 68.5]
    res = [("R2", "47k", "DC_12", "BAT_DIV", 37.5), ("R3", "10k", "BAT_DIV", "GND", 37.5),
           ("R4", "1k", "BAT_DIV", "BAT_SNS", 37.5), ("R5", "220R", "V3V3", "FUEL_SNS", 37.5),
           ("R6", "10k", "V3V3", "COOL_SNS", 37.5), ("R7", "10k", "V3V3", "LDR_SNS", 37.5),
           ("R8", "1k", "IGN_12", "IGN_LED", 41.0), ("R9", "10k", "IGN_SENSE", "GND", 41.0),
           ("R10", "1k", "HALL_SIG", "HALL_LED", 41.0), ("R11", "10k", "HALL_OUT", "GND", 41.0),
           ("R12", "10k", "TFT_BL", "GND", 41.0), ("R13", "1k", "HALL_OUT", "HALL", 41.0)]
    for i, (ref, val, a, b, y) in enumerate(res):
        P.append((ref, RVERT, RX[i % 6], y, 0.0,
                  {"1": N(a), "2": N(b)}, val, "F.Cu", 0))

    CDISC = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm"
    CX = [39.6, 45.6, 51.6, 57.6, 63.6, 69.3]
    caps = [("C5", "BAT_SNS"), ("C6", "FUEL_SNS"), ("C7", "COOL_SNS"),
            ("C8", "LDR_SNS"), ("C9", "IGN_SENSE"), ("C10", "HALL")]
    for i, (ref, net) in enumerate(caps):
        P.append((ref, CDISC, CX[i], 44.5, 0.0, {"1": N(net), "2": N("GND")},
                  "100n", "F.Cu", 0))
    P.append(("C11", CDISC, 63.5, 21.5, 0.0, {"1": N("TRIP_BTN"), "2": N("GND")},
              "100n", "F.Cu", 0))
    P.append(("C13", CDISC, 30.5, 55.5, 0.0, {"1": N("V3V3"), "2": N("GND")},
              "100n", "F.Cu", 0))
    P.append(("D3", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              39.5, 57.0, 0.0, {"1": N("HALL"), "2": N("GND")}, "P6KE6V2",
              "F.Cu", 0))

    # ---- trip button, south spare zone
    P.append(("SW1", "Button_Switch_THT:SW_PUSH_6mm", 48.5, 53.6, 0.0,
              {"1": N("TRIP_BTN"), "2": N("TRIP_BTN"),
               "3": N("GND"), "4": N("GND")}, "TRIP", "F.Cu", 0))

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
    P.append(padgroup("J15", "SPARE", 62.5, 56.5,
                      PIN_SILK["J15"], ["SPARE_D26", "SPARE_D21", "SPARE_D22",
                                        "SPARE_D19", "SPARE_D15", "GND"]))

    # ---- test points (north spare zone, below the GNSS header)
    for i, (ref, net) in enumerate([("TP1", "IGN_SENSE"), ("TP2", "HALL"),
                                    ("TP3", "BAT_SNS"), ("TP4", "FUEL_SNS"),
                                    ("TP5", "GND")]):
        P.append((ref, "TestPoint:TestPoint_Pad_D1.5mm", 38.0 + i * 2.5, 19.5,
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

# TFT 1x16 socket ("4.0'' TFT SPI 480X320 V1.1"), pin 1..16 as read off the
# display's own silkscreen (plan section 1). Only the LCD lines are connected:
# touch and the SD pads stay open (no MISO in firmware, touch removed in 1.3.6).
TFT_MAP = ["V5", "GND", "TFT_CS", "TFT_RST", "TFT_DC", "TFT_MOSI", "TFT_SCK",
           "TFT_BL", "NC", "NC", "NC", "NC", "NC", "NC", "NC", "NC"]
TFT_LABEL = ["VCC", "GND", "CS", "RST", "DC", "SDI", "SCK", "LED", "SDO",
             "T_CLK", "T_CS", "T_DIN", "TPEN", "T_DO", "SD", "NC"]

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
        out.append('(gr_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid))'
                   ' (layer "Edge.Cuts") (uuid "%s"))'
                   % (a[0], a[1], b[0], b[1], kf.uid()))

    boxes = []
    for ent in parts():
        ref, fp_id, x, y, rot, netmap, value, layer, dnp = ent[:9]
        bb = box_of(ent)
        if bb:
            boxes.append((ref, bb))

        if fp_id is None:                      # custom footprint (pads pre-resolved)
            out.append(kf.custom_footprint(ref, value, (x, y), rot, netmap,
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
        out.append(kf.board_footprint(fp_id, (x, y), rot, ref, value, netmap, layer=layer))

    for ref, bb in boxes:
        if not (BOARD_X0 <= bb[0] and bb[2] <= BOARD_X1
                and BOARD_Y0 <= bb[1] and bb[3] <= BOARD_Y1):
            print("  OUTSIDE BOARD  %-6s %s" % (ref, tuple(round(v, 2) for v in bb)))
        if not keepout_clear(bb):
            print("  IN ANTENNA AREA %-6s %s" % (ref, tuple(round(v, 2) for v in bb)))
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            ra, a = boxes[i]
            rb, b = boxes[j]
            if ra in BOTTOM_SIDE or rb in BOTTOM_SIDE:
                continue        # the module hangs under the PCB, no clash
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                print("  OVERLAP %s / %s" % (ra, rb))

    out += silks()
    out += netcheck()

    # copper keep-out under the ESP32 module antenna (all copper layers)
    kx0, ky0, kx1, ky1 = KEEP
    keep = [(kx0, ky0), (kx1, ky0), (kx1, ky1), (kx0, ky1), (kx0, ky0)]

    def poly(pts):
        return "(polygon (pts %s))" % " ".join("(xy %.2f %.2f)" % q for q in pts)

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
    eff = '(effects (font (size %.2f %.2f) (thickness %.2f)) (justify left top%s))' % (
        size, size, thick, " mirror" if mirror else "")
    return ['(gr_text "%s" (at %.2f %.2f %.1f) (layer "%s") (uuid "%s") %s)'
            % (text, x, y, rot, layer, kf.uid(), eff)]


def _dashrect(x0, y0, x1, y1, layer="Dwgs.User"):
    out = []
    for a, b in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)),
                 ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        out.append('(gr_line (start %.3f %.3f) (end %.3f %.3f)'
                   ' (stroke (width 0.12) (type dash)) (layer "%s")'
                   ' (uuid "%s"))' % (a[0], a[1], b[0], b[1], layer, kf.uid()))
    return out


def silks():
    """Silkscreen + documentation marks, placed from the CAD geometry.

    Text height 0.8 mm minimum (fab limit). Per-pin text is only used where the
    2.54 mm pitch leaves room for it; the 2.00 mm JST-PH shells carry one
    two-circuit label instead.
    """
    out = []

    # documentation outlines from the drawing (not printed on the board)
    out += _dashrect(ESP_BODY[0], ESP_BODY[1], ESP_BODY[2], ESP_BODY[3])
    out += _dashrect(KEEP[0], KEEP[1], KEEP[2], KEEP[3])
    out += _dashrect(*LM_BODY)
    out += _dashrect(*GNSS_ZONE)
    for ref in JST_ZONES:
        out += _dashrect(*JST_ZONES[ref][0])
    for z in (ZONE_SPARE_N, ZONE_SPARE_S, ZONE_SMD_MID, ZONE_SMD_WEST):
        out += _dashrect(*z)

    # dev board rows: pin 1 is VIN, at the dev board's USB end
    out += _txt(44.0, 26.9, "PIN 1 = USB END", size=0.8)
    out += _txt(KEEP[0] + 0.8, KEEP[1] + 0.8, "ANTENNA - NO COPPER", size=0.8)

    # TFT socket, standing at the east edge (16-way, pin 1 = VCC to the north)
    out += _txt(TFT["x"] - 4.2, TFT["y1"] - 0.5, "1", size=0.9)
    out += _txt(TFT["x"] - 4.4, TFT["y_end"] - 0.5, "16", size=0.9)
    out += _txt(TFT["x"] - 4.4, 36.0, "TFT", size=0.9, rot=90.0)

    # 12 V entry polarity, next to the XT30 (B.Cu part -> mirrored silks)
    out += _txt(26.2, 12.4, "+12V", layer="B.SilkS", size=0.9, mirror=True)
    out += _txt(25.6, 14.4, "GND", layer="B.SilkS", size=0.9, mirror=True)

    # DC-DC module (under the board): labels outside its 21 x 43 outline
    inx, iny = LM["x"] - LM["span_x"] / 2, LM["y"] - LM["span_y"] / 2
    outx, outy = LM["x"] - LM["span_x"] / 2, LM["y"] + LM["span_y"] / 2
    # B.SilkS text is mirrored, so the anchor is the text's RIGHT edge
    for lab, x, y in [("IN+", inx + 0.9, iny - 4.3), ("IN-", inx + 17.9, iny - 4.3),
                      ("OUT+", outx + 1.2, outy + 1.6), ("OUT-", outx + 18.2, outy + 1.6)]:
        out += _txt(x, y, lab, layer="B.SilkS", size=0.9, mirror=True)

    # JST-PH shells: one label per two-circuit zone (2.00 mm pitch is too fine
    # for per-pin text); the pin order is in the README / Dwgs.User pin map
    for lab, x, y in [("TRIP  EN", 63.2, 17.4), ("IGN  COOL", 75.2, 17.4),
                      ("LDR  FUEL", 76.6, 57.6), ("HALL", 57.0, 57.6)]:
        out += _txt(x, y, lab, size=0.8)

    # GNSS + hall supply selector + trip button
    out += _txt(55.2, 17.6, "GNSS", size=0.8)
    out += _txt(48.2, 60.0, "TRIP", size=0.8)

    # per-pin text where there is room (2.54 mm pitch groups)
    SIDE = {"J8": 2.4, "JP1": 1.9, "J13": -1.4, "J15": 1.2}
    for ent in parts():
        ref, x, y = ent[0], ent[2], ent[3]
        if ref not in PIN_SILK or ref not in SIDE:
            continue
        for i, lab in enumerate(PIN_SILK[ref]):
            out += _txt(x + i * 2.54 - 0.7 * len(lab) / 2.0, y + SIDE[ref],
                        lab, size=0.8)

    # build documentation (not printed): zones, pin map, test points
    docs = [("SPARE / DEBUG PADS - see J13 (TX RX GND 5V) and J15", 23.6),
            ("J13 DEBUG  J15 SPARE (D26 SDA SCL D19 D15 GND)", 25.0),
            ("TFT 1..16: VCC GND CS RST DC SDI SCK LED SDO T_CLK T_CS", 4.0),
            ("TFT cont: T_DINTPEN T_DO SD NC NC NC NC", 5.0),
            ("ESP near row 1..15 (USB end first): VIN GND 13 12 14 27 26 25", 7.0),
            ("  33 32 35 34 39 36 EN", 8.0),
            ("ESP far row 16..30 (USB end first): 3V3 GND 15 2 4 16 17 5 18", 9.0),
            ("  19 21 3 1 22 23", 10.0),
            ("J17 IGN GND COOL GND   J14 TRIP GND EN GND", 12.0),
            ("J6 LDR GND FUEL GND    J9 +V SIG GND", 13.0),
            ("TP1 IGN_SENSE  TP2 HALL  TP3 BAT_SNS  TP4 FUEL_SNS  TP5 GND", 21.0)]
    for txt, y in docs:
        out += _txt(10.5, y, txt, layer="Dwgs.User", size=1.0)
    out += _txt(10.5, 66.4, "Dashboard++ for ESP32 carrier - CAD layout rev 1",
                layer="Dwgs.User", size=1.2)
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
