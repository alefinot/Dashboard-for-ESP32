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

GRID = 0.10          # mm, node spacing
SLACK = 0.00         # mm, half a cell + margin (see above)
EDGE = 0.40          # mm, copper to board outline.  The project constraint is
                     # 0.372 because the JST shells' pads already sit 0.40 mm
                     # from the outline, and JLC's own limit is 0.2 (internal)
                     # / 0.3 (outer); 0.40 is what this board can actually use.
VIA_COST = 4.5       # cost of one layer change, in grid units (= 0.9 mm of track)
TURN = 0.06          # cost of a direction change, in grid units
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

    def grow(self, bm, x0, y0, x1, y1, d):
        """Block every node inside the rectangle [x0,x1]x[y0,y1] grown by d.

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
    def _maps(self, net, half, via_r=None):
        """Blocked bitmap per layer for one task (net + track half width, or for
        a via of radius via_r).  Cached until foreign copper changes."""
        key = (net, half, via_r)
        if getattr(self, "_ck", None) == key and \
                getattr(self, "_cg", -1) == self.gen:
            return self._cm
        tc = self.cls(net)
        extra = half if via_r is None else via_r      # radius of the moving thing
        inset = EDGE + extra + SLACK
        bms = []
        for L in self.layers:
            bm = bytearray(L.n)
            for it in self.static + self.copper:
                if L.name not in it["layers"] or it["net"] == net:
                    continue
                L.grow(bm, it["a"][0], it["a"][1], it["b"][0], it["b"][1],
                       self.clear(tc, it["cls"]) + extra + it["half"])
            fx0, fy0, fx1, fy1 = self.cfg["frame"]
            L.block_ring(bm, fx0 + inset, fy0 + inset, fx1 - inset, fy1 - inset)
            if via_r is not None:
                # drilled holes: a via's hole must clear every other hole, and
                # its copper must clear foreign drilled pads' copper too
                vd = self.cfg["via"][tc][1]
                for (hx, hy, dr, hnet) in self.holes:
                    # hole-to-hole and hole-to-foreign-copper are fab limits and
                    # apply whatever the net is: this is also what keeps a via
                    # from landing inside its own net's through-hole pad
                    need = vd / 2.0 + dr / 2.0 + max(HOLE_MIN, COPPER_HOLE)
                    L.grow(bm, hx, hy, hx, hy, need)
            bms.append(bm)
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
                self.segs.append({"net": net, "layer": lay[a[0]],
                                  "a": (ax, ay), "b": (bx, by), "w": width})
                self.copper.append({"kind": "tr", "a": (ax, ay), "b": (bx, by),
                                    "half": width / 2.0, "net": net,
                                    "cls": self.cls(net), "layers": {lay[a[0]]}})
                total += ln
            else:
                vd, vdr = self.cfg["via"][self.cls(net)]
                self.vias.append({"net": net, "x": a[1], "y": a[2],
                                  "dia": vd, "drill": vdr, "layer": lay[a[0]]})
                self.copper.append({"kind": "via", "a": (a[1] - vd / 2, a[2] - vd / 2),
                                    "b": (a[1] + vd / 2, a[2] + vd / 2),
                                    "half": 0.0, "net": net, "cls": self.cls(net),
                                    "layers": {l.name for l in self.layers}})
                # a via drills a hole: it joins the hole list so a later via (or a
                # via near this one's own net) keeps the fab's hole-to-hole
                # spacing, which a copper-only dilation does NOT guarantee
                # (0.25 mm of copper clearance between two 0.7 mm vias leaves a
                # 0.25 mm gap between their holes, not 0.5 mm)
                self.holes.append((a[1], a[2], vdr, net))
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
        """Kruskal over that net's pads (shortest pair first), A* per edge."""
        pts = [((p["box"][0] + p["box"][2]) / 2.0,
                (p["box"][1] + p["box"][3]) / 2.0, p) for p in pads_of_net]
        if len(pts) < 2:
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
        for (d, i, j) in pairs:
            a, b = find(i), find(j)
            if a == b:
                continue
            path = self.astar(net, width / 2.0, pts[i][:2], pts[j][:2],
                              pts[i][2]["layers"], pts[j][2]["layers"])
            if path is None:
                fails += 1
                self.log.append("  UNROUTABLE  %-10s %s.%s -> %s.%s  %.2f mm straight"
                                % (net, pts[i][2]["ref"], pts[i][2]["pad"],
                                   pts[j][2]["ref"], pts[j][2]["pad"], d))
                continue
            # a path that doubles back between layers parks two of its own holes
            # too close: block around the earlier one and try again
            for _ in range(3):
                vd, vdr = self.cfg["via"][self.cls(net)]
                hot = self._tight_via_pair(path, net)
                if hot is None:
                    break
                self.holes.append((hot[0], hot[1], vdr, net))
                self.gen += 1
                path = self.astar(net, width / 2.0, pts[i][:2], pts[j][:2],
                                  pts[i][2]["layers"], pts[j][2]["layers"])
                if path is None:
                    break
            if path is None:
                fails += 1
                continue
            length += self.emit(path, net, width)
            edges += 1
            par[a] = b
        return (edges, fails, length)

    # ------------------------------------------------------------------ run --
    def run(self, order):
        by_net = {}
        for p in self.cfg["pads"]:
            if p["net"]:
                by_net.setdefault(p["net"], []).append(p)
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
        self.log.append("  TOTAL          edges %3d  fails %d  length %7.1f mm"
                        "   vias %d" % (tot[0], tot[1], tot[2], len(self.vias)))
        return tot


def _collinear(a, b, c):
    return abs((b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1])) < 1e-9


def route(cfg):
    """cfg: pads, frame, keep, classes, clearance, widths, via, route_layers,
    order [(group, [nets])].  Returns (segments, vias, log)."""
    r = Router(cfg)
    r.add_static(cfg["pads"], cfg["keep"])
    tot = r.run(cfg["order"])
    return r.segs, r.vias, r.log


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
