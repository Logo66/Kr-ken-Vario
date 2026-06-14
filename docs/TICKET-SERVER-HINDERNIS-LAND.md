# TICKET → Server-Baumeister: Hindernisse PRO LAND (ganzes Land, wie die Lufträume)

## Stand 2026-06-14 · Krücke-Firmware: Offline-Land-Modell gebaut, Server-Endpoint offen

---

## Warum (Architektur-Entscheid)
Streckenflug geht heute **bis 400 km**. Der bestehende `GET /obstacles?lat&lon&r` (Umkreis, gekappt auf 8'000) deckt das **nicht** ab — man fliegt aus der Blase raus, und **im Flug gibt es kein Netz** (Alpen, 3000 m).

Deshalb: Hindernisse funktionieren **genau wie die Lufträume** —
1. **ganzes Land** liegt auf der **SD** des Geräts,
2. wird beim Boot in **PSRAM** geladen,
3. **im Flug OFFLINE** abgefragt (kein Netz),
4. beim Neustart ein **kurzer ETag-Abgleich** (304 = unverändert → kein Re-Download).

Das ist das Skytraxx-Modell. Der Radius-Endpoint bleibt als **Reissleine** (leere SD → wenigstens die Umgebung), ist aber nicht mehr die Grundlage.

---

## Was ich brauche: ein Pro-Land-Endpoint

```
GET  {SERVER}/obstacles/country?lat=<lat>&lon=<lon>
     Authorization: Bearer <device_token>          (wie Map-Packs)
     If-None-Match: <etag>                          (optional — Neustart-Abgleich)
 →   200 text/plain   Body = GANZE Land-Datei
         lat1;lon1;lat2;lon2;top_m;type            (6 Dezimalstellen, eine Zeile je Segment)
     Header: ETag · X-SHA256 · X-Obstacle-Count · X-Country (ISO, z.B. "CH")
     304   bei If-None-Match unverändert (Body leer) → Gerät behält SD-Datei
     404   wenn für (lat,lon) (noch) kein Land-Datensatz existiert
```

- **Land serverseitig** aus `(lat,lon)` bestimmen (welches Land deckt den Punkt) → ganze Datei dieses Landes. Das Gerät schickt nur seinen GPS-Standort; es muss das Land **nicht** selbst kennen.
- **Auth wie Map-Packs** (Bearer Device-Token, Pfad nicht öffentlich).
- **ETag/SHA/Count** wie beim Radius-Endpoint — der Neustart-Abgleich ist damit billig (304 wenn unverändert).

### Format (identisch zum jetzigen, ohne Name)
`lat1;lon1;lat2;lon2;top_m;type`
- **LineString** (Kabel/Leitung/Seilbahn) → **je Spannfeld ein Segment** (zwei Endpunkte).
- **Point** (Mast/Windrad/Kran) → `lat2/lon2 == lat1/lon1`.
- `top_m` = `topElevationAMSL` gerundet (Oberkante über Meer).
- **`type`:** `0`=Mast/Antenne (inkl. STACK/CHIMNEY), `1`=Kabel/Leitung (TRANSMISSION_LINE, CATENARY, CABLE_CAR), `2`=Seilbahn (CABLEWAY/ROPEWAY), `3`=Windrad, `4`=Kran.
- **RAUS (sichtbar, kein Warnwert):** BUILDING, BRIDGE, TREE, VEGETATION.
- Name-Feld **weglassen** (Gerät nutzt es nicht → spart ~25 % Dateigrösse + RAM).

### Grösse / Transfer
- **CH = ~104'446 Segmente = ~4,7 MB** (Text). Andere Länder ähnliche Grössenordnung.
- Optional **gzip** (`Content-Encoding`) — Text komprimiert ~5× (~1 MB). ⚠️ Der ESP32-`HTTPClient` entpackt gzip **nicht automatisch**; bis das geräteseitig steht, bitte **unkomprimiert** ausliefern (oder per `Accept-Encoding: identity` steuerbar lassen).

---

## Pro Land = mehrere nationale Quellen
„Für jedes bekannte Land, wo es die Datenbanken gibt." CH ist BAZL (geo.admin.ch, läuft schon). Bitte ergänzen, wo offene Luftfahrthindernis-Daten existieren — z. B. **DE, AT, FR, IT, LI** etc. (du kennst die Datenlage besser). Pro Land: einlesen → gleiches Segment-/Typ-Schema → täglich/wöchentlich regenerieren → ETag ändert sich nur bei echter Änderung.

---

## Geräte-Seite (mein Part, Folge-Schritt)
- `obstacle_fetch.h` von `?lat&lon&r` auf `/obstacles/country?lat&lon` umbiegen + **Neustart-ETag-Abgleich** (If-None-Match → 304 → SD behalten).
- Bis dahin lädt die Krücke die **ganze Land-Datei manuell** (SD `/obstacles/obstacles.txt`, schneller Loader steht, ~104k in wenigen Sekunden, Bounding-Box-Vorfilter im Flug).
- Sag mir, wenn der Endpoint steht — dann verdrahte ich den Auto-Abgleich.
