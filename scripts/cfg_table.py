#!/usr/bin/env python3
"""Host-side reader for the CFG_TABLE rows in src/config.cpp.

The parameters used to be declared with CFG_INT()/CFG_UINT()/... macros; they are
now one row each in a single table (global, JSON key, NVS key, default, band,
kind). Every host script that needs the bands and defaults - the WebUI limit
generator and the offline range checker - reads them through here, so there is
one parser to keep in step with the table.

Row layout (src/config.cpp, struct CfgParam):
    { &GLOBAL, "JsonKey", "NvsKey", defStr, lo, hi, dInt, fLo, fHi, fDef,
      size, CK_KIND },
"""
import re

CFG = "src/config.cpp"

# Numbers and constexpr names (SPI_SPEED_MIN_HZ, 60000000, 1.5f, -40.0f).
SYM = r"[-+0-9.eExXfFA-Z_]+"

ROW = re.compile(
    r"(?m)^\s*\{\s*&([A-Z0-9_]+)\s*,\s*\"([^\"]+)\"\s*,\s*\"([^\"]*)\"\s*,\s*"
    r"(?:nullptr|\"((?:[^\"\\]|\\.)*)\"|[A-Z0-9_]+)\s*,\s*(" + SYM + r")\s*,\s*(" + SYM +
    r")\s*,\s*(" + SYM + r")\s*,\s*(" + SYM + r")\s*,\s*(" + SYM + r")\s*,\s*"
    r"(" + SYM + r")\s*,\s*[^,]+?\s*,\s*(CK_[A-Z]+)\s*\},")

CONSTEXPR = re.compile(r"constexpr\s+\w+\s+([A-Z0-9_]+)\s*=\s*([0-9.eExX+]+)UL?;")

NUMERIC_KINDS = ("CK_INT", "CK_UINT", "CK_FLT")


def config_text():
    return open(CFG, encoding="utf-8").read()


def constants(files=(CFG, "src/dashboard.h")):
    """constexpr numbers the bands and defaults can reference."""
    out = {}
    for f in files:
        for m in CONSTEXPR.finditer(open(f, encoding="utf-8").read()):
            out[m.group(1)] = float(m.group(2))
    return out


def resolve(expr, consts):
    """Numeric literal, or the name of a constexpr from config.cpp/dashboard.h."""
    try:
        return float(expr.rstrip("f"))
    except ValueError:
        return consts[expr]


def all_rows(text=None):
    """Every CFG_TABLE row, in table order.

    Fails loudly rather than quietly dropping a row: an unparsed parameter would
    silently lose its band in the WebUI mirror and in the range check.
    """
    text = config_text() if text is None else text
    start = text.index("static const CfgParam CFG_TABLE[] = {")
    end = text.index("\n};", start)
    code = [l for l in text[start:end].splitlines() if l.strip().startswith("{")]
    rows = []
    for line in code:
        m = ROW.match(line)
        if not m:
            continue
        glob, name, nvs, dstr, lo, hi, di, flo, fhi, df, kind = m.groups()
        rows.append(dict(global_=glob, name=name, nvs=nvs, dstr=dstr,
                         lo=lo, hi=hi, di=di, flo=flo, fhi=fhi, df=df,
                         kind=kind))
    if len(rows) != len(code):
        missed = [l.strip() for l in code if not ROW.match(l)]
        raise ValueError(
            "CFG_TABLE: %d of %d rows not parsed by scripts/cfg_table.py "
            "(the row format changed?) - first miss: %s"
            % (len(missed), len(code), missed[0][:120]))
    return rows


def numeric_rows(text=None, consts=None):
    """(name, kind, lo, hi, default) for every banded numeric parameter.

    CK_INT/CK_UINT use lo/hi and the integer default; CK_FLT uses flo/fhi and the
    float default. CK_BOOL, CK_STR and CK_SECRET carry no band and are skipped.
    Raises KeyError(name) if a band or default names a constant not found.
    """
    consts = constants() if consts is None else consts
    out = []
    for r in all_rows(text):
        if r["kind"] not in NUMERIC_KINDS:
            continue
        if r["kind"] == "CK_FLT":
            lo, hi, dflt = r["flo"], r["fhi"], r["df"]
        else:
            lo, hi, dflt = r["lo"], r["hi"], r["di"]
        out.append((r["name"], r["kind"], resolve(lo, consts),
                    resolve(hi, consts), resolve(dflt, consts)))
    return out


def fmt_number(v):
    """1000000.0 -> '1000000', 0.5 -> '0.5' (matches the WebUI table format)."""
    if v == int(v):
        return str(int(v))
    return repr(v)
