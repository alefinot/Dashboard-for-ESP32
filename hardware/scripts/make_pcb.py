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

ESP_ROWS = {                            # ESP32 DevKit V1 clone (30 pin, 2x15)
    "row_spacing": 25.40,               # centre-to-centre of the two header rows
    "pitch": 2.54,
    "pins": 15,
    "y_near": 20.00,                    # rows run along X; near row is the top one
    "x_start": 22.00,                   # pin 1 of the FAR row (3V3 end)
    # The board is mounted flipped versus the old draft: the DevKit's USB end
    # sits at x_start and its EN / 3V3 / antenna end sits at x_end, so the
    # antenna keep-out lands in the free right-hand column.
}
ESP_ROWS["x_end"] = ESP_ROWS["x_start"] + 14 * ESP_ROWS["pitch"]   # 57.56
ESP_ROWS["y_far"] = ESP_ROWS["y_near"] + ESP_ROWS["row_spacing"]   # 45.40

# DevKit board outline under the carrier (51 x 27.9 mm body, rows inset 1.3 mm)
ESP_BODY = (13.6, 18.6, 67.6, 46.8)

TFT = {
    "x_start": 25.00, "y": 61.50, "pitch": 2.54,   # 1x16 socket, F.Cu edge
}

# LM2596 module: 4 solder pads, IN pair -> OUT pair 40 mm apart, 18 mm + to -
LM = {"x": 84.00, "y": 37.50, "span_y": 40.00, "span_x": 16.00}

# copper keep-out under the DevKit PCB antenna (between EN and IO23, all layers)
KEEP = (ESP_ROWS["x_end"] - 8.6, ESP_ROWS["y_near"] + 2.0,
        ESP_BODY[2] + 0.6,       ESP_ROWS["y_far"] - 2.0)


def keepout_clear(bb):
    kx0, ky0, kx1, ky1 = KEEP
    return not (bb[0] < kx1 and kx0 < bb[2] and bb[1] < ky1 and ky0 < bb[3])


def esp_pad(n):
    """Absolute pad centre for ESP32 socket position n (1..15 near row,
    16..30 far row), counted from the EN / 3V3 end, which is the far-x end."""
    i = (n - 1) % 15
    x = ESP_ROWS["x_end"] - i * ESP_ROWS["pitch"]
    y = ESP_ROWS["y_near"] if n <= 15 else ESP_ROWS["y_far"]
    return (x, y)


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
BOTTOM_SIDE = {"J10a", "J10b", "U4"}

# ------------------------------------------------------------- parts -------
# All through-hole. Entry:
#   (ref, footprint_id, x, y, rot, {pad: net}, value, layer, dnp[, outline])
# For headers at rot 90/270 pin 1 sits at (x, y) and the row runs along +X.
def parts():
    P = []

    # ---- ESP32 DevKit sockets, B.Cu (the dev board hangs under the carrier)
    near = {str(i): NETMAP_ESP[i] for i in range(1, 16)}
    far = {str(i - 15): NETMAP_ESP[i] for i in range(16, 31)}
    P.append(("J10a", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_near"], 90.0, near, "ESP32-DEVKIT-A",
              "B.Cu", 0))
    P.append(("J10b", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_far"], 90.0, far, "ESP32-DEVKIT-B",
              "B.Cu", 0))

    # ---- TFT display socket, F.Cu along the bottom edge
    tft = {str(i + 1): n for i, n in enumerate(TFT_MAP)}
    P.append(("J3", "Connector_PinSocket_2.54mm:PinSocket_1x16_P2.54mm_Vertical",
              TFT["x_start"], TFT["y"], 90.0, tft, "TFT-ILI9488", "F.Cu", 0))

    # ---- 12 V entry + protection (top right, next to the module pads) ----
    # XT30 -> F1 fuse -> Q1 P-MOS high side -> module IN+;  gate zener D1.
    P.append(("J1", "Connector_AMASS:AMASS_XT30U-M_1x02_P5.0mm_Vertical",
              83.0, 20.0, 0.0, {"1": N("DC_IN"), "2": N("GND")}, "XT30U-M",
              "F.Cu", 0))
    P.append(("F1", "Fuse:Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal",
              90.0, 36.5, 180.0, {"1": N("DC_IN"), "2": N("DC_F")}, "5x20 2A",
              "F.Cu", 0))
    P.append(("Q1", "Package_TO_SOT_THT:TO-220-3_Vertical",
              72.6, 45.0, 0.0, {"1": N("MOS_G"), "2": N("DC_12"), "3": N("DC_F")},
              "IRF9540N", "F.Cu", 0))
    P.append(("D1", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              71.0, 27.0, 0.0, {"1": N("MOS_G"), "2": N("DC_F")}, "BZX55C15V",
              "F.Cu", 0))
    P.append(("D2", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              78.0, 27.0, 0.0, {"1": N("DC_12"), "2": N("GND")}, "P6KE33A",
              "F.Cu", 0))
    P.append(("R1", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical",
              85.0, 27.0, 0.0, {"1": N("MOS_G"), "2": N("DC_F")}, "10k", "F.Cu", 0))
    P.append(("C2", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              90.5, 27.0, 0.0, {"1": N("DC_12"), "2": N("GND")}, "100n", "F.Cu", 0))
    P.append(("C1", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm",
              86.0, 43.4, 0.0, {"1": N("DC_12"), "2": N("GND")}, "220u/50V",
              "F.Cu", 0))

    # ---- LM2596 module: four solder pads on the BOTTOM face (custom fp) ----
    pads = [("1", -LM["span_x"] / 2, -LM["span_y"] / 2, 3.5, 2.5, 1.0,
             (N("DC_12"), "DC_12")),
            ("2", LM["span_x"] / 2, -LM["span_y"] / 2, 3.5, 2.5, 1.0,
             (N("GND"), "GND")),
            ("3", -LM["span_x"] / 2, LM["span_y"] / 2, 3.5, 2.5, 1.0,
             (N("V5"), "V5")),
            ("4", LM["span_x"] / 2, LM["span_y"] / 2, 3.5, 2.5, 1.0,
             (N("GND"), "GND"))]
    P.append(("U4", None, LM["x"], LM["y"], 0.0, pads, "LM2596-MOD", "B.Cu", 0,
              [(-10.5, -22.5), (10.5, -22.5), (10.5, 22.5), (-10.5, 22.5),
               (-10.5, -22.5)]))

    P.append(("C4", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              84.0, 62.0, 0.0, {"1": N("V5"), "2": N("GND")}, "100n", "F.Cu", 0))
    # hall sensor supply: jumper 5V <-> sel <-> 12V
    P.append(("JP1", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
              53.8, 51.2, 90.0,
              {"1": N("V5"), "2": N("HALL_VCC"), "3": N("DC_12")}, "HALL +V",
              "F.Cu", 0))

    # ---- optocouplers: ignition sense + hall (mid-left, under the dev board)
    P.append(("OK1", "Package_DIP:DIP-4_W7.62mm",
              13.0, 34.5, 0.0,
              {"1": N("IGN_LED"), "2": N("GND"),
               "3": N("IGN_SENSE"), "4": N("V3V3")}, "PC817B", "F.Cu", 0))
    P.append(("OK2", "Package_DIP:DIP-4_W7.62mm",
              25.5, 34.5, 0.0,
              {"1": N("HALL_LED"), "2": N("GND"),
               "3": N("HALL_OUT"), "4": N("V3V3")}, "PC817B", "F.Cu", 0))

    # ---- analog decoupling (quiet island, left of the antenna keep-out)
    for ref, net, x, y in [("C5", "BAT_SNS", 12.5, 25.0), ("C6", "FUEL_SNS", 19.5, 25.0),
                           ("C7", "COOL_SNS", 26.5, 25.0), ("C8", "LDR_SNS", 33.5, 25.0),
                           ("C9", "IGN_SENSE", 40.5, 25.0), ("C10", "HALL", 19.5, 30.0)]:
        P.append((ref, "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm", x, y, 0.0,
                  {"1": N(net), "2": N("GND")}, "100n", "F.Cu", 0))

    # ---- sensor signal conditioning resistors (bottom strip rows) ---------
    RVERT = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical"
    res = [
        ("R2", "47k",  None, "DC_12", "BAT_DIV", 11.5, 56.0),
        ("R3", "10k",  None, "BAT_DIV", "GND", 17.5, 55.8),
        ("R4", "1k",   None, "BAT_DIV", "BAT_SNS", 23.5, 57.8),
        ("R5", "220R", None, "V3V3", "FUEL_SNS", 29.5, 57.8),
        ("R6", "10k",  None, "V3V3", "COOL_SNS", 35.5, 57.8),
        ("R7", "10k",  None, "LDR_SNS", "GND", 41.5, 57.8),
        ("R8", "1k",   None, "IGN_12", "IGN_LED", 47.5, 57.8),
        ("R9", "10k",  None, "IGN_SENSE", "GND", 53.5, 57.8),
        ("R10", "1k",  None, "HALL_SIG", "HALL_LED", 59.5, 57.8),
        ("R11", "10k", None, "HALL_OUT", "GND", 11.5, 41.0),
        ("R12", "10k", None, "TFT_BL", "GND", 18.5, 41.0),
        ("R13", "1k",  None, "HALL_OUT", "HALL", 40.5, 41.0),
    ]
    for ref, val, _x, a, b, x, y in res:
        P.append((ref, RVERT, x, y, 0.0, {"1": N(a), "2": N(b)}, val, "F.Cu", 0))

    P.append(("D3", "Diode_THT:D_DO-15_P2.54mm_Vertical_AnodeUp",
              24.5, 41.0, 0.0, {"1": N("HALL"), "2": N("GND")}, "P6KE6V2",
              "F.Cu", 0))
    P.append(("C11", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              12.0, 45.5, 0.0, {"1": N("TRIP_BTN"), "2": N("GND")}, "100n",
              "F.Cu", 0))
    P.append(("C13", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
              33.5, 41.0, 0.0, {"1": N("V3V3"), "2": N("GND")}, "100n", "F.Cu", 0))

    # ---- vehicle-side connectors (bottom strip, pins along +X) ------------
    H2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
    H3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
    H4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"
    H5 = "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical"
    H6 = "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical"
    conns = [
        ("J14", H2, "TRIP",     12.0, 51.2, {"1": "TRIP_BTN", "2": "GND"}),
        ("J5",  H2, "COOLANT",  18.6, 51.2, {"1": "COOL_SNS", "2": "GND"}),
        ("J7",  H2, "FUEL",     25.2, 51.2, {"1": "FUEL_SNS", "2": "GND"}),
        ("J6",  H2, "LDR",      31.8, 51.2, {"1": "LDR_SNS", "2": "GND"}),
        ("J9",  H3, "HALL",     38.4, 51.2,
         {"1": "HALL_VCC", "2": "HALL_SIG", "3": "GND"}),
        ("J17", H2, "IGNITION", 47.5, 51.2, {"1": "IGN_12", "2": "GND"}),
    ]
    for ref, fp, val, x, y, nm in conns:
        P.append((ref, fp, x, y, 90.0, {k: N(v) for k, v in nm.items()}, val,
                  "F.Cu", 0))

    # ---- headers along the top edge --------------------------------------
    P.append(("J13", H4, 18.0, 14.5, 90.0,
              {"1": N("DBG_TX"), "2": N("DBG_RX"), "3": N("GND"), "4": N("V5")},
              "DEBUG", "F.Cu", 0))
    P.append(("J15", H6, 30.0, 14.5, 90.0,
              {"1": N("SPARE_D26"), "2": N("SPARE_D21"), "3": N("SPARE_D22"),
               "4": N("SPARE_D19"), "5": N("SPARE_D15"), "6": N("GND")}, "SPARE",
              "F.Cu", 0))
    P.append(("J16", H2, 46.5, 14.5, 90.0,
              {"1": N("EN"), "2": N("GND")}, "RESET", "F.Cu", 0))
    P.append(("J8", H6, 52.8, 14.5, 90.0,
              {"1": N("SPARE_D21"), "2": N("SPARE_D22"), "3": N("V5"),
               "4": N("GND"), "5": N("GPS_TX"), "6": N("GPS_RX")}, "GPS",
              "F.Cu", 0))
    P.append(("J11", "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical",
              62.5, 49.5, 90.0,
              {"1": N("GND"), "2": N("V5"), "3": N("SPARE_D26"),
               "4": N("SPARE_D13"), "5": N("IGN_12"), "6": N("SPARE_D2")}, "SMART",
              "F.Cu", 0))

    # ---- trip button (6 mm tactile, on-board; the panel button is optional)
    P.append(("SW1", "Button_Switch_THT:SW_PUSH_6mm", 65.3, 53.0, 0.0,
              {"1": N("TRIP_BTN"), "2": N("TRIP_BTN"),
               "3": N("GND"), "4": N("GND")}, "TRIP", "F.Cu", 0))

    # ---- test points (top strip row) -------------------------------------
    tps = [("TP1", "IGN_SENSE"), ("TP2", "HALL"), ("TP3", "BAT_SNS"),
                      ("TP4", "FUEL_SNS")]
    for i, (ref, net) in enumerate(tps):
        P.append((ref, "TestPoint:TestPoint_Pad_D1.5mm",
                  61.5 + i * 2.5, 19.5, 0.0, {"1": N(net)}, net, "F.Cu", 0))

    # ---- mounting holes --------------------------------------------------
    # DIN 965 / ISO 14581 variant: 3.2 mm clearance drill for an M3 flat (flat,
    # countersunk) head, plus the library's 6.1 mm countersink marker on the
    # silkscreen so the seat area is kept clear.  Four corners, >= 3.5 mm of
    # copper-free room around each cone.
    for i, (x, y) in enumerate([(12.8, 11.8), (82.4, 11.8), (12.6, 63.4),
                                (92.4, 63.4)]):
        P.append(("MH%d" % (i + 1),
                  "MountingHole:MountingHole_3.2mm_M3_DIN965", x, y,
                  0.0, {}, "M3 countersunk", "F.Cu", 0))
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
PIN_SILK = {
    "J9": ["+V", "SIG", "G"],          # hall: solder-jumper selects +V (JP1)
    "J17": ["IGN", "GND"],
    "J5": ["SIG", "GND"],             # coolant NTC
    "J7": ["SIG", "GND"],             # fuel sender (SAE)
    "J6": ["SNS", "GND"],             # LDR (solder the LDR here too)
    "J14": ["TRIP", "GND"],
    "J13": ["TX", "RX", "GND", "5V"],
    "J15": ["D26", "D21", "D22", "D19", "D15", "GND"],
    "J16": ["EN", "GND"],
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

    # mounting holes are NPTH only: nothing to add beyond the footprint

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
    """Silkscreen: short human markers only (0.8 mm min height for the fabs).

    The 2.54 mm pitch of the DevKit / TFT headers cannot carry per-pad text
    at a legal silkscreen height, so the rows are marked at pin 1 and the full
    pin map is printed on Dwgs.User (visible in KiCad, not on the board) and
    in the README.
    """
    out = []
    ex = ESP_ROWS

    # dev board outline + antenna keep-out (documentation layers only)
    out += _dashrect(ESP_BODY[0], ESP_BODY[1], ESP_BODY[2], ESP_BODY[3])
    out += _dashrect(KEEP[0], KEEP[1], KEEP[2], KEEP[3])

    # ESP32 DevKit sockets: pad 1 (square pad) sits at the USB end
    out += _txt(ex["x_start"] - 4.0, ex["y_near"] - 0.4, "1", size=0.9)
    out += _txt(ex["x_start"] - 4.0, ex["y_far"] - 0.4, "1", size=0.9)
    out += _txt(15.0, 22.6, "ESP PIN 1 = USB END", size=0.8)
    out += _txt(55.0, 32.5, "NO COPPER", size=0.8)

    # TFT display socket: 16-way along the bottom edge, pin 1 = VCC
    out += _txt(TFT["x_start"] - 3.4, TFT["y"] - 0.4, "1", size=0.9)
    x_end = TFT["x_start"] + (len(TFT_LABEL) - 1) * TFT["pitch"]
    out += _txt(x_end + 2.2, TFT["y"] - 0.4, "16", size=0.9)

    # vehicle / accessory headers: one short label per pin
    for ent in parts():
        ref, x, y = ent[0], ent[2], ent[3]
        if ref not in PIN_SILK:
            continue
        below = y > 30.0
        for i, lab in enumerate(PIN_SILK[ref]):
            out += _txt(x + i * 2.54 - 0.5, y + (2.4 if below else -3.2), lab,
                        size=0.8)

    # LM2596 module lives on the bottom face -> label on B.SilkS
    for lab, x, y in [("IN+", LM["x"] - LM["span_x"] / 2 - 4.6, LM["y"] - LM["span_y"] / 2 + 0.4),
                      ("IN-", LM["x"] + LM["span_x"] / 2 - 6.2, LM["y"] - LM["span_y"] / 2 - 2.9),
                      ("OUT+", LM["x"] - LM["span_x"] / 2 - 5.5, LM["y"] + LM["span_y"] / 2 + 2.5),
                      ("OUT-", LM["x"] + LM["span_x"] / 2 - 4.0, LM["y"] + LM["span_y"] / 2 - 2.5)]:
        out += _txt(x, y, lab, layer="B.SilkS", size=0.9, mirror=True)
    out += _txt(65.5, 63.6, "LM2596 MODULE", layer="B.SilkS", size=1.0,
                mirror=True)

    # power entry polarity
    out += _txt(80.6, 15.4, "+12V", size=1.0)
    out += _txt(88.2, 15.4, "GND", size=1.0)

    # build documentation (not printed): pin map + test point list
    docs = [("TP1 IGN_SENSE  TP2 HALL  TP3 BAT_SNS  TP4 FUEL_SNS", 2.0),
            ("TFT 1..16: VCC GND CS RST DC SDI SCK LED SDO T_CLK T_CS", 4.0),
            ("TFL16 cont: T_DINTPEN T_DO SD NC NC NC NC", 5.0),
            ("ESP far row 16..30 (USB end first): 3V3 GND 15 2 4 16 17 5 18", 7.0),
            ("  19 21 3 1 22 23", 8.0),
            ("ESP near row 1..15 (USB end first): VIN GND 13 12 14 27 26 25", 9.0),
            ("  33 32 35 34 39 36 EN", 10.0)]
    for txt, y in docs:
        out += _txt(6.0, y, txt, layer="Dwgs.User", size=1.0)
    out += _txt(46.0, 65.8, "Dashboard++ for ESP32 carrier V0.1",
                layer="Dwgs.User", size=1.2)
    return out


def netcheck():
    """Count pads per net so a typo or a missing connection shows up at once."""
    cnt = {}
    for ent in parts():
        netmap = ent[5]
        if ent[1] is None:                  # custom footprint: pads carry nets
            for p in netmap:
                if p[6]:
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


def main():
    txt = build()
    open(OUT, "w", encoding="utf-8").write(txt)
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
