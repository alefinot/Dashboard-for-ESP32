"""Shared helpers for the Dashboard++ carrier board generator scripts.

Pulls symbol blocks out of the installed KiCad symbol libraries, computes absolute
pin coordinates, and expands stock footprint files into pcbnew board entries with
nets attached.  Written against KiCad 10.0 (sch 20250610 / pcb 20241229 /
.kicad_mod 20260206).
"""
import os
import re
import uuid
import glob

KICAD = os.environ.get("KICAD_ROOT",
                       r"C:/Users/Kurose AE/AppData/Local/Programs/KiCad/10.0")
SYM_DIR = os.path.join(KICAD, "share", "kicad", "symbols")
FP_DIR = os.path.join(KICAD, "share", "kicad", "footprints")


def uid():
    return str(uuid.uuid4())


def _skip_sexp(text, i):
    """Index just past the balanced group that starts at text[i] == '('."""
    depth = 0
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced s-expression")


def _children(text):
    """Split a group's contents into top-level child groups / atoms."""
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif c == "(":
            j = _skip_sexp(text, i)
            out.append(text[i:j])
            i = j
        elif c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in "()":
                j += 1
            out.append(text[i:j])
            i = j
    return out


# ---------------------------------------------------------------- symbols ---
_lib_cache = {}


def _lib_files():
    if not _lib_cache:
        for f in glob.glob(os.path.join(SYM_DIR, "*.kicad_sym")):
            _lib_cache[os.path.basename(f)[:-10]] = open(f, encoding="utf-8").read()
    return _lib_cache


_sym_cache = {}


def get_symbol(lib_id):
    """Return (block_text, {pin_number: (dx, dy)}) for 'Lib:Name'.

    Coordinates come from the unit that carries the pins (usually <Name>_1_1).
    """
    if lib_id in _sym_cache:
        return _sym_cache[lib_id]
    lib, name = lib_id.split(":")
    src = _lib_files()[lib]
    mm = re.search(r'\n\t\(symbol "%s"' % re.escape(name), src)
    if not mm:
        raise KeyError(lib_id)
    start = mm.start() + 1
    block = src[start:_skip_sexp(src, start)]

    pins = {}
    for unit_name in re.findall(r'\n\t\t\(symbol "([^"]+)"', block):
        um = re.search(r'\n\t\t\(symbol "%s"' % re.escape(unit_name), block)
        ub = block[um.start() + 1:_skip_sexp(block, um.start() + 1)]
        for pm in re.finditer(r'\(pin\s+\S+\s+\S+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)', ub):
            gend = _skip_sexp(ub, pm.start())
            num = re.search(r'\(number\s+"([^"]*)"', ub[pm.start():gend])
            if num:
                pins[num.group(1)] = (float(pm.group(1)), float(pm.group(2)))
    _sym_cache[lib_id] = (block, pins)
    return block, pins


def pin_pos(inst_xy, lib_id, pin_number):
    """Absolute pin end for an unrotated (rot 0) instance."""
    _, pins = get_symbol(lib_id)
    dx, dy = pins[str(pin_number)]
    return (round(inst_xy[0] + dx, 4), round(inst_xy[1] + dy, 4))


# -------------------------------------------------------------- footprints ---
_mod_cache = {}


def get_module(fp_id):
    if fp_id in _mod_cache:
        return _mod_cache[fp_id]
    lib, name = fp_id.split(":")
    path = os.path.join(FP_DIR, lib + ".pretty", name + ".kicad_mod")
    _mod_cache[fp_id] = open(path, encoding="utf-8").read().strip()
    return _mod_cache[fp_id]


DROP_TOKENS = {"version", "generator", "generator_version", "layer", "tedit",
               "tstamp", "uuid", "property", "embedded_fonts", "model"}


def board_footprint(fp_id, at, rot, ref, value, netmap, layer="F.Cu",
                    silk_off=-1.3, fab_off=1.3):
    """Expand a stock footprint file into a pcbnew (footprint ...) board entry.

    netmap: {pad_number: (net_number, net_name)}; pads not in the map stay unbound.
    """
    txt = get_module(fp_id)
    assert txt.startswith("(module") or txt.startswith("(footprint"), txt[:20]
    kids = _children(txt[1:_skip_sexp(txt, 0) - 1])[1:]  # drop the footprint name

    kept = []
    for g in kids[1:]:
        head = re.match(r"\((\w+)", g)
        tok = head.group(1) if head else g
        if tok in DROP_TOKENS:
            continue
        if tok == "pad":
            num = re.match(r'\(pad\s+"([^"]*)"', g)
            if num and num.group(1) in netmap:
                n, name = netmap[num.group(1)]
                k = g.find("(at ")
                g = g[:k] + '(net %d "%s") ' % (n, name) + g[k:]
        kept.append(g)

    x, y = at
    out = ['(footprint "%s"' % fp_id,
           '\t(layer "%s")' % layer,
           '\t(uuid "%s")' % uid(),
           '\t(at %.4f %.4f %.1f)' % (x, y, rot),
           '\t(property "Reference" "%s" (at %.2f %.2f %.1f) (layer "F.Fab")'
           ' (effects (font (size 1.0 1.0) (thickness 0.15))))' % (ref, 0.0, silk_off, rot),
           '\t(property "Value" "%s" (at %.2f %.2f %.1f) (layer "F.Fab")'
           ' (effects (font (size 1.0 1.0) (thickness 0.15))))' % (value, 0.0, fab_off, rot),
           '\t(property "Footprint" "" (at 0 0 0) (effects (hide yes)))',
           '\t(property "Datasheet" "" (at 0 0 0) (effects (hide yes)))',
           '\t(property "Description" "" (at 0 0 0) (effects (hide yes)))']
    out.extend(kept)
    return "\n".join(out) + "\n)"


def custom_footprint(ref, value, at, rot, pads, layer="F.Cu",
                     outline=None, silk_off=-1.3, fab_off=1.3, courtyard=True,
                     courtyard_box=None):
    """Build a footprint from scratch.

    pads: list of (number, x, y, w, h, drill, net_or_None)  -- rect/round pads.
    outline: list of (x, y) for a Fab/Silk polygon (courtyard is +0.25mm box of
    the outline when given, otherwise computed from the pads).
    """
    x0, y0 = at
    if outline is None:
        xs = [p[1] - p[3] / 2 for p in pads] + [p[1] + p[3] / 2 for p in pads]
        ys = [p[2] - p[4] / 2 for p in pads] + [p[2] + p[4] / 2 for p in pads]
        minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    else:
        minx = min(p[0] for p in outline)
        maxx = max(p[0] for p in outline)
        miny = min(p[1] for p in outline)
        maxy = max(p[1] for p in outline)
    out = ['(footprint "custom:%s"' % ref,
           '\t(layer "%s")' % layer,
           '\t(uuid "%s")' % uid(),
           '\t(at %.4f %.4f %.1f)' % (x0, y0, rot),
           '\t(property "Reference" "%s" (at %.2f %.2f %.1f) (layer "F.Fab")'
           ' (effects (font (size 1.0 1.0) (thickness 0.15))))' % (ref, 0.0, silk_off, rot),
           '\t(property "Value" "%s" (at %.2f %.2f %.1f) (layer "F.Fab")'
           ' (effects (font (size 1.0 1.0) (thickness 0.15))))' % (value, 0.0, fab_off, rot),
           '\t(property "Footprint" "" (at 0 0 0) (effects (hide yes)))',
           '\t(property "Datasheet" "" (at 0 0 0) (effects (hide yes)))',
           '\t(property "Description" "" (at 0 0 0) (effects (hide yes)))']
    for (num, px, py, w, h, dr, net) in pads:
        s = '\t(pad "%s" smd rect (at %.3f %.3f 0) (size %.3f %.3f)' % (num, px, py, w, h)
        if dr:
            s = ('\t(pad "%s" thru_hole rect (at %.3f %.3f 0) (size %.3f %.3f)'
                 ' (drill %.3f)' % (num, px, py, w, h, dr))
        if net:
            n, name = net
            s += ' (net %d "%s")' % (n, name)
        s += ' (layers %s))' % ('"F.Cu" "B.Cu"' if dr else '"F.Cu" "F.Paste" "F.Mask"')
        out.append(s)
    if outline:
        pts = " ".join('(xy %.3f %.3f)' % p for p in outline)
        out.append('	(fp_poly (pts %s) (stroke (width 0.12) (type solid)) (layer "F.Fab"))' % pts)
    if courtyard_box:
        minx, miny, maxx, maxy = courtyard_box
    elif not courtyard:
        # DRC falls back to the pad bounding box when a footprint declares no
        # courtyard, so emit a deliberately tiny one instead of none.
        minx = miny = -0.2
        maxx = maxy = 0.2
    for a, b in [((minx, miny), (maxx, miny)), ((maxx, miny), (maxx, maxy)),
                 ((maxx, maxy), (minx, maxy)), ((minx, maxy), (minx, miny))]:
        out.append('\t(fp_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))'
                   % (a[0], a[1], b[0], b[1]))
    return "\n".join(out) + "\n)"


def courtyard(fp_id):
    """Return (minx, miny, maxx, maxy) of the CrtYd courtyard of a stock module."""
    txt = get_module(fp_id)
    kids = _children(txt[1:_skip_sexp(txt, 0) - 1])
    pts = []
    for g in kids[1:]:
        head = re.match(r"\((\w+)", g)
        tok = head.group(1) if head else ""
        if tok in ("gr_line", "fp_line", "gr_arc", "fp_arc"):
            if '"F.CrtYd"' not in g:
                continue
            for c in re.finditer(r"\((?:start|end|mid) ([-\d.]+) ([-\d.]+)\)", g):
                pts.append((float(c.group(1)), float(c.group(2))))
        elif tok in ("gr_rect", "fp_rect"):
            if '"F.CrtYd"' not in g:
                continue
            for c in re.finditer(r"\((?:start|end) ([-\d.]+) ([-\d.]+)\)", g):
                pts.append((float(c.group(1)), float(c.group(2))))
        elif tok == "pad":
            m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', g)
            w = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', g)
            if m and w:
                x, y = float(m.group(1)), float(m.group(2))
                pw, ph = float(w.group(1)), float(w.group(2))
                pts += [(x - pw / 2, y - ph / 2), (x + pw / 2, y + ph / 2)]
    if not pts:
        return None
    return (min(p[0] for p in pts), min(p[1] for p in pts),
            max(p[0] for p in pts), max(p[1] for p in pts))
