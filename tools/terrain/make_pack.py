# make_pack.py — erzeugt ein region_<name>_v1.pack nach AURA-KRUECKE-8 §3 (Little-Endian)
# Stufe 1: nur Gipfel (n_contours=0). Quelle: peaks.txt (lat;lon;ele;name).
# Enthaelt einen Round-Trip-Selbsttest (schreiben -> zurueklesen -> vergleichen).
import struct, os, math
from collections import defaultdict

HERE = os.path.dirname(__file__)
SRC  = os.path.join(HERE, "peaks.txt")
OUT  = os.path.join(HERE, "region_ch_v1.pack")

TILE_DEG = 0.25            # Kachelkante in Grad (<=0.327 wegen int16-Deltas in 1e-5)
TILE_SIZE_FIELD = 25       # 1/100 Grad

def rank_of(ele):
    if ele >= 2500: return 0
    if ele >= 1500: return 1
    if ele >= 1000: return 2
    return 3

# --- Gipfel lesen ---
peaks = []
with open(SRC, encoding="ascii") as f:
    for line in f:
        line = line.strip()
        if not line: continue
        p = line.split(";")
        if len(p) < 4: continue
        peaks.append((float(p[0]), float(p[1]), int(p[2]), p[3][:24]))

# --- nach Kacheln gruppieren ---
tiles = defaultdict(list)
for lat, lon, ele, name in peaks:
    tiles[(math.floor(lat/TILE_DEG), math.floor(lon/TILE_DEG))].append((lat,lon,ele,name))

# --- Tile-Bloecke bauen ---
blocks = []   # (lat0_1e7, lon0_1e7, bytes)
for (ti,tj) in sorted(tiles.keys()):
    lat0, lon0 = ti*TILE_DEG, tj*TILE_DEG
    b = bytearray()
    b += struct.pack("<H", 0)                       # n_contours = 0 (Stufe 1)
    pk = tiles[(ti,tj)]
    b += struct.pack("<H", len(pk))                 # n_peaks
    for lat,lon,ele,name in pk:
        dlat = max(-32768, min(32767, int(round((lat-lat0)*1e5))))
        dlon = max(-32768, min(32767, int(round((lon-lon0)*1e5))))
        ele16 = max(-32768, min(32767, ele))
        nm = name.encode("ascii","ignore")[:255]
        b += struct.pack("<hhhBB", dlat, dlon, ele16, rank_of(ele), len(nm))
        b += nm
    blocks.append((int(round(lat0*1e7)), int(round(lon0*1e7)), bytes(b)))

# --- Datei zusammensetzen ---
HEADER_SIZE = 4+1+16+2+2          # = 25
INDEX_ENTRY = 4+4+4+4            # = 16
data_start = HEADER_SIZE + INDEX_ENTRY*len(blocks)

out = bytearray()
out += b"AURA"
out += struct.pack("<B", 1)
rid = b"CH"
out += rid + b"\x00"*(16-len(rid))
out += struct.pack("<H", TILE_SIZE_FIELD)
out += struct.pack("<H", len(blocks))
offset = data_start
for (lat0_1e7, lon0_1e7, b) in blocks:
    out += struct.pack("<iiII", lat0_1e7, lon0_1e7, offset, len(b))
    offset += len(b)
for (_,_,b) in blocks:
    out += b

with open(OUT, "wb") as f:
    f.write(out)
print(f"Pack geschrieben: {OUT}")
print(f"  {len(out)} bytes, {len(blocks)} Kacheln, {len(peaks)} Gipfel")

# --- Round-Trip-Selbsttest: zuruecklesen wie das Geraet ---
def rd(buf, pos, fmt):
    sz = struct.calcsize(fmt)
    return struct.unpack(fmt, buf[pos:pos+sz]) + (pos+sz,)

buf = open(OUT,"rb").read()
assert buf[:4]==b"AURA", "Magic falsch!"
ver = buf[4]
region = buf[5:21].split(b"\x00")[0].decode()
(tsize,), pos = struct.unpack("<H", buf[21:23]), 23
(tcount,), pos = struct.unpack("<H", buf[23:25]), 25
got = []
for t in range(tcount):
    ip = 25 + t*16
    lat0,lon0,off,length = struct.unpack("<iiII", buf[ip:ip+16])
    dlat0, dlon0 = lat0/1e7, lon0/1e7
    p = off
    (ncont,), p = struct.unpack("<H", buf[p:p+2]), p+2
    # (Stufe 1: ncont=0, kein Skip noetig)
    (npk,), p = struct.unpack("<H", buf[p:p+2]), p+2
    for _ in range(npk):
        pdlat,pdlon,ele,rank,nlen = struct.unpack("<hhhBB", buf[p:p+8]); p+=8
        name = buf[p:p+nlen].decode("ascii","ignore"); p+=nlen
        got.append((dlat0+pdlat/1e5, dlon0+pdlon/1e5, ele, name))
print(f"  Selbsttest: v{ver} region={region} -> {len(got)} Gipfel zurueckgelesen")
# Vergleich gegen Original (Position auf ~11 m genau wegen 1e-5 Rundung)
err = 0
src_sorted = sorted(peaks, key=lambda x:-x[2])
got_sorted = sorted(got, key=lambda x:-x[2])
for (a,b) in zip(src_sorted[:5], got_sorted[:5]):
    dlat_m = abs(a[0]-b[0])*111320; dlon_m = abs(a[1]-b[1])*111320*math.cos(a[0]*math.pi/180)
    d = (dlat_m**2+dlon_m**2)**0.5
    print(f"    {a[3][:18]:18s} {a[2]}m  Abweichung {d:.1f} m")
    if d > 2.0: err += 1
print("  OK" if err==0 else f"  WARNUNG: {err} Positionen >2 m ab")
