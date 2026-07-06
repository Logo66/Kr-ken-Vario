"""SOLID variant: base on sb_prep (already 0 tracks); drop slot, relax clearance, solid zones."""
import pcbnew, os, gc
WD  = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(WD, "sb_prep.kicad_pcb")   # already track-free, has neck-slot
OUT = os.path.join(WD, "sb_solid_prep.kicad_pcb")
MM, TMM = pcbnew.FromMM, pcbnew.ToMM
log = open(os.path.join(WD,"prep_solid.log"),"w")
def L(*a):
    s=" ".join(str(x) for x in a); log.write(s+"\n"); log.flush(); os.fsync(log.fileno())
def safe_list(fn):
    for _ in range(6):
        try: return list(fn())
        except TypeError: gc.collect()
    return list(fn())

b = pcbnew.LoadBoard(SRC); L("loaded")

# drop ALL slot Edge.Cuts (w=0.35) -> solid 30x30
draw = safe_list(b.GetDrawings); L("drawings:", len(draw))
rm=0
for d in draw:
    if d.GetClass()=="PCB_SHAPE" and d.GetLayer()==pcbnew.Edge_Cuts \
       and d.GetShape()==pcbnew.SHAPE_T_SEGMENT and abs(TMM(d.GetWidth())-0.35)<0.05:
        b.Remove(d); rm+=1
L("removed slot segments:", rm)

# net class / rules
ds = b.GetDesignSettings()
try:
    dnc = ds.m_NetSettings.GetDefaultNetclass()
    dnc.SetClearance(MM(0.1)); dnc.SetTrackWidth(MM(0.2))
    dnc.SetViaDiameter(MM(0.6)); dnc.SetViaDrill(MM(0.3))
    L("netclass clearance now:", TMM(dnc.GetClearance()))
except Exception as e:
    L("netclass FAILED:", repr(e))
ds.m_TrackMinWidth = MM(0.15); ds.m_ViasMinSize = MM(0.45); ds.m_ViasMinDrill = MM(0.3)

# zones solid (no thermal relief)
for z in safe_list(b.Zones): z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
L("zones set solid")

b.Save(OUT); L("saved", os.path.getsize(OUT), "bytes")
