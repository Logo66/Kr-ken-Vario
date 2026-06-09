# make_contract.py — erzeugt Vertrags-Test-Packs (AURA-KRUECKE-8 §3) fuer den Cross-Read.
# - contract_test_v1.pack : 1 Tile, 2 Konturen (normal+Index), 1 Peak mit UTF-8-Umlaut, gemischt
# - contract_bad_magic.pack   : Magic "XURA"  (P4)
# - contract_bad_version.pack : version = 2    (P4)
# Gibt die ERWARTETEN Werte aus (= expected, zum Vergleich mit dem Serial-Dump des Geraets).
import struct, os
HERE = os.path.dirname(__file__)

TILE_SIZE_FIELD = 25
# 1 Tile, Origin 46.0000000 N / 8.0000000 E
LAT0, LON0 = 46.0, 8.0
lat0_1e7, lon0_1e7 = int(LAT0*1e7), int(LON0*1e7)

# Konturen: (height_m, flag, [(dlat,dlon) in 1e-5 rel Origin])
contours = [
    (2000, 0, [(1000,1000),(1000,2000),(2000,2000),(2000,1000)]),   # normal, 4 Punkte
    (2500, 1, [(1500,1500),(1500,1800),(1800,1500)]),               # Index, 3 Punkte
]
# Peak mit Umlaut (UTF-8): "Hoernli" als "Hörnli"
peak_name = "Hörnli"            # H ö r n l i  -> ö = 0xC3 0xB6
peak = (1700, 1600, 1133, 2, peak_name)   # dlat,dlon,ele,rank,name

# --- Tile-Block bauen ---
b = bytearray()
b += struct.pack("<H", len(contours))
for h, flag, pts in contours:
    b += struct.pack("<hBH", h, flag, len(pts))
    for da, dn in pts:
        b += struct.pack("<hh", da, dn)
b += struct.pack("<H", 1)            # n_peaks = 1
da, dn, ele, rank, name = peak
nm = name.encode("utf-8")
b += struct.pack("<hhhBB", da, dn, ele, rank, len(nm))
b += nm
block = bytes(b)

# --- Datei bauen ---
def build(magic=b"AURA", version=1):
    out = bytearray()
    out += magic
    out += struct.pack("<B", version)
    rid = b"CONTRACT"
    out += rid + b"\x00"*(16-len(rid))
    out += struct.pack("<H", TILE_SIZE_FIELD)
    out += struct.pack("<H", 1)                 # tile_count
    data_start = 25 + 16
    out += struct.pack("<iiII", lat0_1e7, lon0_1e7, data_start, len(block))
    out += block
    return bytes(out)

open(os.path.join(HERE,"contract_test_v1.pack"),"wb").write(build())
open(os.path.join(HERE,"contract_bad_magic.pack"),"wb").write(build(magic=b"XURA"))
open(os.path.join(HERE,"contract_bad_version.pack"),"wb").write(build(version=2))

# --- ERWARTETE WERTE (expected) ausgeben ---
print("=== contract_test_v1.pack — ERWARTETE Cross-Read-Werte ===")
print(f"HEADER magic=AURA version=1 region=CONTRACT tile_size={TILE_SIZE_FIELD} tile_count=1")
print(f"TILE 0 origin={LAT0:.7f},{LON0:.7f}")
print(f"  n_contours={len(contours)}")
for h, flag, pts in contours:
    f = (LAT0+pts[0][0]/1e5, LON0+pts[0][1]/1e5)
    l = (LAT0+pts[-1][0]/1e5, LON0+pts[-1][1]/1e5)
    print(f"  CONTOUR h={h} flag={flag} pts={len(pts)} first={f[0]:.5f},{f[1]:.5f} last={l[0]:.5f},{l[1]:.5f}")
print(f"  n_peaks=1")
print(f"  PEAK {LAT0+da/1e5:.5f},{LON0+dn/1e5:.5f} ele={ele} rank={rank} name={name}  (UTF-8 bytes: {nm.hex(' ')})")
print()
print("Dateien:", os.path.getsize(os.path.join(HERE,'contract_test_v1.pack')), "bytes (gut),",
      "+ contract_bad_magic.pack, contract_bad_version.pack")
