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
    """Stable id: regenerating the board keeps the same uuids, so rerunning the
    generator does not churn the file (and a git diff shows real changes)."""
    _uid_seq[0] += 1
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "dpp/%d" % _uid_seq[0]))


_uid_seq = [0]


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
            if j <= i:
                raise ValueError("unbalanced s-expression in child list")
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
    _mod_cache[fp_id] = localize_models(
        open(path, encoding="utf-8").read().strip())
    return _mod_cache[fp_id]


# Project-local 3D packages (scripts/make_3d_models.py writes them; the vendor
# MCAD they are built from is in footprints/vendor_step/ with its SOURCES.md).
MODEL_DIR = os.path.join(KICAD, "share", "kicad", "3dmodels")
PRJ_3D = "footprints/dpp.3dshapes"
_MODEL_LINK = re.compile(r'\$\{KICAD10_3DMODEL_DIR\}/([^\s"]+?)\.step')


def localize_models(text):
    """Repoint a model link at the repo's own 3dshapes when KiCad lacks the file.

    Five footprints on this board ask for packages the KiCad release does not
    ship - AMASS_XT30U-M_1x02_P5.0mm_Vertical, the two JST PH SxB-PH-SM4-TB
    variants, SO-4_4.4x3.9mm_P2.54mm and the Littelfuse 521 fuse clip - so their
    links dangle in a stock install and the parts render as nothing.  Those five
    are rebuilt in footprints/dpp.3dshapes/ and linked with ${KIPRJMOD}, which
    makes the board's 3D self-contained: it renders the same on any machine, and
    the .step files travel with the repo instead of being dropped into somebody's
    KiCad install.  Links that do resolve are left exactly as the library wrote
    them, so nothing here shadows a package the install really has.
    """
    def sub(m):
        rel = m.group(1)
        if os.path.isfile(os.path.join(MODEL_DIR, rel + ".step")):
            return m.group(0)
        return "${KIPRJMOD}/%s/%s.step" % (PRJ_3D, rel.rsplit("/", 1)[-1])

    return _MODEL_LINK.sub(sub, text)


# Everything we drop is bookkeeping that pcbnew re-creates (ids, timestamps);
# 3D models and the library's own Reference/Value text blocks are KEPT so the
# board footprint still matches its library copy (DRC "lib_footprint_mismatch")
# and the 3D viewer stays populated.
DROP_TOKENS = {"version", "generator", "generator_version", "layer", "tedit",
               "tstamp", "uuid", "embedded_fonts", "fp_text"}

# Nickname of the project-local footprint library (footprints/<lib>.pretty)
# that holds the footprints this generator builds from scratch.
LOCAL_LIB = "dpp"
# .kicad_mod format stamp: without these lines KiCad's library reader refuses the
# whole .pretty ("failed to load library") and every board footprint then warns
# "library not enabled".
MOD_VERSION, MOD_GEN, MOD_GENVER = "20241229", "make_pcb.py", "9.0"

_local_mods = {}


def bake_rot(group, rot):
    """Add the footprint's orientation to a child item's own (at x y rot).

    pcbnew stores every pad/text/graphic with the footprint rotation baked into
    its own angle; if we leave them at 0 while the footprint is rotated, KiCad's
    "footprint matches its library copy" check fails for every rotated part.
    """
    if not rot:
        return group

    def sub(m):
        r = (float(m.group(3) or 0) + rot) % 360.0
        return "(at %s %s %g)" % (m.group(1), m.group(2), r)

    return re.sub(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", sub, group)


BACK_SWAP = [('"F.Cu"', '"B.Cu"'), ('"F.Paste"', '"B.Paste"'),
             ('"F.Mask"', '"B.Mask"'), ('"F.CrtYd"', '"B.CrtYd"'),
             ('"F.Fab"', '"B.Fab"')]


def _mirror_text(group):
    """Flag a back-side text item as mirrored (KiCad keeps the flag in justify)."""
    if "justify mirror" in group:
        return group
    j = group.find("(justify")
    if j >= 0:                        # extend the existing justify list
        e = group.index(")", j)
        return group[:e] + " mirror" + group[e:]
    k = group.find("(effects")
    if k < 0:
        return group
    end = _skip_sexp(group, k)
    return group[:end - 1] + " (justify mirror)" + group[end - 1:]


def _to_back(group):
    """Move a front-side child item to its back-side layer (layer names only)."""
    for a, b in BACK_SWAP:
        group = group.replace(a, b)
    return group


def mirror_y(group):
    """Mirror one footprint child item in y - how KiCad stores a B.Cu footprint.

    KiCad writes an underside footprint with the library geometry mirrored about
    the part's origin (the pad/graphics y coordinates change sign, x and the
    items' own angles stay put, text gets "justify mirror"); the 3D model block
    is left exactly as the library has it, because the viewer applies the
    bottom-side mirroring itself.  Proof: every official KiCad demo board that
    uses the same footprint on both faces stores the B.Cu copy y-negated (8 of 8
    asymmetric cases in demos/jetson-agx-thor-baseboard, 0 identical), and the
    GUI-saved B.Cu pads of demos/.../SOT-23-6 read (at -0.954 -1.1 90) where the
    F.Cu copy reads (at -0.954 1.1 90).

    Storing the library geometry unmirrored instead (what a naive generator does)
    keeps the copper exactly where the placement math says - so gerbers, DRC and
    nets all look fine - but the 3D body of the part lands mirrored about the
    origin: for a row of pads it sits a whole row-length away from its own pads,
    usually hanging off the board edge.  See flip_rot() for the compensation.
    """
    if group.lstrip().startswith("(model"):
        return group                      # 3D blocks stay as the library wrote them
    group = re.sub(r"\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)",
                   lambda m: "(at %s %s%s)" % (m.group(1), -float(m.group(2)),
                                               " " + m.group(3) if m.group(3) else ""),
                   group)
    for tok in ("start", "end", "mid", "center"):
        group = re.sub(r"\(%s ([-\d.]+) ([-\d.]+)\)" % tok,
                       lambda m, t=tok: "(%s %s %s)" % (t, m.group(1),
                                                        -float(m.group(2))), group)
    return re.sub(r"\(xy ([-\d.]+) ([-\d.]+)\)",
                  lambda m: "(xy %s %s)" % (m.group(1), -float(m.group(2))), group)


def flip_rot(fp_id, rot):
    """Placement angle that puts the copper back where `rot` had it, once the
    child geometry is mirrored in y.  Returns None when the part's pad pattern
    cannot survive that (its pad numbers would end up on other pads, i.e. the
    nets would move) - that is a real wiring decision, not a formatting one.
    """
    import math

    def place(r, mir):
        th = math.radians(r)
        c, s = math.cos(th), math.sin(th)
        out = {}
        for num, x, y, _, _, _ in pads_of(fp_id):
            yy = -y if mir else y
            out[num] = (round(x * c - yy * s, 6), round(x * s + yy * c, 6))
        return out

    want = place(rot, False)
    for cand in (rot, (360.0 - rot) % 360.0, (rot + 180.0) % 360.0,
                 (180.0 - rot) % 360.0):
        if place(cand, True) == want:
            return cand
    return None


def pads_of(fp_id):
    """[(number, x, y, w, h, drill)] of a stock module's pads (local coords)."""
    txt = get_module(fp_id)
    out = []
    for g in _children(txt[1:_skip_sexp(txt, 0) - 1]):
        if not g.startswith("(pad"):
            continue
        num = re.match(r'\(pad\s+"([^"]*)"', g)
        at = re.search(r'\(at ([-\d.]+) ([-\d.]+)', g)
        size = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', g)
        drill = re.search(r'\(drill (?:circle |oval |square )?([-\d.]+)', g)
        if num and at and size:
            out.append((num.group(1), float(at.group(1)), float(at.group(2)),
                        float(size.group(1)), float(size.group(2)),
                        float(drill.group(1)) if drill else 0.0))
    return out


def pads_full(fp_id):
    """Like pads_of, plus the set of copper layers the pad sits on."""
    txt = get_module(fp_id)
    out = []
    for g in _children(txt[1:_skip_sexp(txt, 0) - 1]):
        if not g.startswith("(pad"):
            continue
        num = re.match(r'\(pad\s+"([^"]*)"', g)
        at = re.search(r'\(at ([-\d.]+) ([-\d.]+)', g)
        size = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', g)
        drill = re.search(r'\(drill (?:circle |oval |square )?([-\d.]+)', g)
        lay = re.search(r'\(layers ([^)]*)\)', g)
        cu = {t.strip('"') for t in lay.group(1).split()}
        cu = {c for c in cu if c.endswith(".Cu")}
        if "*.Cu" in cu:                       # plated through-hole: both faces
            cu = {"F.Cu", "B.Cu"}
        cu = cu or {"F.Cu"}
        if num and at and size:
            out.append((num.group(1), float(at.group(1)), float(at.group(2)),
                        float(size.group(1)), float(size.group(2)),
                        float(drill.group(1)) if drill else 0.0, cu))
    return out


def silk_reference(txt, layer="F.SilkS", size=None, at=None):
    """Move one footprint's Reference field onto a silkscreen layer.

    Called on the already-expanded board entry (see board_footprint's ref_to_fab,
    which does the opposite and keeps the designator off the board).  `size` is
    the glyph height in mm - the stroke follows at 15 %, the KiCad convention -
    and `at` is an optional (x, y) local offset in mm for parts whose library
    default would land the text on a pad.  The text angle stays as emitted, so a
    rot-270 part reads its label sideways like any other rotated part.
    """
    i = txt.find('(property "Reference"')
    if i < 0:
        return txt
    j = _skip_sexp(txt, i)
    blk = txt[i:j]
    blk = re.sub(r'\(layer "[^"]*"\)', '(layer "%s")' % layer, blk, count=1)
    if at is not None:
        m = re.search(r'\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', blk)
        if m:
            blk = (blk[:m.start()] + "(at %.2f %.2f %s)" % (at[0], at[1], m.group(3))
                   + blk[m.end():])
    if size:
        blk = re.sub(r'\(size [^)]*\)', "(size %.2f %.2f)" % (size, size), blk, count=1)
        blk = re.sub(r'\(thickness [^)]*\)', "(thickness %.2f)" % (0.15 * size),
                     blk, count=1)
    return txt[:i] + blk + txt[j:]


def board_footprint(fp_id, at, rot, ref, value, netmap, layer="F.Cu",
                    silk_off=-1.3, fab_off=1.3, ref_to_fab=True, bcu_flip=False,
                    bcu_keep_pads=True):
    """Expand a stock footprint file into a pcbnew (footprint ...) board entry.

    netmap: {pad_number: (net_number, net_name)}; pads not in the map stay unbound.
    bcu_flip: store the underside footprint the way KiCad's GUI does - child
    geometry mirrored in y (flip_rot).  Without it the 3D body of a bottom-mounted
    part renders mirrored about the part origin, i.e. off its own pads, and an
    edge-mounted shell mates towards the board instead of off its edge.
    bcu_keep_pads: with bcu_flip, also re-pick the placement angle so the copper
    lands exactly where the unmirrored placement had it.  Only possible for parts
    whose pattern survives a 180-degree turn; False mirrors in place and lets the
    footprint's own pattern decide which side faces the board edge.
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
        elif tok == "property":
            m = re.match(r'\(property\s+"([^"]*)"', g)
            what = m.group(1) if m else ""
            if what == "Reference":
                g = re.sub(r'(\(property "Reference" )"[^"]*"',
                           lambda mm: mm.group(1) + '"%s"' % ref, g, count=1)
                # print the reference on the fabrication layer, not the
                # silkscreen: silk text at 2.54 mm pitch fails clearance DRC
                if ref_to_fab:
                    g = g.replace('(layer "F.SilkS")', '(layer "F.Fab")')
            elif what == "Value":
                g = re.sub(r'(\(property "Value" )"[^"]*"',
                           lambda mm: mm.group(1) + '"%s"' % value, g, count=1)
                # values on the fab layer too: silk text at component pitch
                # collides everywhere (see the Reference branch above)
                if ref_to_fab:
                    g = g.replace('(layer "F.SilkS")', '(layer "F.Fab")')
        # inner ids come from the library file: two instances of the same part
        # would share them, so give every child item its own stable id
        kept.append(re.sub(r'\(uuid "[^"]*"\)',
                           lambda mm: '(uuid "%s")' % uid(), g))

    if layer == "B.Cu":
        # Bottom-side mounting: pads go to the back copper (module pads keep
        # their own drill), fab/courtyard move to the back layers, text gets the
        # mirror flag.  The library's own silkscreen graphics are dropped - they
        # would sit against the back-side pads (silk_over_copper), and the
        # top-side labels ("TRIP EN", "GNSS", ...) are placed by hand instead.
        bk = []
        for g in kept:
            if '"F.SilkS"' in g or '"B.SilkS"' in g:
                continue
            g = _to_back(g)
            if bcu_flip:
                g = mirror_y(g)
            if g.lstrip().startswith("(property"):
                g = _mirror_text(g)
            bk.append(g)
        kept = bk
        if bcu_flip and bcu_keep_pads:
            keep_rot = flip_rot(fp_id, rot)
            assert keep_rot is not None, \
                "%s: mirrored storage cannot keep its copper at rot %g" % (ref, rot)
            rot = keep_rot

    x, y = at
    # ship the rewritten copy in the project library: DRC compares the board
    # footprint with its library copy, and we do rewrite it (nets, ref/value,
    # reference on F.Fab, layer for bottom-mounted parts)
    out = ['(footprint "%s"' % to_local_lib(fp_id, kept, layer),
           '\t(layer "%s")' % layer,
           '\t(uuid "%s")' % uid(),
           '\t(at %.4f %.4f %.1f)' % (x, y, rot)]
    # pcbnew saves a rotated part with the placement angle baked into every
    # child; the library copy keeps the children local (unrotated), which is
    # what pcbnew compares after un-rotating the board copy.
    out.extend([bake_rot(g, rot) for g in kept])
    return "\n".join(out) + "\n)"


def to_local_lib(fp_id, kept, layer="F.Cu"):
    """Register the (rewritten) footprint in the project-local library.

    DRC compares every board footprint with its library copy; because this
    generator rewrites footprints (real ref/value text, reference on F.Fab,
    no library bookkeeping) it would always report 'footprint does not match
    the library copy'.  Shipping the exact footprints we emit in
    footprints/dpp.pretty makes the board self-contained and warning-free.
    Returns the nickname to use on the board (LOCAL_LIB:<name>).
    """
    lib, name = fp_id.split(":")
    if name not in _local_mods:
        head = ['(footprint "%s"' % name,
                '\t(version %s)' % MOD_VERSION, '\t(generator "%s")' % MOD_GEN,
                '\t(generator_version "%s")' % MOD_GENVER,
                '	(layer "%s")' % layer,
                '\t(tstamp "%s")' % uid()]
        body = []
        for g in kept:
            g = re.sub(r'\(net \d+ "[^"]*"\)\s*', "", g)
            m = re.match(r'\(property\s+"([^"]*)"', g)
            what = m.group(1) if m else ""
            if what in ("Reference", "Value"):
                g = re.sub(r'\(uuid "[^"]*"\)', '', g)
                g = re.sub(r'(\(property "%s" )"[^"]*"' % what,
                           lambda mm, w=what: mm.group(1) + ('"REF**"' if w == "Reference" else '""'),
                           g, count=1)
            body.append(g)
        _local_mods[name] = "\n".join(head + body) + "\n)\n"
    return "%s:%s" % (LOCAL_LIB, name)


def write_local_lib(pretty_dir):
    """Write every registered footprint as a .kicad_mod under pretty_dir."""
    os.makedirs(pretty_dir, exist_ok=True)
    names = sorted(_local_mods)
    for name in names:
        open(os.path.join(pretty_dir, name + ".kicad_mod"), "w",
             encoding="utf-8").write(_local_mods[name])
    return names


def custom_footprint(ref, value, at, rot, pads, layer="F.Cu",
                     outline=None, silk_off=-1.3, fab_off=1.3, courtyard=True,
                     courtyard_box=None, fp_name=None):
    """Build a footprint from scratch.

    pads: list of (number, x, y, w, h, drill, net_or_None)  -- rect/round pads.
    outline: list of (x, y) for a Fab/Silk polygon.  The courtyard falls back to
    the outline's bounding box (or the pads' when there is no outline) unless
    courtyard_box spells one out; pass a box with a margin around the body, as
    the DC-DC module does (+0.25 mm), or courtyard=False for a bare pad field.
    """
    x0, y0 = at
    fabl = "B.Fab" if layer == "B.Cu" else "F.Fab"
    eff = (' (effects (font (size 1.0 1.0) (thickness 0.15))%s))'
           % (" (justify mirror)" if layer == "B.Cu" else ""))
    kept = ['	(property "Reference" "%s" (at %.2f %.2f %.1f) (layer "%s")%s'
            % (ref, 0.0, silk_off, rot, fabl, eff),
            '	(property "Value" "%s" (at %.2f %.2f %.1f) (layer "%s")%s'
            % (value, 0.0, fab_off, rot, fabl, eff),
            '	(property "Footprint" "" (at 0 0 0) (effects (hide yes)))',
            '	(property "Datasheet" "" (at 0 0 0) (effects (hide yes)))',
            '	(property "Description" "" (at 0 0 0) (effects (hide yes)))']
    if outline is None:
        xs = [p[1] - p[3] / 2 for p in pads] + [p[1] + p[3] / 2 for p in pads]
        ys = [p[2] - p[4] / 2 for p in pads] + [p[2] + p[4] / 2 for p in pads]
        minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    else:
        minx = min(p[0] for p in outline)
        maxx = max(p[0] for p in outline)
        miny = min(p[1] for p in outline)
        maxy = max(p[1] for p in outline)
    for (num, px, py, w, h, dr, net) in pads:
        if net == "npth":            # unplated mounting hole: pad size = keep-out
            kept.append('\t(pad "%s" np_thru_hole circle (at %.3f %.3f 0) (size %.3f %.3f)'
                        ' (drill %.3f) (layers "F.Mask" "B.Mask"))'
                        % (num, px, py, w, h, dr))
            continue
        s = '\t(pad "%s" smd rect (at %.3f %.3f 0) (size %.3f %.3f)' % (num, px, py, w, h)
        if dr:
            s = ('\t(pad "%s" thru_hole rect (at %.3f %.3f 0) (size %.3f %.3f)'
                 ' (drill %.3f)' % (num, px, py, w, h, dr))
        if net:
            n, name = net
            s += ' (net %d "%s")' % (n, name)
        if dr:
            s += ' (layers "F.Cu" "B.Cu"))'
        else:
            s += ' (layers %s))' % ('"B.Cu" "B.Paste" "B.Mask"'
                                    if layer == "B.Cu"
                                    else '"F.Cu" "F.Paste" "F.Mask"')
        kept.append(s)
    if outline:
        pts = " ".join('(xy %.3f %.3f)' % p for p in outline)
        kept.append('	(fp_poly (pts %s) (stroke (width 0.12) (type solid)) (layer "%s"))'
                    % (pts, "B.Fab" if layer == "B.Cu" else "F.Fab"))
    if courtyard_box:
        minx, miny, maxx, maxy = courtyard_box
    elif not courtyard:
        # DRC falls back to the pad bounding box when a footprint declares no
        # courtyard, so emit a deliberately tiny one instead of none.
        minx = miny = -0.2
        maxx = maxy = 0.2
    crtl = "B.CrtYd" if layer == "B.Cu" else "F.CrtYd"
    for a, b in [((minx, miny), (maxx, miny)), ((maxx, miny), (maxx, maxy)),
                 ((maxx, maxy), (minx, maxy)), ((minx, maxy), (minx, miny))]:
        kept.append('	(fp_line (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.05) (type solid)) (layer "%s"))'
                    % (a[0], a[1], b[0], b[1], crtl))
    name = fp_name or re.sub(r"[^A-Za-z0-9_.-]+", "_", value)
    if name not in _local_mods:
        # stable id so regenerating the library does not churn the file
        head = ['(footprint "%s"' % name,
                '	(version %s)' % MOD_VERSION, '	(generator "%s")' % MOD_GEN,
                '	(generator_version "%s")' % MOD_GENVER,
                '	(layer "%s")' % layer,
                '	(tstamp "%s")' % uuid.uuid5(uuid.NAMESPACE_URL, LOCAL_LIB + "/" + name)]
        body = [re.sub(r'\(net \d+ "[^"]*"\)\s*', "", g) for g in kept]
        for i, g in enumerate(body):
            m = re.match(r'\(property\s+"(Reference|Value)"', g)
            if m:
                body[i] = re.sub(r'(\(property "%s" )"[^"]*"' % m.group(1),
                                 lambda mm: mm.group(1) + ('"REF**"' if m.group(1) == "Reference" else '""'),
                                 g, count=1)
                body[i] = re.sub(r'\(uuid "[^"]*"\)', '', body[i])
        _local_mods[name] = "\n".join(head + body) + "\n)\n"
    out = ['(footprint "%s:%s"' % (LOCAL_LIB, name),
           '	(layer "%s")' % layer,
           '	(uuid "%s")' % uid(),
           '	(at %.4f %.4f %.1f)' % (x0, y0, rot)]
    out.extend([bake_rot(g, rot) for g in kept])
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
        elif tok in ("gr_circle", "fp_circle"):
            # courtyards drawn as a circle (test points, mounting pads)
            if '"F.CrtYd"' not in g:
                continue
            c0 = re.search(r"\(center ([-\d.]+) ([-\d.]+)\)", g)
            e0 = re.search(r"\(end ([-\d.]+) ([-\d.]+)\)", g)
            if c0 and e0:
                cx, cy = float(c0.group(1)), float(c0.group(2))
                r = ((float(e0.group(1)) - cx) ** 2
                     + (float(e0.group(2)) - cy) ** 2) ** 0.5
                pts += [(cx - r, cy - r), (cx + r, cy + r)]
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
