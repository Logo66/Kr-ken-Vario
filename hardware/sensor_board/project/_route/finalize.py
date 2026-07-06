"""Import SES, set clearance, fill, save.  Usage: finalize.py <prep> <ses> <out> <clearance_mm>"""
import pcbnew, os, sys
WD = os.path.dirname(os.path.abspath(__file__))
PREP, SES, OUT, CLR = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
MM, TMM = pcbnew.FromMM, pcbnew.ToMM
log = open(os.path.join(WD,"finalize.log"),"w")
def L(*a):
    s=" ".join(str(x) for x in a); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
b = pcbnew.LoadBoard(PREP); L("loaded", PREP)
ok = pcbnew.ImportSpecctraSES(b, SES); L("import ok=", ok)
ds = b.GetDesignSettings()
ds.m_MinClearance = MM(0.10)          # JLC supports 0.1mm; relax board minimum
dnc = ds.m_NetSettings.GetDefaultNetclass()
dnc.SetClearance(MM(CLR)); L("clearance ->", TMM(dnc.GetClearance()))
b.BuildConnectivity(); L("tracks=", len(list(b.GetTracks())))
pcbnew.ZONE_FILLER(b).Fill(b.Zones()); L("zones filled")
b.Save(OUT); L("saved", os.path.getsize(OUT), "bytes")
