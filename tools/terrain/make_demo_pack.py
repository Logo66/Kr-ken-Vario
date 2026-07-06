# make_demo_pack.py — DEMO-Region-Pack (AURA-KRUECKE-8 §3) mit Gipfeln + DEMO-Konturen.
# ACHTUNG: Die Konturen hier sind SYNTHETISCHE Ringe um Gipfel (Demo, um den Renderer
# sichtbar zu machen) — NICHT echtes Terrain. Echte Hoehenlinien kommen vom Server-DEM-Pack.
# Erzeugt region_ch_v1.pack (ersetzt das peaks-only Pack).
import struct, os, math
from collections import defaultdict

HERE = os.path.dirname(__file__)
SRC  = os.path.join(HERE, "peaks.txt")
OUT  = os.path.join(HERE, "region_ch_v1.pack")
TILE_DEG = 0.25
TILE_SIZE_FIELD = 25
RING_CELL = 0.22          # raeumliche Ausduennung der Ring-Gipfel
RINGS = [(0.004, 0), (0.008, 1), (0.012, 0)]   # (radius_grad, flag)  flag1=Index(dick)

peaks = []
for line in open(SRC, encoding="ascii"):
    line = line.strip()
    if not line: continue
    p = line.split(";")
    if len(p) < 4: continue
    peaks.append((float(p[0]), float(p[1]), int(p[2]), p[3][:24]))

# Gipfel fuer Demo-Ringe ausduennen (hoechster je Zelle)
thin = {}
for la, lo, el, nm in peaks:
    k = (round(la/RING_CELL), round(lo/RING_CELL))
    if k not in thin or el > thin[k][2]: thin[k] = (la, lo, el, nm)
ring_peaks = list(thin.values())

def ellipse(clat, clon, r_deg, n=16):
    pts = []
    coslat = math.cos(clat*math.pi/180.0)
    for i in range(n+1):                          # +1: Ring schliessen
        a = 2*math.pi*i/n
        pts.append((clat + r_deg*math.cos(a), clon + r_deg*math.sin(a)/coslat))
    return pts

tiles_cont = defaultdict(list)
tiles_peak = defaultdict(list)
for la, lo, el, nm in peaks:
    tiles_peak[(math.floor(la/TILE_DEG), math.floor(lo/TILE_DEG))].append((la, lo, el, nm))
for la, lo, el, nm in ring_peaks:
    key = (math.floor(la/TILE_DEG), math.floor(lo/TILE_DEG))
    for ri, (r, flag) in enumerate(RINGS):
        h = el - ri*100
        tiles_cont[key].append((h, flag, ellipse(la, lo, r)))

def clip16(v): return max(-32768, min(32767, int(round(v))))

keys = sorted(set(list(tiles_cont.keys()) + list(tiles_peak.keys())))
blocks = []
for (ti, tj) in keys:
    lat0, lon0 = ti*TILE_DEG, tj*TILE_DEG
    b = bytearray()
    conts = tiles_cont.get((ti, tj), [])
    b += struct.pack("<H", len(conts))
    for h, flag, pts in conts:
        b += struct.pack("<hBH", clip16(h), flag, len(pts))
        for (la, lo) in pts:
            b += struct.pack("<hh", clip16((la-lat0)*1e5), clip16((lo-lon0)*1e5))
    pks = tiles_peak.get((ti, tj), [])
    b += struct.pack("<H", len(pks))
    for la, lo, el, nm in pks:
        nmb = nm.encode("ascii", "ignore")[:255]
        rank = 0 if el>=2500 else 1 if el>=1500 else 2 if el>=1000 else 3
        b += struct.pack("<hhhBB", clip16((la-lat0)*1e5), clip16((lo-lon0)*1e5), clip16(el), rank, len(nmb))
        b += nmb
    b += struct.pack("<HH", 0, 0)                 # n_airspace, n_obstacles
    blocks.append((int(round(lat0*1e7)), int(round(lon0*1e7)), bytes(b)))

HEADER, INDEX = 25, 16
data_start = HEADER + INDEX*len(blocks)
out = bytearray()
out += b"AURA" + struct.pack("<B", 1)
rid = b"CH"; out += rid + b"\x00"*(16-len(rid))
out += struct.pack("<H", TILE_SIZE_FIELD) + struct.pack("<H", len(blocks))
off = data_start
for (lat0, lon0, b) in blocks:
    out += struct.pack("<iiII", lat0, lon0, off, len(b)); off += len(b)
for (_, _, b) in blocks: out += b
open(OUT, "wb").write(out)

ncont = sum(len(v) for v in tiles_cont.values())
npts  = sum(len(p) for v in tiles_cont.values() for (_,_,p) in v)
print(f"DEMO-Pack: {OUT}")
print(f"  {len(out)} bytes, {len(blocks)} Kacheln, {len(peaks)} Gipfel,")
print(f"  {ncont} Demo-Konturen ({npts} Punkte) um {len(ring_peaks)} Gipfel")
