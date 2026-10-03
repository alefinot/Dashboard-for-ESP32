#!/usr/bin/env python3
"""One-shot migration helper: processConfig's CFG_* macro calls -> CfgParam table.

`extract` (default) parses the macro call sites out of config.cpp (working tree
or --ref REV) and prints a normalized CSV of every parameter: kind, C++ name,
NVS key, default, band, buffer size. Run it before the migration (over the old
file) and after (over the new file, which parses the generated table instead) and
diff the two outputs: identical rows in identical order == the three modes
(boot load, GET /api/config, save) still address the same global, with the same
NVS key, the same shipped default and the same allowed band.
"""
import argparse, re, subprocess, sys

KINDS = ("INT", "UINT", "FLT", "STR", "BOOL")
DECL = re.compile(
    r"^(?:static\s+)?(int|bool|float|uint32_t|uint16_t|char|long|unsigned)\s+"
    r"([A-Z][A-Z0-9_]*)\s*(\[(\d+)\])?\s*(=|;)", re.M)


def src_of(path, ref):
    if not ref:
        return open(path, encoding="utf-8", newline="").read()
    return subprocess.run(["git", "show", f"{ref}:{path}"], check=True,
                          capture_output=True, text=True).stdout


def func_body(text, fname):
    m = re.search(rf"^void {fname}\(.*$", text, re.M)
    if not m:
        raise SystemExit(f"function {fname} not found")
    depth, out, i = 0, [], m.start()
    for j in range(m.start(), len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[m.start():j + 1]
    raise SystemExit("unbalanced braces")


def split_args(s):
    args, depth, cur, k, q = [], 0, "", 0, None
    while k < len(s):
        c = s[k]
        if q:
            cur += c
            if c == "\\":
                cur += s[k + 1]; k += 2; continue
            if c == q:
                q = None
        elif c in "\"'":
            q = c; cur += c
        elif c in "([{":
            depth += 1; cur += c
        elif c in ")]}":
            depth -= 1; cur += c
        elif c == "," and depth == 0:
            args.append(cur.strip()); cur = ""
        else:
            cur += c
        k += 1
    if cur.strip():
        args.append(cur.strip())
    return args


def strip_comments(s):
    out, k, q = [], 0, None
    while k < len(s):
        c = s[k]
        if q:
            out.append(c)
            if c == "\\":
                out.append(s[k + 1]); k += 2; continue
            if c == q:
                q = None
            k += 1; continue
        if c in "\"'":
            q = c; out.append(c); k += 1; continue
        if s.startswith("//", k):
            while k < len(s) and s[k] != "\n":
                k += 1
            continue
        if s.startswith("/*", k):
            k = s.find("*/", k + 2) + 2
            continue
        out.append(c); k += 1
    return "".join(out)


def norm(s):
    """Collapse indentation/newlines but never whitespace inside a string."""
    out, k, q = [], 0, None
    while k < len(s):
        c = s[k]
        if q:
            out.append(c)
            if c == "\\":
                out.append(s[k + 1]); k += 2; continue
            if c == q:
                q = None
            k += 1; continue
        if c in "\"'":
            q = c; out.append(c); k += 1; continue
        if c.isspace():
            while k < len(s) and s[k].isspace():
                k += 1
            out.append(" ") if out and out[-1] != " " else None
            continue
        out.append(c); k += 1
    return "".join(out).strip()


def parse_macros(text):
    body = strip_comments(func_body(text, "processConfig"))
    rows = []
    for m in re.finditer(r"\bCFG_(%s)\s*\(" % "|".join(KINDS), body):
        kind = m.group(1)
        depth, k = 1, m.end()
        while depth:
            if body[k] == "(":
                depth += 1
            elif body[k] == ")":
                depth -= 1
            k += 1
        args = split_args(body[m.end():k - 1])
        var, key = args[0], args[1]
        if kind in ("INT", "UINT", "FLT"):
            assert len(args) == 5, args
            dflt, lo, hi = args[2], args[3], args[4]
        elif kind == "BOOL":
            assert len(args) == 3, args
            dflt, lo, hi = args[2], "", ""
        else:
            assert len(args) == 3, args
            dflt, lo, hi = args[2], "", ""
        rows.append((kind, var, key.strip('"'), norm(dflt),
                     norm(lo), norm(hi)))
    return rows


def types(text):
    return {name: (t, int(sz) if sz else 0)
            for t, name, _g, sz, _term in DECL.findall(text)}


SECRETS = [  # handled by hand in the old processConfig; CK_SECRET rows
    ("WIFI_PASSWORD", "WIFI_PWD", 64),
    ("WIFI_PASSWORD_1", "WIFI_P1", 64),
    ("WIFI_PASSWORD_2", "WIFI_P2", 64),
    ("WIFI_PASSWORD_3", "WIFI_P3", 64),
    ("WIFI_PASSWORD_4", "WIFI_P4", 64),
]


def emit(text):
    """Print the C++ table equivalent of the CFG_* call list."""
    rows = parse_macros(text)
    out = []
    for kind, var, key, dflt, lo, hi in rows:
        k = {"INT": "CK_INT", "UINT": "CK_UINT", "FLT": "CK_FLT",
             "BOOL": "CK_BOOL", "STR": "CK_STR"}[kind]
        d = {"INT": dflt, "UINT": dflt, "FLT": "0",
             "BOOL": "1" if dflt == "true" else "0", "STR": "0"}[kind]
        df = dflt if kind == "FLT" else "0.0f"
        ds = dflt if kind == "STR" else "nullptr"
        blo = lo if kind in ("INT", "UINT") else "0"
        bhi = hi if kind in ("INT", "UINT") else "0"
        flo = lo if kind == "FLT" else "0.0f"
        fhi = hi if kind == "FLT" else "0.0f"
        sz = f"(uint16_t)sizeof({var})" if kind == "STR" else "0"
        out.append(f"  {{ &{var}, \"{var}\", \"{key}\", {ds}, {blo}, {bhi}, "
                   f"{d}, {flo}, {fhi}, {df}, {sz}, {k} }},")
    for var, key, _sz in SECRETS:
        out.append(f"  {{ &{var}, \"{var}\", \"{key}\", \"\", 0, 0, 0, 0.0f, "
                   f"0.0f, 0.0f, (uint16_t)sizeof({var}), CK_SECRET }},")
    print("\n".join(out))
    print(f"# {len(rows)} + {len(SECRETS)} secret rows", file=sys.stderr)


KIND_BACK = {"CK_INT": "INT", "CK_UINT": "UINT", "CK_FLT": "FLT",
             "CK_BOOL": "BOOL", "CK_STR": "STR", "CK_SECRET": "SECRET"}


def parse_table(text):
    """Read CFG_TABLE back out of the generated file, same columns as the macros."""
    m = re.search(r"static const CfgParam CFG_TABLE\[\] = \{(.*?)\r?\n\};", text, re.S)
    if not m:
        raise SystemExit("CFG_TABLE not found")
    rows = []
    for line in m.group(1).splitlines():
        line = line.strip().rstrip(",")
        if not line:
            continue
        g = split_args(line[1:-1])
        assert len(g) == 12, line
        kind = KIND_BACK[g[11]]
        var = g[0].lstrip("&")
        name, key, dstr = g[1].strip('"'), g[2].strip('"'), g[3]
        lo, hi, di, flo, fhi, df, size = g[4:11]
        if kind == "STR" or kind == "SECRET":
            dflt = "" if kind == "SECRET" else dstr
            blo = bhi = ""
            assert dstr != "null", line
        elif kind == "FLT":
            dflt, blo, bhi = df, flo, fhi
        elif kind == "BOOL":
            dflt, blo, bhi = ("true" if di != "0" else "false"), "", ""
        else:
            dflt, blo, bhi = di, lo, hi
        assert name == var, f"name/var mismatch: {name} vs {var}"
        rows.append((kind, var, key, dflt, blo, bhi, size))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", default="src/config.cpp")
    ap.add_argument("--ref", help="git rev to read the file from")
    ap.add_argument("--emit", action="store_true", help="print the C++ table")
    ap.add_argument("--rows", action="store_true",
                    help="print the CFG_TABLE rows of an already-converted file")
    a = ap.parse_args()
    text = src_of(a.path, a.ref)
    if a.rows:
        for r in parse_table(text):
            print("\t".join(r))
        print(f"# {len(parse_table(text))} table rows", file=sys.stderr)
        return
    if a.emit:
        emit(text)
        return
    rows = parse_macros(text)
    ty = types(text)
    for kind, var, key, dflt, lo, hi in rows:
        d = ty.get(var)
        want = {"INT": "int", "UINT": "uint32_t", "FLT": "float",
                "BOOL": "bool", "STR": "char"}[kind]
        flag = ""
        if d is None:
            flag = "  <-- NO DECLARATION FOUND"
        elif d[0] != want:
            flag = f"  <-- TYPE {d[0]} != {want}"
        size = d[1] if kind == "STR" else ""
        if kind == "STR" and not d[1]:
            flag += "  <-- STR WITHOUT ARRAY SIZE"
        print(f"{kind}\t{var}\t{key}\t{dflt}\t{lo}\t{hi}\t{size}{flag}")
    print(f"# {len(rows)} parameters", file=sys.stderr)
    bad = [r for r in rows if ty.get(r[1]) is None]
    if bad:
        print(f"# UNRESOLVED: {bad}", file=sys.stderr)
        sys.exit(1)


main()
