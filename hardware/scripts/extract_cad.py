#!/usr/bin/env python3
"""Re-extract the vector geometry of hardware/screenshots/Dashboard++ PCB with
dimensions.pdf into hardware/cad/<name>.geo.json.

The PDF is a paper-space print of an AutoCAD layout: one 595.2 x 841.89 pt page
holding three views of the carrier board, stacked vertically and labelled
'FRONT VIEW' (elevation), 'TOP VIEW' (F.Cu, display side) and 'BOTTOM VIEW'
(B.Cu, ESP32 side) in layer 'Quote'.  Each plan view is a real 85.00 x 55.01 mm
outline, so the sheet scale is recovered from that outline and every item is
reported in the drawing's own millimetre frame:

    u = mm east from the view outline's west edge   (sheet x, grows east)
    v = mm south from the view outline's north edge (sheet y, grows south)

KiCad conversion (KiCad origin at the board's NW corner, x east, y south,
BOARD_X0/Y0 = 10/10 in make_pcb.py):

    as-drawn : x = BOARD_X0 + u          y = BOARD_Y0 + v
    mirrored : x = BOARD_X0+BOARD_X1 - u - ... i.e. x' = 105 - (10 + u)

The three views share one u frame (see hardware/cad/layout_notes.md): the DC-DC
module, JST zones and XT30 sit at identical u in the BOTTOM view and the
elevation, and the display header sits at identical u in the TOP view and the
elevation.  The drawing is therefore NOT face-mirrored.

Usage:  python extract_cad.py [pdf] [out.json]
"""
import json
import math
import sys
import os

import pymupdf

HERE = os.path.dirname(os.path.abspath(__file__))
DEF_PDF = os.path.join(HERE, os.pardir, "screenshots",
                       "Dashboard++ PCB with dimensions.pdf")
DEF_OUT = os.path.join(HERE, os.pardir, "cad",
                       "Dashboard++ PCB with dimensions.geo.json")

TOL = 0.05          # mm, clustering tolerance for connected line segments


def _pts(item):
    k = item[0]
    if k == "re":
        r = item[1]
        return [(r.x0, r.y0), (r.x1, r.y0), (r.x1, r.y1), (r.x0, r.y1)]
    if k == "qu":
        q = item[1]
        return [(q.ul.x, q.ul.y), (q.ur.x, q.ur.y),
                (q.lr.x, q.lr.y), (q.ll.x, q.ll.y)]
    if k == "l":
        return [(item[1].x, item[1].y), (item[2].x, item[2].y)]
    if k == "c":
        return [(p.x, p.y) for p in item[1:5]]
    return []


def extract(pdf_path):
    doc = pymupdf.open(pdf_path)
    page = doc[0]
    drawings = page.get_drawings()
    spans = [s for b in page.get_text("dict")["blocks"]
             for l in b.get("lines", []) for s in l["spans"]]

    # ---- scale from a known dimension: the DevKit body is 50.00 x 28.00 ---
    boxes = []
    for dr in drawings:
        for it in dr["items"]:
            p = _pts(it)
            if not p:
                continue
            xs, ys = [q[0] for q in p], [q[1] for q in p]
            boxes.append((min(xs), min(ys), max(xs), max(ys)))
    scale = None
    for (x0, y0, x1, y1) in boxes:
        w, h = x1 - x0, y1 - y0
        if 180 < w < 195 and 100 < h < 110:            # 50 x 28 mm in points
            scale = w / 50.0
            break
    if scale is None:                                   # fall back on 85 x 55
        for (x0, y0, x1, y1) in boxes:
            w, h = x1 - x0, y1 - y0
            if w > 300 and abs(w / h - 85.0 / 55.0) < 0.02:
                scale = w / 85.0
                break
    if scale is None:
        raise SystemExit("could not recover the sheet scale")

    # ---- board outlines: axis-aligned 85 x 55 quads in layer '0' ----------
    outlines = []
    edge = []
    for dr in drawings:
        for it in dr["items"]:
            if it[0] not in ("qu", "re"):
                continue
            p = _pts(it)
            xs, ys = [q[0] for q in p], [q[1] for q in p]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            w, h = (x1 - x0) / scale, (y1 - y0) / scale
            if abs(w - 85.0) < 1.0 and abs(h - 55.0) < 1.0:
                outlines.append((x0, y0, x1, y1))
            elif abs(w - 85.0) < 1.0 and abs(h - 2.41) < 0.2:
                edge.append((x0, y0, x1, y1))   # the carrier seen edge-on

    # ---- name the views --------------------------------------------------
    labels = [(s["text"].strip(), s["origin"]) for s in spans
              if s["text"].strip() in ("TOP VIEW", "BOTTOM VIEW", "FRONT VIEW")]
    views = {}
    for o in outlines:
        name = "PLAN?"
        for txt, org in labels:
            # a view label sits directly above its view on the sheet
            if o[1] - 60 <= org[1] <= o[1] and o[0] - 30 <= org[0] <= o[2] + 30:
                name = {"TOP VIEW": "TOP", "BOTTOM VIEW": "BOTTOM"}.get(txt, name)
        views[name] = o
    if edge:      # elevation origin = top face of the carrier (F.Cu), u shared
        e = edge[0]
        views["ELEV"] = (e[0], e[1], e[2], e[1])

    def view_of(cx, cy):
        best, bd = None, 1e9
        for name, o in views.items():
            dx = max(o[0] - cx, 0, cx - o[2])
            dy = max(o[1] - cy, 0, cy - o[3])
            d = math.hypot(dx, dy) / scale
            if d < bd:
                best, bd = name, d
        return best if bd <= 25.0 else None

    def frame(name):
        o = views[name]
        return (o[0], o[1], scale)        # u = (x - x0)/S, v = (y - y0)/S

    out = {"pdf": os.path.basename(pdf_path), "scale_pt_per_mm": scale,
           "views": {}}
    for name, o in views.items():
        x0, y0 = o[0], o[1]
        buckets = {"outlines": [], "parts": [], "zones": [], "lines": [],
                   "quotes": [], "text": []}
        segs = []
        for dr in drawings:
            lay = dr["layer"]
            for it in dr["items"]:
                p = _pts(it)
                if not p:
                    continue
                cx = sum(q[0] for q in p) / len(p)
                cy = sum(q[1] for q in p) / len(p)
                if view_of(cx, cy) != name:
                    continue
                xs, ys = [q[0] for q in p], [q[1] for q in p]
                bb = (min(xs), min(ys), max(xs), max(ys))
                w, h = (bb[2] - bb[0]) / scale, (bb[3] - bb[1]) / scale
                rec = {"layer": lay, "seq": dr["seqno"], "kind": it[0],
                       "u0": round((bb[0] - x0) / scale, 3),
                       "v0": round((bb[1] - y0) / scale, 3),
                       "u1": round((bb[2] - x0) / scale, 3),
                       "v1": round((bb[3] - y0) / scale, 3),
                       "w": round(w, 3), "h": round(h, 3)}
                if it[0] in ("qu", "re"):
                    if (w > 80 and h > 50) or (w > 80 and h < 5):
                        buckets["outlines"].append(rec)
                    elif lay in ("Devices", "Connectors") and min(w, h) >= 0.9:
                        buckets["parts"].append(rec)
                    else:
                        buckets["quotes"].append(rec)
                elif it[0] == "l":
                    segs.append((lay, dr["seqno"],
                                 ((p[0][0] - x0) / scale, (p[0][1] - y0) / scale),
                                 ((p[1][0] - x0) / scale, (p[1][1] - y0) / scale)))
                else:
                    buckets["quotes"].append(rec)

        # cluster line segments into rectangles (the dashed annotation zones)
        parent = list(range(len(segs)))

        def find(i):
            while parent[i] != i:
                parent[i] = parent[parent[i]]
                i = parent[i]
            return i

        def near(a, b):
            return abs(a[0] - b[0]) < TOL and abs(a[1] - b[1]) < TOL

        for i in range(len(segs)):
            for j in range(i + 1, len(segs)):
                ai, bi = segs[i][2], segs[i][3]
                aj, bj = segs[j][2], segs[j][3]
                if any(near(p, q) for p in (ai, bi) for q in (aj, bj)):
                    ri, rj = find(i), find(j)
                    if ri != rj:
                        parent[ri] = rj
        groups = {}
        for i, s in enumerate(segs):
            groups.setdefault(find(i), []).append(s)
        for g in groups.values():
            pts = [p for _, _, a, b in g for p in (a, b)]
            u0 = min(p[0] for p in pts); u1 = max(p[0] for p in pts)
            v0 = min(p[1] for p in pts); v1 = max(p[1] for p in pts)
            rec = {"layer": g[0][0], "nseg": len(g),
                   "u0": round(u0, 3), "v0": round(v0, 3),
                   "u1": round(u1, 3), "v1": round(v1, 3),
                   "w": round(u1 - u0, 3), "h": round(v1 - v0, 3)}
            if rec["layer"] == "Quote":
                buckets["quotes"].append(rec)
            elif min(rec["w"], rec["h"]) < 0.3 and len(g) < 3:
                buckets["lines"].append(rec)     # extension / dimension lines
            else:
                buckets["lines"].append(rec)     # bracket groups rebuilt below

        # the dashed annotation rectangles are drawn as four corner brackets:
        # rebuild them from diagonal bracket pairs whose other two corners exist
        brs = [z for z in buckets["lines"] if z["layer"] == "0" and z["nseg"] >= 2]
        have = {(round(z["u0"], 1), round(z["v0"], 1)) for z in brs}
        for a in brs:
            for b in brs:
                if b["u0"] < a["u1"] - 0.1 or b["v0"] < a["v1"] - 0.1:
                    continue
                c = (round(b["u0"], 1), round(a["v0"], 1))
                d = (round(a["u0"], 1), round(b["v0"], 1))
                if c not in have or d not in have:
                    continue
                w = b["u1"] - a["u0"]
                h = b["v1"] - a["v0"]
                if w < 10 or h < 10:
                    continue
                # require real (dashed) segments along all four edges, so that
                # corner combinations that are not actually drawn get dropped
                def cover(horizontal, at, lo, hi, span):
                    tot = 0.0
                    for lay, _, p, q in segs:
                        if lay != "0":            # dimension lines don't count
                            continue
                        if horizontal:
                            if abs(p[1] - at) < 0.06 and abs(q[1] - at) < 0.06 \
                                    and min(p[0], q[0]) > lo - 0.1 \
                                    and max(p[0], q[0]) < hi + 0.1:
                                tot += abs(p[0] - q[0])
                        else:
                            if abs(p[0] - at) < 0.06 and abs(q[0] - at) < 0.06 \
                                    and min(p[1], q[1]) > lo - 0.1 \
                                    and max(p[1], q[1]) < hi + 0.1:
                                tot += abs(p[1] - q[1])
                    return tot / span

                if min(cover(True, a["v0"], a["u0"], b["u1"], w),
                       cover(True, b["v1"], a["u0"], b["u1"], w),
                       cover(False, a["u0"], a["v0"], b["v1"], h),
                       cover(False, b["u1"], a["v0"], b["v1"], h)) < 0.5:
                    continue

                # reject unions: a fully drawn line through the interior means
                # this is two neighbouring zones, not one rectangle
                hkeys = {round((p[1] + q[1]) / 2, 2) for lay, _, p, q in segs
                         if lay == "0" and abs(p[1] - q[1]) < 0.02}
                vkeys = {round((p[0] + q[0]) / 2, 2) for lay, _, p, q in segs
                         if lay == "0" and abs(p[0] - q[0]) < 0.02}
                split = any(cover(True, k, a["u0"], b["u1"], w) >= 0.5
                            for k in hkeys if a["v0"] + 0.5 < k < b["v1"] - 0.5)
                split = split or any(cover(False, k, a["v0"], b["v1"], h) >= 0.5
                                     for k in vkeys if a["u0"] + 0.5 < k < b["u1"] - 0.5)
                if split:
                    continue
                buckets["zones"].append({"layer": "0-dashed", "nseg": 4,
                                         "u0": round(a["u0"], 3), "v0": round(a["v0"], 3),
                                         "u1": round(b["u1"], 3), "v1": round(b["v1"], 3),
                                         "w": round(w, 3), "h": round(h, 3)})
        for s in spans:
            cx, cy = s["origin"]
            if view_of(cx, cy) != name:
                continue
            buckets["text"].append({"text": s["text"], "size": round(s["size"], 2),
                                    "u": round((cx - x0) / scale, 2),
                                    "v": round((cy - y0) / scale, 2)})
        out["views"][name] = {"outline_mm": [round((o[2] - o[0]) / scale, 2),
                                             round((o[3] - o[1]) / scale, 2)],
                              "sheet_bbox_pt": [round(v, 2) for v in o],
                              **buckets}

    # text that sits outside every view (view labels, title block)
    stray = [s["text"].strip() for s in spans if view_of(*s["origin"]) is None]
    out["sheet_text"] = sorted(set(stray))
    return out


if __name__ == "__main__":
    pdf = sys.argv[1] if len(sys.argv) > 1 else DEF_PDF
    dst = sys.argv[2] if len(sys.argv) > 2 else DEF_OUT
    data = extract(pdf)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=True)
    print("wrote", dst)
    for name, v in data["views"].items():
        print("  %-9s outline %s mm  parts=%d zones=%d quotes=%d text=%d" %
              (name, v["outline_mm"], len(v["parts"]), len(v["zones"]),
               len(v["quotes"]), len(v["text"])))
