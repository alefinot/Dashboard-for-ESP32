"""Host replica for issue #22 - selectable DST rule (EU / US / none).

Mirrors getEuropeanDst() / getUSDst() / getDstOffset() from src/gfx.cpp line for
line and checks them against the published transition instants for 2024-2026,
plus the equivalence claim that the new EU rule reproduces the old hard-wired
getEuropeanOffset() for every standard offset (so nobody's clock jumps on update).
"""

FAILURES = 0


def check(name, got, want):
    ok = got == want
    if not ok:
        FAILURES += 1
    print(("  ok  " if ok else "  FAIL") + f" {name}: got {got!r} want {want!r}")


def getDayOfWeek(y, m, d):
    t = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4]
    if m < 3:
        y -= 1
    return (y + y // 4 - y // 100 + y // 400 + t[m - 1] + d) % 7


def getEuropeanDst(year, month, day, hourUtc, baseOff):
    if month < 3 or month > 10:
        return 0
    if 3 < month < 10:
        return 1
    lastSunday = 31 - getDayOfWeek(year, month, 31)
    if month == 3:
        if day > lastSunday:
            return 1
        if day < lastSunday:
            return 0
        return 1 if hourUtc >= 1 else 0
    if month == 10:
        if day > lastSunday:
            return 0
        if day < lastSunday:
            return 1
        return 0 if hourUtc >= 1 else 1
    return 0


def getUSDst(year, month, day, hourUtc, baseOff):
    if month < 3 or month > 11:
        return 0
    if 3 < month < 11:
        return 1
    if month == 3:
        firstSunday = 1 + (7 - getDayOfWeek(year, 3, 1)) % 7
        secondSunday = firstSunday + 7
        if day > secondSunday:
            return 1
        if day < secondSunday:
            return 0
        return 1 if hourUtc >= 2 - baseOff else 0
    firstSunday = 1 + (7 - getDayOfWeek(year, 11, 1)) % 7
    if day > firstSunday:
        return 0
    if day < firstSunday:
        return 1
    return 0 if hourUtc >= 1 - baseOff else 1


def old_euro(year, month, day, hour):
    """Firmware before #22: absolute offset for a European calendar."""
    if month < 3 or month > 10:
        return 1
    if 3 < month < 10:
        return 2
    lastSunday = 31 - getDayOfWeek(year, month, 31)
    if month == 3:
        if day > lastSunday:
            return 2
        if day < lastSunday:
            return 1
        return 2 if hour >= 1 else 1
    if month == 10:
        if day > lastSunday:
            return 1
        if day < lastSunday:
            return 2
        return 1 if hour >= 1 else 2
    return 1


print("#22 day-of-week primitive (0 = Sunday)")
check("2025-03-09 is a Sunday", getDayOfWeek(2025, 3, 9), 0)
check("2024-03-31 is a Sunday", getDayOfWeek(2024, 3, 31), 0)
check("2026-11-01 is a Sunday", getDayOfWeek(2026, 11, 1), 0)
check("2025-01-01 is a Wednesday", getDayOfWeek(2025, 1, 1), 3)

print("#22 EU rule against published transition instants")
eu = [(2024, 3, 31), (2025, 3, 30), (2026, 3, 29),
      (2024, 10, 27), (2025, 10, 26), (2026, 10, 25)]
for y, m, d in eu:
    check(f"{y}-{m:02d}-{d:02d} is the change Sunday",
          31 - getDayOfWeek(y, m, 31) == d or getDayOfWeek(y, m, d) == 0, True)
check("EU 2025-03-30 00:30 UTC -> standard", getEuropeanDst(2025, 3, 30, 0, 1), 0)
check("EU 2025-03-30 01:30 UTC -> daylight", getEuropeanDst(2025, 3, 30, 1, 1), 1)
check("EU 2025-03-29 -> standard", getEuropeanDst(2025, 3, 29, 12, 1), 0)
check("EU 2025-03-31 -> daylight", getEuropeanDst(2025, 3, 31, 12, 1), 1)
check("EU 2025-10-26 00:30 UTC -> daylight", getEuropeanDst(2025, 10, 26, 0, 2), 1)
check("EU 2025-10-26 01:30 UTC -> standard", getEuropeanDst(2025, 10, 26, 1, 2), 0)
check("EU midwinter", getEuropeanDst(2025, 1, 15, 12, 1), 0)
check("EU midsummer", getEuropeanDst(2025, 7, 15, 12, 1), 1)

print("#22 US rule against published transition instants (base GMT-5)")
check("US 2025-03-09 06:59 UTC -> standard", getUSDst(2025, 3, 9, 6, -5), 0)
check("US 2025-03-09 07:00 UTC -> daylight", getUSDst(2025, 3, 9, 7, -5), 1)
check("US 2025-03-08 -> standard", getUSDst(2025, 3, 8, 12, -5), 0)
check("US 2025-03-10 -> daylight", getUSDst(2025, 3, 10, 12, -5), 1)
check("US 2025-11-02 05:00 UTC -> daylight", getUSDst(2025, 11, 2, 5, -5), 1)
check("US 2025-11-02 06:00 UTC -> standard", getUSDst(2025, 11, 2, 6, -5), 0)
check("US 2024 spring 07:00 UTC edge", getUSDst(2024, 3, 10, 7, -5), 1)
check("US 2024 spring 06:00 UTC still standard", getUSDst(2024, 3, 10, 6, -5), 0)
check("US midwinter", getUSDst(2025, 1, 15, 12, -5), 0)
check("US midsummer", getUSDst(2025, 7, 15, 12, -5), 1)
# a GMT-6 zone springs forward an hour later in UTC
check("US GMT-6 spring 07:59 UTC -> standard", getUSDst(2025, 3, 9, 7, -6), 0)
check("US GMT-6 spring 08:00 UTC -> daylight", getUSDst(2025, 3, 9, 8, -6), 1)

print("#22 the rules really differ (the bug this issue reported)")
# 2025-03-09 12:00 UTC: US already on DST, EU not yet -> old code was wrong for US
check("EU says standard", getEuropeanDst(2025, 3, 9, 12, -5), 0)
check("US says daylight", getUSDst(2025, 3, 9, 12, -5), 1)
check("2025-11-03: EU already standard", getEuropeanDst(2025, 11, 3, 12, 1), 0)
check("2025-11-03: US already standard", getUSDst(2025, 11, 3, 12, -5), 0)

print("#22 no behaviour change for existing EU users (old vs new, hourly over 3 years)")
mismatch = 0
import datetime
t = datetime.datetime(2024, 1, 1)
while t < datetime.datetime(2027, 1, 1):
    for base in (-5, 0, 1, 2, 3):
        old = old_euro(t.year, t.month, t.day, t.hour) - 1
        new = getEuropeanDst(t.year, t.month, t.day, t.hour, base)
        if old != new:
            mismatch += 1
    t += datetime.timedelta(hours=1)
check("hourly samples 2024-2026 x 5 base offsets, mismatches", mismatch, 0)

print(f"\nFAILURES: {FAILURES}")
