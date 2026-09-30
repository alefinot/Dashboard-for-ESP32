#!/usr/bin/env python3
"""Host replica of the marine resistive fuel sender chain (issue #18).

Mirrors the arithmetic in src/sensors.cpp (fuelOhmsFromCode /
fuelLevelFromOhms / demoCodeForFuelLevel), src/config.cpp
(fillFuelTableFromOhms) and the Web UI excitation-resistor suggestion
(fuelSuggestExciter), then checks the properties the feature depends on:

  * code -> ohms -> code round-trips, for both sender conventions
  * the calibration table is in ohms, so changing the excitation resistor
    never changes what a captured table means
  * every tank level maps to a distinct ADC code (resolution check)
  * demo mode is read back through the production pipeline unchanged
  * open / shorted sender is detected and never invents a level
  * the excitation suggestion honours the sender current budget

Run:  python scripts/verify_fuel_ohms.py
Exit: 0 = all checks pass, 1 = at least one failure.
"""

import math
import sys

FAILURES = 0


def check(name, got, want, tol=1e-4):
    global FAILURES
    ok = (got == want) if isinstance(want, (bool, str)) else abs(got - want) <= tol
    if not ok:
        FAILURES += 1
        print(f"FAIL {name}: got {got!r} want {want!r}")


def ok(name, cond, detail=""):
    global FAILURES
    if not cond:
        FAILURES += 1
        print(f"FAIL {name}: {detail}")


# ---------------------------------------------------------------------------
# firmware mirrors
# ---------------------------------------------------------------------------

CODE_MAX = 4095.0


def code_from_ohms(ohms, rexc, vref=3.3):
    """ADC code the divider 3V3 - R_exc - pin - sender - GND produces."""
    return CODE_MAX * ohms / (ohms + rexc)


def ohms_from_code(code, rexc, vref=3.3):
    """fuelOhmsFromCode(): R = R_exc x V / (Vref - V)."""
    v = code * vref / CODE_MAX
    headroom = vref - v
    if headroom <= 0.001:
        return 1e9
    return rexc * v / headroom


def level_from_ohms(table, ohms):
    """fuelLevelFromOhms(): fractional tank level, direction from the ends."""
    n = len(table)
    first, last = table[0], table[n - 1]
    descending = first >= last
    if ohms >= first if descending else ohms <= first:
        return 0.0
    if ohms <= last if descending else ohms >= last:
        return float(n - 1)
    for i in range(n - 1):
        lo, hi = min(table[i], table[i + 1]), max(table[i], table[i + 1])
        if ohms < lo or ohms > hi:
            continue
        span = table[i + 1] - table[i]
        return i + ((ohms - table[i]) / span if span else 0.0)
    # unreachable in the monotonic cases below
    return 0.0 if abs(ohms - first) <= abs(ohms - last) else float(n - 1)


def fill_table(points, ohm_empty, ohm_full):
    """fillFuelTableFromOhms(): linear ohm ramp across the tank slots."""
    return [ohm_empty + (ohm_full - ohm_empty) * i / (points - 1)
            for i in range(points)]


def demo_code(level, table, rexc):
    """demoCodeForFuelLevel(): level -> ohm (same table) -> divider code."""
    n = len(table)
    level = max(0.0, min(level, float(n - 1)))
    i = int(level)
    if i >= n - 1:
        i = n - 2
    frac = level - i
    ohms = table[i] + frac * (table[i + 1] - table[i])
    return round(code_from_ohms(ohms, rexc))


FUEL_INPUT_OFF, FUEL_INPUT_OK, FUEL_INPUT_OPEN, FUEL_INPUT_SHORT = 0, 1, 2, 3


def input_state(code, vref, ohm_empty, ohm_full, rexc):
    """processFuelSensor() fault detection band."""
    lohm, hohm = min(ohm_empty, ohm_full), max(ohm_empty, ohm_full)
    v = code * vref / CODE_MAX
    ohms = ohms_from_code(code, rexc, vref)
    if v >= vref * 0.995:
        return FUEL_INPUT_OPEN
    if ohms < lohm - max(5.0, lohm * 0.25):
        return FUEL_INPUT_SHORT
    if ohms > hohm * 3.0:
        return FUEL_INPUT_OPEN
    return FUEL_INPUT_OK


# ---------------------------------------------------------------------------
# Web UI mirror: excitation resistor suggestion
# ---------------------------------------------------------------------------

E12 = [10, 12, 15, 18, 22, 27, 33, 39, 47, 56, 68, 82, 100, 120, 150, 180, 220,
       270, 330, 390, 470, 560, 680, 820, 1000, 1200, 1500, 1800, 2200, 2700,
       3300, 3900, 4700, 5600, 6800, 8200, 10000]
SENDER_MAX_MA = 15.0


def divider_info(rexc, ohm_empty, ohm_full, vref=3.3):
    lo, hi = min(ohm_empty, ohm_full), max(ohm_empty, ohm_full)
    v_lo = vref * lo / (lo + rexc)
    v_hi = vref * hi / (hi + rexc)
    return {"spanV": abs(v_hi - v_lo), "topV": max(v_lo, v_hi),
            "mA": vref * 1000.0 / (rexc + lo)}


def suggest_exciter(ohm_empty, ohm_full, vref=3.3):
    best = None
    for r in E12:
        info = divider_info(r, ohm_empty, ohm_full, vref)
        if info["mA"] > SENDER_MAX_MA:
            continue
        if info["topV"] > 2.6:
            continue
        if best is None or info["spanV"] > best[1]["spanV"]:
            best = (r, info)
    return best


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------

def main():
    # 1. divider round-trip, SAE 10..180 ohm behind the shipped 220 ohm default
    rexc, vref = 220.0, 3.3
    for ohms in (10.0, 47.0, 90.0, 180.0, 240.0):
        code = round(code_from_ohms(ohms, rexc, vref))
        # one code of quantisation is the floor here: dR/dcode grows with R, so
        # the tolerance scales with the resistance being recovered
        check(f"round-trip {ohms:.0f} ohm -> code {code} -> ohm",
              ohms_from_code(code, rexc, vref), ohms, tol=ohms * 0.001)

    # 2. the table is in ohms, so it survives an excitation-resistor change:
    #    the same physical tank level has to land on the same level no matter
    #    which resistor is fitted.
    table = fill_table(8, 10.0, 180.0)
    for rexc_a, rexc_b in ((220.0, 47.0), (220.0, 1000.0), (47.0, 330.0)):
        worst = 0.0
        for level in (0.0, 1.5, 3.5, 5.9, 7.0):
            ca = demo_code(level, table, rexc_a)
            cb = demo_code(level, table, rexc_b)
            la = level_from_ohms(table, ohms_from_code(ca, rexc_a, vref))
            lb = level_from_ohms(table, ohms_from_code(cb, rexc_b, vref))
            worst = max(worst, abs(la - level), abs(lb - level))
        ok(f"table independent of exciter ({rexc_a} vs {rexc_b} ohm)",
           worst < 0.02, f"worst level error {worst:.3f} slot")

    # 3. European / VDO convention: 240 ohm empty -> 33 ohm full (falling)
    eu_table = fill_table(8, 240.0, 33.0)
    empty = level_from_ohms(eu_table, 240.0)
    full = level_from_ohms(eu_table, 33.0)
    check("EU table: 240 ohm reads empty", empty, 0.0)
    check("EU table: 33 ohm reads full", full, 7.0)
    mid = level_from_ohms(eu_table, ohms_from_code(
        round(code_from_ohms(136.5, 100.0, vref)), 100.0, vref))
    check("EU table: midpoint reads mid-tank", mid, 3.5, tol=0.01)

    # 4. GM convention: 0..90 ohm. A genuine 0 ohm must read empty, not "short".
    gm_table = fill_table(8, 0.0, 90.0)
    code0 = round(code_from_ohms(0.0, 47.0, vref))
    check("GM sender: 0 ohm still reads empty",
          level_from_ohms(gm_table, ohms_from_code(code0, 47.0, vref)), 0.0)
    check("GM sender: 0 ohm not reported as a fault",
          input_state(code0, vref, 0.0, 90.0, 47.0), FUEL_INPUT_OK)

    # 5. resolution: every tank level must land on a different code, otherwise
    #    the gauge cannot tell two slots apart.
    for label, (e, f), ex in (("SAE 10-180 @220", (10.0, 180.0), 220.0),
                              ("SAE 10-180 @47", (10.0, 180.0), 47.0),
                              ("EU 240-33 @330", (240.0, 33.0), 330.0),
                              ("GM 0-90 @47", (0.0, 90.0), 47.0)):
        t = fill_table(8, e, f)
        codes = [demo_code(lv, t, ex) for lv in range(8)]
        distinct = len(set(codes))
        ok(f"resolution {label}: 8 slots use distinct codes",
           distinct == 8, f"codes={codes}")
        span = max(codes) - min(codes)
        ok(f"resolution {label}: span over 200 codes",
           span > 200, f"span={span}")

    # 6. the shipped excitation default keeps the sender current in budget
    info = divider_info(220.0, 10.0, 180.0)
    ok("default 220 ohm exciter within 15 mA sender budget",
       info["mA"] <= SENDER_MAX_MA, f"{info['mA']:.1f} mA")
    sug = suggest_exciter(10.0, 180.0)
    # With the 15 mA sender-current budget the shipped 220 ohm default is the
    # widest-span E12 value for a 10-180 ohm sender, so a stock unit is already
    # optimal. (Without the budget a ~47 ohm exciter would spread the range
    # further but push ~50 mA through the sender element.)
    check("suggested exciter for 10-180 ohm sender", sug[0], 220)
    ok("suggested exciter within current budget",
       sug[1]["mA"] <= SENDER_MAX_MA, f"{sug[1]['mA']:.1f} mA")
    low = suggest_exciter(0.0, 90.0)
    # A 0-90 ohm GM sender can short the rail, so the same 15 mA budget pins the
    # exciter at 220 ohm and the usable span is inherently narrower.
    check("GM 0-90 ohm sender suggestion is current-limited", low[0], 220)
    ok("GM 0-90 ohm span is narrower than the SAE range",
       low[1]["spanV"] < info["spanV"], f"{low[1]['spanV']:.3f} V")
    # and it must be the best E12 choice, not just some choice
    for r in E12:
        i = divider_info(r, 10.0, 180.0)
        if i["mA"] <= SENDER_MAX_MA and i["topV"] <= 2.6:
            ok(f"no E12 {r} ohm beats the suggestion",
               i["spanV"] <= sug[1]["spanV"] + 1e-9, f"{r} ohm span {i['spanV']}")

    # 7. fault detection: an open sender pin sits at the rail and must never be
    #    turned into a tank level
    for code in (4095, 4090, 4080):
        check(f"open circuit at code {code}",
              input_state(code, vref, 10.0, 180.0, 220.0), FUEL_INPUT_OPEN)
    check("open circuit pins at the rail => ohms saturate",
          ohms_from_code(4095, 220.0, vref), 1e9)
    check("shorted input detected for a 10-180 ohm sender",
          input_state(2, vref, 10.0, 180.0, 220.0), FUEL_INPUT_SHORT)
    check("just below the entered range is not a fault",
          input_state(round(code_from_ohms(9.0, 220.0, vref)), vref,
                      10.0, 180.0, 220.0), FUEL_INPUT_OK)
    check("reading far above the entered range => open",
          input_state(round(code_from_ohms(900.0, 220.0, vref)), vref,
                      10.0, 180.0, 220.0), FUEL_INPUT_OPEN)
    check("in-range reading is OK",
          input_state(round(code_from_ohms(90.0, 220.0, vref)), vref,
                      10.0, 180.0, 220.0), FUEL_INPUT_OK)

    # 8. demo mode exercises the production path: level -> code -> ohms -> level
    for (e, f) in ((10.0, 180.0), (240.0, 33.0), (0.0, 90.0)):
        t = fill_table(8, e, f)
        worst = 0.0
        for i in range(71):          # 0.1 .. 7.0 slots
            want = i / 10.0
            code = demo_code(want, t, 220.0)
            got = level_from_ohms(t, ohms_from_code(code, 220.0, vref))
            worst = max(worst, abs(got - want))
        ok(f"demo round-trip {e:.0f}-{f:.0f} ohm",
           worst < 0.02, f"worst {worst:.3f} slot")

    # 9. changing the point count regenerates a usable ramp (no stale slots)
    for points in (2, 5, 8, 20):
        t = fill_table(points, 10.0, 180.0)
        check(f"regenerated {points}-point ramp: empty",
              level_from_ohms(t, 10.0), 0.0)
        check(f"regenerated {points}-point ramp: full",
              level_from_ohms(t, 180.0), float(points - 1))
        # every interior slot must be reachable from its own resistance
        for i in range(1, points - 1):
            check(f"{points}-point slot {i}",
                  level_from_ohms(t, t[i]), float(i), tol=1e-9)

    # 10. oversampling: averaging N conversions must not shift the derived ohms
    #     (a linear average of a linear quantity), so it is safe as a pure
    #     noise filter in the code domain.
    rexc = 220.0
    center = code_from_ohms(90.0, rexc, vref)
    noisy = [center + d for d in (-30, 30, -12, 12, 0)]
    avg = sum(noisy) / len(noisy)
    check("averaging codes keeps the ohm value",
          ohms_from_code(avg, rexc, vref), 90.0, tol=0.05)

    print(f"\nFAILURES: {FAILURES}")
    if FAILURES:
        return 1
    print("ok   divider math, ohm-domain table, both sender conventions, "
          "demo round-trip, fault detection, exciter suggestion")
    return 0


if __name__ == "__main__":
    sys.exit(main())
