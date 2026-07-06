"""Re-set netclass clearance on an existing prepped board, save + (optional) re-export DSN."""
import pcbnew, os, sys
WD = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1]; OUT = sys.argv[2]; CLR = float(sys.argv[3])
DSN = sys.argv[4] if len(sys.argv) > 4 else None
MM, TMM = pcbnew.FromMM, pcbnew.ToMM
log = open(os.path.join(WD,"reclear.log"),"w")
def L(*a):
    s=" ".join(str(x) for x in a); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
b = pcbnew.LoadBoard(SRC)
dnc = b.GetDesignSettings().m_NetSettings.GetDefaultNetclass()
dnc.SetClearance(MM(CLR))
L("clearance ->", TMM(dnc.GetClearance()))
b.Save(OUT); L("saved", OUT)
if DSN:
    b.BuildConnectivity()
    ok = pcbnew.ExportSpecctraDSN(b, DSN)
    L("dsn ok", ok, os.path.getsize(DSN) if os.path.exists(DSN) else "MISSING")
