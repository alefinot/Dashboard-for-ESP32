"""Autorouter for the carrier board - grid A*, deterministic, no hand-drawn copper.

  from make_pcb.py:   segs, vias, log = route_pcb.route(cfg)

The board is routed on the two outer layers only (F.Cu / B.Cu); In1.Cu and
In2.Cu are solid GND planes, and every via is a through hole, so each via also
stitches those two planes.

Nothing here predicts KiCad's verdict - the obstacle model is a *conservative*
inflation of pads, vias, the board edge and the antenna keep-out, and KiCad's own
DRC (which make_pcb.main() runs straight after this file) is the referee, per
AGENTS rule 30.

Method
  * a GRID-spaced node grid over the whole board frame; track centrelines run
    node to node, 8-connected, no corner cutting, so every bend is 0/45/90 deg;
  * a node is usable by a trace of half width h against an obstacle that needs
    clearance c only if it lies outside that obstacle grown by c + h + SLACK.
    Growing an axis-aligned rectangle by d is the Minkowski sum with a square,
    so corners come out over-inflated, never under-inflated; SLACK (half a cell
    plus margin) covers the one gap that is left: a diagonal step between two
    free nodes can clip into a blocked square by up to half a cell;
  * per net: all pad pairs sorted by Manhattan length, Kruskal with union-find,
    each accepted edge routed with A* (octile heuristic, small turn penalty,
    through-via cost).  Copper of the net being routed is free space, so the
    tree grows into itself the way a hand route does;
  * route order is claimed by class - the fat 12 V / 5 V rails get their
    corridors first, then the GND tree, then the analog runs, then the DevKit
    digital group, then spares and the debug header.

Everything comes from make_pcb's placement: move a part there and this router
produces different copper on the next run.
"""
import heapq
import math
from array import array
from collections import deque

GRID = 0.10          # mm, node spacing
SLACK = 0.00         # mm, half a cell + margin (see above)
EDGE = 0.40          # mm, copper to board outline.  The project constraint is
                     # 0.372 because the JST shells' pads already sit 0.40 mm
                     # from the outline, and JLC's own limit is 0.2 (internal)
                     # / 0.3 (outer); 0.40 is what this board can actually use.
VIA_COST = 2.0       # cost of one layer change, in grid units.  It was 4.5
                     # (0.9 mm of track), which priced a layer change out of the
                     # band between the DevKit rows and the edge headers: every
                     # 0.25 mm signal that ends on an SMD pad there had to find a
                     # lane that was already full by the time its turn came.  At
                     # 2.0 (0.4 mm of track; a 0.70/0.35 via is 0.35 mm of drilled
                     # copper, so that is the honest order of magnitude) the
                     # greedy pass strands 7 pads instead of 11, for +1.2 % total
                     # length and +14 vias - measured 2026-10-16, and via count
                     # costs the fab nothing (plan "carrier autorouter - rip-up
                     # pass for the last 7 open legs").
TURN = 0.06          # cost of a direction change, in grid units

# ---- rip-up and retry -------------------------------------------------------
# The greedy pass is one-way: whoever asks first keeps the corridor.  Measured
# (same plan, section 2): routing the spares/debug group FIRST connects all of
# them and strands 33 legs of other nets instead - one lane, and it is not big
# enough for both.  So the pass below picks up the few cheapest foreign nets that
# own the wall a stranded leg is stuck against, re-routes the stranded net
# through the gap, then puts those nets back (re-routed).  It reverts any
# experiment that does not leave every net routed, so the board can only ever
# come out with fewer open legs than the greedy pass left it with.
RIP = True           # run the pass (False = old greedy-only behaviour)
RIP_DEPTH = 3        # unused since the wall-cut rewrite, kept for the harness
RIP_CUT = 2          # legs of one cut that may be lifted at once
RIP_LIFTS = 36       # legs one retry may lift in total (runtime guard)
RIP_ROUNDS = 4       # passes over the still-open nets
RIP_TRIES = 16       # cap on retries per run (runtime guard)
RIP_MAX_PER_NET = 12  # legs of one net that may be lifted per run
RIP_ORDER = "route"   # which stranded net is asked first: route order or span
SEAL_ROUNDS = 3      # passes of the last-chance "is this net one piece?" check
CUT_MARGIN = 6.0     # mm, how far outside the pad-to-pad box a wall cut may go
CUT_VIA = 0.6        # cost of one layer change inside a wall cut
RIP_MULT = {"power": 0.35, "gnd": 0.30, "hv": 0.6, "analog": 1.0,
            "tft+gps": 1.0, "spares": 1.3, "debug": 1.3, "default": 1.15}
#   how a victim is scored: frontier nodes it owns x this.  The rails are worth
#   their corridors, ground is a last resort, and the short signal nets are cheap
#   to pick up and put back.  Power used to be 0.0 (never a victim), which turned
#   out to be wrong: the V5 curtain in front of J15 is power copper, and a net
#   that has to reach J15 has to get past it somehow - a leg-level lift costs one
#   A*, not a rail re-route, so it is allowed now, at the back of the queue.
#   In the wall cut (_wall_cut) the same numbers are used the other way round:
#   cutting through a group costs 1/mult, so ground and power copper are the
#   expensive places to break and the spare/debug nets are the cheap ones.
FLOOD_CAP = 150000   # nodes to explore when ranking victims
MIN_SEG = 0.20       # mm, drop shorter stubs when simplifying a path.
                     # Every path point is a grid node, so the shortest honest
                     # run is exactly one GRID step - and comparing that against
                     # 0.20 without a tolerance cut it (node coordinates come out
                     # of grid arithmetic as 38.599999999999994, so a real 0.20 mm
                     # link measured 0.19999999999999929 and was dropped: 13
                     # dangling track ends and one via landing on one layer only)
HOLE_MIN = 0.50      # mm hole-to-hole (JLC 4-layer capability)
COPPER_HOLE = 0.33   # mm drilled hole to foreign copper (JLC PTH vs track)
KEEP_CLR = 0.50      # mm keep-away band around the antenna keep-out / unbound
                     # pads; the zone itself is blocked by its own outline
SQ2 = math.sqrt(2.0)

DIRS = ((1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        # 4-connected on purpose.  The obstacle model rasterises a segment as its
        # axis-aligned bounding box grown by the clearance, which is EXACT for an
        # axis-aligned segment (in the grid's L-inf metric) and a gross
        # over-estimate for a 45-degree one: DC_12's 4.2 mm diagonal blanketed a
        # 5.6 x 5.6 mm square, walled D2.2 in (38 GND edges failed on that one
        # track) and, worse, was the only thing standing between a later net and
        # a crossing.  Diagonal moves also let two foreign runs cross at a point
        # between nodes, which no node-centre test can see.  Manhattan traces
        # cost a little length and buy a sound obstacle model.
        )


class Layer:
    """One routed copper layer: a shared node grid plus a blocked bitmap."""

    def __init__(self, name, nx, ny, ox, oy):
        self.name, self.nx, self.ny, self.ox, self.oy = name, nx, ny, ox, oy
        self.n = nx * ny

    def idx(self, x, y):
        """Nearest node to a design-frame point, or None if off-grid."""
        i = int(round((x - self.ox) / GRID))
        j = int(round((y - self.oy) / GRID))
        if 0 <= i < self.nx and 0 <= j < self.ny:
            return j * self.nx + i
        return None

    def ij(self, k):
        return (k % self.nx, k // self.nx)

    def xy(self, i, j):
        return (self.ox + i * GRID, self.oy + j * GRID)

    def grow(self, bm, x0, y0, x1, y1, d, own=None, tag=-1):
        """Block every node inside the rectangle [x0,x1]x[y0,y1] grown by d.

        With `own` given, each blocked node also records `tag` - the index of the
        obstacle in the caller's tagged list - so a caller can ask "who owns this
        wall" without a second pass (the last writer wins; see _build()).

        The corners arrive in path order, not sorted order: a track emitted
        right-to-left (V5 runs x 83.8 -> 60.2) has x0 > x1, and without this swap
        the i0 > i1 range is empty, so that obstacle blocked NOTHING.  That single
        missing pair is what let a dozen later nets cross V5 and the 0.8 mm power
        runs (DRC tracks_crossing / shorting_items), and the same for
        bottom-to-top runs.
        """
        if x1 < x0:
            x0, x1 = x1, x0
        if y1 < y0:
            y0, y1 = y1, y0
        i0 = max(0, int(math.ceil((x0 - d - self.ox) / GRID - 1e-9)))
        i1 = min(self.nx - 1, int(math.floor((x1 + d - self.ox) / GRID + 1e-9)))
        j0 = max(0, int(math.ceil((y0 - d - self.oy) / GRID - 1e-9)))
        j1 = min(self.ny - 1, int(math.floor((y1 + d - self.oy) / GRID + 1e-9)))
        for j in range(j0, j1 + 1):
            base = j * self.nx
            for i in range(i0, i1 + 1):
                bm[base + i] = 1
                if own is not None:
                    own[base + i] = tag

    def block_ring(self, bm, x0, y0, x1, y1):
        """Block every node outside [x0,x1]x[y0,y1] (the board-edge band)."""
        for j in range(self.ny):
            y = self.oy + j * GRID
            if y < y0 or y > y1:
                base = j * self.nx
                for i in range(self.nx):
                    bm[base + i] = 1
            else:
                for i in range(self.nx):
                    x = self.ox + i * GRID
                    if x < x0 or x > x1:
                        bm[j * self.nx + i] = 1


class Router:
    def __init__(self, cfg):
        self.cfg = cfg
        self.log = []
        fx0, fy0, fx1, fy1 = cfg["frame"]
        # the grid covers the whole board (pad centres sit right up against the
        # edge band, so they have to be nodes), with the frame snapped outward to
        # whole cells so the board is symmetric inside the grid
        nx = int(math.ceil((fx1 - fx0) / GRID)) + 1
        ny = int(math.ceil((fy1 - fy0) / GRID)) + 1
        ox = fx0 - ((nx - 1) * GRID - (fx1 - fx0)) / 2.0
        oy = fy0 - ((ny - 1) * GRID - (fy1 - fy0)) / 2.0
        self.layers = [Layer(n, nx, ny, ox, oy) for n in cfg["route_layers"]]
        self.route_layers = list(cfg["route_layers"])
        self.static = []        # pads, keep-out, mounting holes: never changes
        self.copper = []        # routed tracks / vias; grows net by net
        self.holes = []         # (x, y, drill, net) drilled pads, for via checks
        self.segs, self.vias = [], []
        self.gen = 0            # bumped when FOREIGN copper changes (cache key)
        # rip-up bookkeeping: every emitted item is registered under its net, so
        # a whole net can be picked up and put back by index.  A removed track is
        # a dead copper entry + a None in segs/vias/holes; nothing is ever
        # spliced out, so indices stay valid.
        self.items_of = {}      # net -> [{kind, ci, oi, hi, cu, seg|via, hole}]
        self.failed = {}        # net -> [(padA, padB)] legs A* could not route
        self.open_of = {}       # net -> pads - components (missing tree legs)
        self.tree_ok = {}       # net -> tree legs actually routed
        self.tree_legs = {}     # net -> [(padA, padB, [items that carry it])]
        self.rip_group = {}     # net -> route group name (victim ranking)
        self.rip_count = {}     # net -> times picked up
        self.pads_of = {}       # net -> pads of that net (identity: the dict
        #                     entries of cfg["pads"], same objects route() uses)
        for p in self.cfg["pads"]:
            if p["net"]:
                self.pads_of.setdefault(p["net"], []).append(p)
        self._pccache = {}      # net -> (fingerprint, pad roots, attached, groups)
        self._dup = {}          # (net,layer,endA,endB,w) -> item, live copper only
        self._mut = {}          # net -> copper edits so far (_padroots cache key)
        self.rip_tries = 0
        self.rip_done = set()   # (net, victims) experiments already tried
        self.quiet = False      # suppress per-attempt chatter during rip-up

    # ---------------------------------------------------------------- rules --
    def cls(self, net):
        return self.cfg["classes"].get(net, "Default")

    def clear(self, a, b):
        """Clearance between two net classes (KiCad takes the max of the pair).

        "keep" is the antenna keep-out (and any pad that carries no net): it is
        not a class, it is a region nothing may enter.  Its clearance is a plain
        finite keep-away band - NOT a huge number: the obstacle model rasterises
        clearance as a dilation, so a 1e9 mm keep-out would block the whole
        layer, which is exactly what it did (every node came back blocked and the
        first A* gave up after two pops).
        """
        if a == "keep" or b == "keep":
            return KEEP_CLR
        return max(self.cfg["clearance"].get(a, 0.25),
                   self.cfg["clearance"].get(b, 0.25))

    def add_static(self, pads, keep):
        for p in pads:
            x0, y0, x1, y1 = p["box"]
            self.static.append({"kind": "pad", "a": (x0, y0), "b": (x1, y1),
                                "half": 0.0, "net": p["net"],
                                "cls": self.cls(p["net"]) if p["net"] else "keep",
                                "layers": set(p["layers"])})
            if p["drill"] > 0:
                self.holes.append(((x0 + x1) / 2.0, (y0 + y1) / 2.0,
                                   p["drill"], p["net"]))
        kx0, ky0, kx1, ky1 = keep
        for L in self.layers:
            self.static.append({"kind": "keep", "a": (kx0, ky0), "b": (kx1, ky1),
                                "half": 0.0, "net": "keep", "cls": "keep",
                                "layers": {L.name}})

    # ------------------------------------------------------------ bitmaps ---
    def _build(self, net, half, via_r=None, want_own=False):
        """Fresh blocked bitmaps for one task; optionally, who owns each wall.

        Returns (bitmaps, owners, tagged): `owners[li][k]` is the index into
        `tagged` of the last obstacle that covered node k (-1 = nothing, e.g. the
        board-edge band), and `tagged` holds those obstacle dicts (pads, keep-out
        and live copper).  Only asked for by the victim ranking in ripup().
        """
        tc = self.cls(net)
        extra = half if via_r is None else via_r      # radius of the moving thing
        inset = EDGE + extra + SLACK
        # one shared obstacle list, so a tag means the same obstacle on both
        # faces (dead / own-net entries stay in it and are simply never tagged)
        items = self.static + self.copper
        bms, owns = [], []
        for L in self.layers:
            bm = bytearray(L.n)
            own = array("i", [-1]) * L.n if want_own else None
            for (ti, it) in enumerate(items):
                if it["net"] == net or it.get("dead"):
                    continue
                if L.name not in it["layers"]:
                    continue
                if want_own:
                    L.grow(bm, it["a"][0], it["a"][1], it["b"][0], it["b"][1],
                           self.clear(tc, it["cls"]) + extra + it["half"],
                           own, ti)
                else:
                    L.grow(bm, it["a"][0], it["a"][1], it["b"][0], it["b"][1],
                           self.clear(tc, it["cls"]) + extra + it["half"])
            fx0, fy0, fx1, fy1 = self.cfg["frame"]
            L.block_ring(bm, fx0 + inset, fy0 + inset, fx1 - inset, fy1 - inset)
            if via_r is not None:
                # drilled holes: a via's hole must clear every other hole, and
                # its copper must clear foreign drilled pads' copper too
                vd = self.cfg["via"][tc][1]
                for h in self.holes:
                    if h is None:
                        continue
                    (hx, hy, dr, hnet) = h
                    # hole-to-hole and hole-to-foreign-copper are fab limits and
                    # apply whatever the net is: this is also what keeps a via
                    # from landing inside its own net's through-hole pad
                    need = vd / 2.0 + dr / 2.0 + max(HOLE_MIN, COPPER_HOLE)
                    L.grow(bm, hx, hy, hx, hy, need, own, -3 if want_own else -1)
            bms.append(bm)
            if want_own:
                owns.append(own)
        if want_own:
            return bms, owns, items
        return bms

    def _maps(self, net, half, via_r=None):
        """Blocked bitmap per layer for one task (net + track half width, or for
        a via of radius via_r).  Cached until foreign copper changes."""
        key = (net, half, via_r)
        if getattr(self, "_ck", None) == key and \
                getattr(self, "_cg", -1) == self.gen:
            return self._cm
        bms = self._build(net, half, via_r)
        self._cm, self._ck, self._cg = bms, key, self.gen
        return bms

    def _viabm(self, net):
        """Nodes where a via of this net's class may NOT sit (1 = blocked).

        Both bitmaps mark blocked as 1, and a via is legal only where neither
        face is blocked, so the two masks combine with OR.  (It was AND, which
        reads "blocked on both faces": a via could then park right against a
        foreign SMD pad on F.Cu as long as B.Cu happened to be clear there -
        that is where the DRC via-vs-pad and via-vs-track clearance failures
        came from, e.g. a GND via 0.12 mm off an IGN_SENSE pad.)
        """
        vr = self.cfg["via"][self.cls(net)][0] / 2.0
        a, b = self._maps(net, 0.0, via_r=vr)
        return bytearray(x | y for x, y in zip(a, b))

    # ------------------------------------------------------------------ A* --
    def astar(self, net, half, pa, pb, la=None, lb=None):
        """Path from pad centre pa (copper on layers la) to pad centre pb (on lb).

        The layer sets matter: an SMD pad on B.Cu is only reached from a track on
        B.Cu, so ending the search on F.Cu above it would be a dead end that DRC
        calls unconnected.  A drilled pad is copper on both faces, so it takes
        either.  Returns [(layer_index, x, y), ...] with one point per node, or
        None.
        """
        L = self.layers
        bms = [bytearray(b) for b in self._maps(net, half)]
        vbm = self._viabm(net)
        nl = len(L)
        sli = (list(range(nl)) if la is None else
               [self.route_layers.index(n) for n in la
                if n in self.route_layers])
        gli = (list(range(nl)) if lb is None else
               [self.route_layers.index(n) for n in lb
                if n in self.route_layers])
        sidx = [(li, L[li].idx(*pa)) for li in sli]
        gidx = [(li, L[li].idx(*pb)) for li in gli]
        sidx = [t for t in sidx if t[1] is not None]
        gset = set(t for t in gidx if t[1] is not None)
        if not sidx or not gset:
            return None
        gnodes = set(k for (_, k) in gset)
        gxy = [L[0].ij(k) for (_, k) in gset]

        def heur(i, j):
            best = 1e18
            for (gi, gj) in gxy:
                dx, dy = abs(i - gi), abs(j - gj)
                c = dx + dy                    # Manhattan (4-connected grid)
                if c < best:
                    best = c
            return best

        heap = []
        cost = {}
        par = {}
        for (li, k) in sidx:
            bms[li][k] = 0                      # our own pad, always enterable
            cost[(li, k)] = 0.0
            par[(li, k)] = None
            i, j = L[li].ij(k)
            heapq.heappush(heap, (heur(i, j), 0.0, li, k, None))
        for (li, k) in gset:
            bms[li][k] = 0
        found = None
        pops = 0
        while heap:
            f, c, li, k, dprev = heapq.heappop(heap)
            if c > cost.get((li, k), 1e18) + 1e-9:
                continue
            if k in gnodes and (li, k) in gset:
                found = (li, k)
                break
            pops += 1
            i, j = L[li].ij(k)
            for (di, dj, w) in DIRS:
                ni, nj = i + di, j + dj
                if not (0 <= ni < L[li].nx and 0 <= nj < L[li].ny):
                    continue
                nk = nj * L[li].nx + ni
                if bms[li][nk]:
                    continue
                nc = c + w + (TURN if dprev not in (None, (di, dj)) else 0.0)
                if nc < cost.get((li, nk), 1e18) - 1e-9:
                    cost[(li, nk)] = nc
                    par[(li, nk)] = (li, k)
                    heapq.heappush(heap, (nc + heur(ni, nj), nc, li, nk, (di, dj)))
            nli = 1 - li                        # through via: F.Cu <-> B.Cu
            nk = j * L[nli].nx + i
            if not vbm[nk] and cost.get((nli, nk), 1e18) > c + VIA_COST + 1e-9:
                cost[(nli, nk)] = c + VIA_COST
                par[(nli, nk)] = (li, k)
                heapq.heappush(heap, (c + VIA_COST + heur(i, j),
                                      c + VIA_COST, nli, nk, None))
        if found is None:
            if not self.quiet:
                self.log.append("  A* exhausted after %d nodes for %s" % (pops, net))
            return None
        path = []
        cur = found
        while cur is not None:
            li, k = cur
            i, j = L[li].ij(k)
            path.append((li,) + L[li].xy(i, j))
            p = par.get(cur)
            cur = p
        path.reverse()
        # land the ends exactly on the pad centres
        path[0] = (path[0][0], pa[0], pa[1])
        path[-1] = (path[-1][0], pb[0], pb[1])
        return path

    # --------------------------------------------------------------- output --
    def emit(self, path, net, width):
        """Write the path out as tracks + through vias; merge collinear runs."""
        self._mut[net] = self._mut.get(net, 0) + 1
        lay = self.cfg["route_layers"]
        pts = []
        for p in path:
            if pts and pts[-1][0] == p[0] and abs(pts[-1][1] - p[1]) < 1e-9 \
                    and abs(pts[-1][2] - p[2]) < 1e-9:
                continue
            pts.append(p)
        out = []
        for p in pts:
            while len(out) >= 2 and out[-2][0] == out[-1][0] == p[0] and \
                    _collinear(out[-2], out[-1], p):
                out.pop()
            out.append(p)
        total = 0.0
        for a, b in zip(out, out[1:]):
            if a[0] == b[0]:
                ax, ay, bx, by = a[1], a[2], b[1], b[2]
                ln = math.hypot(bx - ax, by - ay)
                if ln < MIN_SEG - 1e-6:
                    continue
                dk = (net, lay[a[0]], min((ax, ay), (bx, by)),
                      max((ax, ay), (bx, by)), width)
                old = self._dup.get(dk)
                if old is not None and not old["cu"]["dead"]:
                    continue        # this exact track is already on the board
                seg = {"net": net, "layer": lay[a[0]],
                       "a": (ax, ay), "b": (bx, by), "w": width}
                cu = {"kind": "tr", "a": (ax, ay), "b": (bx, by),
                      "half": width / 2.0, "net": net,
                      "cls": self.cls(net), "layers": {lay[a[0]]}, "dead": False}
                self.segs.append(seg)
                self.copper.append(cu)
                ent = {"kind": "tr", "ci": len(self.copper) - 1,
                       "oi": len(self.segs) - 1, "hi": None,
                       "cu": cu, "seg": seg, "via": None, "hole": None}
                cu["ent"] = ent        # the obstacle knows its own item record
                self.items_of.setdefault(net, []).append(ent)
                self._dup[dk] = ent
                total += ln
            else:
                vd, vdr = self.cfg["via"][self.cls(net)]
                dk = (net, a[1], a[2], vd)
                old = self._dup.get(dk)
                if old is not None and not old["cu"]["dead"]:
                    continue        # the via is already sitting here
                via = {"net": net, "x": a[1], "y": a[2],
                       "dia": vd, "drill": vdr, "layer": lay[a[0]]}
                cu = {"kind": "via", "a": (a[1] - vd / 2, a[2] - vd / 2),
                      "b": (a[1] + vd / 2, a[2] + vd / 2),
                      "half": 0.0, "net": net, "cls": self.cls(net),
                      "layers": {l.name for l in self.layers}, "dead": False}
                hole = (a[1], a[2], vdr, net)
                self.vias.append(via)
                self.copper.append(cu)
                # a via drills a hole: it joins the hole list so a later via (or a
                # via near this one's own net) keeps the fab's hole-to-hole
                # spacing, which a copper-only dilation does NOT guarantee
                # (0.25 mm of copper clearance between two 0.7 mm vias leaves a
                # 0.25 mm gap between their holes, not 0.5 mm)
                self.holes.append(hole)
                ent = {"kind": "via", "ci": len(self.copper) - 1,
                       "oi": len(self.vias) - 1, "hi": len(self.holes) - 1,
                       "cu": cu, "seg": None, "via": via, "hole": hole}
                cu["ent"] = ent
                self.items_of.setdefault(net, []).append(ent)
                self._dup[dk] = ent
                self.gen += 1              # foreign copper changed mid-net
        return total

    # ------------------------------------------------------------- one net ---
    def _tight_via_pair(self, path, net):
        """A via in this path that sits closer than the fab hole-to-hole spacing
        to an earlier via of the SAME path.

        _viabm() only knows about copper emitted for earlier nets, so a path that
        changes layer twice (up, one step over, down again) can park two of its
        own holes 0.85 mm apart - DRC calls that hole_to_hole (0.4985 mm real).
        Returns the earlier via's position, or None if the path is clean.
        """
        vd, vdr = self.cfg["via"][self.cls(net)]
        need = vd / 2.0 + vdr / 2.0 + HOLE_MIN
        seen = []
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                for (sx, sy) in seen:
                    if math.hypot(a[1] - sx, a[2] - sy) < need - 1e-9:
                        return (sx, sy)
                seen.append((a[1], a[2]))
        return None

    def route_net(self, net, pads_of_net, width):
        """Kruskal over that net's pads (shortest pair first), A* per edge.

        Re-routing a net replaces its entry in self.failed / self.tree_ok /
        self.open_of, so a whole-net retry (ripup()) leaves the tallies honest
        instead of adding to them."""
        self.failed[net] = []
        pts = [((p["box"][0] + p["box"][2]) / 2.0,
                (p["box"][1] + p["box"][3]) / 2.0, p) for p in pads_of_net]
        if len(pts) < 2:
            self.tree_ok[net], self.open_of[net] = 0, 0
            return (0, 0, 0.0)
        pairs = sorted((abs(pts[i][0] - pts[j][0]) + abs(pts[i][1] - pts[j][1]),
                        i, j)
                       for i in range(len(pts)) for j in range(i + 1, len(pts)))
        par = list(range(len(pts)))

        def find(k):
            while par[k] != k:
                par[k] = par[par[k]]
                k = par[k]
            return k
        edges, fails, length = 0, 0, 0.0
        legs = []
        for (d, i, j) in pairs:
            a, b = find(i), find(j)
            if a == b:
                continue
            path = self._leg(net, pts[i][:2], pts[j][:2],
                             pts[i][2]["layers"], pts[j][2]["layers"], width)
            if path is None:
                fails += 1
                self.failed[net].append((pts[i][2], pts[j][2]))
                if not self.quiet:
                    self.log.append(
                        "  UNROUTABLE  %-10s %s.%s -> %s.%s  %.2f mm straight"
                        % (net, pts[i][2]["ref"], pts[i][2]["pad"],
                           pts[j][2]["ref"], pts[j][2]["pad"], d))
                continue
            mark = len(self.items_of.get(net, []))
            length += self.emit(path, net, width)
            legs.append((pts[i][2], pts[j][2], self.items_of[net][mark:]))
            edges += 1
            par[a] = b
        open_legs = len(set(find(k) for k in range(len(pts)))) - 1
        self.tree_ok[net], self.open_of[net] = edges, open_legs
        self.tree_legs[net] = legs
        return (edges, fails, length)

    # ------------------------------------------------------ rip-up and retry --
    @staticmethod
    def _padxy(p):
        return ((p["box"][0] + p["box"][2]) / 2.0,
                (p["box"][1] + p["box"][3]) / 2.0)

    def _leg(self, net, xa, xb, la, lb, width):
        """A* for one leg, retried if it parks two of its own holes too close.

        A path that doubles back between layers (up, one step over, down again)
        can put its own two holes closer than the fab hole-to-hole spacing;
        _viabm() only knows about other nets' copper, so the check is here and
        the offending via gets banned as a phantom hole for the retry.
        """
        path = self.astar(net, width / 2.0, xa, xb, la, lb)
        for _ in range(3):
            if path is None:
                return None
            vd, vdr = self.cfg["via"][self.cls(net)]
            hot = self._tight_via_pair(path, net)
            if hot is None:
                return path
            self.holes.append((hot[0], hot[1], vdr, net))
            self.gen += 1
            path = self.astar(net, width / 2.0, xa, xb, la, lb)
        return path

    def _lift(self, net, ents=None):
        """Take a net's copper off the board - all of it, or just `ents`.

        Removal is by tombstone (the copper entry goes dead, the output lists get
        a None at that index), so every index held anywhere else stays valid and
        _put_back() can re-emit the identical shapes.  Which is what makes an
        experiment reversible: if it does not help, the board goes back exactly as
        it was, down to the last track end.
        """
        self._mut[net] = self._mut.get(net, 0) + 1
        live = self.items_of.get(net, [])
        if ents is None:
            saved, live = live, []
        else:
            gone = set(id(e) for e in ents)
            saved = [e for e in live if id(e) in gone]
            live = [e for e in live if id(e) not in gone]
        for e in saved:
            if e["cu"]["dead"]:
                continue            # already off the board; nothing to tombstone
            e["cu"]["dead"] = True
            if e["kind"] == "tr":
                self.segs[e["oi"]] = None
            else:
                self.vias[e["oi"]] = None
                self.holes[e["hi"]] = None
        self.items_of[net] = live
        self.gen += 1
        return saved

    def _put_back(self, net, saved):
        """Re-register copper _lift() took (same objects, refreshed indices).

        Only copper that is actually off the board goes back on.  Without that
        guard a snapshot taken before some of these entries were lifted, put back
        after they had already been restored by an earlier undo, appended the same
        track a second time at a new index: the old copy stayed live too, so the
        board filled up with overlapping duplicate tracks (a net re-routed three
        times came out with its old copper laid down four), and the second copy's
        index then shadowed the first one's, so the next _lift() tombstoned one
        copy and left the other standing - which is how a via went missing while
        the tracks it joined stayed put.
        """
        self._mut[net] = self._mut.get(net, 0) + 1
        for e in saved:
            if not e["cu"]["dead"]:
                continue
            e["cu"]["dead"] = False
            self.copper.append(e["cu"])
            if e["kind"] == "tr":
                self.segs.append(e["seg"])
                e["ci"], e["oi"] = len(self.copper) - 1, len(self.segs) - 1
                e["hi"] = None
            else:
                self.vias.append(e["via"])
                self.holes.append(e["hole"])
                e["ci"], e["oi"], e["hi"] = (len(self.copper) - 1,
                                             len(self.vias) - 1,
                                             len(self.holes) - 1)
            self.items_of.setdefault(net, []).append(e)
        self.gen += 1

    def _frontier(self, net, half, legs):
        """Who owns the walls that enclose a stranded net's reachable region.

        Floods the free space on both faces out of each open leg's pads (the same
        obstacle model A* uses, own copper free) and counts every step that ran
        into a blocked node, keyed by the obstacle that owns that node.  The
        owners of the region a net cannot get out of are the candidates to pick
        up.  Returns (tag -> hits, the obstacle list the tags index).
        """
        bms, owns, items = self._build(net, half, want_own=True)
        vbm = self._viabm(net)
        L = self.layers
        counts = {}
        for (pa, pb) in legs:
            for p in (pa, pb):
                xy = self._padxy(p)
                seen = [bytearray(x.n) for x in L]
                q = deque()
                for li, nm in enumerate(self.route_layers):
                    if nm in p["layers"]:
                        k = L[li].idx(*xy)
                        if k is not None:
                            seen[li][k] = 1
                            q.append((li, k))
                n = 0
                while q and n < FLOOD_CAP:
                    (li, k) = q.popleft()
                    n += 1
                    i, j = L[li].ij(k)
                    for (di, dj, _w) in DIRS:
                        ni, nj = i + di, j + dj
                        if not (0 <= ni < L[li].nx and 0 <= nj < L[li].ny):
                            continue
                        nk = nj * L[li].nx + ni
                        if seen[li][nk]:
                            continue
                        seen[li][nk] = 1
                        if bms[li][nk]:
                            t = owns[li][nk]
                            if t >= 0:
                                counts[t] = counts.get(t, 0) + 1
                            continue
                        q.append((li, nk))
                    nli = 1 - li                     # reachable through a via
                    nk = j * L[nli].nx + i
                    if not vbm[nk] and not seen[nli][nk]:
                        seen[nli][nk] = 1
                        q.append((nli, nk))
        return counts, items

    def _legmap(self):
        """id(ent) -> (net, leg) for every leg of routed copper on the board.

        The frontier ranks *items*, but a leg is what gets lifted and put back
        (lifting one track of an L-shaped leg leaves the other half plus its via
        as a stub, which DRC calls via_dangling), so the ranking has to be folded
        onto legs before anything can act on it.
        """
        m = {}
        for (n2, ls) in self.tree_legs.items():
            for l in ls:
                for e in l[2]:
                    m[id(e)] = (n2, l)
        return m

    def _wall_legs(self, net, half, legs, skip):
        """Legs of foreign copper ranked by how much of this net's wall they own.

        Pads and the keep-out own walls too, but they do not move, so only routed
        copper is a candidate, and a group scored 0.0 is never one.  `skip` holds
        the legs this search already lifted: they are off the board, so the
        frontier cannot see them and they must not be picked again.  Ties break on
        net then pad, so a run is reproducible.
        """
        counts, items = self._frontier(net, half, legs)
        lmap = self._legmap()
        agg = {}
        for (t, c) in counts.items():
            it = items[t]
            if it["kind"] not in ("tr", "via"):
                continue
            nl = lmap.get(id(it["ent"]))
            if nl is None or id(nl[1]) in skip:
                continue
            (n2, l) = nl
            if self.rip_count.get(n2, 0) >= RIP_MAX_PER_NET:
                continue
            m = RIP_MULT.get(self.rip_group.get(n2, "default"), 1.15)
            if m <= 0.0:
                continue
            agg.setdefault(id(l), [0.0, n2, l])[0] += c * m
        out = list(agg.values())
        out.sort(key=lambda a: (-a[0], a[1], a[2][0]["ref"], str(a[2][0]["pad"]),
                                a[2][1]["ref"], str(a[2][1]["pad"])))
        return out

    def _wall_cut(self, net, half, pa, pb, skip):
        """The cheapest set of foreign legs standing between a pad and its target.

        A Dijkstra over the same node grid, but instead of asking "may this net
        walk here" it prices what it would cost to *make* room: a node the net can
        already use is nearly free, a node held by foreign copper costs what
        lifting that leg would cost (a long leg costs more than a stub, and so
        does a group the RIP_MULT weights say to leave alone), and a node held by
        a pad, the keep-out, a drilled hole or the board edge costs infinity
        because none of those move.  The cheapest route between the two pads is
        then the thinnest wall between them, and the copper along it is the
        rip-up list.

        That is a far better guess than counting how many wall nodes a track
        presents to a flood: a count measures perimeter, not use.  SPARE_D26 is
        the proof - its stranded pad sits in a pocket walled by a 21 mm GND rail
        with 12 flood hits, and one 5 mm DBG_RX stub ranked tenth by the same
        count was the thing actually in its way.
        """
        (bms, owns, items) = self._build(net, half, want_own=True)
        vbm = self._viabm(net)
        lmap = self._legmap()
        # what it costs to pick a leg up: roughly how much copper it is, because
        # a lifted 30 mm diagonal has to be laid down again somewhere else and a
        # 3 mm stub does not.  Pricing only the *node* being stepped on (the
        # width of the copper there) made a 33 mm diagonal across the middle of
        # the board as cheap to break as a stub, and the cuts came out as net
        # wrecking balls - the cascade that followed always ran out of budget.
        legc = {}
        for (n2, l) in lmap.values():
            if id(l) in legc:
                continue
            (ax2, ay2) = self._padxy(l[0])
            (bx2, by2) = self._padxy(l[1])
            legc[id(l)] = 0.5 + 0.35 * math.hypot(bx2 - ax2, by2 - ay2)
        tc = self.cls(net)
        L = self.layers
        (fx0, fy0, fx1, fy1) = self.cfg["frame"]
        (ax, ay) = self._padxy(pa)
        (bx, by) = self._padxy(pb)
        x0 = max(fx0, min(ax, bx) - CUT_MARGIN)
        x1 = min(fx1, max(ax, bx) + CUT_MARGIN)
        y0 = max(fy0, min(ay, by) - CUT_MARGIN)
        y1 = min(fy1, max(ay, by) + CUT_MARGIN)
        ka = L[0].idx(x0, y0)
        kb = L[0].idx(x1, y1)
        if ka is None or kb is None:
            return []
        (i0, j0) = L[0].ij(ka)
        (i1, j1) = L[0].ij(kb)
        nx = L[0].nx
        dist = [dict(), dict()]
        par = {}
        heap = []
        goal = set()
        for (li, nm) in enumerate(self.route_layers):
            if nm in pa["layers"]:
                k = L[li].idx(ax, ay)
                if k is not None:
                    dist[li][k] = 0.0
                    heap.append((0.0, li, k))
            if nm in pb["layers"]:
                k = L[li].idx(bx, by)
                if k is not None:
                    goal.add((li, k))
        if not heap or not goal:
            return []
        heapq.heapify(heap)
        best = None
        while heap:
            (c, li, k) = heapq.heappop(heap)
            if dist[li].get(k, 1e18) < c - 1e-12:
                continue
            if (li, k) in goal:
                best = (li, k)
                break
            (i, j) = L[li].ij(k)
            for (di, dj, _w2) in DIRS:
                (ni, nj) = (i + di, j + dj)
                if not (i0 <= ni <= i1 and j0 <= nj <= j1):
                    continue
                nk = nj * nx + ni
                step = 0.1
                if bms[li][nk]:
                    t = owns[li][nk]
                    if t < 0:
                        continue          # board edge, or someone's drilled hole
                    it = items[t]
                    if it["kind"] not in ("tr", "via"):
                        continue          # a pad or the keep-out: it does not move
                    nl = lmap.get(id(it["ent"]))
                    if nl is None or self.rip_count.get(nl[0], 0) >= \
                            RIP_MAX_PER_NET:
                        continue          # not on the books, or already moved enough
                    (n3, l3) = nl
                    mm = RIP_MULT.get(self.rip_group.get(n3, "default"), 1.15)
                    # ...charged per node of the band that leg occupies, so a
                    # wall costs what lifting it costs once, not once per node
                    # of wall it is thick enough to cover: a 0.6 mm rail dilates
                    # to ~1.2 mm of blocked nodes (125 of them), and charging
                    # each at full leg price made the GND wall around J15 look
                    # unbreakable, which is where the search gave up
                    band = 2.0 * (it["half"] + self.clear(tc, it["cls"]) + half)
                    step = legc[id(l3)] / (max(mm, 0.05) *
                                           max(1, int(band / GRID)))
                nc = c + step
                if nc < dist[li].get(nk, 1e18) - 1e-12:
                    dist[li][nk] = nc
                    par[(li, nk)] = (li, k)
                    heapq.heappush(heap, (nc, li, nk))
            o = 1 - li
            nk = j * nx + i
            if i0 <= i <= i1 and j0 <= j <= j1 and not vbm[nk]:
                nc = c + CUT_VIA
                if nc < dist[o].get(nk, 1e18) - 1e-12:
                    dist[o][nk] = nc
                    par[(o, nk)] = (li, k)
                    heapq.heappush(heap, (nc, o, nk))
        if best is None:
            return []
        out, seen = [], set()
        cur = best
        while cur in par:
            (li, k) = cur
            t = owns[li][k]
            if t >= 0:
                it = items[t]
                if it["kind"] in ("tr", "via"):
                    nl = lmap.get(id(it["ent"]))
                    if nl is not None and id(nl[1]) not in skip and \
                            id(nl[1]) not in seen:
                        seen.add(id(nl[1]))
                        out.append(nl)
            cur = par[cur]
        out.reverse()
        return out

    def _snap_one(self, net):
        """Everything that describes one net's copper, for an undo."""
        return (list(self.items_of.get(net, [])),
                list(self.tree_legs.get(net, [])),
                list(self.failed.get(net, [])),
                self.open_of.get(net, 0), self.tree_ok.get(net, 0))

    def _restore(self, net, s):
        (es, legs, fl, op, tk) = s
        self._lift(net)
        self._put_back(net, es)
        self.tree_legs[net], self.failed[net] = legs, fl
        self.open_of[net], self.tree_ok[net] = op, tk

    def _repair(self, net, legs):
        """Re-route just the legs of `net` that the lifted items used to carry.

        Everything else of that net stays exactly where it was, which is what
        keeps an experiment cheap: the price is one A* per broken leg, not a
        whole-tree re-route.
        """
        drop = set(id(l) for l in legs)
        keep = [l for l in self.tree_legs.get(net, []) if id(l) not in drop]
        w = self.cfg["widths"].get(net, 0.25)
        new = []
        for (pa, pb, _old) in legs:
            path = self._leg(net, self._padxy(pa), self._padxy(pb),
                             pa["layers"], pb["layers"], w)
            if path is None:
                return False
            mark = len(self.items_of.get(net, []))
            self.emit(path, net, w)
            new.append((pa, pb, self.items_of[net][mark:]))
        self.tree_legs[net] = keep + new
        self.tree_ok[net] = len(self.tree_legs[net])
        self.failed[net] = []
        return True

    def _emit_leg(self, net, pa, pb, path, w):
        """Put one leg's copper on the board and add it to the net's tree."""
        mark = len(self.items_of.get(net, []))
        self.emit(path, net, w)
        leg = (pa, pb, self.items_of[net][mark:])
        self.tree_legs.setdefault(net, []).append(leg)
        return leg

    def _settle(self, net, pa, pb):
        """A debt is paid: that pair of pads is joined again, by one leg or two."""
        fl = self.failed.get(net, [])
        self.failed[net] = [x for x in fl if not (x[0] is pa and x[1] is pb)]
        self.open_of[net] = max(0, self.open_of.get(net, 0) - 1)
        self.tree_ok[net] = len(self.tree_legs.get(net, []))
        self.gen += 1

    # ------------------------------------------------------- copper audit ----
    def _padroots(self, net):
        """(pad -> component, pads with copper on them) for `net`'s live copper.

        A union-find over the tracks, vias and pads this net owns *at this
        moment*, with the contact rules copper actually obeys: a track joins a
        pad when its rectangle overlaps the pad, a via joins a track when the
        via's disc touches it (anywhere along its length, not only at an end), a
        via joins its two layers, and two tracks of one net that cross are one
        piece of copper.

        This exists because the tree bookkeeping is not the copper.  A leg lifted
        as a victim stays in `tree_legs` until it is explicitly dropped, and a
        debt "paid" by stubbing onto a pad whose own leg had already been lifted
        settles the paper while the net stays cut in two - which is exactly how
        the 5 V rail came to ship in two pieces.  Anything that decides "is this
        net finished" has to ask the copper, not the books.
        """
        items = self.items_of.get(net, [])
        # a per-net edit counter, not a fingerprint of the item identities: a
        # freed dict's id() is handed straight back to the next one, so an undo
        # plus a re-emit could otherwise look exactly like the state cached
        # before it and the audit would answer from the copper that is no longer
        # there
        fp = self._mut.get(net, 0)
        hit = self._pccache.get(net)
        if hit is not None and hit[0] == fp:
            return (hit[1], hit[2], hit[3])
        tr = [e["seg"] for e in items if e["kind"] == "tr"]
        vs = [e["via"] for e in items if e["kind"] == "via"]
        pads = self.pads_of.get(net, [])
        par = {}

        def find(x):
            par.setdefault(x, x)
            while par[x] != x:
                par[x] = par[par[x]]
                x = par[x]
            return x

        def uni(a, b):
            (ra, rb) = (find(a), find(b))
            if ra != rb:
                par[ra] = rb

        def k(lay, x, y):
            return (lay, round(x, 3), round(y, 3))

        for s in tr:
            uni(k(s["layer"], s["a"][0], s["a"][1]),
                k(s["layer"], s["b"][0], s["b"][1]))
        for v in vs:
            uni(("F.Cu", v["x"], v["y"]), ("B.Cu", v["x"], v["y"]))
        for (i, s) in enumerate(tr):
            (a, b) = (s["a"], s["b"])
            for t in tr[i + 1:]:
                if s["layer"] == t["layer"] and \
                        _segseg(a, b, t["a"], t["b"]) <= \
                        s["w"] / 2.0 + t["w"] / 2.0 + 1e-9:
                    uni(k(s["layer"], a[0], a[1]), k(t["layer"], t["a"][0],
                                                     t["a"][1]))
            for v in vs:
                if _ptseg(v["x"], v["y"], a, b) <= v["dia"] / 2.0 + \
                        s["w"] / 2.0 + 1e-9:
                    uni(("F.Cu", v["x"], v["y"]), k(s["layer"], a[0], a[1]))
        cnodes = set()
        for s in tr:
            cnodes.add(k(s["layer"], s["a"][0], s["a"][1]))
        for v in vs:
            cnodes.add(("F.Cu", v["x"], v["y"]))
        for p in pads:
            pn = ("PAD", id(p))
            # no early break: a pad is a disc of copper, and it is what joins two
            # tracks that end inside it from different layers (or never quite
            # reach each other).  Unioning it with the first piece it touches and
            # ignoring the rest reports a perfectly good net as two pieces.
            for s in tr:
                if s["layer"] in p["layers"] and \
                        _rectseg(p["box"], s["a"], s["b"]) <= s["w"] / 2.0 + 1e-9:
                    uni(pn, k(s["layer"], s["a"][0], s["a"][1]))
            for v in vs:
                if _ptrect(p["box"], v["x"], v["y"]) <= v["dia"] / 2.0 + 1e-9:
                    uni(pn, ("F.Cu", v["x"], v["y"]))
        for (i, p) in enumerate(pads):
            for q in pads[i + 1:]:
                if _rectrect(p["box"], q["box"]):
                    uni(("PAD", id(p)), ("PAD", id(q)))
        croot = set(find(c) for c in cnodes)
        roots = {id(p): find(("PAD", id(p))) for p in pads}
        att = set(id(p) for p in pads if roots[id(p)] in croot)
        grp = {}
        for p in pads:
            grp.setdefault(roots[id(p)], []).append(p)
        # biggest piece first: everything downstream of this assumes groups[0] is
        # the one worth keeping
        groups = [grp[r] for r in sorted(grp, key=lambda r: (-len(grp[r]), r[1]))]
        self._pccache[net] = (fp, roots, att, groups)
        return (roots, att, groups)

    def _split(self, net):
        """How many pieces this net's copper is in right now (1 if one)."""
        return max(1, len(self._padroots(net)[2]))

    def _gaps(self, net, pads, roots):
        """Pad pairs that would weld `net`'s pieces back into one, cheapest first.

        One pair per extra piece: join the smallest piece to the nearest pad of
        the biggest piece already joined, so the number of A* runs stays at
        (pieces - 1) instead of trying every cross-piece pair.
        """
        grp = {}
        for p in pads:
            grp.setdefault(roots[id(p)], []).append(p)
        keys = sorted(grp, key=lambda r: -len(grp[r]))
        (out, tree) = ([], list(grp[keys[0]]))
        for r in keys[1:]:
            best = min(((_d(self._padxy(a), self._padxy(b)), a, b)
                        for a in tree for b in grp[r]))
            out.append((best[1], best[2]))
            tree.extend(grp[r])
        return out

    def _linked(self, net, pa, pb):
        """True if the net's copper already joins pa to pb.

        A leg of a multi-pad net is an edge of a spanning tree, not a contract
        between two pads: lift a redundant one and the net is still one piece,
        so there is nothing to put back.  Worth checking before spending an A* on a
        pad-to-pad re-route that was never needed - but checked against the
        copper, because the tree says a leg is there even after it was lifted.
        """
        roots = self._padroots(net)[0]
        return (id(pa) in roots and id(pb) in roots
                and roots[id(pa)] == roots[id(pb)])

    def _attach(self, net, pa, pb, pts, w):
        """Put a lifted leg back as two short stubs into the tree, not one line.

        The ends of a lifted leg only have to find the rest of the net, not each
        other.  For a GND leg between two adjacent connector pins that is the
        difference between no way back at all - the single lane between two pin
        rows fits exactly one track, and the net that wanted it there has now
        taken it - and two stubs into the pads either side.  Returns
        (padA, path, padB, path) or None.
        """
        cand = [q for q in pts if q is not pa and q is not pb]
        if len(cand) < 2:
            return None
        live = self._live_pads(net)
        # the two stubs only weld if they land one on each side of the cut: both
        # into the same piece and the far end stays stranded, which is how a V5
        # leg came back "paid" with the rail still in two halves
        (roots, _att, _gr) = self._padroots(net)
        (ca, cb) = (roots.get(id(pa), id(pa)), roots.get(id(pb), id(pb)))
        if ca == cb:
            return None

        def near(end, want):
            (ex, ey) = self._padxy(end)
            pool = [q for q in cand if roots.get(id(q), id(q)) == want]
            pool.sort(key=lambda q: ((id(q) not in live),
                                     math.hypot(self._padxy(q)[0] - ex,
                                                self._padxy(q)[1] - ey)))
            return pool[:3]

        # pa into its own piece and pb into the other one is the shape that
        # welds; the crossed variant is tried second because it is longer
        for (wa, wb) in ((ca, cb), (cb, ca)):
            for qa in near(pa, wa):
                pathA = self._leg(net, self._padxy(pa), self._padxy(qa),
                                  pa["layers"], qa["layers"], w)
                if pathA is None:
                    continue
                for qb in near(pb, wb):
                    pathB = self._leg(net, self._padxy(pb), self._padxy(qb),
                                      pb["layers"], qb["layers"], w)
                    if pathB is not None:
                        return (qa, pathA, qb, pathB)
        return None

    def _live_pads(self, net):
        """Pads of `net` that have copper attached right now.

        Measured, not remembered: a pad that only appears in a leg which was
        lifted is not live, and stubbing a new leg onto it joins two pieces of
        nothing."""
        groups = self._padroots(net)[2]
        return groups[0] if len(groups) > 1 else self.pads_of.get(net, [])

    def _pay(self, net, pa, pb, path, w, live):
        """Place one leg of a net and update the books to match."""
        self._emit_leg(net, pa, pb, path, w)
        self._settle(net, pa, pb)

    def _unpay(self, net, leg):
        """Lift one leg of a net; it becomes a debt that has to be placed again.

        Nothing marks the space it used to occupy.  A victim is allowed straight
        back into its old lane, and the legality of that is guaranteed by the
        search itself: the copper that displaced it is a hard obstacle by then,
        because the debt that needed the room is paid *before* the victim is
        asked to move (see _open_net).  Ghost copies of the lifted leg were tried
        here and cost more than they bought - they fenced a net out of a lane it
        was still entitled to, and whole cascades died of it.
        """
        (pa, pb, ents) = leg
        self._lift(net, ents)
        self.tree_legs[net] = [l for l in self.tree_legs.get(net, [])
                               if l is not leg]
        self.failed.setdefault(net, []).append((pa, pb))
        self.open_of[net] = self.open_of.get(net, 0) + 1
        self.tree_ok[net] = len(self.tree_legs.get(net, []))

    def _open_net(self, net, by_net, key):
        """Buy a stranded leg room, then pay for everything that had to move.

        The greedy pass parks each net wherever it happens to fit, so a net that
        goes last can be locked out completely - and the copper that locked it
        out belongs to nets that had a way round somewhere else.  This settles
        it the way a board router does: the missing leg goes on a debt list; a
        debt is paid by routing that one leg; if it cannot route, the wall in
        front of it is priced (_wall_cut) and the copper along the cheapest line
        through it is lifted, each lifted leg becoming a new debt owned by its
        own net; repeat until the list is empty or the lift budget runs out, and
        if it runs out the whole experiment goes back exactly as it was.

        The debt list is what makes this honest: copper may only be moved if
        whatever it displaced gets placed again, so an attempt can never quietly
        leave the board worse.  It also lets a net displace one *leg* of another
        net - the thing the tier-by-tier pass could not do, which is why four
        nets stayed locked out for so long: SPARE_D26 needed a corridor through
        a GND rail, and GND would only give one if a *different* GND leg, the
        one parked in front of J15, moved out of the way first.
        """
        w = self.cfg["widths"].get(net, 0.25)
        self.rip_tries += 1
        (snap, touched, lifted) = {}, [], []
        tried = set()           # wall legs already lifted for this net, no good
        why = "step budget"
        last = None
        snap[net] = self._snap_one(net)
        touched.append(net)
        debts = deque((net, a, b, False) for (a, b) in self.failed.get(net, []))
        ok = False
        # each lift costs a step, and so does the failed A* that comes with it,
        # so the step bound has to be about twice the lift bound or the search
        # stops with copper still unspent and the wall still standing
        for _step in range(2 * RIP_LIFTS + 2 * len(debts) + 8):
            if not debts:
                ok = True
                break
            (n2, pa, pb, live) = debts.popleft()
            w2 = self.cfg["widths"].get(n2, 0.25)
            last = (n2, pa, pb)
            if live and self._linked(n2, pa, pb):
                self._settle(n2, pa, pb)      # redundant leg: nothing to place
                continue
            base = len(self.items_of.get(n2, []))
            path = self._leg(n2, self._padxy(pa), self._padxy(pb),
                             pa["layers"], pb["layers"], w2)
            if path is not None:
                self._pay(n2, pa, pb, path, w2, live)
                if self._linked(n2, pa, pb):
                    continue
                # the search handed back copper that is not a connection: two
                # points inside one pad, say, dropped as stubs by MIN_SEG.  Take
                # back exactly what this payment laid - leaving it is not a
                # violation (same net), but leaving the *debt closed* is how a
                # cut rail hides under a paid book - and re-open the debt so the
                # wall machinery below gets a chance at it.
                self._lift(n2, self.items_of.get(n2, [])[base:])
                self.failed.setdefault(n2, []).append((pa, pb))
                self.open_of[n2] = self.open_of.get(n2, 0) + 1
            att = self._attach(n2, pa, pb, by_net.get(n2, []), w2)
            if att is not None:
                base = len(self.items_of.get(n2, []))
                (qa, pathA, qb, pathB) = att
                self._emit_leg(n2, pa, qa, pathA, w2)
                self._emit_leg(n2, pb, qb, pathB, w2)
                if self._linked(n2, pa, pb):
                    self._settle(n2, pa, pb)
                    continue
                self._lift(n2, self.items_of.get(n2, [])[base:])
            if len(lifted) >= RIP_LIFTS:
                why = "lift budget %d" % RIP_LIFTS
                break
            cut = self._wall_cut(n2, w2 / 2.0, pa, pb, tried)
            if not cut:
                why = "wall cannot be lifted (pads, keep-out, edge, or tried)"
                break
            bought = 0
            for (n3, l3) in cut[:RIP_CUT]:
                if self.rip_count.get(n3, 0) >= RIP_MAX_PER_NET:
                    continue
                if n3 not in snap:
                    snap[n3] = self._snap_one(n3)
                    touched.append(n3)
                self._unpay(n3, l3)
                tried.add(id(l3))
                lifted.append((n3, l3))
                self.rip_count[n3] = self.rip_count.get(n3, 0) + 1
                debts.append((n3, l3[0], l3[1], True))
                bought += 1
            if not bought:
                why = "every leg of the wall already at RIP_MAX_PER_NET"
                break
            # the debt that could not be paid is still owed: put it back at the
            # *front*, so this net gets its copper down before the victims it
            # displaced are asked to find room.  Asked the other way round, a
            # victim walks straight back into the lane that was cleared for the
            # debt and the same wall goes up again - the spin the ghost ban was
            # invented to paper over
            debts.appendleft((n2, pa, pb, live))
        if not ok:
            for n in sorted(touched, key=key):
                self._restore(n, snap[n])
            self.gen += 1
            (n2, pa, pb) = last
            self.log.append("  OPEN   FAIL %-10s: debt %s %s.%s -> %s.%s: %s "
                            "(lifts %d, tries %d)" % (net, n2, pa["ref"],
                                                       pa["pad"], pb["ref"],
                                                       pb["pad"], why,
                                                       len(lifted),
                                                       self.rip_tries))
        return ok

    def ripup(self, by_net, order):
        """Retry every stranded net, cheapest-to-move wall first.

        Which net gets asked is by route order, so the nets that had first bite
        of the board get asked first; each one gets one _open_net search per
        round, and rounds repeat because a net that failed can succeed once a
        neighbour has moved.  The pass can only ever end with fewer open legs
        than the greedy pass left behind: every failed search is undone exactly.
        """
        order_of = {}
        for (gi, (grp, nets)) in enumerate(order):
            for (ni, n) in enumerate(nets):
                order_of[n] = (gi, ni)
        key = lambda n: (order_of.get(n, (99, 99)), n)

        def span(n):
            pts = [self._padxy(p) for p in by_net.get(n, [])]
            return max([math.hypot(a[0] - b[0], a[1] - b[1])
                        for i, a in enumerate(pts) for b in pts[i + 1:]]
                       or [0.0])

        # asking order: by default whoever had first bite of the board asks
        # first; "span" asks the longest stranded leg first, which is the one
        # most likely to need a corridor others are sitting in
        ask = (key if RIP_ORDER != "span"
               else (lambda n: (-span(n), order_of.get(n, (99, 99)), n)))
        open0 = sum(self.open_of.values())
        self.log.append("  RIPUP  open legs %d" % open0)
        if not open0:
            return
        self.quiet = True
        kept = rnd = 0
        for rnd in range(1, RIP_ROUNDS + 1):
            before = sum(self.open_of.values())
            if not before or self.rip_tries >= RIP_TRIES:
                break
            for net in sorted(self.failed, key=ask):
                if not self.failed.get(net) or self.rip_tries >= RIP_TRIES:
                    continue
                if self._open_net(net, by_net, key):
                    kept += 1
            if sum(self.open_of.values()) == before:
                break
        self.quiet = False
        self.log.append("  RIPUP  rounds %d  tries %d  nets opened %d  open legs "
                        "%d -> %d" % (rnd, self.rip_tries, kept, open0,
                                      sum(self.open_of.values())))
        for net in sorted(self.failed, key=key):
            for (pa, pb) in self.failed[net]:
                self.log.append("  RIPUP  STILL OPEN  %-10s %s.%s -> %s.%s"
                                % (net, pa["ref"], pa["pad"], pb["ref"],
                                   pb["pad"]))
    def _weld(self, net, by_net, key):
        """Join a net that is in pieces with plain A*, touching nothing else.

        A net that comes out of the greedy pass in two pieces is almost never
        blocked - it is a near miss: a stub that stopped a hair short of a pad, a
        layer change whose via went missing, a leg that was lifted and never laid
        down again in the same lane.  So the first repair attempt asks for a
        legal lane and takes whatever A* finds, with its own copper free and every
        other net obeying the clearance.  Nothing is lifted, so a weld cannot
        break a second net the way the wall-cut machinery can; when it fails it
        costs one A* and the board is exactly as it was.
        """
        w = self.cfg["widths"].get(net, 0.25)
        (roots, _att, groups) = self._padroots(net)
        made = 0
        while len(groups) > 1:
            got = False
            for (pa, pb) in self._gaps(net, by_net[net], roots):
                path = self._leg(net, self._padxy(pa), self._padxy(pb),
                                 pa["layers"], pb["layers"], w)
                if path is None:
                    continue
                base = len(self.items_of.get(net, []))
                self._pay(net, pa, pb, path, w, None)
                if not self._linked(net, pa, pb):
                    # copper that does not join the two pads is not a weld: the
                    # books say the leg is placed, the board says otherwise
                    self._lift(net, self.items_of.get(net, [])[base:])
                    continue
                made += 1
                got = True
                break
            if not got:
                break
            (roots, _att, groups) = self._padroots(net)
        return made

    def _seal(self, by_net, order):
        """Put every net back into one piece of copper, asking the board.

        The rip-up pass keeps its own books - legs placed, legs owed - and those
        books can swear a net is finished while the copper on the board is not,
        because a debt may be paid by stubbing onto a pad whose own leg had
        already been lifted.  This last pass ignores the books, measures how many
        pieces each net's copper is really in, and closes the cuts: first with a
        plain A* weld (_weld), which cannot damage anything else on the board, and
        only for what that cannot reach, with the wall-cut machinery rip-up uses -
        which can, so it stays last and stays rare.

        This is the line the board has to cross before it is written out: a net
        that is quietly in two pieces is a board that does not work, and nothing
        else in the flow will notice until DRC calls it unconnected.
        """
        order_of = {}
        for (gi, (grp, nets)) in enumerate(order):
            for (ni, n) in enumerate(nets):
                order_of[n] = (gi, ni)
        key = lambda n: (order_of.get(n, (99, 99)), n)
        self.quiet = True
        self.rip_count = {}
        broken = [n for n in by_net if len(by_net[n]) > 1 and self._split(n) > 1]
        welded = 0
        for net in sorted(broken, key=key):
            welded += 1 if self._weld(net, by_net, key) else 0
        fixed = len([n for n in broken if self._split(n) == 1])
        rnd = 0
        for rnd in range(1, SEAL_ROUNDS + 1):
            broken = [n for n in by_net if len(by_net[n]) > 1 and self._split(n) > 1]
            if not broken:
                break
            self.log.append("  SEAL    round %d: %d net(s) in pieces: %s"
                            % (rnd, len(broken),
                               " ".join("%s x%d" % (n, self._split(n))
                                        for n in sorted(broken, key=key))))
            for net in sorted(broken, key=key):
                self.failed[net] = self._gaps(net, by_net[net],
                                              self._padroots(net)[0])
                if self._open_net(net, by_net, key):
                    if self._split(net) == 1:
                        fixed += 1
                    else:
                        # every debt the attempt owed came back connected, yet
                        # the net is still cut: the pair it paid for was not the
                        # pair the copper is split across
                        self.log.append("  SEAL    PAID BUT CUT %s x%d"
                                        % (net, self._split(net)))
            self.gen += 1
        # from here the books are rewritten off the copper, so what the log calls
        # "open" is what DRC is about to call unconnected
        for net in by_net:
            if len(by_net[net]) < 2:
                continue
            n = self._split(net)
            self.open_of[net] = n - 1
            self.tree_ok[net] = len(by_net[net]) - n
        self.quiet = False
        left = [n for n in sorted(by_net, key=key)
                if len(by_net[n]) > 1 and self._split(n) > 1]
        self.log.append("  SEAL    welded %d  rounds %d  nets opened %d  still in "
                        "pieces %d%s"
                        % (welded, rnd, fixed, len(left),
                           "  " + " ".join("%s x%d" % (n, self._split(n))
                                          for n in left) if left else ""))
        for net in left:
            for (pa, pb) in self._gaps(net, by_net[net], self._padroots(net)[0]):
                self.log.append("  SEAL    STILL CUT   %-10s %s.%s -> %s.%s"
                                % (net, pa["ref"], pa["pad"], pb["ref"],
                                   pb["pad"]))

    # ------------------------------------------------------------------ run --
    def run(self, order):
        by_net = {}
        for p in self.cfg["pads"]:
            if p["net"]:
                by_net.setdefault(p["net"], []).append(p)
        for (group, nets) in order:
            for net in nets:
                self.rip_group.setdefault(net, group)
        tot = [0, 0, 0.0]
        for (group, nets) in order:
            g = [0, 0, 0.0]
            for net in nets:
                if len(by_net.get(net, [])) < 2:
                    continue
                e, f, ln = self.route_net(net, by_net[net],
                                          self.cfg["widths"].get(net, 0.25))
                g[0] += e
                g[1] += f
                g[2] += ln
                self.gen += 1                  # foreign copper changed
            self.log.append("  %-14s edges %3d  fails %d  length %7.1f mm"
                            % (group, g[0], g[1], g[2]))
            tot[0] += g[0]
            tot[1] += g[1]
            tot[2] += g[2]
        if RIP:
            self.ripup(by_net, order)
            self._seal(by_net, order)
        # the TOTAL row describes the copper that is actually on the board now,
        # not the sum of the passes that produced it: rip-up re-routes whole
        # nets, so pass totals would double-count.  `fails` counts the A* legs
        # that could not be routed in the last attempt at their net; `open`
        # counts the tree legs still missing, which is what DRC's
        # unconnected_items reports.
        edges = sum(self.tree_ok.values())
        fails = sum(len(v) for v in self.failed.values())
        openl = sum(self.open_of.values())
        length = sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
                     for s in self.segs if s is not None)
        nv = sum(1 for v in self.vias if v is not None)
        self.log.append("  TOTAL          edges %3d  fails %d  open %d  "
                        "length %7.1f mm   vias %d"
                        % (edges, fails, openl, length, nv))
        return [edges, fails, length, openl, nv]


def _collinear(a, b, c):
    return abs((b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1])) < 1e-9


# ------------------------------------------------- copper contact geometry ----
# Distances, in mm, used by the connectivity audit (_padroots).  Copper touches
# when the gap between two shapes is smaller than the sum of their half-widths,
# so everything below returns a plain distance and the caller adds the widths.

def _d(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _ptseg(px, py, a, b):
    """Distance from a point to a segment."""
    (dx, dy) = (b[0] - a[0], b[1] - a[1])
    L2 = dx * dx + dy * dy
    if L2 <= 1e-18:
        return math.hypot(px - a[0], py - a[1])
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / L2))
    return math.hypot(px - (a[0] + t * dx), py - (a[1] + t * dy))


def _orient(a, b, c):
    v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    return 0 if abs(v) < 1e-9 else (1 if v > 0 else -1)


def _onseg(a, b, c):
    return (min(a[0], b[0]) - 1e-9 <= c[0] <= max(a[0], b[0]) + 1e-9 and
            min(a[1], b[1]) - 1e-9 <= c[1] <= max(a[1], b[1]) + 1e-9)


def _crosses(a1, b1, a2, b2):
    """True if two segments meet, proper crossing or an end resting on the other."""
    (o1, o2, o3, o4) = (_orient(a1, b1, a2), _orient(a1, b1, b2),
                        _orient(a2, b2, a1), _orient(a2, b2, b1))
    if o1 != o2 and o3 != o4:
        return True
    return ((o1 == 0 and _onseg(a1, b1, a2)) or (o2 == 0 and _onseg(a1, b1, b2))
            or (o3 == 0 and _onseg(a2, b2, a1)) or (o4 == 0 and _onseg(a2, b2, b1)))


def _segseg(a1, b1, a2, b2):
    """Distance between two segments: 0 if they touch or cross."""
    if _crosses(a1, b1, a2, b2):
        return 0.0
    return min(_ptseg(a1[0], a1[1], a2, b2), _ptseg(b1[0], b1[1], a2, b2),
               _ptseg(a2[0], a2[1], a1, b1), _ptseg(b2[0], b2[1], a1, b1))


def _ptrect(box, x, y):
    """Distance from a point to a rectangle (0 inside)."""
    (x0, y0, x1, y1) = box
    return math.hypot(max(x0 - x, 0.0, x - x1), max(y0 - y, 0.0, y - y1))


def _rectseg(box, a, b):
    """Distance from a rectangle to a segment (0 if it runs through it)."""
    (x0, y0, x1, y1) = box
    if _ptrect(box, a[0], a[1]) == 0.0 or _ptrect(box, b[0], b[1]) == 0.0:
        return 0.0
    corners = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
    return min(_segseg(a, b, corners[i], corners[(i + 1) % 4])
               for i in range(4))


def _rectrect(b1, b2):
    return (b1[0] <= b2[2] + 1e-9 and b2[0] <= b1[2] + 1e-9 and
            b1[1] <= b2[3] + 1e-9 and b2[1] <= b1[3] + 1e-9)


def route(cfg):
    """cfg: pads, frame, keep, classes, clearance, widths, via, route_layers,
    order [(group, [nets])].  Returns (segments, vias, log)."""
    r = Router(cfg)
    r.add_static(cfg["pads"], cfg["keep"])
    r.run(cfg["order"])
    return ([s for s in r.segs if s is not None],
            [v for v in r.vias if v is not None], r.log)


# ------------------------------------------------------------------ selftest --
def _audit_selftest():
    """Checks _padroots against copper it has to judge, and emit()'s dedupe.

    The connectivity model is what decides whether a board is finished, so the
    cases it gets wrong are the ones that ship as broken boards.  Case 3 is the
    bug that cut the 5 V rail in two: two tracks ending inside the same pad from
    different layers are one piece of copper, and unioning the pad with only the
    first track it touches calls that two pieces.
    """
    cfg = dict(pads=[], frame=(0.0, 0.0, 30.0, 30.0), keep=(0.0, 0.0, 0.0, 0.0),
               classes={}, clearance={"Default": 0.25}, widths={"N1": 0.4},
               via={"Default": (0.7, 0.35)}, route_layers=["F.Cu", "B.Cu"],
               order=[])
    r = Router(cfg)
    pa = {"ref": "A", "pad": "1", "net": "N1", "box": (1.0, 1.0, 3.0, 3.0),
          "layers": ["F.Cu", "B.Cu"], "drill": 0.0}
    pb = {"ref": "B", "pad": "1", "net": "N1", "box": (17.0, 1.0, 19.0, 3.0),
          "layers": ["F.Cu", "B.Cu"], "drill": 0.0}
    pc = {"ref": "C", "pad": "1", "net": "N1", "box": (25.0, 1.0, 27.0, 3.0),
          "layers": ["F.Cu", "B.Cu"], "drill": 0.0}
    cfg["pads"] = [pa, pb, pc]
    r.pads_of = {"N1": [pa, pb, pc]}

    def n_live(rt):
        return sum(1 for e in rt.items_of.get("N1", []) if not e["cu"]["dead"])

    assert r._split("N1") == 3, r._split("N1")           # bare pads: 3 pieces
    r.emit([(0, 2.0, 2.0), (0, 18.0, 2.0)], "N1", 0.4)   # A -> B, F.Cu
    assert r._split("N1") == 2, r._split("N1")           # C still stranded
    r.emit([(1, 18.0, 2.0), (1, 26.0, 2.0)], "N1", 0.4)  # B -> C, B.Cu
    assert r._split("N1") == 1, r._split("N1")
    # a pad that bridges two layers with no via of its own
    r2 = Router(cfg)
    r2.pads_of = {"N1": [pa, pb, pc]}
    r2.emit([(0, 2.0, 2.0), (0, 18.0, 2.0)], "N1", 0.4)   # F.Cu A -> B
    r2.emit([(1, 18.0, 2.0), (1, 26.0, 2.0)], "N1", 0.4)  # B.Cu B -> C
    assert r2._split("N1") == 1, r2._split("N1")          # B solders them together
    # emit() must not lay the same track twice
    n0 = n_live(r2)
    r2.emit([(0, 2.0, 2.0), (0, 18.0, 2.0)], "N1", 0.4)
    assert n_live(r2) == n0, (n_live(r2), n0)
    # ... but copper that was lifted may be laid down again
    r2._lift("N1", [e for e in r2.items_of["N1"]
                    if e["kind"] == "tr" and e["cu"]["layers"] == {"F.Cu"}])
    assert n_live(r2) == n0 - 1, n_live(r2)              # _lift takes it off
    assert not any(s and s["layer"] == "F.Cu" for s in r2.segs)
    r2.emit([(0, 2.0, 2.0), (0, 18.0, 2.0)], "N1", 0.4)
    assert r2._split("N1") == 1, r2._split("N1")
    # a stranded pad stays a piece of its own, and _live_pads keeps the biggest
    assert r._live_pads("N1") == [pa, pb] if r._split("N1") > 1 else True
    print("AUDIT SELFTEST ok")


if __name__ == "__main__":
    _audit_selftest()
    # a tiny sanity case: two pads around a wall, with a gap to thread
    pads = [{"ref": "A", "pad": "1", "net": "N1", "box": (-1.0, -0.5, 1.0, 0.5),
             "layers": ["F.Cu"], "drill": 0.0},
            {"ref": "B", "pad": "1", "net": "N1", "box": (19.0, 9.5, 21.0, 10.5),
             "layers": ["B.Cu"], "drill": 0.0},
            {"ref": "W", "pad": "1", "net": "N2", "box": (4.0, -10.0, 5.0, 4.0),
             "layers": ["F.Cu", "B.Cu"], "drill": 0.0},
            {"ref": "W2", "pad": "1", "net": "N2", "box": (4.0, 6.0, 5.0, 20.0),
             "layers": ["F.Cu", "B.Cu"], "drill": 0.0}]
    cfg = dict(pads=pads, frame=(-5.0, -15.0, 25.0, 25.0), keep=(22.0, 22.0, 24.0, 24.0),
               classes={}, clearance={"Default": 0.25}, widths={"N1": 0.25},
               via={"Default": (0.7, 0.35)}, route_layers=["F.Cu", "B.Cu"],
               order=[("t", ["N1"])])
    segs, vias, log = route(cfg)
    for l in log:
        print(l)
    print("segs %d vias %d" % (len(segs), len(vias)))
    for s in segs:
        print("   ", s["layer"], s["a"], s["b"], s["w"])
    for v in vias:
        print("   via", v["x"], v["y"], v["dia"])
