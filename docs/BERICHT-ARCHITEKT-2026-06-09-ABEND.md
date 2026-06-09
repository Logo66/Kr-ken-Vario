# Bericht an den Architekten — Aura Krücke
## Session 2026-06-09 (Abend) · Gerät: LilyGo T5 E-Paper S3 Pro

**Alles in diesem Bericht ist auf der echten Hardware verifiziert.** RAM 18.8 %, Flash 24.3 %.

---

## Kurzfassung
Sehr produktive Session: Brick-Recovery → kompletter Vektor-Karten-Umbau → neuer
**Luftraum-Schnitt-Screen** → und ein **Wurzel-Fix für WiFi/BLE** (Speicher). Dazu zwei neue
Tickets (Schnitt, Map-Pack) und ein Feinschliff-Backlog.

---

## 1. Recovery (Start der Session)
- Gerät nach PC-Neustart eingefroren („Menü sichtbar, tot").
- **Diagnose (belegt):** E-Paper hält sein Bild auch nach Absturz → „Menü" war Geisterbild der
  abgestürzten/uncommitteten Firmware. Zusätzlich: USB-CDC-Auto-Reset greift nach PC-Neustart
  beim Flashen nicht zuverlässig.
- **Recovery:** zurück auf letzten *geflashten* Stand `f627657`, neu gebaut + geflasht → Gerät lief
  wieder (Serial-Beweis: Boot, Sensoren, GPS-FIX, FANET).

## 2. Vektor-Lufträume (KRUECKE-6B)
- **Root-Cause Parser:** openaip.net liefert Leerzeichen-Format `47:13:4.0080 N 008:55:26.0040 E`
  (nicht komma-getrennt). Neuer `parseLatLon()` deckt beide Formate ab → **192 Lufträume** laden.
- `drawAirspaces()` zeichnet dicke Polygone + Klasse/Grenzen-Label; Raster-Tiles Default AUS.
- Neuer `parseAltM()` (FL / ft / GND / m → Meter) für Floor/Ceiling im Schnitt.

## 3. Gipfel-Layer (KRUECKE-6C Stufe 1)
- PC-Pipeline `tools/terrain/make_peaks.py`: OSM-Overpass → 14'466 CH-Gipfel →
  **räumliche Ausdünnung** (höchster Gipfel je 0.09°-Zelle, ele≥450) → **1'036 Gipfel**.
  Inkl. UTF-8-Mojibake-Fix + ASCII-Transliteration (Umlaute) fürs 1-Bit-Font.
- Ausgabe `peaks.txt` (`lat;lon;ele;name`), Geräte-Renderer `drawPeaks()` (Dreieck + Name + Höhe).
- **HINWEIS:** Dieser PC-`peaks.txt`-Weg wird durch das **Pack-Format (KRUECKE-8)** ersetzt.

## 4. Karten-Rendering-Fixes
- **Zoom-Stufen 20 km + 50 km** ergänzt (Flachland-Sicht — nächster Gipfel war 14 km weg).
- **Ghosting behoben (drei Ursachen):**
  1. Karte raus aus dem 1-Hz-`MODE_DU`-Overlay (Ticket §5: kein Partial-Geschiebe).
  2. `memset(back_fb)` in `showMapScreen` **entfernt** — bei dünner Vektor-Karte überschreiben die
     weißen Flächen sonst den Vorscreen nicht → Ghosting beim Karten-Eintritt.
  3. **GC16 beim Verlassen** der Karte (Swipe/BOOT-Knopf/Menü) gegen Carry-over auf andere Screens.
- Luftraum-Labels nur ≤10 km Zoom (Clutter) + in-Fenster geklemmt (nicht in Statusleiste).

## 5. Luftraum-Schnitt-Screen (KRUECKE-7 — NEU)
- Neuer Flug-Screen (Seitenansicht): **Höhe über Distanz**, Luftraum voraus per
  **Strahl-Polygon-Schnitt entlang Heading**, projizierter **Gleitpfad**, **Konflikt-Erkennung**,
  Daten-Spalte mit **invertiertem KONFLIKT-Balken** (weiß auf schwarz).
- Projektion **als Formel aus dem Mockup rückgerechnet** (kein Raten):
  `x = 96 + km·49`, `y = 494 − m·0.111`.
- Eigene Position als **Gleitschirm-Symbol**. Stufe 1 ohne Terrain-Masse (→ Stufe 2, braucht Höhendaten).
- Ticket: `docs/AURA-KRUECKE-7-LUFTRAUMSCHNITT.md`.

## 6. WiFi/BLE-Wurzelfix (WICHTIG — Speicher)
- **Symptom:** WiFi „Timeout (Code 255)" = `WL_NO_SHIELD` (WiFi initialisiert gar nicht);
  BLE-Einschalten → **Gerät rebootet**.
- **Diagnose (belegt durch den Code-255-Fingerzeig):** Luftraum-Array (115 KB) + Gipfel-Array
  (42 KB) lagen im **DRAM** → RAM 67.8 % → **zu wenig DRAM-Heap** für die WiFi/BLE-Initialisierung.
- **Fix:** beide Arrays per `heap_caps_malloc(MALLOC_CAP_SPIRAM)` nach **PSRAM** (einmalig, kein
  realloc). **RAM 67.8 % → 18.8 %.** WiFi **und** BLE laufen wieder (vom Piloten bestätigt).
- **Architektur-Lehre:** Große Datenstrukturen gehören in PSRAM (8 MB frei); der knappe interne
  DRAM-Heap muss für Funk (WiFi/BLE) frei bleiben.

## 7. Pack-Format (KRUECKE-8 — NEU, strategisch)
- **Entscheidung:** Kartendaten (Höhenlinien/Gipfel/Luftraum) kommen künftig als **binäre `.pack`**
  vom Server (Rechner 2). Gerät **liest + zeichnet nur**, erzeugt nichts. Ersetzt den PC-`peaks.txt`-Weg.
- Byte-genaues Format als **„heiliger Vertrag" (§3)** dokumentiert: `docs/AURA-KRUECKE-8-MAP-PACK.md`.
- Stufenplan: Pack-Reader → Höhenlinien → `MODE_DU`-Refresh + PSRAM-Frame-Cache (5-s-Ruck weg) →
  WLAN-Download/SHA-256-Verify/Entpacken → Luftraum/Hindernis.

---

## Aktueller Stand (läuft, hardware-verifiziert)
| Bereich | Status |
|---|---|
| Cruise / Thermik / Ziel / Karte / **Schnitt** | ✅ |
| Sensoren (BMP581, SHT45, LSM6DSO32) · RTC · Akku | ✅ |
| GPS (L76K) · FANET RX (echtes Paket empfangen) | ✅ |
| **WiFi · BLE** | ✅ (nach PSRAM-Fix) |
| Vektor-Lufträume (192) · Gipfel (1036) | ✅ (PSRAM) |

## Offene Punkte (nach Priorität)
1. **Pack-Pipeline Stufe 2–4:** Pack-Reader (§3) + Karten-Verwaltungs-Screen (laden · prüfen ·
   entpacken · alte löschen). Braucht Server-Endpoint (Rechner 2).
2. **Terrain-Masse** für den Schnitt (Höhendaten aus Pack).
3. **Feinschliff-Backlog** (`docs/FEINSCHLIFF-BACKLOG.md`): einheitliche Statusbar auf allen Screens,
   Buddy-Verbindungsanzeige auf Cruise, Thermik-Layout vereinheitlichen, Fadenkreuz-Button von der
   Karte entfernen (+/− größer), Gleitzahl-Anzeige prüfen, Diagnose-Zähler „G/L" entfernen.
4. **FANET TX spec-konform** (Sicherheit — aus früherem Architekt-Review).
5. **Anti-Ghosting-Zähler** (periodischer GC16 nach N DU-Refreshes).

## Risiken / Hinweise
- Karte + Schnitt sind **„Hilfe, keine Gewähr"** — im UI so gelabelt halten (Daten ab 25 m, nicht vollständig).
- USB-CDC-Auto-Reset nach PC-Neustart unzuverlässig → manueller Reset als Fallback beim Flashen.
- Diagnose-Zähler „G<Gipfel> L<Lufträume>" auf der Karte ist **temporär** (wieder entfernen).
- Großdaten ausschließlich über WLAN laden, **nie über BLE**; kein Firmware-OTA über diesen Weg.

---
*Erstellt von der Code-Krücke (Gerät-Seite). Branch dieser Session: `aura-kruecke/2-vario-map`.*
