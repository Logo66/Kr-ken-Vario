"""
Targeted hand-router for the connections FreeRouting left open.
- I2C_SCL/SDA to island sensors (U1/U5): 2-layer A* (B.Cu is free of signals).
- +3V3/GND crowded BNO085 pads (U4.28/26): omnidirectional via-to-plane search.

Usage: handroute.py <in.kicad_pcb> <out.kicad_pcb>
"""
import pcbnew, os, sys, math, heapq
try:
    import wx; wx.DisableAsserts()   # KiCad10 PCB_VIA asserts would pop a blocking dialog
except Exception:
    pass

WD = os.path.dirname(os.path.abspath(__file__))
SRC, OUT = sys.argv[1], sys.argv[2]
MM = pcbnew.FromMM
R = MM(0.20)
N = int(MM(30.0) / R) + 2
ILO, IHI = int(round(MM(0.4) / R)), int(round(MM(29.6) / R))
FCU, BCU = pcbnew.F_Cu, pcbnew.B_Cu
TRW = MM(0.20); CLR = MM(0.10); KEEP = TRW // 2 + CLR
VIA_OD = MM(0.50); VIA_DR = MM(0.30)
DIAG = math.sqrt(2)

def L(*a):
    print(" ".join(str(x) for x in a), flush=True)
L("start grid N=", N)

L("loading", SRC)
b = pcbnew.LoadBoard(SRC)
L("loaded ok, collecting items")
PADS, VIAS, TRACKS, padmap = [], [], [], {}
for fp in b.GetFootprints():
    for pad in fp.Pads():
        pos = pad.GetPosition(); sz = pad.GetSize()
        PADS.append((pos.x, pos.y, sz.x // 2, sz.y // 2, pad.GetNetCode()))
        padmap[fp.GetReference() + "." + pad.GetNumber()] = pad
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition(); VIAS.append((p.x, p.y, MM(0.30), t.GetNetCode()))  # fixed radius, avoid via GetWidth assert
    else:
        s = t.GetStart(); e = t.GetEnd()
        TRACKS.append((s.x, s.y, e.x, e.y, t.GetWidth(), t.GetLayer(), t.GetNetCode()))
L("items pads", len(PADS), "vias", len(VIAS), "tracks", len(TRACKS))

def gi(v):
    i = int(round(v / R)); return 0 if i < 0 else (N - 1 if i >= N else i)
def gc(i):
    return int(i * R)
def newgrid():
    return bytearray(N * N)
def disc(g, cx, cy, rad):
    r = int(math.ceil(rad / R)); cix, ciy = gi(cx), gi(cy)
    for dx in range(-r, r + 1):
        xx = cix + dx
        if 0 <= xx < N:
            for dy in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r:
                    yy = ciy + dy
                    if 0 <= yy < N:
                        g[xx * N + yy] = 1
def rectm(g, cx, cy, hx, hy):
    rx = int(math.ceil(hx / R)); ry = int(math.ceil(hy / R)); cix, ciy = gi(cx), gi(cy)
    for dx in range(-rx, rx + 1):
        xx = cix + dx
        if 0 <= xx < N:
            for dy in range(-ry, ry + 1):
                yy = ciy + dy
                if 0 <= yy < N:
                    g[xx * N + yy] = 1
def segm(g, x1, y1, x2, y2, rad):
    d = math.hypot(x2 - x1, y2 - y1); steps = max(1, int(d / (R / 2)))
    for s in range(steps + 1):
        t = float(s) / steps
        disc(g, int(x1 + (x2 - x1) * t), int(y1 + (y2 - y1) * t), rad)

def build_block(netcode):
    bf, bb = newgrid(), newgrid()
    for (x, y, hx, hy, nc) in PADS:
        if nc != netcode:
            rectm(bf, x, y, hx + KEEP, hy + KEEP)
    for (x, y, r, nc) in VIAS:
        if nc != netcode:
            disc(bf, x, y, r + KEEP); disc(bb, x, y, r + KEEP)
    for (x1, y1, x2, y2, w, layer, nc) in TRACKS:
        if nc != netcode:
            segm(bf if layer == FCU else bb, x1, y1, x2, y2, w // 2 + KEEP)
    return bf, bb

def via_ok(ix, iy, bf, bb):
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            jx, jy = ix + dx, iy + dy
            if not (0 <= jx < N and 0 <= jy < N):
                return False
            if bf[jx * N + jy] or bb[jx * N + jy]:
                return False
    return True

def astar(start, target, bf, bb):
    tx, ty = target
    def h(ix, iy):
        return math.hypot(ix - tx, iy - ty)
    VIA_PEN = 9.0
    sx, sy = start
    openh = [(h(sx, sy), 0.0, sx, sy, 0)]
    came = {}; g0 = {(sx, sy, 0): 0.0}; seen = set(); EXP = 0
    while openh:
        f, gcur, ix, iy, layer = heapq.heappop(openh)
        st = (ix, iy, layer)
        if st in seen:
            continue
        seen.add(st)
        if ix == tx and iy == ty and layer == 0:
            path = [st]
            while st in came:
                st = came[st]; path.append(st)
            path.reverse(); return path
        EXP += 1
        if EXP % 20000 == 0:
            L("    ...EXP", EXP, "open", len(openh))
        if EXP > 80000:
            L("  astar cap hit"); return None
        grid = bf if layer == 0 else bb
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                jx, jy = ix + dx, iy + dy
                if jx < ILO or jx > IHI or jy < ILO or jy > IHI:
                    continue
                if grid[jx * N + jy] and not (jx == tx and jy == ty):
                    continue
                ng = gcur + (DIAG if (dx and dy) else 1.0)
                key = (jx, jy, layer)
                if ng < g0.get(key, 1e18):
                    g0[key] = ng; came[key] = (ix, iy, layer)
                    heapq.heappush(openh, (ng + h(jx, jy), ng, jx, jy, layer))
        if via_ok(ix, iy, bf, bb):
            nl = 1 - layer; key = (ix, iy, nl); ng = gcur + VIA_PEN
            if ng < g0.get(key, 1e18):
                g0[key] = ng; came[key] = (ix, iy, layer)
                heapq.heappush(openh, (ng + h(ix, iy), ng, ix, iy, nl))
    return None

def simplify(path):
    if len(path) < 3:
        return path
    out = [path[0]]
    for k in range(1, len(path) - 1):
        a, c, n = path[k - 1], path[k], path[k + 1]
        if c[2] != a[2] or c[2] != n[2]:
            out.append(c); continue
        if (c[0] - a[0], c[1] - a[1]) != (n[0] - c[0], n[1] - c[1]):
            out.append(c)
    out.append(path[-1]); return out

def lay(path, netobj):
    wp = simplify(path); segs, vias = [], []; s = wp[0]
    for k in range(1, len(wp)):
        if wp[k][2] != wp[k - 1][2]:
            segs.append((s, wp[k - 1])); vias.append(wp[k - 1]); s = wp[k]
    segs.append((s, wp[-1]))
    for a, c in segs:
        if a[0] == c[0] and a[1] == c[1]:
            continue
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(gc(a[0]), gc(a[1])))
        t.SetEnd(pcbnew.VECTOR2I(gc(c[0]), gc(c[1])))
        t.SetWidth(TRW); t.SetLayer(FCU if a[2] == 0 else BCU); t.SetNet(netobj); b.Add(t)
    for v in vias:
        via = pcbnew.PCB_VIA(b)
        via.SetPosition(pcbnew.VECTOR2I(gc(v[0]), gc(v[1])))
        via.SetWidth(VIA_OD); via.SetDrill(VIA_DR); via.SetNet(netobj); b.Add(via)
    return [(nd[0], nd[1]) for nd in path if nd[2] == 0]

def anchor_cells(nc, exclude):
    cells = []
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetCode() == nc and (fp.GetReference() + "." + pad.GetNumber()) not in exclude:
                p = pad.GetPosition(); cells.append((gi(p.x), gi(p.y)))
    for (x1, y1, x2, y2, w, layer, ncc) in TRACKS:
        if ncc == nc and layer == FCU:
            d = math.hypot(x2 - x1, y2 - y1); steps = max(1, int(d / R))
            for s in range(steps + 1):
                t = float(s) / steps
                cells.append((gi(int(x1 + (x2 - x1) * t)), gi(int(y1 + (y2 - y1) * t))))
    return cells

# ---- I2C island routes ----
TO_ROUTE = [("I2C_SCL", ["U3.13", "U5.2", "U1.2"]),
            ("I2C_SDA", ["U5.1", "U1.4"])]
for net, refs in TO_ROUTE:
    nc = padmap[refs[0]].GetNetCode(); netobj = padmap[refs[0]].GetNet()
    bf, bb = build_block(nc)
    rset = set(refs)
    anchors = anchor_cells(nc, rset)
    for ref in refs:
        pad = padmap[ref]; pp = pad.GetPosition(); s = (gi(pp.x), gi(pp.y))
        if not anchors:
            L("no anchors for", ref); continue
        tgt = min(anchors, key=lambda c: (c[0] - s[0]) ** 2 + (c[1] - s[1]) ** 2)
        L("route", ref, "->", tgt, "dist", int(math.hypot(tgt[0] - s[0], tgt[1] - s[1])))
        path = astar(s, tgt, bf, bb)
        if path:
            newc = lay(path, netobj); anchors.extend(newc)
            L("  OK len", len(path), "vias", sum(1 for k in range(1, len(path)) if path[k][2] != path[k - 1][2]))
        else:
            L("  FAILED", ref)

# ---- power pads: omnidirectional via to plane ----
for ref in ["U4.28", "U4.26"]:
    pad = padmap[ref]; nc = pad.GetNetCode(); netobj = pad.GetNet(); pp = pad.GetPosition()
    bf, bb = build_block(nc); done = False
    for rad in [0.45, 0.55, 0.65, 0.75, 0.9, 1.05, 1.2]:
        if done:
            break
        for ang in range(0, 360, 15):
            a = math.radians(ang)
            vx = int(pp.x + MM(rad) * math.cos(a)); vy = int(pp.y + MM(rad) * math.sin(a))
            if not via_ok(gi(vx), gi(vy), bf, bb):
                continue
            ok = True; d = math.hypot(vx - pp.x, vy - pp.y); steps = max(1, int(d / (R / 2)))
            for st in range(steps + 1):
                tt = float(st) / steps
                cx, cy = gi(int(pp.x + (vx - pp.x) * tt)), gi(int(pp.y + (vy - pp.y) * tt))
                if bf[cx * N + cy]:
                    ok = False; break
            if not ok:
                continue
            via = pcbnew.PCB_VIA(b); via.SetPosition(pcbnew.VECTOR2I(vx, vy))
            via.SetWidth(VIA_OD); via.SetDrill(VIA_DR); via.SetNet(netobj); b.Add(via)
            t = pcbnew.PCB_TRACK(b); t.SetStart(pp); t.SetEnd(pcbnew.VECTOR2I(vx, vy))
            t.SetWidth(TRW); t.SetLayer(FCU); t.SetNet(netobj); b.Add(t)
            L("power via", ref, "rad", rad, "ang", ang); done = True; break
    if not done:
        L("power FAILED", ref)

b.BuildConnectivity()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(OUT)
L("saved", os.path.getsize(OUT))
