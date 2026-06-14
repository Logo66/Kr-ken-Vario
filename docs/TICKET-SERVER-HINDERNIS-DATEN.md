# TICKET → Server-Baumeister: Hindernis-Daten generieren + nach Standort bereitstellen

## Stand 2026-06-14 · Entscheid Ivo: **Server generiert + hostet, Krücke holt nach Standort**

> Wie bei den Karten-Packs: kein SD-Kopieren, kein On-Device-Parsen von Riesen-Files.
> Die **Konsum-Seite auf der Krücke ist fertig** (liest `/obstacles/obstacles.txt`, warnt via
> 3D-Schutzkugel). Offen: (1) **du** generierst + hostest die Daten, (2) die Krücke holt sie
> nach Standort (Firmware-Fetch, bauen wir, sobald die API unten steht).

---

## 1. Datenquelle (amtlich, tagesaktuell)
BAZL Luftfahrthindernisse, STAC-Asset **`luftfahrthindernis_4326.kmz`** (WGS84):
`https://data.geo.admin.ch/api/stac/v0.9/collections/ch.bazl.luftfahrthindernis/items` → Asset `luftfahrthindernis_4326.kmz`.
- KMZ = gezippte KML, **14’319 Hindernisse**, je Placemark `obstacleType`, `topElevationAMSL`, `MultiGeometry` (Point + LineString, `lon,lat,alt`). Täglich ~04:00 aktualisiert.

## 2. Generierung — **Referenz-Konverter liegt bei**
`tools/obstacles/bazl_to_obstacles.py` (im Repo, in Downloads) macht die ganze Logik schon — bitte als Referenz/Basis nehmen:
- **Filtert sichtbares raus** (Gebäude, Brücke, Baum, Vegetation = `type 5`). Pilot fliegt auf Sicht.
- **Behält die schlecht sichtbaren** Gefahren. Typ-Mapping:

| code | Bedeutung | BAZL obstacleType |
|---|---|---|
| 0 | Mast/Antenne | POLE, STACK, CHIMNEY, SILO, (Default) |
| 1 | Kabel/Leitung | TRANSMISSION_LINE, CATENARY, POWER, WIRE, OVERHEAD |
| 2 | Seilbahn | CABLE_CAR, CABLEWAY, ROPEWAY |
| 3 | Windrad | WINDMILL |
| 4 | Kran | CRANE |
| 5 | **RAUS (sichtbar)** | BUILDING, BRIDGE, TREE, VEGETATION |

- **LineString → einzelne Spannfeld-Segmente** (je zwei aufeinanderfolgende Stützpunkte = ein Eintrag). **Point → Punkt** (`lat2/lon2 == lat1/lon1`).
- `top_m` = `topElevationAMSL` (Oberkante m ü. M., ganzzahlig gerundet).
- **Flugbarkeits-/Standort-Filter:** ganze CH = 104’446 Segmente (zu viel). Pro Standort/Region (Bounding-Box) filtern → der Konverter kann das schon (`argv = S W N E`). Ostschweiz/Hörnli-Box ≈ 6’400.

## 3. Dateiformat (`obstacles.txt`, ASCII, WGS84)
Eine Zeile pro Hindernis(-Segment), `#` = Kommentar:
```
lat1;lon1;lat2;lon2;top_m;type;name
47.399157;7.028390;47.398639;7.029427;501;1;
47.605700;8.768200;47.605700;8.768200;1133;0;
```
- Dezimalgrad, **6 Nachkommastellen**. `name` optional (darf leer sein; Krücke zeigt sonst nur den Typ).
- Max **8’000 Zeilen pro Antwort** (Krücke `OBST_MAX = 8000`, PSRAM).

## 4. API — Krücke holt **nach Standort** (Vorschlag, bitte bestätigen)
```
GET  {SERVER}/obstacles?lat=<lat>&lon=<lon>&r=<radius_km>
Authorization: Bearer <device_token>        # gleicher Auth-Weg wie Map-Packs
-> 200 text/plain   Body = obstacles.txt (Format §3)
   Header: ETag oder X-SHA256 (für Cache/Verify)
```
- **`r` Default 40 km** (Gleitschirm-Reichweite); Server liefert nur Hindernisse in dem Umkreis/der Box, ≤ 8’000 Segmente.
- Alternativ **tiled** (`/obstacles/{lat0}_{lon0}.txt`, 0.5°-Kacheln) — falls dir Caching lieber ist; die Krücke kann beides, sag was dir passt.
- **Sicherheit:** Bearer/Werks-Token **wie Map-Packs** (kein Secret im Klartext, nicht ins Repo/Log).

## 5. Krücke-Verhalten (Fetch — bauen wir nach API-OK)
- Holt Hindernisse **bei erstem Bedarf** und **neu, wenn Standort > r/2 vom letzten Fetch** wegwandert **oder Cache > 14 Tage** alt ist.
- Speichert nach `/obstacles/obstacles.txt`, dann `parseObstacles()` → 3D-Kugel ist scharf.
- Offline/kein Server → letzter Cache bleibt gültig (Hindernisse ändern sich langsam).

## 6. Was schon fertig ist (Krücke, committet)
- **3D-Schutzkugel** (zwei Radien `warn.sphere_outer_m`/`inner_m`, live über App), Punkt-zu-Segment-Abstand für Kabel, Kapsel-Höhenmodell (unter Oberkante zählt horizontale Distanz).
- **Parser** `/obstacles/obstacles.txt` (Format §3), PSRAM, Boot-Selbsttest (`[KUGEL-TEST]`).
- Konverter `bazl_to_obstacles.py` als Referenz.

**Dein Part:** §1–4 (BAZL → obstacles.txt nach Standort, gehostet + Auth). **Mein Part:** §5 (Fetch), sobald die API in §4 steht.
