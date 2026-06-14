#!/usr/bin/env python3
# bazl_to_obstacles.py — BAZL-Luftfahrthindernis-KMZ -> /obstacles/obstacles.txt
#
# Quelle: https://data.geo.admin.ch/.../luftfahrthindernis_4326.kmz  (WGS84)
# Ziel je Zeile:  lat1;lon1;lat2;lon2;top_m;type;name
#   - LineString (Kabel/Leitung/Seilbahn): je Spannfeld ein Segment.
#   - Point (Mast/Antenne/Windrad/Kran): lat1==lat2.
#   - type: 0=Mast/Antenne 1=Kabel/Leitung 2=Seilbahn 3=Windrad 4=Kran 5=sichtbar(raus)
#
# Filter: nur SCHLECHT SICHTBARE Gefahren. Gebaeude/Bruecken/Baeume (sichtbar) raus.
# Optional Bounding-Box (Region): argv = S W N E  (sonst ganze Schweiz).
#
# Aufruf:  py bazl_to_obstacles.py [pfad.kmz] [S W N E]
import zipfile, re, os, sys
from collections import Counter

def typecode(t):
    t = t.upper()
    if 'BUILD' in t or 'BRIDGE' in t or 'TREE' in t or 'VEGETATION' in t: return 5   # sichtbar -> raus
    if 'CABLE_CAR' in t or 'CABLEWAY' in t or 'ROPEWAY' in t or 'SEILBAHN' in t: return 2
    if 'CATENARY' in t or 'CABLE' in t or 'POWER' in t or 'OVERHEAD' in t or 'WIRE' in t \
       or 'LINE' in t or 'TRANSMISSION' in t: return 1
    if 'WIND' in t: return 3
    if 'CRANE' in t: return 4
    if 'STACK' in t or 'CHIMNEY' in t or 'SILO' in t: return 0         # hoher Punkt -> wie Mast
    return 0  # MAST/POLE/ANTENNA/Default

KEEP = lambda c: c in (0, 1, 2, 3, 4)   # 5 (sichtbar) fliegt raus

args = sys.argv[1:]
kmz = args[0] if args and args[0].lower().endswith(('.kmz', '.zip')) else os.path.join(os.environ.get('TEMP', '.'), 'bazl.kmz')
bbox = None
nums = [a for a in args if re.fullmatch(r'-?\d+(\.\d+)?', a)]
if len(nums) == 4:
    s, w, n, e = map(float, nums); bbox = (s, w, n, e)

z = zipfile.ZipFile(kmz)
kmlname = [x for x in z.namelist() if x.endswith('.kml')][0]
data = z.read(kmlname).decode('utf-8', 'replace')

def inbox(lat, lon):
    if not bbox: return True
    s, w, n, e = bbox
    return s <= lat <= n and w <= lon <= e

cnt, kept_types = Counter(), Counter()
out = []
for pm in re.findall(r'<Placemark\b.*?</Placemark>', data, re.S):
    mt = re.search(r'name="obstacleType">([^<]*)<', pm)
    otype = mt.group(1) if mt else '?'
    cnt[otype] += 1
    code = typecode(otype)
    if not KEEP(code):
        continue
    me = re.search(r'name="topElevationAMSL">([^<]*)<', pm)
    try:    top = int(round(float(me.group(1)))) if me else 0
    except: top = 0
    ls = re.search(r'<LineString>.*?<coordinates>([^<]*)</coordinates>', pm, re.S)
    pts = []
    if ls:
        for c in ls.group(1).split():
            p = c.split(',')
            if len(p) >= 2: pts.append((float(p[1]), float(p[0])))
    else:
        ps = re.search(r'<Point>.*?<coordinates>([^<]*)</coordinates>', pm, re.S)
        if ps:
            p = ps.group(1).strip().split(',')
            if len(p) >= 2: pts.append((float(p[1]), float(p[0])))
    if not pts:
        continue
    if not any(inbox(la, lo) for la, lo in pts):
        continue
    kept_types[code] += 1
    if len(pts) == 1:
        la, lo = pts[0]
        out.append(f"{la:.6f};{lo:.6f};{la:.6f};{lo:.6f};{top};{code};")
    else:
        for i in range(len(pts) - 1):
            a, b = pts[i], pts[i + 1]
            out.append(f"{a[0]:.6f};{a[1]:.6f};{b[0]:.6f};{b[1]:.6f};{top};{code};")

print("OBSTACLE-TYPE VERTEILUNG (gesamt):")
for t, c in cnt.most_common():
    print(f"  {t:24s} {c:6d}  -> code {typecode(t)}{'  (RAUS)' if not KEEP(typecode(t)) else ''}")
print("BEHALTEN je code:", dict(kept_types))
print("EMITTIERTE ZEILEN:", len(out))

outdir = os.path.dirname(os.path.abspath(__file__))
suffix = "_region" if bbox else "_ch"
path = os.path.join(outdir, f"obstacles{suffix}.txt")
with open(path, "w", encoding="ascii", errors="replace", newline="\n") as f:
    f.write("# BAZL Luftfahrthindernisse -> Schutzkugel  lat1;lon1;lat2;lon2;top_m;type;name\n")
    f.write("\n".join(out) + "\n")
print("GESCHRIEBEN:", path, os.path.getsize(path), "bytes")
