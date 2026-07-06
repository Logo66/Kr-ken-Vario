import pcbnew, os
WD = os.path.dirname(os.path.abspath(__file__))
log = open(os.path.join(WD,"filltest2.log"),"w")
def L(*a):
    s=" ".join(str(x) for x in a); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
b = pcbnew.LoadBoard(os.path.join(WD,"sb_prep.kicad_pcb")); L("loaded")
b.BuildConnectivity(); L("connectivity built")
f = pcbnew.ZONE_FILLER(b); L("filler object created")
ok = f.Fill(b.Zones()); L("fill returned", ok)
b.Save(os.path.join(WD,"filltest2.kicad_pcb")); L("saved ok")
