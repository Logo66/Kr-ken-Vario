"""Export Specctra DSN.  Usage: export_dsn.py <prep.kicad_pcb> <out.dsn>"""
import pcbnew, os, sys
WD = os.path.dirname(os.path.abspath(__file__))
PREP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WD, "sb_prep.kicad_pcb")
DSN  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(WD, "sb.dsn")
log = open(os.path.join(WD,"dsn.log"),"w")
def L(*a):
    s=" ".join(str(x) for x in a); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
L("load", PREP)
b = pcbnew.LoadBoard(PREP)
b.BuildConnectivity()
ok = pcbnew.ExportSpecctraDSN(b, DSN)
L("ok=", ok, "size=", os.path.getsize(DSN) if os.path.exists(DSN) else "MISSING")
