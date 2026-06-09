# TICKET — KRÜCKE (Rechner 2, T5-Gerät): Karte aus Pack lesen & rendern

**Für:** Code-KRÜCKE · Rechner mit dem LilyGo T5 E-Paper · Projekt `aura_kruecke`.
**Aufgabe:** Das vom Server gelieferte Region-Pack **lesen**, Höhenlinien + Gipfel als
**1-Bit-Vektor** zeichnen, per WLAN laden/prüfen/entpacken. Raster-Tiles als Hauptkarte raus.
**Grenze:** Du **erzeugst keine** Kartendaten — die kommen fertig als `.pack` vom Server.
Du liest sie und zeigst sie an.
**Verbindliche Optik:** `aura_map_bw_960x540.svg` (Konturringe + „Niesen 2362"-Marker).
**Arbeitsregel:** ein Ziel, ein Beweis, STOP. Position immer gegen bekannten Berg prüfen.

> ERSETZT den PC-seitigen `peaks.txt`-Weg (KRUECKE-6C). Der Pack-Reader ist die neue Quelle.
> Blockiert, bis der Server (Rechner 2) das erste Pack liefert — bis dahin Reader gegen §3 bauen.

---
## 0. PROBLEM (warum Umbau)
Aktuell: OSM-Raster-PNGs von SD → dekodiert → geschwellt → GC16-Voll-Refresh → **5 s Ruck, schwarzer Brei**.
Neu: **Vektor aus Pack** (Höhenlinien/Gipfel/Luftraum) → schnelles Zeichnen → **MODE_DU**.

## 1. RASTER RAUS
- OSM-Raster ist **nicht** mehr die Karte. Bestehender Tile-Code darf bleiben, aber nur als
  optionaler Hintergrund hinter Einstellung „Hintergrundkarte" — **Default AUS**.
- Im Flug-Default **kein PNG dekodieren**.

## 2. LAYER (zeichnen, Hierarchie über Linienstärke; KEINE Flächen)
| Layer | Quelle | Darstellung |
|---|---|---|
| Höhenlinien | Pack `/terrain/` | normal **1.5 px**, Index(500m) **2.5 px** |
| Gipfel | Pack `/peaks/` | gefülltes Dreieck + Name + Höhe (wie Mockup) |
| Luftraum | Pack/`/airspace/` (wenn vorhanden) | **dick 4 px**, Klasse+Untergrenze beschriftet |
| Hindernis | Pack/`/obstacles/` | Symbol + Höhe |
| Eigene Position / Track | GPS | Dreieck Mitte (400,288) / Track 3.5 px (vorhanden) |
- **NICHT zeichnen:** Wald, Gebäude, Landnutzung.

## 3. 🔒 PACK-FORMAT — HEILIGER VERTRAG (identisch im Server-Ticket)
> **Server schreibt nach diesem Format, du liest exakt danach. Ändern nur im Doppel.**
```
Datei: region_<name>_v<N>.pack  (Little-Endian)
HEADER:
  magic        4 Bytes  "AURA"
  version      uint8    Formatversion = 1
  region_id    char[16] (null-terminiert)
  tile_size    uint16   Kachelkantenlänge in 1/100 Grad
  tile_count   uint16
TILE-INDEX (tile_count Einträge):
  tile_lat0    int32    Origin lat in 1e-7 Grad (SW-Ecke)
  tile_lon0    int32    Origin lon in 1e-7 Grad
  offset       uint32   Byte-Offset des Tile-Blocks
  length       uint32   Länge des Tile-Blocks
TILE-BLOCK (je Kachel):
  n_contours   uint16
  je Kontur: height_m int16, flag uint8 (0=normal,1=Index), n_points uint16,
             Punkte: int16 dlat, int16 dlon  (rel. Origin, 1e-5 Grad)
  n_peaks      uint16
  je Gipfel: dlat int16, dlon int16, height_m int16, rank uint8,
             name_len uint8, name char[name_len] (UTF-8)
  # Luftraum/Hindernis: analoge Blöcke, in v1 evtl. leer (n=0)
```
- Koordinaten **WGS84**. Rekonstruktion: `lat = tile_lat0/1e7 + dlat/1e5`, analog lon.
- `rank` (Gipfel): kleiner = wichtiger → bei hohem Zoom nur niedrige Ränge zeichnen.

## 4. RENDERER
- Header lesen, Tile-Index laden, **nur sichtbare Kacheln** (BBox-Schnitt) dekodieren.
- Projektion exakt wie AURA-KRUECKE-6 §5 (north-up, eigene Position (400,288), m/px je Zoom, Haversine, clippen).
- **Voll-Refresh nur bei Zoom/Re-Center/spürbarer Bewegung**; Wisch zeigt gecachten PSRAM-Frame sofort.
- Sobald Karte reines 1-Bit-Vektor ist: **Refresh GC16 → MODE_DU** (statt 1–2 s nur ~75–300 ms).
- Konturen unter Luftraum/Hindernis/Track.

## 5. 1-Bit-Lesbarkeit
- Hoher Zoom (5/10 km): nur Index-Konturen + wichtigste Gipfel (niedriger rank).
- Beschriftung sparsam: Index-Konturen / Gipfel, nie jede Linie.

## 6. WLAN-DOWNLOAD (Menü „Karte → Region/Update")
- `/maps/index` holen (Auth `X-Buddy-Key`) → Region wählen → `/maps/<r>/download` laden (Range/resumable).
- **SHA-256 gegen Manifest prüfen** → erst dann auf SD entpacken nach `/terrain/ /peaks/ /airspace/ /obstacles/`.
- Gleiche Version (Checksumme) nicht erneut laden. Nur Daten über WLAN — kein Firmware-OTA. Großdaten nie über BLE.

## 7. GATEs
| Gate | Bedingung |
|---|---|
| G1 | Raster nicht mehr Default-Karte; kein Wald-/Gebäude-Brei (Clutter-Check). |
| G2 | Pack nach §3 byte-genau gelesen (Header „AURA" erkannt, Tiles dekodiert). |
| G3 | Gipfel + Höhenlinien an **korrekter Position** (gegen bekannten Berg geprüft — sonst Koordinaten-/Delta-Fehler). |
| G4 | Verschachtelte Ringe wie Mockup; Index dick/normal dünn; alle 5 Zoomstufen lesbar. |
| G5 | Karten-Refresh läuft auf **MODE_DU**; Wisch nicht mehr 5 s (gecachter Frame sofort). |
| G6 | WLAN-Update: Pack geladen, SHA-256 ok, entpackt, Karte rendert daraus. |

## 8. Stufenplan
1. **Pack-Reader** + Gipfel zeichnen (Server liefert zuerst ein Gipfel-Pack). STOP & Position prüfen.
2. Höhenlinien zeichnen (normal/Index). STOP & gegen bekannten Berg prüfen.
3. Refresh auf MODE_DU + Frame-Cache (5-s-Ruck weg). **Vorher** die 5 s einmal mit Zeitstempeln messen.
4. WLAN-Download/Verify/Entpacken im Menü.
5. Luftraum/Hindernis-Layer (wenn im Pack).

## 9. Stop / Heilig
- Du **erzeugst keine** Kartendaten — nur Pack lesen & zeichnen.
- **Pack-Format (§3) ist Vertrag** — nicht einseitig ändern.
- Kein Raster als Default-Hauptkarte; keine On-Device-Konturberechnung.
- Terrain/Luftraum = **Hilfe, keine Gewähr** labeln.
- Andere Screens / Sensorlogik / BLE / FANET / `buddy_chat` unangetastet. 1-Bit S/W, fette Fonts.
