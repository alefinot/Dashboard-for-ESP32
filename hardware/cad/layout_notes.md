# Drawing geometry notes — `Dashboard++ PCB with dimensions.pdf`

Machine-extracted with [`../scripts/extract_cad.py`](../scripts/extract_cad.py)
→ [`Dashboard++ PCB with dimensions.geo.json`](Dashboard++%20PCB%20with%20dimensions.geo.json).
Reads the PDF's vector layer (no OCR, no text-layer guessing), so every number
below is a drawn dimension, not a transcription.

Scale recovered from the DevKit body (drawn 50.00 × 28.00 mm): **3.7368 pt/mm**.

## Views and their frames

| View | Label above it on the sheet | Board outline drawn |
|---|---|---|
| `TOP` (upper) | `TOP VIEW` | 85.00 × 55.01, layer `0` |
| `BOTTOM` (lower) | `BOTTOM VIEW` | 85.00 × 55.01, layer `0` |
| `ELEV` (front) | `FRONT VIEW` | 85.00 × **2.41**, layer `0` |

`u` = mm right from the view outline's west edge, `v` = mm down from its north
edge. All three views start at the same sheet x (158.40 pt), so **they share one
`u` frame**:

* DevKit `u 8.80..58.80` — identical in `BOTTOM` and `ELEV`
* DC-DC module `u 61.75..82.75` — identical in `BOTTOM` and `ELEV`
* JST zones `u 8.80..32.82`, XT30 `u 72.77..82.75` — identical in `BOTTOM`/`ELEV`
* Display header `u 1.99..2.99` — identical in `TOP` and `ELEV`

The drawing therefore does **not** face-mirror the two plan views; it is a
single layout seen from one side (the `ELEV` puts the display header above the
board and the DevKit / module / JSTs / XT30 below it). Applying
`x = 10 + u` or `x = 95 − u` to *all* views is a free choice that only mirrors
the whole board left↔right.

## `BOTTOM` view — parts drawn (the parts that hang below the carrier)

| Object (layer) | Drawn size | u0..u1 | v0..v1 |
|---|---|---|---|
| DevKit body (`Devices`) | 50.00 × 28.00 | 8.80..58.80 | 13.49..41.49 |
| DevKit header strip, silks `J10b` (`Connectors`) | 36.99 × 1.00 | 12.81..49.81 | 13.81..14.80 |
| DevKit header strip, silks `J10a` (`Connectors`) | 36.99 × 1.00 | 12.81..49.81 | 40.21..41.20 |
| DC-DC step-down module (`Devices`) | 21.00 × 43.00 | 61.75..82.75 | 8.64..51.64 |
| XT30 `J1` (`Connectors`) | 9.99 × 5.01 | 72.77..82.75 | 1.48..6.49 |
| JST-PH zone, silks `J17, J5` | 12.01 × 7.00 | 8.80..20.81 | 0.00..7.00 |
| JST-PH zone, silks `J14, J16` | 12.01 × 7.00 | 20.81..32.82 | 0.00..7.00 |
| JST-PH zone, silks `J6, J7` | 12.01 × 7.00 | 8.80..20.81 | 48.01..55.01 |
| JST-PH zone, silks `J9` | 12.01 × 7.00 | 20.81..32.82 | 48.01..55.01 |
| GNSS `J8` pad zone (`Connectors`) | 22.99 × 7.00 | 35.81..58.80 | 0.00..7.00 |
| `JP1` box (`Connectors`) | 6.00 × 3.50 | 34.65..40.66 | 49.74..53.24 |

Text inside the drawing (verbatim): `JST-PH` / `2mm 4pin male` (×4),
`J17, J5`, `J14, J16`, `J6, J7`, `J9`, `GNSS (J8)` / `6 solder pads for
proprietary connector`, `XT-30 (J1)`, `DC-DC STEP DOWN MODULE`, `ESP32`,
`ANTENNA SIDE` (u 13.42), `USB SIDE` (u 54.91), `JP1` / `DIP SWITCH` (u 36.5,
v 51-53, i.e. right under the JP1 box), `J10a` / `J10b` (v 40.94 / 14.55).

Module polarity silks: `−` at u 63.46 and `in +` at u 70.84 on the **north**
(v 11.91) end; `−` at u 64.16 and `out +` at u 71.64 on the **south** (v 50.90)
end → IN at the north end, `+` on the west side (u 61.75 side), OUT at the south
end with the same west `+`.

Drawn gaps: `8,81` (west edge → DevKit body), `4` + `9` (body edge → header
strip ends), `28` (body height), `50` (body width), `43`/`21` (module),
`2,24` (module east edge → board edge), `2,16` (XT30 east edge → board edge),
`5` (XT30 depth), `7` (JST depth), `12` (JST width), `3,01` (JST zone →
GNSS zone gap), `1,85`.

**DevKit row spacing as drawn = 26.40 mm** (strip centre lines v 14.305 and
40.705). The real DevKit V1 is 25.40 → already ruled a drafting error, rows
re-centred on the body centre (v 27.49) → **v 14.79 / 40.19**.

## `TOP` view — what is actually drawn

Only **one** part: the display header — `Connectors` 1.00 × 35.00 at
u 1.99..2.99, v 7.00..42.00, silks `Display header 14 pin (J3)`, dimensioned
`7` (north gap) / `35` (the strip) / `13` (south gap).

Four dashed annotation rectangles (rebuilt from their corner brackets, each
confirmed by its own dimension pair):

| Zone | Drawn size | u0..u1 | v0..v1 | Silks inside |
|---|---|---|---|---|
| A | 43.38 × 12.52 | 13.74..57.13 | 0.80..13.33 | `Area for Spare and debug pads` (`12,54`) |
| B | 43.38 × 18.91 | 13.74..57.13 | 17.89..36.80 | `AREA FOR SMD COMPONENTS (small parts)` (`18,91`) |
| C | 43.38 × 12.56 | 13.74..57.13 | 41.17..53.73 | `Area for Spare and debug pads` (`12,54`) |
| D | 26.17 × 52.92 | 57.84..84.01 | 0.80..53.73 | `AREA FOR SMD COMPONENTS (small parts)` (`26,17`, `52,92`) |

Nothing else: **no display-module outline, no mounting holes, no fuse, no
switch, no debug/spare parts, no passives.** A single `Devices` tick pair at the
DevKit's east end (0.55 / 0.67 mm) marks the antenna corner.

## `ELEV` view — heights (v measured from the carrier's top face)

| Object | Drawn size | meaning |
|---|---|---|
| carrier edge | 85.00 × **2.41** | the **carrier's own board thickness**, full 85 mm |
| display header | 1.00 × 10.02 above the board | stands 10.02 mm proud of the top face |
| DevKit | 50.00 × 15.00 below | dimension `15` |
| DC-DC module | 21.00 × 16.99 below | dimension `17` |
| XT30 | 9.99 × 9.99 below | dimension `10` |
| 2 × JST-PH | 12.01 × 5.01 below | dimension `5` |

## Corrections to plan v5 (`Implementation plans/PCB-REARRANGEMENT-FROM-CAD-2026-10-10.md`)

1. **"Display module outline (TOP view) 70.26 × 52.92"** — not drawn. That box
   is the bounding rectangle of annotation zones A–D (u 13.74..84.01 = 70.27,
   v 0.80..53.72 = 52.92). No display module outline exists in the PDF.
2. **"Display shelf … 2.41 mm band → parts under it must be ≤ ~2.4 mm tall"** —
   the 85 × 2.41 rectangle is the carrier's own thickness (layer `0`, spans the
   whole 85 mm). The display header stands **10.02 mm** above F.Cu, so the
   height budget on F.Cu is ~10 mm, not 2.4 mm.
3. **`JP1` silks position** — the plan places it at (58.46, **13.85**); the PDF
   draws it at u 36.54, **v 51.16** (south, under its box) together with the
   text `DIP SWITCH`.
4. **`JP1` type** — the plan reads the 6.00 × 3.50 box as "jumper header
   confirmed"; the drawing's own text says `DIP SWITCH`.
5. **ATO blade-fuse holder** — the plan lists it as drawn (B.Cu, 20.5 × 6.5).
   There is no fuse anywhere in the PDF.
6. **Mixed-frame worry (earlier review)** — unfounded: plan v5 applies
   `x = 95 − u` to both plan views, consistently. It is a deliberate whole-board
   mirror, not an error; the choice (antenna east vs west) still needs a
   decision.
7. Confirmed correct in plan v5: DevKit body/headers, module rect + polarity,
   XT30 zone, 4 JST zones + their two-per-zone designators, GNSS zone, JP1 box,
   J3 strip and its 33.02/35 mm reading, the antenna-corner ticks, and the
   26.40 → 25.40 row-pitch correction.
