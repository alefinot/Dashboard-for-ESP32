"""Board file ops for the Freerouting yardstick - one operation per process.

KiCad 10.0's SWIG bindings are not safe across several LoadBoard/GC cycles in
one interpreter: a discarded BOARD can poison the next LoadBoard, which then
returns a raw SwigPyObject instead of a BOARD (hit during the step 5 yardstick
run: "AttributeError: 'SwigPyObject' object has no attribute 'Save'").  So
every operation that loads a board runs in its own KiCad python process and
this file exposes them as small subcommands.

Usage (always via KiCad's own python):
    python pcb_ops.py strip <in.kicad_pcb> <out.kicad_pcb>
        copy with every track and via removed; prints the removed count
    python pcb_ops.py dsn <board.kicad_pcb> <out.dsn>
        ExportSpecctraDSN; prints "ok" on success
    python pcb_ops.py ses <board.kicad_pcb> <in.ses> <out.kicad_pcb>
        LoadBoard + ImportSpecctraSES + Save; prints "ok" on success
    python pcb_ops.py counts <board.kicad_pcb>
        prints one JSON line:
        {"bcu_mm": F+B length, "vias": count, "per": {layer: mm}}

SWIG prints a "memory leak" line per item we hold no destructor for; those go
to stdout in raw C writes that can interleave mid-line, so results are instead
written to stderr with a "RESULT " prefix and the caller only reads those.
"""
import json
import sys


def op_strip(src, dst):
    import pcbnew
    board = pcbnew.LoadBoard(src)
    tracks = list(board.GetTracks())
    for t in tracks:
        board.Remove(t)
    board.Save(dst)
    return len(tracks)


def op_dsn(board_path, dsn_path):
    import pcbnew
    board = pcbnew.LoadBoard(board_path)
    return bool(pcbnew.ExportSpecctraDSN(board, dsn_path))


def op_ses(board_path, ses_path, dst):
    import pcbnew
    board = pcbnew.LoadBoard(board_path)
    ok = bool(pcbnew.ImportSpecctraSES(board, ses_path))
    if ok:
        board.Save(dst)
    return ok


def op_counts(path):
    import pcbnew
    board = pcbnew.LoadBoard(path)
    bcu = 0.0
    vias = 0
    per = {}
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            vias += 1
            continue
        length = t.GetLength() / 1e6
        name = board.GetLayerName(t.GetLayer())
        per[name] = per.get(name, 0.0) + length
        if t.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu):
            bcu += length
    return {"bcu_mm": round(bcu, 1), "vias": vias,
            "per": {k: round(v, 1) for k, v in per.items()}}


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: pcb_ops.py strip|dsn|ses|counts ...\n")
        return 2
    op = argv[1]
    try:
        if op == "strip":
            sys.stderr.write("RESULT %d\n" % op_strip(argv[2], argv[3]))
        elif op == "dsn":
            ok = op_dsn(argv[2], argv[3])
            if not ok:
                return 1
            sys.stderr.write("RESULT ok\n")
        elif op == "ses":
            ok = op_ses(argv[2], argv[3], argv[4])
            if not ok:
                return 1
            sys.stderr.write("RESULT ok\n")
        elif op == "counts":
            sys.stderr.write("RESULT " + json.dumps(op_counts(argv[2])) + "\n")
        else:
            sys.stderr.write("unknown op %r\n" % op)
            return 2
    except IndexError:
        sys.stderr.write("missing argument for %r\n" % op)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
