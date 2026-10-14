# vendor_step — where the real 3D packages came from

Three of the five 3D packages this board needs are real vendor MCAD.  The files
here are **byte-identical copies** of what was downloaded — nothing was edited,
re-scaled or re-exported.  The move that puts each one on its own footprint
origin lives in `../scripts/make_3d_models.py` (`VENDOR`), and the packages it
produces are written to `../dpp.3dshapes/`.

| file | bytes | sha256 |
|---|---|---|
| `AMASS_XT30U-M.step` | 134506 | `b2ea5f1e6ee84b8d1762a58a9c6873f1117e7080c4ecaf0e5a226ab64cc905cd` |
| `JST_S3B-PH-SM4-TB.step` | 172009 | `98a1508bc92b0ba8f9c9f891083eddcd52d6efc486ca9cf0ca37ec6dafbad95c` |
| `JST_S4B-PH-SM4-TB.step` | 193653 | `3e8aa7175fb8243ebc144eadeaecea16649f442289dc430b4e2ae5501df88ca9` |

## Provenance

Fetched 2026-10-14 from the public GitHub library **`issus/altium-library`**
(a re-hosted collection of vendor component CAD), over
`raw.githubusercontent.com`:

```
STEP/Connector - Amass/AMASS - XT30U-M.step   ->  AMASS_XT30U-M.step
STEP/Connector/JST S3B-PH-SM4-TB.step         ->  JST_S3B-PH-SM4-TB.step
STEP/Connector/JST S4B-PH-SM4-TB.step         ->  JST_S4B-PH-SM4-TB.step
```

Those three paths were confirmed present in the repo at fetch time
(`api.github.com/repos/issus/altium-library/git/trees/master?recursive=1`,
32690 entries, not truncated).

Not fetched from the makers directly, and that is worth knowing: JST's own 3D CAD
download and Amass's design-data page both want a session/account, and every
component-CAD portal that has these parts (SnapEDA, Ultra Librarian, Digi-Key
"Model Descriptions", componentsearchengine) answers 403 to a plain HTTP fetch —
as does Littelfuse's own asset CDN, which is why the 521 fuse clip is still
parametric.  So these are copies of vendor data, one step removed from the
source.

## What was checked before using them

Each file was measured (cross-sections sliced out of the STEP at a height where
only the solder tails / pins exist) against the footprint it is being attached
to, and against the datasheet dimensions quoted in the footprint's own `descr`:

| package | measured from the STEP | footprint |
|---|---|---|
| XT30U-M | 10.16 × 5.60 × 13.75 overall; two 2.50 × 2.50 pins 3.00 below the seating plane, nothing proud of the 10.75 housing top | pads 3.80 × 3.80 / 2.70 drill at 5.00 pitch; Fab 10.20 × 5.20; Amass drawing gives 3.00 pin protrusion |
| JST S3B | 9.90 × 8.61 × 5.60; 3 contact tails 0.50 × 2.55 at 2.00 pitch, 2 bracket tails 0.40 × 2.91, housing cross-section 9.90 × 7.61 | Fab 9.90 × 7.60 (0.01 difference in y); signal pads 1.00 × 3.50, MP pads 1.50 × 3.40 |
| JST S4B | 11.90 × 8.61 × 5.60; 4 contact tails, 2 bracket tails, housing 11.90 × 7.61 | Fab 11.90 × 7.60 (0.01 in y); same pad set |

After the move, `make_3d_models.py` re-measures every package against the copper
(0.00–0.10 mm off pad centres) and against its courtyard, and fails the run if
either is wrong — so the numbers above are not a one-off claim, they are checked
on every regeneration.  The two places the real part differs from the library's
drawing are recorded there: the XT30 moulding is 0.4 wider than its Fab (0.2 a side,
inside the courtyard by 0.30) and the JST contact tails run 0.93 past their Fab at the
cable-entry end (inside the courtyard by 0.61).

## Licensing caveat

The library these came from carries no license statement for the CAD files it
mirrors, and neither maker publishes a licence text next to its 3D data (both
publish CAD for use in designs, which is how this uses them).  Kept as design-aid
data for this board's 3D view.  If the board is ever commercialised, re-fetch the
packages from JST and Amass under their current terms and replace these three
files, then re-run `python scripts/make_3d_models.py` and re-check the numbers.

## The other two packages

`SO-4_4.4x3.9mm_P2.54mm` (EL357N-B optocoupler, D1/OK2) and
`Fuseholder_Clip-5x20mm_Littelfuse_521_Lateral_P17.00x5.00mm_D1.30mm_Horizontal`
(F1) have no package here: none exists in the KiCad release and none could be
reached without an account.  Both are drawn from their datasheet numbers in
`../scripts/make_3d_models.py`, with the cited dimensions in each builder's
docstring.
