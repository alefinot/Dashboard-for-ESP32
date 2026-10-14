# vendor_step — where the real 3D packages came from

Four of the five 3D packages this board needs that KiCad does not ship are real
vendor MCAD (the fifth, F1's 521 fuse clip, is parametric — see "What is not
here").  The files
here are **byte-identical copies** of what was downloaded — nothing was edited,
re-scaled or re-exported.  The turn and move that puts each one on its own
footprint origin lives in `../scripts/make_3d_models.py` (`VENDOR`), and the
packages it produces are written to `../dpp.3dshapes/`.

| file | bytes | sha256 |
|---|---|---|
| `AMASS_XT30U-M.step` | 134506 | `b2ea5f1e6ee84b8d1762a58a9c6873f1117e7080c4ecaf0e5a226ab64cc905cd` |
| `JST_S3B-PH-SM4-TB.step` | 172009 | `98a1508bc92b0ba8f9c9f891083eddcd52d6efc486ca9cf0ca37ec6dafbad95c` |
| `JST_S4B-PH-SM4-TB.step` | 193653 | `3e8aa7175fb8243ebc144eadeaecea16649f442289dc430b4e2ae5501df88ca9` |
| `LiteOn_LTV-357T.step` | 258777 | `2f6b4bc566c9c2547a7a2a3ef13d3798b0dac59f0adbc650d86d06aa8c0b82a3` |

## Provenance

Fetched 2026-10-14 from the public GitHub library **`issus/altium-library`**
(a re-hosted collection of vendor component CAD), over
`raw.githubusercontent.com`:

```
STEP/Connector - Amass/AMASS - XT30U-M.step   ->  AMASS_XT30U-M.step
STEP/Connector/JST S3B-PH-SM4-TB.step         ->  JST_S3B-PH-SM4-TB.step
STEP/Connector/JST S4B-PH-SM4-TB.step         ->  JST_S4B-PH-SM4-TB.step
STEP/Optoisolator/LTV-357T.STEP               ->  LiteOn_LTV-357T.step
```

Those four paths were confirmed present in the repo at fetch time
(`api.github.com/repos/issus/altium-library/git/trees/master?recursive=1`,
32690 entries, not truncated); the LTV-357T copy was re-fetched and re-hashed on
2026-10-14 and is identical to the file in this folder.

Not fetched from the makers directly, and that is worth knowing: JST's own 3D CAD
download and Amass's design-data page both want a session/account, and every
component-CAD portal that has these parts (SnapEDA, Ultra Librarian, Digi-Key
"Model Descriptions", componentsearchengine) answers 403 to a plain HTTP fetch —
as does Lite-On's MCAD request page for the LTV-357T, which only hands the file
over after a citation form is filled in, and Littelfuse's own asset CDN, which is
why the 521 fuse clip is still parametric (see below).  So these are copies of
vendor data, one step removed from the source.

## What was checked before using them

Each file was measured (cross-sections sliced out of the STEP at a height where
only the solder tails / pins exist) against the footprint it is being attached
to, and against the datasheet dimensions quoted in the footprint's own `descr`:

| package | measured from the STEP | footprint |
|---|---|---|
| XT30U-M | 10.16 × 5.60 × 13.75 overall; two 2.50 × 2.50 pins 3.00 below the seating plane, nothing proud of the 10.75 housing top | pads 3.80 × 3.80 / 2.70 drill at 5.00 pitch; Fab 10.20 × 5.20; Amass drawing gives 3.00 pin protrusion |
| JST S3B | 9.90 × 8.61 × 5.60; 3 contact tails 0.50 × 2.55 at 2.00 pitch, 2 bracket tails 0.40 × 2.91, housing cross-section 9.90 × 7.61 | Fab 9.90 × 7.60 (0.01 difference in y); signal pads 1.00 × 3.50, MP pads 1.50 × 3.40 |
| JST S4B | 11.90 × 8.61 × 5.60; 4 contact tails, 2 bracket tails, housing 11.90 × 7.61 | Fab 11.90 × 7.60 (0.01 in y); same pad set |
| LTV-357T | 7.03 × 3.85 × 2.10; four lead toes 0.40 wide spanning x 2.62..3.51 at the seating plane, moulding 4.26..4.38 × 3.72..3.85 (draft), top at 2.10 | pads 1.80 × 0.60 at (±3.15, ±1.27) — every toe dead centred on its pad (0.00 mm); Fab 4.62 × 4.12, courtyard 8.60 × 4.40 |

After the move, `make_3d_models.py` re-measures every package against the copper
(0.00–0.10 mm off pad centres) and against its courtyard, and fails the run if
either is wrong — so the numbers above are not a one-off claim, they are checked
on every regeneration.  The three places the real part differs from the library's
drawing are recorded there: the XT30 moulding is 0.4 wider than its Fab (0.2 a side,
inside the courtyard by 0.30), the JST contact tails run 0.93 past their Fab at the
cable-entry end (inside the courtyard by 0.61), and the LTV-357T toes stop 0.79
short of its courtyard while its moulding is 0.27 inside (the library draws 4.62 ×
4.12 of body where the real moulding is 4.38 × 3.85).

## Licensing caveat

The library these came from carries no license statement for the CAD files it
mirrors, and none of the four makers publishes a licence text next to its 3D data
(all publish CAD for use in designs, which is how this uses them).  Kept as
design-aid data for this board's 3D view.  If the board is ever commercialised,
re-fetch the packages from the makers under their current terms and replace these
four files, then re-run `python scripts/make_3d_models.py` and re-check the numbers.

## What is not here — the 521 fuse clip stays parametric

`Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal`
(F1) has no vendor package in this folder: nothing of it in the KiCad release,
and Littelfuse's design center needs a browser session and an account.  It is
drawn from the 520/521 datasheet numbers in `../scripts/make_3d_models.py`
(`littelfuse_521`, cited dimensions in its docstring).

F1 keeps that clip rather than a captive (shrouded) cartridge holder, measured on
this layout 2026-10-14: every closed 5x20 PCB holder in the stock library needs
33.0 mm of run or more (Schurter FAB 33.0, Bulgin FX0457 35.6, Schurter FUP
55.3), while the west column the fuse sits in has 32.05 mm between Q1's courtyard
(y 32.95) and the board edge (y 65.00), with no sideways escape — Q1's courtyard
reaches west to x 15.65 and R1 sits at x 12.35..14.25.  A whole-board scan of
every position and orientation (0.30 mm to F.Cu courtyards, body 0.50 mm from the
milled edge, B.Cu courtyards allowed to overlap as the clip already does) leaves
only two windows for a closed horizontal holder, x 38.25..58.75 / y 52.25..54.00
and x 77.00..82.25 / y 10.75..28.75 — both on the far side of the board from the
XT30 entry (20.25, 13.99) and the module's IN pads (x 12.75 / 32.75), which would
send the 12 V feed on a detour across the logic area.  Closed **vertical** holders
would fit (Schurter FPG4 13.0 × 13.0, Bulgin FX0456 13.7 × 13.1, EATON H15-V-1
14.3 × 14.3, EATON HBW/HBV 15.4 × 15.4) but stand the cartridge 20–25 mm above
the board instead of the clip's 13.

The optocoupler is worth spelling out: the BOM part is the Everlight EL357N-B and
the footprint is the generic `Package_SO:SO-4_4.4x3.9mm_P2.54mm` form (body 4.4 ×
3.9, pads 1.80 × 0.60, 2.54 pitch — the form both makers use).  Everlight's own
MCAD was not reachable, so the package rendered is Lite-On's LTV-357T, which is
the same 4 pin SOP form measured 4.26..4.38 × 3.72..3.85 × 2.10 with 7.02 across
the leads.  If the EL357N-B's own package is ever obtained, drop it in here as
`Everlight_EL357N.step`, point the `SO-4_4.4x3.9mm_P2.54mm` entry in `VENDOR` at
it, and re-run the script — `verify()` will say whether its leads still land on
the same copper.
