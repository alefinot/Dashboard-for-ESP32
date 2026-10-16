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
RIP_TIERS = (1, 2, 4, 8)   # victim nets a single retry may pick up
RIP_ROUNDS = 3       # passes over the still-open nets
RIP_TRIES = 40       # cap on experiments per run (runtime guard)
RIP_MAX_PER_NET = 3  # times one net may be picked up per run (anti-oscillation)
RIP_MULT = {"power": 0.0, "gnd": 0.12, "hv": 0.6, "analog": 1.0,
            "tft+gps": 1.0, "spares": 1.3, "debug": 1.3, "default": 1.15}
#   how a victim is scored: frontier nodes it owns x this.  The rails are worth
#   their corridors (power is never a candidate: 0.0 means never), ground is a
#   last resort, and the short signal nets are cheap to pick up and put back.
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
                if it.get("dead") or L.name not in it["layers"] or it["net"] == net:
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
                total += ln
            else:
                vd, vdr = self.cfg["via"][self.cls(net)]
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
        live = self.items_of.get(net, [])
        if ents is None:
            saved, live = live, []
        else:
            gone = set(id(e) for e in ents)
            saved = [e for e in live if id(e) in gone]
            live = [e for e in live if id(e) not in gone]
        for e in saved:
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
        """Re-register copper _lift() took (same objects, refreshed indices)."""
        for e in saved:
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

    def _victims(self, net, half, legs):
        """Foreign tracks / vias ranked by how much of this net's wall they own.

        Pads and the keep-out own walls too, but they do not move, so only routed
        copper is a candidate, and a group scored 0.0 is never one.  What a
        candidate costs is one A* for its owner - the one leg that item carried,
        see _repair - whichever net it belongs to, so even the ground tree is
        fair game here: picking up one 0.60 mm rail is not re-routing 38 legs.
        Ties break on net then position, so a run is reproducible.
        """
        counts, items = self._frontier(net, half, legs)
        out = []
        for (t, c) in counts.items():
            it = items[t]
            if it["kind"] not in ("tr", "via"):
                continue
            n2 = it["net"]
            if self.rip_count.get(n2, 0) >= RIP_MAX_PER_NET:
                continue
            m = RIP_MULT.get(self.rip_group.get(n2, "default"), 1.15)
            if m <= 0.0:
                continue
            out.append((c * m, n2, it["ent"]))
        out.sort(key=lambda t: (-t[0], t[1], t[2]["cu"]["a"][0],
                                t[2]["cu"]["a"][1]))
        return out

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

    def ripup(self, by_net, order):
        """Retry every stranded net by lifting a few foreign tracks out of it.

        For each open net: rank the foreign tracks that own its wall (frontier),
        lift the top 1 / 2 / 3 / 5 / 8 of them, re-route the stranded net through
        the gap, then re-route the one leg each lifted track used to carry.  The
        attempt is kept only if every net came out whole; otherwise all the copper
        goes back exactly as it was, so the run can only ever end with fewer open
        legs than the greedy pass left behind.
        """
        order_of = {}
        for (gi, (grp, nets)) in enumerate(order):
            for (ni, n) in enumerate(nets):
                order_of[n] = (gi, ni)
        key = lambda n: (order_of.get(n, (99, 99)), n)
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
            for net in sorted(self.failed, key=key):
                if not self.failed.get(net) or self.rip_tries >= RIP_TRIES:
                    continue
                w = self.cfg["widths"].get(net, 0.25)
                ranked = self._victims(net, w / 2.0, self.failed[net])
                if not ranked:
                    self.log.append("  RIPUP  no candidate wall for %s" % net)
                    continue
                for k in RIP_TIERS:
                    if self.rip_tries >= RIP_TRIES:
                        break
                    cand = ranked[:k]
                    mark = tuple(sorted((n2, tuple(e["cu"]["a"]),
                                         tuple(e["cu"]["b"]))
                                        for (_s, n2, e) in cand))
                    if (net, mark) in self.rip_done:
                        continue
                    self.rip_done.add((net, mark))
                    self.rip_tries += 1
                    owners = sorted(set(n2 for (_s, n2, _e) in cand))
                    for n2 in owners:
                        self.rip_count[n2] = self.rip_count.get(n2, 0) + 1
                    # a candidate item is only the *reason* to touch a net: what
                    # actually gets lifted is every item of each leg it carries,
                    # because a leg is repaired as a whole.  Lifting one track of
                    # an L-shaped leg would leave the other half (and its via) as
                    # a stub - DRC calls that via_dangling.
                    cids = set(id(e) for (_s, _n2, e) in cand)
                    broken = dict(
                        (n2, [l for l in self.tree_legs.get(n2, [])
                              if any(id(e) in cids for e in l[2])])
                        for n2 in owners)
                    # snapshot everything the experiment can touch, so a failed
                    # attempt is undone to the last track end
                    snap = {}
                    for n2 in owners + [net]:
                        snap[n2] = (list(self.items_of.get(n2, [])),
                                    list(self.tree_legs.get(n2, [])),
                                    list(self.failed.get(n2, [])),
                                    self.open_of.get(n2, 0),
                                    self.tree_ok.get(n2, 0))
                    for n2 in owners:
                        self._lift(n2, [e for l in broken[n2] for e in l[2]])
                    self._lift(net)
                    self.route_net(net, by_net[net], w)
                    good = not self.failed.get(net)
                    if good:
                        for n2 in sorted(owners, key=key):
                            if not self._repair(n2, broken[n2]):
                                good = False
                                break
                    if good:
                        kept += 1
                        self.log.append("  RIPUP  round %d  %-10s through %s"
                                        % (rnd, net, "+".join(owners)))
                        break
                    for n2 in owners + [net]:     # bought nothing: put it back
                        (es, legs, fl, op, tk) = snap[n2]
                        self._lift(n2)
                        self._put_back(n2, es)
                        self.tree_legs[n2], self.failed[n2] = legs, fl
                        self.open_of[n2], self.tree_ok[n2] = op, tk
            if sum(self.open_of.values()) == before:
                break
        self.quiet = False
        self.log.append("  RIPUP  rounds %d  tries %d  nets helped %d  open legs "
                        "%d -> %d" % (rnd, self.rip_tries, kept, open0,
                                      sum(self.open_of.values())))
        for net in sorted(self.failed, key=key):
            for (pa, pb) in self.failed[net]:
                self.log.append("  RIPUP  STILL OPEN  %-10s %s.%s -> %s.%s"
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


def route(cfg):
    """cfg: pads, frame, keep, classes, clearance, widths, via, route_layers,
    order [(group, [nets])].  Returns (segments, vias, log)."""
    r = Router(cfg)
    r.add_static(cfg["pads"], cfg["keep"])
    r.run(cfg["order"])
    return ([s for s in r.segs if s is not None],
            [v for v in r.vias if v is not None], r.log)


# ------------------------------------------------------------------ selftest --
if __name__ == "__main__":
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
