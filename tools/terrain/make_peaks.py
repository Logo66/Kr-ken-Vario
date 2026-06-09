# make_peaks.py — OSM-Rohgipfel (TSV) -> kompakte peaks.txt fuer das Geraet
# Format Ausgabe: "lat;lon;ele;name" (ASCII, transliteriert)
import os, sys

SRC = os.path.join(os.path.dirname(__file__), "peaks_ch_raw.tsv")
OUT = os.path.join(os.path.dirname(__file__), "peaks.txt")

# Transliteration fuer 1-Bit-Font (ASCII)
TRANS = {
    'ä':'ae','ö':'oe','ü':'ue','Ä':'Ae','Ö':'Oe','Ü':'Ue','ß':'ss',
    'à':'a','â':'a','á':'a','è':'e','é':'e','ê':'e','ë':'e',
    'î':'i','ï':'i','í':'i','ô':'o','ó':'o','ò':'o','û':'u','ú':'u','ù':'u',
    'ç':'c','ñ':'n','’':"'",'-':'-',
}
def repair(s):
    # OSM-Antwort kam UTF-8, wurde aber als Latin-1 gelesen (Mojibake "Ã¼"). Rueckgaengig.
    try:
        return s.encode('latin-1').decode('utf-8')
    except (UnicodeEncodeError, UnicodeDecodeError):
        return s

def translit(s):
    out = []
    for ch in s:
        if ch in TRANS: out.append(TRANS[ch])
        elif ord(ch) < 128: out.append(ch)
        else: pass  # unbekanntes Nicht-ASCII weglassen
    return ''.join(out).strip()

peaks = []
with open(SRC, encoding="utf-8") as f:
    for line in f:
        line = line.rstrip("\n").rstrip("\r")
        if not line.strip(): continue
        parts = line.split("\t")
        if len(parts) < 4: continue
        try:
            lat = float(parts[0]); lon = float(parts[1])
            ele = int(round(float(parts[2])))
        except ValueError:
            continue
        name = translit(repair(parts[3]))
        if not name: continue
        if ele <= 0 or ele > 5000: continue
        peaks.append((lat, lon, ele, name))

print("parsed:", len(peaks))
for thr in [0,600,800,900,1000,1200,1500,2000,2500,3000]:
    print(f"  ele>={thr}: {sum(1 for p in peaks if p[2]>=thr)}")

# Raeumliche Ausduennung: pro Rasterzelle nur hoechsten Gipfel
ELE_MIN = int(sys.argv[2]) if len(sys.argv) > 2 else 700
def thin(cell):
    grid = {}
    for lat,lon,ele,name in peaks:
        if ele < ELE_MIN: continue
        key = (round(lat/cell), round(lon/cell))
        if key not in grid or ele > grid[key][2]:
            grid[key] = (lat,lon,ele,name)
    return list(grid.values())

print(f"\nAusduennung (ELE_MIN={ELE_MIN}):")
for c in [0.04,0.05,0.06,0.08,0.10]:
    print(f"  cell={c}deg -> {len(thin(c))} Gipfel")

CELL = float(sys.argv[1]) if len(sys.argv) > 1 else 0.06
sel = thin(CELL)
sel.sort(key=lambda p: -p[2])
with open(OUT, "w", encoding="ascii") as f:
    for lat,lon,ele,name in sel:
        f.write(f"{lat:.5f};{lon:.5f};{ele};{name[:24]}\n")
print(f"\nCELL={CELL} -> {len(sel)} Gipfel geschrieben")
print("Datei:", OUT, os.path.getsize(OUT), "bytes")
