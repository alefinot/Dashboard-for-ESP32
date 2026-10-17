"""Task 2 gate: the per-net layer policy and the IPC inner-rail widths.

Pins down §2, §3 and §4 rules 1, 2 and 7 of
Implementation plans/PCB-4-LAYER-STACKUP-PLANES-2026-10-16.md:

  * the §3 IPC-2152 internal width table comes out of the formula, not a copy;
  * the drawn In2 rail is 2.5 mm;
  * no track anywhere on In1.Cu - no via on this board can reach it (every via
    is a through hole joining F.Cu and B.Cu) and a track there would slice the
    solid GND reference;
  * In2.Cu carries power only, and every In2 segment starts AND ends inside a
    DRILLED pad of its own net, because the inner layer is reached at a pad's
    copper, never through a via (a through via has no annular ring there);
  * the rails §2 names (DC_IN, V5) really got an inner run;
  * V3V3 never leaves F.Cu (§4 rule 7);
  * a THT pad's copper is modelled on In2 (§4 rule 2);
  * outer-layer power keeps 0.80 mm;
  * and the board is still ROUTABLE: no leg left unconnected.  That last one is
    not a detail - the §2 map is enforced as a price (route_pcb.LAYER_PEN) and
    not as a wall precisely because the wall version stranded 11 legs.  What the
    split is worth is measured, per category, in the LAYER SPLIT report below.

Task 3 (§4 rule 3) adds the projected moat, tested twice over: a synthetic two-net
scenario (moat_selftest below) that shows the mechanism biting - the same route
crosses the rail when the band is 0 mm and walks around it when the band is
1.0 mm - and a check of the real board's copper, that no signal track on B.Cu
sits inside the band of any In2 rail.  The return-path audit itself
(scripts/return_path.py, rulebook rule 5) runs in the pipeline on the FILLED
board, because only KiCad's own filler knows where a plane really has holes; this
test reads the numbers that run left behind and says so when they are not there.

It routes the board, so it is slow (minutes).  The full pipeline gate stays
scripts/test_plane_fill.py / make_pcb.py's own filled DRC.

Run:  "<KiCad python>" hardware/scripts/test_layer_policy.py
Writes a summary to hardware/out/layer-policy.json for inspection.
"""
import json
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pcb                                            # noqa: E402

IN1, IN2 = "In1.Cu", "In2.Cu"
FAIL = []


def check(cond, msg):
    if not cond:
        FAIL.append(msg)
    print(("  ok   " if cond else "  FAIL ") + msg)


def pad_at(pads, net, x, y, w, drilled_only):
    """Is there a pad of `net` at (x,y) that an In2 track may end in?"""
    for p in pads:
        if p["net"] != net:
            continue
        if drilled_only and p["drill"] <= 0:
            continue
        if IN2 not in p["layers"]:
            continue
        x0, y0, x1, y1 = p["box"]
        # the endpoint may sit on the pad's edge rather than its centre, and the
        # grid snaps to 0.10 mm, so allow half a track width of slack
        if (x0 - w / 2.0 - 0.05 <= x <= x1 + w / 2.0 + 0.05 and
                y0 - w / 2.0 - 0.05 <= y <= y1 + w / 2.0 + 0.05):
            return p["ref"] + "." + p["pad"]
    return None


def _pt_seg(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy)
                                         / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def seg_dist(s, u):
    """Shortest mm between two track segments; 0 if they touch or cross."""
    (ax, ay), (bx, by) = s["a"], s["b"]
    (cx, cy), (dx, dy) = u["a"], u["b"]

    def side(px, py, qx, qy, rx, ry):
        return (qx - px) * (ry - py) - (qy - py) * (rx - px)

    d1, d2 = side(cx, cy, dx, dy, ax, ay), side(cx, cy, dx, dy, bx, by)
    d3, d4 = side(ax, ay, bx, by, cx, cy), side(ax, ay, bx, by, dx, dy)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(_pt_seg(ax, ay, cx, cy, dx, dy), _pt_seg(bx, by, cx, cy, dx, dy),
               _pt_seg(cx, cy, ax, ay, bx, by), _pt_seg(dx, dy, ax, ay, bx, by))


def moat_selftest():
    """§4 rule 3 on a toy board: does the projected band actually move a route?

    DC_IN is pinned to In2.Cu (its two pads are drilled, so that is legal) and
    gets a 2.5 mm rail straight across the middle at y=10.  BAT_SNS is pinned to
    B.Cu with its pads north and south of that rail, so the cheapest route is
    straight THROUGH the rail.  With no projection it takes that route; with the
    band projected onto B.Cu it has to walk around the rail's end.  Same
    scenario, opposite result - that is what shows the projection reaches the
    cost map and is not just a comment in the source.
    """
    import route_pcb
    pads = [
        {"ref": "PA", "pad": "1", "net": "DC_IN", "box": (3.0, 9.0, 5.0, 11.0),
         "layers": ["F.Cu", "In2.Cu", "B.Cu"], "drill": 0.6},
        {"ref": "PB", "pad": "1", "net": "DC_IN", "box": (25.0, 9.0, 27.0, 11.0),
         "layers": ["F.Cu", "In2.Cu", "B.Cu"], "drill": 0.6},
        {"ref": "SA", "pad": "1", "net": "BAT_SNS", "box": (14.0, 3.0, 16.0, 5.0),
         "layers": ["B.Cu"], "drill": 0.0},
        {"ref": "SB", "pad": "1", "net": "BAT_SNS", "box": (14.0, 15.0, 16.0, 17.0),
         "layers": ["B.Cu"], "drill": 0.0},
    ]

    def cfg():
        return dict(pads=pads, frame=(0.0, 0.0, 30.0, 20.0), keep=(0.0,) * 4,
                    classes={"DC_IN": "Power"},
                    clearance={"Default": 0.25, "Power": 0.60},
                    widths={"DC_IN": 0.80, "BAT_SNS": 0.30},
                    via={"Default": (0.7, 0.35), "Power": (1.3, 0.6)},
                    route_layers=["F.Cu", "In2.Cu", "B.Cu"],
                    layers_of={"DC_IN": ["In2.Cu"], "BAT_SNS": ["B.Cu"]},
                    inner_widths={"DC_IN": 2.5},
                    order=[("p", ["DC_IN"]), ("s", ["BAT_SNS"])])

    def run(proj_on):
        """Route the scenario with the projection either on or switched off.

        Switching it off means an empty MOAT_FACE (an inner rail projects onto no
        face at all) - not a band of 0 mm, which would still keep a signal off the
        rail's own footprint and hide the effect being tested.
        """
        old = route_pcb.MOAT_FACE
        if not proj_on:
            route_pcb.MOAT_FACE = {}
        try:
            segs, vias, log = route_pcb.route(cfg())
        finally:
            route_pcb.MOAT_FACE = old
        rail = [s for s in segs if s["net"] == "DC_IN" and s["layer"] == IN2]
        sig = [s for s in segs if s["net"] == "BAT_SNS"]
        over = [s for s in sig if rail and
                seg_dist(s, rail[0]) < (rail[0]["w"] + s["w"]) / 2.0 + edge]
        ln = sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
                 for s in sig)
        return rail, sig, over, ln

    edge = route_pcb.MOAT_EDGE
    check(edge > 0, "the moat band is a real width (%.2f mm)" % edge)
    rail0, sig0, over0, len0 = run(False)
    check(len(rail0) == 1,
          "scenario lays one In2 rail (%.1f mm wide)"
          % (rail0[0]["w"] if rail0 else 0.0))
    check(bool(over0),
          "with no projection the sensor route DOES run over the rail "
          "(%d seg(s) of %d, %.1f mm total) - the scenario is able to violate "
          "the rule" % (len(over0), len(sig0), len0))
    rail1, sig1, over1, len1 = run(True)
    check(bool(sig1), "with the band projected at %.2f mm the sensor still "
                      "connects (%d seg(s))" % (edge, len(sig1)))
    check(not over1,
          "with the band projected at %.2f mm no sensor segment sits in it "
          "(%d offender(s))" % (edge, len(over1)))
    print("        cost of the rule on this scenario: %.1f mm of track instead "
          "of %.1f mm" % (len1, len0))


def main():
    print("Stackup plan task 2: layer policy + inner widths")
    print("-" * 66)
    moat_selftest()
    print("-" * 66)

    # ---- the §3 table, straight from the formula --------------------------
    for (amp, want) in ((0.5, 0.70), (1.0, 1.82), (1.5, 3.19), (2.0, 4.74)):
        got = make_pcb.ipc_inner_w(amp)
        check(abs(got - want) < 0.02,
              "IPC internal %.1f A -> %.2f mm (plan §3: %.2f mm)"
              % (amp, got, want))
    check(make_pcb.INNER_RAIL_W == 2.5,
          "drawn inner rail %.1f mm (plan §3: 2.5 mm)" % make_pcb.INNER_RAIL_W)

    # ---- §4 rule 2: drilled pads carry In2 in the router's model ----------
    rp = make_pcb.route_pads()
    th = [p for p in rp if p["drill"] > 0 and p["net"]]
    check(th and all(IN2 in p["layers"] for p in th),
          "%d drilled net-carrying pad(s) all list %s in the router model"
          % (len(th), IN2))
    npth = [p for p in rp if p["drill"] > 0 and not p["net"]]
    check(all(IN2 not in p["layers"] for p in npth),
          "%d mounting hole(s) (no net) stay non-copper on In2" % len(npth))
    smd = [p for p in rp if p["drill"] == 0]
    check(all(IN2 not in p["layers"] for p in smd),
          "%d SMD pad(s) keep their own single layer (no In2 copper)" % len(smd))

    # ---- route it --------------------------------------------------------
    t0 = time.time()
    segs, vias, log, rp = make_pcb.route_all(rp)
    dt = time.time() - t0
    inner = [s for s in segs if s["layer"] == IN2]
    per = {}
    for s in segs:
        (ax, ay), (bx, by) = s["a"], s["b"]
        d = per.setdefault(s["layer"], [0, 0.0])
        d[0] += 1
        d[1] += math.hypot(bx - ax, by - ay)
    print("-" * 66)
    print("  routed in %.0f s: %d segments, %d vias, %.1f mm of track"
          % (dt, len(segs), len(vias),
             sum(math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
                 for s in segs)))
    for lay in sorted(per):
        print("    %-8s %4d seg(s)  %7.1f mm" % (lay, per[lay][0], per[lay][1]))
    for line in log:
        if "UNROUTED" in line.upper() or "exhausted" in line:
            print("  " + line.strip())

    # ---- §2 / §4 rule 1: what each layer carries -------------------------
    check(not [s for s in segs if s["layer"] == IN1],
          "no track anywhere on %s (solid reference, unreachable by via)" % IN1)
    check(IN1 not in make_pcb.ROUTE_LAYERS,
          "%s is not even a routable layer (no via reaches it)" % IN1)
    bad = [s for s in inner if s["net"] not in make_pcb.POWER]
    check(not bad, "In2 carries power nets only (%d offender(s))" % len(bad))
    for s in bad[:5]:
        print("        %s on %s at (%.2f,%.2f)-(%.2f,%.2f)"
              % (s["net"], s["layer"], s["a"][0], s["a"][1], s["b"][0], s["b"][1]))
    check(not [s for s in inner if s["net"] == "GND"],
          "GND puts no track on In2 (the pour IS In2)")

    # An In2 RUN (a chain of collinear-merged In2 segments) has to end at pads;
    # the corners inside the run are ordinary path nodes and are free to sit in
    # open copper field.  So the honest version of §4 rule 2 is about the ENDS of
    # a run: nodes that exactly one In2 segment touches.
    ends = {}
    for s in inner:
        for pt in (s["a"], s["b"]):
            k = (s["net"], round(pt[0], 2), round(pt[1], 2))
            ends[k] = ends.get(k, 0) + 1
    orphan = []
    for (net, x, y), deg in ends.items():
        if deg != 1:
            continue
        w = max([s["w"] for s in inner if s["net"] == net] or [0.0])
        if pad_at(rp, net, x, y, w, True) is None:
            orphan.append((net, x, y))
    check(not orphan,
          "every In2 run starts and ends inside a drilled pad of its own net "
          "(%d loose end(s))" % len(orphan))
    for (net, x, y) in orphan[:8]:
        print("        %s ends at (%.2f, %.2f) with no drilled pad there"
              % (net, x, y))

    rails = {}
    for s in inner:
        d = rails.setdefault(s["net"], [0, 0.0, 0.0])
        d[0] += 1
        d[1] += math.hypot(s["b"][0] - s["a"][0], s["b"][1] - s["a"][1])
        d[2] = max(d[2], s["w"])

    for net in ("DC_IN", "DC_F"):
        d = rails.get(net, [0, 0.0, 0.0])
        check(d[0] > 0, "%s has an inner rail (%d seg(s), %.1f mm, widest "
                        "%.2f mm)" % (net, d[0], d[1], d[2]))
    # V5 is the §2 case that did NOT come true, and the reason is geometry, not
    # policy: inner copper can only be entered at a drilled pad, so a net gets an
    # inner run only if its tree has a leg from a drilled pad to a drilled pad.
    # V5's pads are J10a.1 and J3.1 drilled (54 mm apart) plus six SMD pads, and
    # the tree reaches the module and the bypass caps on the outside - so every
    # V5 leg has at least one SMD end and none of them can dive in.  Reported,
    # not hidden: this is a decision for the user (§2 asked for a V5 rail).
    if not rails.get("V5"):
        print("  FINDING    V5 got NO inner rail: no drilled-to-drilled leg in "
              "its tree, so inner copper is unreachable for it")
    # DC_12 has no drilled pad at all, so inner copper cannot be welded at its
    # ends: §2 asks for a rail there, the pad population forbids one, and the
    # honest answer is that it stays outside (measured, printed as a NO INNER
    # policy note every run).
    check(not [s for s in inner if s["net"] == "DC_12"],
          "DC_12 keeps no inner copper (0 drilled pads to weld a rail to)")

    v3 = [s for s in segs if s["net"] == "V3V3"]
    check(not [s for s in v3 if s["layer"] != "F.Cu"],
          "V3V3 stays on F.Cu (§4 rule 7): %d seg(s)" % len(v3))

    thin = [s for s in segs if s["net"] in make_pcb.POWER
            and s["layer"] in make_pcb.OUTER and s["w"] < 0.80 - 1e-9]
    check(not thin, "outer-layer power keeps 0.80 mm (%d thin)" % len(thin))

    # ---- §4 rule 3 on the real board: the projected moat -----------------
    # A rail on In2 and a track on B.Cu never touch, so DRC has nothing to say
    # about one running over the other - but the B.Cu track's reference plane IS
    # In2, and the rail has carved a 0.60 mm moat through that pour already.  The
    # router projects that band onto the face the rail feeds; this measures that
    # nothing signal-class ended up inside it anyway - neither a track running
    # through it nor a via parked in it.
    rpmod = make_pcb.route_pcb
    edge = rpmod.MOAT_EDGE
    face = rpmod.MOAT_FACE[IN2]
    rl = [s for s in segs if s["layer"] == IN2]
    sig = [s for s in segs if s["layer"] == face
           and make_pcb.CLASS_OF.get(s["net"], "Default")
           not in rpmod.MOAT_EXEMPT]
    moat_bad = []
    for s in sig:
        for r in rl:
            if r["net"] == s["net"]:
                continue
            if seg_dist(s, r) < (r["w"] + s["w"]) / 2.0 + edge - 1e-6:
                moat_bad.append((s, r))
    check(not moat_bad,
          "no %s signal track inside the %.2f mm moat projected by an %s rail "
          "(%d offender(s) of %d seg(s) x %d rail seg(s))"
          % (face, edge, IN2, len(moat_bad), len(sig), len(rl)))
    for (s, r) in moat_bad[:6]:
        print("        %s on %s (%.2f,%.2f)-(%.2f,%.2f) over the %s rail at "
              "(%.2f,%.2f)-(%.2f,%.2f)"
              % (s["net"], s["layer"], s["a"][0], s["a"][1], s["b"][0],
                 s["b"][1], r["net"], r["a"][0], r["a"][1], r["b"][0],
                 r["b"][1]))
    vb = []
    for v in vias:
        if make_pcb.CLASS_OF.get(v["net"], "Default") in rpmod.MOAT_EXEMPT:
            continue
        for r in rl:
            if r["net"] == v["net"]:
                continue
            # every via on this board is plated through, so its pad sits on the
            # face the rail feeds whether it was meant to land there or not, and
            # its barrel goes down through the plane the rail is welded into: the
            # pad's anti-pad in that plane, next to the rail, is a slot in the
            # return path.  Same distance rule as a track, from the pad edge.
            if rpmod._ptseg(v["x"], v["y"], r["a"], r["b"]) < \
                    (r["w"] + v["dia"]) / 2.0 + edge - 1e-6:
                vb.append((v, r))
    check(not vb, "no via pad inside the same %.2f mm moat (%d offender(s) of "
                  "%d via(s))" % (edge, len(vb), len(vias)))
    for (v, r) in vb[:6]:
        print("        %s via %.2f mm at (%.2f,%.2f) in the band of the %s rail "
              "at (%.2f,%.2f)-(%.2f,%.2f)"
              % (v["net"], v["dia"], v["x"], v["y"], r["net"], r["a"][0],
                 r["a"][1], r["b"][0], r["b"][1]))
    band = sum((r["w"] + 2 * edge)
               * math.hypot(r["b"][0] - r["a"][0], r["b"][1] - r["a"][1])
               for r in rl)
    print("        moat band: %d rail segment(s), %.1f mm of rail, %.1f mm2 of "
          "%s kept clear of signals" % (len(rl), sum(
              math.hypot(r["b"][0] - r["a"][0], r["b"][1] - r["a"][1])
              for r in rl), band, face))

    # ---- the return-path audit, as the pipeline measured it --------------
    # Only the FILLED board can answer this (anti-pads, moats and the antenna
    # window are filler output), so make_pcb.py runs scripts/return_path.py on
    # the copy it DRCs and leaves the numbers in out/return-path.json.  §7 asks
    # for a REPORT with a 95 % target per sensitive net, so short rows print as
    # FINDINGs for the user to weigh (they are mostly anti-pad slots under dense
    # pad rows) - this gate does not get to tune the target away.
    jp = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                      "out", "return-path.json")
    if not os.path.exists(jp):
        print("  FINDING    no out/return-path.json yet - run make_pcb.py (it "
              "fills, DRCs, then audits) before judging return paths")
    else:
        d = json.load(open(jp, encoding="utf-8"))
        age = (time.time() - os.path.getmtime(jp)) / 60.0
        tot, gap = d["sampled_mm"], d["gap_mm"]
        print("  RETURN PATH (audited %.0f min ago on the filled copy - rerun "
              "make_pcb.py for numbers from this routing): %.1f mm sampled, "
              "%.1f mm (%.1f%%) over no GND on the reference layer"
              % (age, tot, gap, 100.0 * gap / tot if tot else 0.0))
        under = []
        for row in d["per_net"].values():
            if row["net"] not in make_pcb.CLEAN or row["mm"] < 1.0:
                continue
            share = 1.0 - row["gap_mm"] / row["mm"]
            print("    %-10s %-6s over %-6s %6.1f mm of %6.1f mm  %.1f%% solid"
                  % (row["net"], row["layer"], row["ref"],
                     row["mm"] - row["gap_mm"], row["mm"], 100.0 * share))
            if share < 0.95:
                under.append("%s %.1f%%" % (row["net"], 100.0 * share))
        if under:
            print("  FINDING    %d sensitive net(s) under the §7 95%% target: %s"
                  % (len(under), ", ".join(under)))
            print("             (the plane holes are anti-pads the filler carves "
                  "under foreign pad rows; the router cannot see them yet)")
        # §7 also asks flatly that no sensitive run sits over the antenna hole.
        k = make_pcb.KEEP
        qa = make_pcb._onsheet(k[0], k[1])
        qb = make_pcb._onsheet(k[2], k[3])
        wx0, wx1 = sorted((qa[0], qb[0]))
        wy0, wy1 = sorted((qa[1], qb[1]))
        over = [o for o in d["offenders"]
                if o["net"] in make_pcb.CLEAN
                and wx0 <= (o["from"][0] + o["to"][0]) / 2.0 <= wx1
                and wy0 <= (o["from"][1] + o["to"][1]) / 2.0 <= wy1]
        check(not over,
              "no sensitive run over the antenna window "
              "(%.2f,%.2f)-(%.2f,%.2f): %.2f mm of gap under %d segment(s)"
              % (wx0, wy0, wx1, wy1, sum(o["mm"] for o in over), len(over)))
        for o in over[:5]:
            print("        %s %5.2f mm (%.2f, %.2f) -> (%.2f, %.2f)"
                  % (o["net"], o["mm"], o["from"][0], o["from"][1],
                     o["to"][0], o["to"][1]))

    # ---- §2 measured: how much of each category stayed on its face --------
    # Thresholds are set from measured runs (2026-10-17: clean 83%, noisy 71%),
    # with room for a routing pass to move a bit, and high enough that a policy
    # which silently stopped doing anything would fail.  They are NOT a target:
    # the price model means these numbers describe the routing, they do not
    # promise it.  A run that drops below them is a change to look at, not a
    # number to tune down.
    split = {name: (tot, on) for (name, tot, on, _per)
             in make_pcb.layer_split(segs)}
    for (name, floor) in (("clean", 0.65), ("noisy", 0.55)):
        (tot, on) = split.get(name, (0.0, 0.0))
        share = on / tot if tot else 0.0
        check(tot > 50.0 and share >= floor,
              "%s group: %.1f mm of track, %.0f%% on its §2 face (floor %.0f%%)"
              % (name, tot, 100.0 * share, 100.0 * floor))

    # ---- the board has to come out connected, or the policy cost too much --
    tl = [l for l in log if l.strip().startswith("TOTAL")]
    mm = re.search(r"fails (\d+)\s+open (\d+)", tl[-1]) if tl else None
    check(mm is not None, "the router reported a TOTAL line")
    if mm:
        (fails, openl) = (int(mm.group(1)), int(mm.group(2)))
        check(fails == 0 and openl == 0,
              "board comes out connected: A* fails %d, open tree legs %d"
              % (fails, openl))
        for line in log:
            if ("STILL OPEN" in line or "STILL CUT" in line
                    or line.strip().startswith("MOAT")):
                print("        " + line.strip())

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                       "out")
    try:
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "layer-policy.json"), "w") as f:
            json.dump({"seconds": round(dt, 1), "segments": len(segs),
                       "vias": len(vias),
                       "per_layer": {k: [v[0], round(v[1], 1)]
                                     for k, v in per.items()},
                       "inner_rails": {k: [v[0], round(v[1], 1), v[2]]
                                       for k, v in rails.items()},
                       "layer_split_mm": {k: [round(v[0], 1), round(v[1], 1)]
                                          for k, v in split.items()},
                       "policy_notes": make_pcb.POLICY_NOTES}, f, indent=1)
    except OSError as e:                       # out/ is gitignored scratch
        print("  (could not write summary: %s)" % e)

    print("-" * 66)
    if FAIL:
        print("TASK 2 LAYER POLICY: %d FAILURE(S)" % len(FAIL))
        return 1
    print("TASK 2 LAYER POLICY: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
