"""Ground stitching: bond In1.Cu to In2.Cu and pin every layer change down.

    python scripts/stitch_pcb.py [board-file]      # audit/report only, writes nothing

Why this exists (numbers measured on this board, 2026-10-18):

* The two inner planes are both GND, but a signal via that crosses from F.Cu to
  B.Cu has no GND hole next to it: median distance from a signal via to the
  nearest GND via was **5.1 mm**, p90 9.8 mm, worst 18.5 mm, with 36 GND vias
  against 235 signal vias.  The return current has to detour centimetres, which
  is the loop that radiates and the loop that receives.
* A via crossing a plane pair with no stitch nearby also forms a resonant
  cavity between the two planes under the pad.

So this places GND holes:

1. **companions** - a GND via as close as the class clearance allows to every
   signal via (the return path for a layer change),
2. a **fence** around the antenna keep-out slot, so the slot in the planes does
   not leak,
3. a general **lattice** (~5 mm, about lambda/10 at 2.45 GHz in FR4) that bonds
   the two planes everywhere and keeps any remaining plane island short.

It is geometry only: it never touches connectivity, and KiCad's DRC (run by
make_pcb.py on the filled board) stays the referee.  The clearance model here is
deliberately the same one scripts/make_pcb.py:classcheck() uses - the through
via's PAD box policed on all four copper layers, axis-aligned gap, clearance =
max of the two classes - because that pairing with DRC was proved on
2026-10-17 (a GND via declared F.Cu-B.Cu 1.8 mm from a 2.5 mm In2 rail reported
`netclass "Power" 0.6000 mm`, i.e. DRC polices the barrel on the layer the via
merely passes).
"""
import math
import re
import sys

# ---- defaults: the shape of the stitch hole, and how thick the lattice is ----
VIA_DIA = 0.70           # same pad/drill as the Default-class vias already on
VIA_DRILL = 0.35         # the board: no new fab feature is introduced by this
PITCH = 5.00             # general lattice pitch (design mm)
FENCE_PITCH = 5.00       # holes around the antenna keep-out
EDGE_MARGIN = 0.50       # stitch pad edge to the board outline
KEEP_MARGIN = 0.50       # stitch pad edge to the antenna keep-out
MIN_SEP = 1.20           # two stitch holes closer than this are one hole's work
HOLE_MIN = 0.50          # mm, the fab's hole-to-hole limit (drill edge to drill
                         # edge) between holes of DIFFERENT nets, the same number
                         # make_pcb.py writes into the project's design rules
INNER_MARGIN = 0.35      # mm demanded ON TOP of the class clearance for copper
                         # that sits on an inner plane layer.  The pour keeps the
                         # class clearance back from that copper, so a stitch pad
                         # placed at exactly the clearance lands on the RIM of the
                         # plane's own hole and DRC reports the via as dangling
                         # ("connected on only one layer") - measured on the first
                         # stitched board, 2026-10-18: two lattice holes touching
                         # In1 only, both on the rim of an In2 rail's moat.
NOPAD_CLEAR = 0.60       # mm around copper that carries NO net: the mounting
                         # hardware (J6 / J9 / J14 / J17 "MP" pads) is an unknown
                         # island - chassis, heatsink, or nothing.  A GND stitch
                         # must not be the thing that decides what it is shorted
                         # to (measured: 7 shorting_items + 7 solder_mask_bridge
                         # on the first stitched board).
SAFE = 0.03              # clearance slack this checker demands, so the emitted
                         # copper never lands on the class limit by rounding and
                         # then fails KiCad's DRC (the referee) by 0.0005 mm
COMPANION_R = (1.25, 1.50, 1.75, 2.00, 2.50, 3.00)   # tried in this order
COMPANION_MAX = 3.50     # how far a companion may be pushed before we give up
LAYERS = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")
INNER = ("In1.Cu", "In2.Cu")


def _gap(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2):
    """classcheck()'s axis-aligned box gap - keep the two checks identical."""
    gx = max(bx1 - ax2, ax1 - bx2, 0.0)
    gy = max(by1 - ay2, ay1 - by2, 0.0)
    return max(gx, gy) if (gx == 0 or gy == 0) else (gx * gx + gy * gy) ** 0.5


class Placer:
    """Holds the obstacles (foreign copper) and answers 'may a GND hole go here?'.

    pads:   [{"net","box":(x0,y0,x1,y1),"layers":[..],"drill":d}]  design frame
    segs:   [{"net","layer","a":(x,y),"b":(x,y),"w":w}]            design frame
    vias:   [{"net","x","y","dia"}]                                design frame
    frame:  (x0,y0,x1,y1) board outline;  keep: (x0,y0,x1,y1) antenna keep-out
    clr:    net -> class clearance (mm edge to edge)
    """

    def __init__(self, frame, keep, pads, segs, vias, clr, inner=None):
        self.frame, self.keep, self.clr = frame, keep, clr
        self.obs = []          # (net, layer, x0,y0,x1,y1)
        self.holes = []        # (x, y, drill, net) of every drilled feature
        for p in pads:
            # A pad with no net is not absent from the board.  The mounting
            # hardware is: J6 / J9 / J14 / J17 carry a copper "MP" pad, and H1-H4
            # are non-plated holes.  Both eat the two violations the first
            # stitched board produced on 2026-10-18 - a GND stitch landing in an
            # MP pad (shorting_items, solder_mask_bridge) or next to a mounting
            # drill (hole_clearance, hole_to_hole) - because the clearance model
            # here skipped every netless pad.  Model them as unknown-purpose
            # copper (NOPAD_CLEAR) AND as drilled features (the hole rules below).
            net = p.get("net") or ""
            # A drilled pad is a plated barrel: it is copper on every layer the
            # drill passes, exactly like a through via, so police it on all four.
            lays = LAYERS if p.get("drill", 0.0) else p["layers"]
            for lay in lays:
                if lay in LAYERS:
                    self.obs.append((net, lay) + tuple(p["box"]))
            if p.get("drill", 0.0):
                bx = p["box"]
                self.holes.append(((bx[0] + bx[2]) / 2.0,
                                   (bx[1] + bx[3]) / 2.0, p["drill"], net))
        for v in vias:
            if not v.get("net"):
                continue
            net = v["net"]
            x0, y0 = v["x"] - v["dia"] / 2.0, v["y"] - v["dia"] / 2.0
            x1, y1 = v["x"] + v["dia"] / 2.0, v["y"] + v["dia"] / 2.0
            for lay in LAYERS:
                self.obs.append((net, lay, x0, y0, x1, y1))
            self.holes.append((v["x"], v["y"], v.get("drill", VIA_DRILL), net))
        for s in segs:
            w = s["w"] / 2.0
            (ax, ay), (bx, by) = s["a"], s["b"]
            self.obs.append((s["net"], s["layer"], min(ax, bx) - w, min(ay, by) - w,
                             max(ax, bx) + w, max(ay, by) + w))
        # bucket by layer so a legal() call only walks the copper it can touch
        self.by_layer = {lay: [o for o in self.obs if o[1] == lay] for lay in LAYERS}
        self.placed = []       # (x, y) of the stitch holes chosen so far

    def legal(self, x, y, dia=VIA_DIA, net="GND", drill=VIA_DRILL, safe=SAFE):
        """True if a `dia` mm pad of net `net` may sit at (x, y).

        `safe` is the extra margin demanded on top of the class clearance;
        scripts/test_stitch.py audits an already-generated board with safe=0,
        i.e. exactly the rule KiCad's DRC will apply.
        """
        x0, y0, x1, y1 = x - dia / 2.0, y - dia / 2.0, x + dia / 2.0, y + dia / 2.0
        fx0, fy0, fx1, fy1 = self.frame
        if x0 < fx0 + EDGE_MARGIN or y0 < fy0 + EDGE_MARGIN or \
           x1 > fx1 - EDGE_MARGIN or y1 > fy1 - EDGE_MARGIN:
            return False
        if self.keep:
            kx0, ky0, kx1, ky1 = self.keep
            if not (x0 > kx1 + KEEP_MARGIN or x1 < kx0 - KEEP_MARGIN or
                    y0 > ky1 + KEEP_MARGIN or y1 < ky0 - KEEP_MARGIN):
                return False          # sits in the antenna slot
        need_self = self.clr(net)
        for lay in LAYERS:
            inner = lay in INNER
            for (onet, _lay, ox0, oy0, ox1, oy1) in self.by_layer[lay]:
                if onet == net:
                    continue
                need = max(need_self, self.clr(onet))
                if onet == "":
                    need = max(need, NOPAD_CLEAR)
                if inner:
                    need += INNER_MARGIN
                if _gap(x0, y0, x1, y1, ox0, oy0, ox1, oy1) < need + safe:
                    return False
        # Drilled features: a stitch hole is a hole too, and the fab sells a
        # minimum wall between two holes of different nets (HOLE_MIN).  Mounting
        # hardware is where this bites - H1-H4 are non-plated 3 mm+ drills with no
        # copper to police, so without this check the only thing stopping a lattice
        # hole from landing inside a mounting hole was luck (measured: 23
        # hole_clearance + 27 hole_to_hole violations on the first stitched board).
        # Net is NOT a reason to skip a pair: measured 2026-10-18, KiCad reported
        # 26 hole_to_hole errors between two GND vias ("reale 0,0000 mm" at several
        # pairs), so the DRC item does not merge same-net holes the way a fab might.
        for (hx, hy, hd, _hnet) in self.holes:
            if math.hypot(x - hx, y - hy) - (hd + drill) / 2.0 < HOLE_MIN + safe:
                return False
        return True

    def _free_of_stitch(self, x, y, sep=MIN_SEP):
        return all((x - px) ** 2 + (y - py) ** 2 >= sep * sep for px, py in self.placed)

    def add(self, x, y):
        self.placed.append((x, y))
        # a placed stitch hole is an obstacle for the next one, at the hole level
        # as well as the pad level, so a whole run of lattice holes cannot creep
        # under the fab's hole-to-hole wall one legal-looking step at a time
        self.holes.append((x, y, VIA_DRILL, "GND"))
        return {"x": x, "y": y, "net": "GND", "dia": VIA_DIA, "drill": VIA_DRILL,
                "layers": ["F.Cu", "B.Cu"]}

    # ---------------------------------------------------------------- steps --
    def companions(self, vias):
        """One GND hole as close as the clearance allows to every signal via."""
        out, missed = [], []
        gnd = [(v["x"], v["y"]) for v in vias if v["net"] == "GND"]
        for v in vias:
            if v["net"] == "GND":
                continue
            if any((v["x"] - gx) ** 2 + (v["y"] - gy) ** 2 <= 1.5 ** 2
                   for gx, gy in gnd + self.placed):
                continue          # already pinned down by the existing GND via
            hit = None
            # nearest legal spot, searched outward: 0.25 mm rings x 16 spokes.
            # A fixed 8-spoke search stranded most vias at 2-3 mm because the
            # four free directions between crowded tracks are rarely cardinal.
            r = 1.10
            while r <= COMPANION_MAX + 1e-6 and hit is None:
                for k in range(16):
                    a = math.radians(11.25 + 22.5 * k)
                    x, y = v["x"] + r * math.cos(a), v["y"] + r * math.sin(a)
                    if self.legal(x, y) and self._free_of_stitch(x, y, 1.00):
                        hit = (round(x, 3), round(y, 3))
                        break
                r += 0.25
            if hit:
                out.append(self.add(*hit))
                gnd.append(hit)
            else:
                missed.append(v)
        return out, missed

    def band(self, rect, pitch=3.00, lo=1.30, hi=4.50):
        """A tight ring of holes around a rectangle (the antenna keep-out slot).

        The slot in both planes needs its edge pinned: surface current running
        into an open slot edge radiates, and a via fence at about lambda/10
        (5.7 mm at 2.45 GHz in FR4, so 3 mm here) short-circuits the edges.  The
        general lattice is too coarse and too blocked by the module's pin rows
        to do this job on its own.
        """
        out = []
        rx0, ry0, rx1, ry1 = rect
        x = rx0 - hi
        while x <= rx1 + hi + 1e-6:
            for y in (ry0 - lo, ry0 - hi, ry1 + lo, ry1 + hi):
                if self.legal(x, y) and self._free_of_stitch(x, y):
                    out.append(self.add(round(x, 3), round(y, 3)))
            x += pitch
        y = ry0 - hi
        while y <= ry1 + hi + 1e-6:
            for x in (rx0 - lo, rx0 - hi, rx1 + lo, rx1 + hi):
                if self.legal(x, y) and self._free_of_stitch(x, y):
                    out.append(self.add(round(x, 3), round(y, 3)))
            y += pitch
        return out

    def fence(self):
        return self.band(self.keep) if self.keep else []

    def lattice(self, x0=None, y0=None, x1=None, y1=None, pitch=PITCH):
        """The general bond between the two planes, over the whole board."""
        fx0, fy0, fx1, fy1 = self.frame
        gx0 = fx0 + 2.0 if x0 is None else x0
        gy0 = fy0 + 2.0 if y0 is None else y0
        gx1 = fx1 - 2.0 if x1 is None else x1
        gy1 = fy1 - 2.0 if y1 is None else y1
        out, blocked = [], 0
        # half-pitch offsets on alternate rows: a square lattice leaves diagonal
        # holes; this is a triangular one, so the worst-plane-spot distance drops
        row = 0
        y = gy0
        while y <= gy1 + 1e-6:
            x = gx0 + (0.0 if row % 2 == 0 else pitch / 2.0)
            while x <= gx1 + 1e-6:
                if self.legal(x, y) and self._free_of_stitch(x, y):
                    out.append(self.add(x, y))
                else:
                    blocked += 1
                x += pitch
            y += pitch
            row += 1
        return out, blocked


def place(frame, keep, pads, segs, vias, clr):
    """Return (new GND vias, report dict).  Order: companions, fence, lattice."""
    p = Placer(frame, keep, pads, segs, vias, clr)
    comp, missed = p.companions(vias)
    fen = p.band(keep) if keep else []
    lat, blocked = p.lattice()
    # Every drilled hole the board ends up with, so the fab's hole-to-hole limit
    # (0.5 mm, and it is a real DRC item) is checked here instead of being
    # discovered by the 12-minute run.  Drilled pads count: their drill passes
    # every layer, exactly like a via's.  Net is not a reason to skip a pair (see
    # legal(): KiCad flagged two GND vias 0.07 mm apart as hard as any other pair).
    holes = [(v["x"], v["y"], v.get("drill", VIA_DRILL), v["net"]) for v in vias]
    holes += [(x, y, VIA_DRILL, "GND") for (x, y) in p.placed]
    for pd in pads:
        if pd.get("drill", 0.0):
            bx = pd["box"]
            holes.append(((bx[0] + bx[2]) / 2.0, (bx[1] + bx[3]) / 2.0,
                          pd["drill"], pd.get("net") or ""))
    hh, who = 9e9, None
    for i in range(len(holes)):
        ax, ay, ad, an = holes[i]
        for j in range(i + 1, len(holes)):
            bx, by, bd, bn = holes[j]
            g = math.hypot(ax - bx, ay - by) - (ad + bd) / 2.0
            if g < hh:
                hh, who = g, (an, round(ax, 2), round(ay, 2), bn, round(bx, 2),
                              round(by, 2))
    allgnd = [(v["x"], v["y"]) for v in vias if v["net"] == "GND"] + list(p.placed)
    sig = [v for v in vias if v["net"] != "GND"]
    d = sorted(min(math.hypot(v["x"] - gx, v["y"] - gy) for gx, gy in allgnd)
               for v in sig)
    bands = [(1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0, 99.0)]
    rep = dict(companions=len(comp), fence=len(fen), lattice=len(lat),
               total=len(p.placed), blocked=blocked,
               unpaired=[(v["net"], round(v["x"], 1), round(v["y"], 1)) for v in missed],
               n_sig=len(sig),
               hist=[sum(1 for x in d if x <= lim) for lim in bands[0]],
               limits=bands[0],
               median=round(d[len(d) // 2], 2) if d else None,
               p90=round(d[int(len(d) * 0.9)], 2) if d else None,
               worst=round(d[-1], 2) if d else None,
               holes=len(holes), min_hole_gap=round(hh, 3), min_hole_who=who)
    return [c for c in comp] + fen + lat, rep


# ------------------------------------------------------- audit mode (file) ----
SEG = re.compile(r'^\s*\(segment \(net (\d+)\) \(start ([\d.-]+) ([\d.-]+)\) '
                 r'\(end ([\d.-]+) ([\d.-]+)\) \(width ([\d.-]+)\) \(layer "([^"]+)"\)')
VIA = re.compile(r'^\s*\(via \(net (\d+)\) \(at ([\d.-]+) ([\d.-]+)\) '
                 r'\(size ([\d.-]+)\) \(drill ([\d.-]+)\) \(layers "([^"]+)" "([^"]+)"\)')


def read_board(path, dx, dy, nets):
    """Board file (sheet frame) -> the design-frame geometry place() wants."""
    segs, vias = [], []
    for ln in open(path, encoding="utf-8"):
        m = SEG.match(ln)
        if m:
            n = nets.get(int(m.group(1)), "?")
            segs.append({"net": n, "layer": m.group(7),
                         "a": (float(m.group(2)) - dx, float(m.group(3)) - dy),
                         "b": (float(m.group(4)) - dx, float(m.group(5)) - dy),
                         "w": float(m.group(6))})
            continue
        m = VIA.match(ln)
        if m:
            vias.append({"net": nets.get(int(m.group(1)), "?"),
                         "x": float(m.group(2)) - dx, "y": float(m.group(3)) - dy,
                         "dia": float(m.group(4)), "drill": float(m.group(5))})
    return segs, vias


def main(argv):
    sys.path.insert(0, __file__.rsplit("/", 1)[0].replace("\\", "/"))
    import make_pcb as mp
    here = __file__.replace("\\", "/").rsplit("/", 1)[0]
    default = here + "/../dashboard-plusplus-carrier.kicad_pcb"
    path = argv[1] if len(argv) > 1 else default
    path = path.replace("\\", "/")
    nets = dict((int(n), nm) for n, nm in
                re.findall(r'\(net (\d+) "([^"]+)"\)', open(path, encoding="utf-8").read()))
    segs, vias = read_board(path, mp.PAGE_DX, mp.PAGE_DY, nets)
    pads = mp.route_pads()
    frame = (mp.BOARD_X0, mp.BOARD_Y0, mp.BOARD_X1, mp.BOARD_Y1)
    new, rep = place(frame, mp.KEEP, pads, segs, vias, mp.net_clr)
    print("stitch audit on %s" % path)
    print("  routed copper: %d segments, %d vias (%d GND)"
          % (len(segs), len(vias), sum(1 for v in vias if v["net"] == "GND")))
    print("  adds: companions %d, fence %d, lattice %d  ->  %d new GND holes "
          "(%d lattice cells blocked by copper); tightest hole-to-hole gap over "
          "the whole board %.3f mm (fab limit 0.50)%s"
          % (rep["companions"], rep["fence"], rep["lattice"], rep["total"],
             rep["blocked"], rep["min_hole_gap"],
             " worst: %s@%s,%s / %s@%s,%s" % rep["min_hole_who"]
             if rep["min_hole_who"] else ""))
    print("  signal vias: %d; nearest GND via after stitching - "
          "%s; median %.2f, p90 %.2f, worst %.2f mm"
          % (rep["n_sig"],
             ", ".join("<=%.1f:%d" % (lim, n) for lim, n in
                       zip(rep["limits"], rep["hist"])),
             rep["median"], rep["p90"], rep["worst"]))
    if rep["unpaired"]:
        print("  NO legal companion for %d via(s): %s"
              % (len(rep["unpaired"]),
                 ", ".join("%s@%s,%s" % u for u in rep["unpaired"][:12])))
    # ASCII map, 2.5 mm cells: o pad, v signal via, g GND via, + new stitch
    import collections
    C = 2.5
    grid = collections.defaultdict(set)
    for p in pads:
        if p["net"]:
            cx, cy = int((p["box"][0] - frame[0]) / C), int((p["box"][1] - frame[1]) / C)
            grid[(cx, cy)].add("o")
    for v in vias:
        grid[(int((v["x"] - frame[0]) / C), int((v["y"] - frame[1]) / C))].add(
            "g" if v["net"] == "GND" else "v")
    for v in new:
        grid[(int((v["x"] - frame[0]) / C), int((v["y"] - frame[1]) / C))].add("+")
    kx0, ky0, kx1, ky1 = mp.KEEP
    rows = []
    for j in range(int((frame[3] - frame[1]) / C) + 1):
        line = []
        for i in range(int((frame[2] - frame[0]) / C) + 1):
            x, y = frame[0] + (i + 0.5) * C, frame[1] + (j + 0.5) * C
            if kx0 <= x <= kx1 and ky0 <= y <= ky1:
                line.append("A")
                continue
            s = grid.get((i, j), set())
            line.append("+" if "+" in s else "g" if "g" in s else
                        "v" if "v" in s else "o" if "o" in s else ".")
        rows.append("".join(line))
    print("  map: . board  o pad  v signal via  g GND via  + new stitch  A antenna keep-out")
    for r in reversed(rows):
        print("   |" + r + "|")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
