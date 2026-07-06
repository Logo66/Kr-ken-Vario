"""
Pre-Fanout add-on for FreeRouting.

FreeRouting connects plane-net (GND/+3V3) pads only where the copper plane can
physically reach them. On fine-pitch LGAs (BMP581 0.4mm, LSM6/BNO 0.5mm) the
plane cannot squeeze between the pads, so those pads stay unconnected and the
autorouter also gives up on the part's signals.

This script places, for every GND/+3V3 pad of the listed parts, a *locked*
fanout via just outside the pad (radially outward from the component centre)
plus a short F.Cu stub pad->via. The via ties the pad to the inner GND/+3V3
plane. Placement is collision-aware (won't overlap other pads or vias). After
this, only the signal nets remain for FreeRouting -> it routes cleanly.

Usage:
  python fanout.py <in.kicad_pcb> <out.kicad_pcb> [refs=U1,U3,U4,U5]
"""
import pcbnew, os, sys, math

WD  = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1]
OUT = sys.argv[2]
REFS = set((sys.argv[3].split(",")) if len(sys.argv) > 3 else ["U1", "U3", "U4", "U5"])
PLANE_NETS = {"GND", "+3V3"}
MM, TMM = pcbnew.FromMM, pcbnew.ToMM

VIA_OD   = MM(0.50)          # 0.45 min via dia, 0.3 min hole, 0.1 min annular -> all OK & fits
VIA_DR   = MM(0.30)
STUB_W   = MM(0.20)
CLR      = MM(0.12)          # via-body placement clearance (final rule lowered to 0.10)
STUB_CLR = MM(0.04)          # stub path: only block real overlap/short, not full clearance
EDGE_LO, EDGE_HI = MM(0.6), MM(29.4)
TRIES    = [MM(d) for d in (0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.15, 1.30)]

def seg_pt_dist(ax, ay, bx, by, px, py):
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / float(dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

log = open(os.path.join(WD, "fanout.log"), "w")
def L(*a):
    s = " ".join(str(x) for x in a); log.write(s + "\n"); log.flush(); os.fsync(log.fileno())

b = pcbnew.LoadBoard(SRC)
ds = b.GetDesignSettings()
ds.m_ViasMinSize = MM(0.45); ds.m_ViasMinDrill = MM(0.2)

# obstacle list: every pad as a circle (x, y, radius, netcode)
obst = []
for fp in b.GetFootprints():
    for pad in fp.Pads():
        p = pad.GetPosition(); sz = pad.GetSize()
        obst.append((p.x, p.y, max(sz.x, sz.y) / 2.0, pad.GetNetCode()))
placed = []  # (x, y, radius)
rvia = VIA_OD / 2.0

def free(px, py, x, y, net):
    # via point inside board edge
    if x < EDGE_LO or x > EDGE_HI or y < EDGE_LO or y > EDGE_HI:
        return False
    swh = STUB_W / 2.0
    for ox, oy, orr, onet in obst:
        if onet == net:
            continue
        if math.hypot(x - ox, y - oy) < rvia + orr + CLR:        # via vs pad: full clearance
            return False
        if seg_pt_dist(px, py, x, y, ox, oy) < swh + orr + STUB_CLR:  # stub vs pad: anti-overlap only
            return False
    for vx, vy, vr in placed:
        if math.hypot(x - vx, y - vy) < rvia + vr + CLR:         # via vs via: full clearance
            return False
        if seg_pt_dist(px, py, x, y, vx, vy) < swh + vr + STUB_CLR:   # stub vs via
            return False
    return True

added, skipped = 0, []
for fp in b.GetFootprints():
    if fp.GetReference() not in REFS:
        continue
    fc = fp.GetPosition()
    for pad in fp.Pads():
        if pad.GetNetname() not in PLANE_NETS:
            continue
        pp = pad.GetPosition()
        dx, dy = pp.x - fc.x, pp.y - fc.y
        d = math.hypot(dx, dy) or 1.0
        ux, uy = dx / d, dy / d
        done = False
        for off in TRIES:
            vx, vy = int(pp.x + ux * off), int(pp.y + uy * off)
            if free(pp.x, pp.y, vx, vy, pad.GetNetCode()):
                via = pcbnew.PCB_VIA(b)
                via.SetPosition(pcbnew.VECTOR2I(vx, vy))
                via.SetWidth(VIA_OD); via.SetDrill(VIA_DR)
                via.SetNet(pad.GetNet())
                try: via.SetLocked(True)
                except Exception: pass
                b.Add(via)
                t = pcbnew.PCB_TRACK(b)
                t.SetStart(pp); t.SetEnd(pcbnew.VECTOR2I(vx, vy))
                t.SetWidth(STUB_W); t.SetLayer(pcbnew.F_Cu); t.SetNet(pad.GetNet())
                try: t.SetLocked(True)
                except Exception: pass
                b.Add(t)
                placed.append((vx, vy, rvia))
                added += 1; done = True; break
        if not done:
            skipped.append("%s.%s[%s]" % (fp.GetReference(), pad.GetNumber(), pad.GetNetname()))

L("fanout vias added:", added)
L("skipped (no free spot):", len(skipped), skipped)

# GND plane stitching: tie GND zones (F/In1/B) together + heal split-zone islands
gnd_net = None
for fp in b.GetFootprints():
    for pad in fp.Pads():
        if pad.GetNetname() == "GND":
            gnd_net = pad.GetNet(); break
    if gnd_net:
        break
stitched = 0
if gnd_net is not None:
    nc = gnd_net.GetNetCode()
    gx = MM(3.0)
    while gx <= MM(27.0):
        gy = MM(3.0)
        while gy <= MM(27.0):
            if free(gx, gy, gx, gy, nc):
                via = pcbnew.PCB_VIA(b)
                via.SetPosition(pcbnew.VECTOR2I(int(gx), int(gy)))
                via.SetWidth(VIA_OD); via.SetDrill(VIA_DR); via.SetNet(gnd_net)
                try: via.SetLocked(True)
                except Exception: pass
                b.Add(via); placed.append((gx, gy, rvia)); stitched += 1
            gy += MM(5.0)
        gx += MM(5.0)
L("gnd stitch vias:", stitched)
b.Save(OUT)
L("saved", OUT, os.path.getsize(OUT), "bytes")
