"""Import SES.  Usage: import_ses.py <prep.kicad_pcb> <in.ses> <out.kicad_pcb> [--fill]"""
import pcbnew, os, sys
WD = os.path.dirname(os.path.abspath(__file__))
a = [x for x in sys.argv[1:] if not x.startswith("--")]
PREP = a[0] if len(a)>0 else os.path.join(WD,"sb_prep.kicad_pcb")
SES  = a[1] if len(a)>1 else os.path.join(WD,"sb.ses")
OUT  = a[2] if len(a)>2 else os.path.join(WD,"sb_routed.kicad_pcb")
DO_FILL = "--fill" in sys.argv
log = open(os.path.join(WD,"import.log"),"w")
def L(*x):
    s=" ".join(str(v) for v in x); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
b = pcbnew.LoadBoard(PREP); L("loaded", PREP)
ok = pcbnew.ImportSpecctraSES(b, SES); L("import ok=", ok)
b.BuildConnectivity(); L("tracks after=", len(list(b.GetTracks())))
if DO_FILL:
    pcbnew.ZONE_FILLER(b).Fill(b.Zones()); L("zones filled")
b.Save(OUT); L("saved", os.path.getsize(OUT), "bytes")
