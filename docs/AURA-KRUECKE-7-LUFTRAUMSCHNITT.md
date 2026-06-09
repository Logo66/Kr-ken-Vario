# AURA-KRUECKE-7 — Luftraum-Schnitt (Cross-Section / Seitenansicht)

**Der letzte Screen der Krücke.** Verbindliche Optik: das gelieferte SVG-Mockup
(„LUFTRAUM VORAUS · 247°"). Nutzt dieselben OpenAir-Daten wie die Karte — seitlich statt von oben.
Arbeitsregel: ein Ziel, ein Beweis, STOP.

---

## 0. Zweck (in einem Blick)
Die **vertikale Dimension über die Distanz**, die eine Draufsicht-Karte nicht zeigt:
Projiziert deinen Gleitpfad nach vorn und markiert, wo er einen Luftraum-Floor voraus verletzt.

## 1. Daten — vorhanden / fehlt (NICHT raten)
| Eingabe | Status |
|---|---|
| OpenAir-Lufträume (Polygone, AL/AH-Strings) | ✅ vorhanden (192 geladen) |
| GPS Höhe (MSL), Heading, Groundspeed | ✅ vorhanden (live-Struct) |
| **AL/AH → Meter** parsen ("2500ft MSL", "FL100", "GND", "m") | ⚠️ neuer Mini-Parser nötig |
| **Gleitzahl GR** (geglättet aus Groundspeed / Sinkrate) | ⚠️ vorhanden, aber bei 0-Sinken undefiniert → dann level |
| **Terrain-Höhenprofil entlang Pfad** (die schwarze Boden-Masse) | ❌ FEHLT — braucht 6C-Höhendaten |

→ **Stufe 1 ohne Terrain-Masse** (dünne GND-Linie als Platzhalter). Masse kommt mit 6C-Höhenraster.

## 2. Layout (Pixel aus Mockup, 960×540)
- **Statusbar** identisch zu allen Screens, inkl. **BUDDY**-Punkt + Text (Feinschliff-Backlog: einheitlich).
- **Titel** y≈88: ausgefülltes Dreieck + `LUFTRAUM VORAUS · <bearing>°`.
- **Chart-Rahmen**: `x14 y100 w632 h372`. Plot-Bereich innen: X 70..634, Y 166..440 (+ Reserve bis y100).
- **Daten-Spalte rechts**: Trennlinie `x660 y108..464`, Werte ab x672.

## 3. Projektion (FORMELN — exakt aus Mockup rückgerechnet)
```
Distanz → X :  x_px = 70 + dist_km * 56.4          # 0 km→70, 10 km→634
Höhe    → Y :  y_px = 440 - alt_m * 0.09133         # GND→440, 1000→349, 2000→257, 3000→166
```
- Achsen-Ticks: Y = GND/1000/2000/3000 m ; X = 0/2/4/6/8/10 km.
- Sichtfenster: **0..10 km** voraus, **0..~3500 m**.
- Verifikation Mockup: JETZT 2847 m → y180 ✓ ; Floor 2500 → y211 ✓ ; Gleitpfad-Ende 10 km → 2014 m (GR 12).

## 4. Logik
1. **Heading φ** aus GPS (bei wenig Speed: letzter Track-Vektor). Titel zeigt φ.
2. **Vorausstrahl** von Pilot-Position entlang φ. Für jeden Luftraum: Strahl-Polygon-Schnitt →
   Eintritts-Distanz `d_in` (km). **Nächsten voraus** (kleinstes d_in ≥ 0) wählen.
3. **Floor f**, **Ceiling c** aus AL/AH parsen (→ Meter).
4. **Block**: Rechteck x = X(d_in)..634, y = Y(c)..Y(f). **Floor-Linie dick (4px) + Schraffur darunter.**
   Label `CTR D` + `Floor <f>` links oben im Block.
5. **JETZT**: horizontale **gestrichelte** Linie bei Y(alt_now), Dreieck + `JETZT <alt_now>`.
6. **Gleitpfad**: von (X(0), Y(alt_now)) mit `alt(d) = alt_now − d_m / GR`. Gestrichelt (9 6).
   - GR = geglättet(Groundspeed / Sinkrate). Sinkrate ~0 → Pfad **level** (flach).
7. **Konflikt**: Abschnitt des Gleitpfads wo `d ≥ d_in` UND `alt(d) > f` → **fett (6px)** + **Kreis am Eintritt**.
8. **Daten-Spalte**:
   - `GRENZE IN` = d_in (km) · `FLOOR` = f (m) · `@ GRENZE` = alt(d_in) (m)
   - Balken unten: `alt(d_in) > f` → **invertiert** (schwarz, weiße Schrift) `KONFLIKT +<Δ> m`,
     sonst normal `FREI −<Δ> m`. (Gleiche „invertieren bei echter Warnung"-Logik wie XC-Screen.)

## 5. Integration
- Neuer Screen `SCR_XSECTION` ins Karussell (Vorschlag: nach MAP).
- Reuse `uiFill`/`uiBox`/`drawText`/`measureText`/Dreieck-Formel. Eigene `xs2px()`/`alt2px()` Helfer.
- Refresh wie Flug-Screens (DU im 1 Hz; GC16 beim Reinwischen wie bei der Karte).

## 6. Stufenplan
1. **Chart + EIN Luftraum voraus + Gleitpfad + Konflikt + Daten-Spalte.** Platzhalter-GND-Linie. STOP & Glas.
2. Terrain-Masse aus 6C-Höhenraster (gefüllte schwarze Fläche).
3. Mehrere Lufträume gestapelt + Ceiling-Konflikt (von unten).

## 7. Stop / Heilig
- AL/AH-Parser muss **FL** (×100 ft), **ft MSL**, **GND**, **m** abdecken — sonst falscher Floor.
- Konflikt-Logik gegen Handrechnung verifizieren (Mockup: 2597 > 2500 → +97 m).
- Schnitt ist **Hilfe, keine Gewähr**; Daten ab 25 m, nicht vollständig.
- Andere Screens / Karte / FANET / BLE unangetastet. 1-Bit S/W, fette Fonts.
