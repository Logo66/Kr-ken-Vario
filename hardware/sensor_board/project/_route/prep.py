"""Prep board for autorouting: neck fix, clear tracks, fill. Logs each step to prep.log."""
import pcbnew, os

WD  = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(WD, "sb.kicad_pcb")
PREP= os.path.join(WD, "sb_prep.kicad_pcb")
MM  = pcbnew.FromMM
TMM = pcbnew.ToMM

_log = open(os.path.join(WD, "prep.log"), "w")
def L(*a):
    s = " ".join(str(x) for x in a)
    _log.write(s + "\n"); _log.flush(); os.fsync(_log.fileno())

L("STEP load")
board = pcbnew.LoadBoard(SRC)
L("  loaded ok; tracks=", len(list(board.GetTracks())), "zones=", board.GetAreaCount())

L("STEP min-rules")
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = MM(0.15); ds.m_ViasMinSize = MM(0.45); ds.m_ViasMinDrill = MM(0.3)
try:
    ds.m_MinClearance = MM(0.12)
except Exception as e:
    L("  minclear skip:", e)
L("  ok")

L("STEP slot-neck")
def near(p, x, y, tol=0.02):
    return abs(TMM(p.x)-x) < tol and abs(TMM(p.y)-y) < tol
bottom = None
for d in list(board.GetDrawings()):
    if d.GetClass() == "PCB_SHAPE" and d.GetLayer() == pcbnew.Edge_Cuts \
       and d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        s, e = d.GetStart(), d.GetEnd()
        if (near(s,2,9) and near(e,12.5,9)) or (near(s,12.5,9) and near(e,2,9)):
            bottom = d; break
if bottom is None:
    L("  ERROR: bottom slot not found"); _log.close(); raise SystemExit(1)
w = bottom.GetWidth()
board.Remove(bottom)
for (x1,y1),(x2,y2) in [((2.0,9.0),(6.0,9.0)), ((8.5,9.0),(12.5,9.0))]:
    seg = pcbnew.PCB_SHAPE(board); seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
    seg.SetStart(pcbnew.VECTOR2I(MM(x1),MM(y1))); seg.SetEnd(pcbnew.VECTOR2I(MM(x2),MM(y2)))
    seg.SetLayer(pcbnew.Edge_Cuts); seg.SetWidth(w); board.Add(seg)
L("  neck x=6.0..8.5 @ y=9 ok")

L("STEP clear-tracks")
n = 0
for t in list(board.GetTracks()):
    board.Remove(t); n += 1
L("  removed", n, "tracks/vias")

L("STEP fill-zones SKIPPED (filler segfaults standalone; planes export from outline; final fill at SES stage)")

L("STEP save")
board.Save(PREP)
L("  saved", PREP, os.path.getsize(PREP), "bytes")
L("DONE")
_log.close()
