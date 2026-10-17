"""Pour the board's copper zones with KiCad's own filler.

Run it with KiCad's own interpreter - the planes have to be produced by the same
code that will plot them for the fab, not by a re-implementation of it:

    <Kicad>/10.0/bin/python.exe scripts/fill_zones.py <in.kicad_pcb> <out.kicad_pcb>

`make_pcb.py` uses this on a scratch copy (hardware/out/drc-check/) so that DRC
judges a board with real planes in it; the committed .kicad_pcb keeps its zones
unfilled (small, diff-friendly), and the fill is re-run before any Gerber or
drill export.

Determinism: for a given KiCad version the fill is deterministic - same input
board, same output.  The version is printed because a KiCad upgrade may change
the pour geometry (the plan pins this to 10.0.6).  "Same input" includes the
PROJECT: the board must sit beside its .kicad_pro under the matching basename,
because the net-class clearances in it are what size a pour's anti-pads.
Measured 2026-10-17: one byte-identical board filled to 3950.0 mm2 per plane
with no project beside it and 4004.8 mm2 with it.  Saved fills are not
byte-reproducible (KiCad re-stamps ids), which is a further reason the filled
copy lives in hardware/out/ and never in git.

SWIG pcbnew (this API) is deprecated as of KiCad 9 and slated for removal in
KiCad 11; that is why the fill lives in one small isolated file rather than
being spread through the generator - the port to the IPC API is then a contained
change (see plan §8c).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kicadfmt                                   # KICAD_PY, layer_table


def fill(src, dst):
    """Fill every zone of the board at `src`, save the result at `dst`.

    Returns (kicad_version, [(net, layer_id, filled_mm2, pieces), ...]).
    """
    import pcbnew                      # only available under KiCad's python

    board = pcbnew.LoadBoard(src)
    zones = [z for z in board.Zones()]
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(zones)
    board.Save(dst)
    # Read the fills back out of the SAVED file: that is what the next tool
    # (kicad-cli drc, the Gerber plotter) will see, not what lives in memory.
    back = pcbnew.LoadBoard(dst)
    report = [(z.GetNetname(), z.GetLayer(), z.GetFilledArea() / 1e12,
               z.GetFilledPolysList(z.GetLayer()).OutlineCount())
              for z in back.Zones()]
    return pcbnew.GetBuildVersion(), report


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    names = {n: l for l, n in kicadfmt.layer_table(
        open(argv[1], encoding="utf-8").read()).items()}
    ver, report = fill(argv[1], argv[2])
    print("fill        KiCad %s: %d zones -> %s" % (ver, len(report), argv[2]))
    empty = []
    for net, layer, area, pieces in report:
        print("            %-8s %-9s %10.1f mm2 in %d piece(s)"
              % (names.get(layer, "id %d" % layer), net or "(keep-out)",
                 area, pieces))
        if net and pieces == 0:
            empty.append(net)
    if empty:
        # A pour that fills nothing is the failure mode this script exists to
        # make visible: the board looks fine and the plane is not there.
        print("FILL FAILED: no copper produced for %s" % ", ".join(empty))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
