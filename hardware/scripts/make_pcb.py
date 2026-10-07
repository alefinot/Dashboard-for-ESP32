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

ESP_ROWS = {                            # ESP32 DevKit V1 (30 pin, DOIT style)
    "row_spacing": 25.40,               # centre-to-centre of the two header rows
    "pitch": 2.54,
    "pins": 15,
    "y_near": 20.00,                    # measured on B.Cu, rows run along +X
    "x_start": 22.00,                   # first pin centre
    # "near" row = the row at y_near, "far" row = y_near + row_spacing
}

TFT = {
    "x_start": 25.00, "y": 61.50, "pitch": 2.54,   # 1x14 header, F.Cu edge
}

LM = {
    "x": 78.00, "y": 20.00, "rot": 90.0, "pitch": 3.50,   # LM2596 module pads
}

# copper keep-out under the ESP32 module antenna (x0, y0, x1, y1)
KEEP = (ESP_ROWS["x_start"] + 14 * ESP_ROWS["pitch"] + 2.0,
        ESP_ROWS["y_near"] - 2.5,
        ESP_ROWS["x_start"] + 14 * ESP_ROWS["pitch"] + 13.5,
        ESP_ROWS["y_near"] + ESP_ROWS["row_spacing"] + 2.5)


def keepout_clear(bb):
    kx0, ky0, kx1, ky1 = KEEP
    return not (bb[0] < kx1 and kx0 < bb[2] and bb[1] < ky1 and ky0 < bb[3])


def esp_pad(n):
    """Absolute pad centre for ESP32 socket pad number n (1..15 near row,
    16..30 far row) given the layout above."""
    i = (n - 1) % 15
    if n <= 15:
        return (ESP_ROWS["x_start"] + i * ESP_ROWS["pitch"], ESP_ROWS["y_near"])
    return (ESP_ROWS["x_start"] + i * ESP_ROWS["pitch"],
            ESP_ROWS["y_near"] + ESP_ROWS["row_spacing"])


# ----------------------------------------------------------------- nets ----
NETS = [
    "GND",              # 1
    "IGN_12",           # 2 switched +12 from the vehicle
    "DC_12",            # 3 fused +12 rail (barrel input, always hot)
    "VREG_IN",          # 4 input side of the LM2596
    "V5",               # 5 LM2596 output -> ESP32 VIN and 5 V rail
    "BAT_SNS",          # 6 battery divider tap -> ESP32 GPIO34 (SENSOR_VP)
    "COOL_SNS",         # 7 coolant NTC tap -> ESP32 GPIO35 (SENSOR_VN)
    "LIGHT_SNS",        # 8 light divider tap -> ESP32 GPIO36 (VP pad)
    "FUEL_SNS",         # 9 tank sender tap -> ESP32 GPIO32
    "IGN_SENSE",        # 10 optocoupler output -> ESP32 GPIO4
    "GPS_RX",           # 11 to GPS module RX (ESP32 TX2 / GPIO17)
    "GPS_TX",           # 12 from GPS module TX (ESP32 RX2 / GPIO16)
    "HALL_A",           # 13 CD4027 latch A -> ESP32 GPIO33
    "HALL_B",           # 14 CD4027 latch B -> ESP32 GPIO26
    "HALL_RAW",         # 15 hall sensor supply / opto anode node (switched VBB)
    "TRIP_BTN",         # 16 trip button -> ESP32 GPIO25
    "TFT_CS",           # 17
    "TFT_DC",           # 18
    "TFT_SCK",          # 19
    "TFT_MOSI",         # 20
    "TFT_RST",          # 21
    "TFT_BL",           # 22 backlight (LOW during reset)
    "ESP_VIN",          # 23 ESP32 5V pin
    "SWDIO",            # 24 debug header
    "SWCLK",            # 25
    "EN",               # 26 ESP32 EN (RC/reset header)
    "GPIO0",            # 27 ESP32 boot strap
    "SMART_TX",         # 28 SMART OBD port
    "SMART_RX",         # 29
    "SCL",              # 30 spare I2C / soft-I2C pads
    "SDA",              # 31
    "SPARE_D2",         # 32 spare GPIO pads
    "SPARE_D15",        # 33
    "SPARE_D1",         # 34 UART0 TX pad
    "SPARE_D3",         # 35 UART0 RX pad
    "SPARE_D22",        # 36
    "SPARE_D19",        # 37
    "SPARE_D13",        # 38
    "V3V3",             # 39 DNP AMS1117 output
    "NC",               # 40
    "IGN_DIV",            # 35 optocoupler input node
    "BATT_IN",            # 36 battery sense input (from fuse)
    "FUEL_IN",            # 37 tank sender signal
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


# round component bodies (electrolytics, TO-220 tab) that KiCad courtyards miss
BODY = {"C1": 8.5, "C3": 10.5}

# ---------------------------------------------------------- placement ------
# ref -> (x, y, rot).  Region map for the 85 x 55 mm carrier:
#   left column  x 10..20  : power in, MOSFET, hall/GPS/trip connectors
#   top strip    y 11..18  : analog dividers, debug + spare headers
#   ESP area     y 20..47 x 20..60 : ESP board under the carrier; parts on top
#   antenna area x 58..70 y 18..48 : copper keep-out, nothing placed
#   right column x 71..95  : LM2596 module + SMART header
#   bottom strip y 50..60  : input filter, opto, bulk caps, test points
PLACE = {
    "J10a": (22.0, 20.0, 90.0), "J10b": (22.0, 45.4, 90.0),
    "J3":   (25.0, 61.5, 90.0),
    "J12":  (15.8, 58.5, 180.0), "D1": (25.8, 50.5, 0.0), "C1": (34.0, 56.2, 0.0),
    "C2":   (40.0, 50.5, 0.0),   "C3": (76.0, 59.5, 0.0), "C4": (68.0, 50.5, 0.0),
    "U4":   (82.0, 32.2, 90.0),
    "OK1":  (58.0, 55.0, 0.0),   "R17": (54.0, 52.0, 0.0), "R16": (54.0, 55.5, 0.0),
    "R18":  (47.5, 52.0, 0.0),
    "R38":  (24.0, 13.0, 90.0),  "R37": (27.0, 13.0, 90.0), "C14": (30.0, 13.0, 90.0),
    "R42":  (33.0, 13.0, 90.0),  "R46": (36.0, 13.0, 90.0), "R43": (39.0, 13.0, 90.0),
    "R41":  (42.0, 13.0, 90.0),  "R44": (45.0, 13.0, 90.0), "C15": (48.0, 13.0, 90.0),
    "J5":   (51.0, 16.5, 90.0),  "J6": (55.5, 16.5, 90.0),  "J7": (60.0, 16.5, 90.0),
    "Q1":   (14.0, 31.0, 0.0),   "R_HG": (21.5, 29.0, 0.0), "D5": (27.0, 24.0, 0.0),
    "OK2":  (24.0, 30.0, 0.0),   "OK3": (34.0, 30.0, 0.0), "U3": (48.0, 30.0, 0.0),
    "J9":   (12.0, 22.0, 90.0),  "J8": (12.0, 26.0, 90.0),  "J14": (12.0, 36.0, 90.0),
    "J11":  (86.0, 57.0, 0.0),   "J13": (24.0, 16.5, 90.0), "J15": (34.0, 16.5, 90.0),
    "TP1":  (23.0, 48.5, 0.0), "TP2": (25.0, 48.5, 0.0), "TP3": (28.0, 48.5, 0.0),
    "TP4":  (31.0, 48.5, 0.0), "TP5": (34.0, 48.5, 0.0), "TP6": (37.0, 48.5, 0.0),
    "TP7":  (40.0, 48.5, 0.0), "TP8": (43.0, 48.5, 0.0), "TP9": (46.0, 48.5, 0.0),
    "TP10": (49.0, 48.5, 0.0),
    "MH1": (13.0, 13.0, 0.0), "MH2": (66.0, 13.5, 0.0),
    "MH3": (13.0, 62.0, 0.0), "MH4": (92.0, 62.0, 0.0),
}

# ------------------------------------------------------------- parts -------
# (ref, footprint, x, y, rot, {pad: net}, value, layer, dnp)
def parts():
    P = []
    fp = kf.board_footprint

    # ---- ESP32 DevKit socket, B.Cu (board lies flat underneath the carrier)
    near = {str(i): NETMAP_ESP[i] for i in range(1, 16)}
    far = {str(i - 15): NETMAP_ESP[i] for i in range(16, 31)}
    P.append(("J10a", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_near"], 270.0, near, "ESP32-DEVKIT", "B.Cu", 0))
    P.append(("J10b", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
              ESP_ROWS["x_start"], ESP_ROWS["y_near"] + ESP_ROWS["row_spacing"], 270.0,
              far, "ESP32-DEVKIT", "B.Cu", 0))

    # ---- TFT display socket, F.Cu
    tft = {str(i + 1): n for i, n in enumerate(TFT_MAP)}
    P.append(("J3", "Connector_PinSocket_2.54mm:PinSocket_1x14_P2.54mm_Vertical",
              TFT["x_start"], TFT["y"], 270.0, tft, "TFT-14", "F.Cu", 0))

    # ---- power input
    P.append(("J12", "Connector_BarrelJack:BarrelJack_CUI_PJ-063AH_Horizontal",
              14.0, 62.0, 180.0, {"1": N("DC_12"), "2": N("GND")}, "DC-IN", "F.Cu", 0))
    P.append(("D1", "Diode_SMD:D_SMB_Handsoldering", 24.0, 55.0, 0.0,
              {"1": N("GND"), "2": N("DC_12")}, "SMBJ36A", "F.Cu", 0))
    P.append(("C1", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm", 30.0, 60.0, 0.0,
              {"1": N("DC_12"), "2": N("GND")}, "100u/50V", "F.Cu", 0))
    P.append(("C2", "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
              27.5, 55.0, 0.0, {"1": N("DC_12"), "2": N("GND")}, "100n", "F.Cu", 0))
    P.append(("C3", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", 84.0, 30.0, 0.0,
              {"1": N("V5"), "2": N("GND")}, "220u", "F.Cu", 0))
    P.append(("C4", "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
              74.5, 24.0, 0.0, {"1": N("V5"), "2": N("GND")}, "100n", "F.Cu", 0))
    # LM2596 module: four solder pins in one row (OUT- OUT+ IN+ IN-), custom fp
    lmp = LM["pitch"]
    lmn = ["GND", "V5", "VREG_IN", "GND"]
    pads = [("%d" % (i + 1), (i - 1.5) * lmp, 8.0, 2.6, 2.6, 1.1,
             (N(lmn[i]), lmn[i])) for i in range(4)]
    P.append(("U4", None, LM["x"], LM["y"], LM["rot"], pads, "LM2596-MOD", "F.Cu", 0,
              [(-22.0, -10.5), (22.0, -10.5), (22.0, 10.5), (-22.0, 10.5),
               (-22.0, -10.5)]))

    # ---- ignition sense optocoupler
    P.append(("OK1", "Package_DIP:DIP-4_W7.62mm", 62.0, 56.0, 0.0,
              {"1": N("IGN_12"), "2": N("IGN_DIV"), "3": N("GND"), "4": N("IGN_SENSE")},
              "PC817", "F.Cu", 0))
    P.append(("R17", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              55.0, 56.0, 0.0, {"1": N("IGN_12"), "2": N("IGN_DIV")}, "1k", "F.Cu", 0))
    P.append(("R16", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              55.0, 59.0, 90.0, {"1": N("IGN_DIV"), "2": N("GND")}, "100k", "F.Cu", 0))
    P.append(("R18", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              68.0, 56.0, 0.0, {"1": N("V5"), "2": N("IGN_SENSE")}, "10k", "F.Cu", 0))

    # ---- analog front ends
    P.append(("R38", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              40.0, 14.0, 0.0, {"1": N("BATT_IN"), "2": N("BAT_SNS")}, "47k", "F.Cu", 0))
    P.append(("R37", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              44.0, 14.0, 90.0, {"1": N("BAT_SNS"), "2": N("GND")}, "10k", "F.Cu", 0))
    P.append(("C14", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              47.5, 14.0, 0.0, {"1": N("BAT_SNS"), "2": N("GND")}, "100n", "F.Cu", 0))
    P.append(("R42", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              52.0, 14.0, 0.0, {"1": N("DC_12"), "2": N("LIGHT_SNS")}, "47k", "F.Cu", 0))
    P.append(("R46", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              56.0, 14.0, 90.0, {"1": N("LIGHT_SNS"), "2": N("GND")}, "10k", "F.Cu", 0))
    P.append(("R43", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              60.0, 14.0, 0.0, {"1": N("V5"), "2": N("COOL_SNS")}, "1k", "F.Cu", 0))
    P.append(("R41", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              34.0, 14.0, 0.0, {"1": N("FUEL_IN"), "2": N("FUEL_SNS")}, "1k", "F.Cu", 0))
    P.append(("R44", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              30.0, 14.0, 90.0, {"1": N("FUEL_SNS"), "2": N("GND")}, "3k3", "F.Cu", 0))
    P.append(("C15", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              26.0, 14.0, 0.0, {"1": N("FUEL_SNS"), "2": N("GND")}, "100n", "F.Cu", 0))

    # ---- hall sensor supply + latch
    P.append(("Q1", "Package_TO_SOT_THT:TO-220-3_Vertical", 15.0, 30.0, 0.0,
              {"1": N("HALL_A"), "2": N("GND"), "3": N("HALL_RAW")}, "IRF9540N", "F.Cu", 0))
    P.append(("R_HG", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
              21.0, 30.0, 0.0, {"1": N("GND"), "2": N("HALL_A")}, "100k", "F.Cu", 0))
    P.append(("D5", "Diode_SMD:D_SMA_Handsoldering", 21.0, 34.0, 0.0,
              {"1": N("HALL_RAW"), "2": N("IGN_12")}, "SB0403", "F.Cu", 0))
    P.append(("OK2", "Package_DIP:DIP-4_W7.62mm", 30.0, 44.0, 0.0,
              {"1": N("HALL_RAW"), "2": N("GND"), "3": N("HALL_A"), "4": N("V5")},
              "PC817", "F.Cu", 0))
    P.append(("OK3", "Package_DIP:DIP-4_W7.62mm", 40.0, 44.0, 0.0,
              {"1": N("HALL_RAW"), "2": N("GND"), "3": N("HALL_B"), "4": N("V5")},
              "PC817", "F.Cu", 0))
    P.append(("U3", "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm", 50.0, 44.0, 0.0,
              {"1": N("HALL_A"), "5": N("HALL_B"), "4": N("GND"), "7": N("V5")},
              "CD4027", "F.Cu", 0))

    # ---- external connectors
    P.append(("J5", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
              64.0, 50.0, 0.0, {"1": N("COOL_SNS"), "2": N("GND")}, "COOLANT", "F.Cu", 0))
    P.append(("J6", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
              69.0, 50.0, 0.0, {"1": N("LIGHT_SNS"), "2": N("GND")}, "LIGHT", "F.Cu", 0))
    P.append(("J7", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
              74.0, 50.0, 0.0, {"1": N("FUEL_IN"), "2": N("GND")}, "FUEL", "F.Cu", 0))
    P.append(("J9", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
              14.0, 40.0, 0.0,
              {"1": N("IGN_12"), "2": N("GND"), "3": N("HALL_RAW")}, "HALL", "F.Cu", 0))
    P.append(("J8", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
              14.0, 22.0, 0.0,
              {"1": N("V5"), "2": N("GND"), "3": N("GPS_RX"), "4": N("GPS_TX")},
              "GPS", "F.Cu", 0))
    P.append(("J11", "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical",
              88.0, 46.0, 90.0,
              {"1": N("GND"), "2": N("V5"), "3": N("SMART_TX"), "4": N("SMART_RX"),
               "5": N("IGN_12"), "6": N("SPARE_D15")}, "SMART", "F.Cu", 0))
    P.append(("J13", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
              24.0, 18.0, 0.0,
              {"1": N("GND"), "2": N("SWDIO"), "3": N("SWCLK"), "4": N("V5")},
              "DEBUG", "F.Cu", 0))
    P.append(("J14", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
              19.0, 26.0, 0.0, {"1": N("TRIP_BTN"), "2": N("GND")}, "TRIP", "F.Cu", 0))

    # ---- spare / strap pads
    P.append(("J15", "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical",
              91.0, 14.0, 90.0,
              {"1": N("SCL"), "2": N("SDA"), "3": N("SPARE_D2"), "4": N("GPIO0"),
               "5": N("GND")}, "SPARE", "F.Cu", 0))

    # ---- test points
    tps = [("TP1", "IGN_SENSE"), ("TP2", "HALL_A"), ("TP3", "HALL_B"),
           ("TP4", "BAT_SNS"), ("TP5", "COOL_SNS"), ("TP6", "LIGHT_SNS"),
           ("TP7", "FUEL_SNS"), ("TP8", "V5"), ("TP9", "TFT_BL"),
           ("TP10", "EN")]
    for i, (ref, net) in enumerate(tps):
        P.append((ref, "TestPoint:TestPoint_Pad_D1.5mm",
                  78.0 + (i % 5) * 3.0, 61.0 + (i // 5) * 3.0, 0.0,
                  {"1": N(net)}, net, "F.Cu", 0))

    # ---- mounting holes
    for i, (x, y) in enumerate([(13.0, 13.0), (92.0, 13.0), (13.0, 62.0), (92.0, 62.0)]):
        P.append(("MH%d" % (i + 1), "MountingHole:MountingHole_3.2mm_M3", x, y, 0.0,
                  {}, "M3", "F.Cu", 0))
    return P


# ESP32 DevKit V1 (30-pin) header position -> net.
# positions 1..15 = near row (y_near), 16..30 = far row, counted from x_start.
NETMAP_ESP = {
    # near row = the row whose silkscreen reads 3V3/EN/IO36 ... (left column)
    1: "EN", 2: "LIGHT_SNS", 3: "NC", 4: "BAT_SNS", 5: "COOL_SNS",
    6: "FUEL_SNS", 7: "HALL_A", 8: "TRIP_BTN", 9: "HALL_B", 10: "TFT_DC",
    11: "TFT_RST", 12: "TFT_BL", 13: "GND", 14: "SPARE_D13", 15: "NC",
    # far row = GND/IO23/IO22/... (right column)
    16: "GND", 17: "TFT_MOSI", 18: "SPARE_D22", 19: "SPARE_D1", 20: "SPARE_D3",
    21: "NC", 22: "GND", 23: "SPARE_D19", 24: "TFT_SCK", 25: "NC",
    26: "GPS_TX", 27: "GPS_RX", 28: "IGN_SENSE", 29: "GPIO0", 30: "SPARE_D2",
}

# silkscreen label printed next to each ESP socket pad (verify against the board!)
ESP_LABEL = {
    1: "EN", 2: "IO36", 3: "IO39", 4: "IO34", 5: "IO35", 6: "IO32", 7: "IO33",
    8: "IO25", 9: "IO26", 10: "IO27", 11: "IO14", 12: "IO12", 13: "GND",
    14: "IO13", 15: "IO9",
    16: "GND", 17: "IO23", 18: "IO22", 19: "IO1", 20: "IO3", 21: "IO21",
    22: "GND", 23: "IO19", 24: "IO18", 25: "IO5", 26: "IO17", 27: "IO16",
    28: "IO4", 29: "IO0", 30: "IO2",
}

# TFT 1x14 header, pin 1..14 in order of the printed silkscreen:
TFT_MAP = ["V5", "GND", "TFT_CS", "TFT_RST", "TFT_DC", "TFT_MOSI", "TFT_SCK",
           "TFT_BL", "", "TFT_SCK", "TFT_CS", "TFT_CS", "", ""]


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
        if ref in PLACE:
            x, y, rot = PLACE[ref]
            ent = (ref, fp_id, x, y, rot, netmap, value, layer, dnp) + ent[9:]
        bb = box_of(ent)
        if bb:
            boxes.append((ref, bb))

        if fp_id is None:                      # custom footprint (pads pre-resolved)
            out.append(kf.custom_footprint(ref, value, (x, y), rot, netmap,
                                           layer=layer, outline=ent[9]))
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
            a, b = boxes[i][1], boxes[j][1]
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                print("  OVERLAP %s / %s" % (boxes[i][0], boxes[j][0]))

    # silkscreen labels for every ESP32 socket pad (orientation / mapping check)
    for n, lbl in sorted(ESP_LABEL.items()):
        x, y = esp_pad(n)
        dy = -1.8 if n <= 15 else 1.8
        back = n <= 15 and True
        out.append('(gr_text "%s" (at %.2f %.2f 0) (layer "%s") (uuid "%s")'
                   ' (effects (font (size 0.8 0.8) (thickness 0.12))%s))'
                   % (lbl, x, y + dy, "B.SilkS", kf.uid(),
                      " (justify mirror)" if back else ""))

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

    # silkscreen notes
    for txt, x, y in [("Dashboard++ carrier v0.1", 12.0, 8.0),
                      ("ESP32 side: B.Cu", 12.0, 67.5),
                      ("ANTENNA KEEP-OUT", 58.5, 21.0)]:
        out.append('(gr_text "%s" (at %.2f %.2f) (layer "F.SilkS") (uuid "%s")'
                   ' (effects (font (size 1.2 1.2) (thickness 0.15))))' % (txt, x, y, kf.uid()))

    out.append(")")
    return "\n".join(out) + "\n"


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
