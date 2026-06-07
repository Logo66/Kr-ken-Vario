# AURA-KRUECKE-3 — Cruise-Screen: 1-Bit S/W + Display-Stabilität

**Host:** LilyGo T5 E-Paper S3 Pro · ED047TC1 960×540 · **epdiy direkt** (Migration aus 2C abgeschlossen)
**Stand:** First Light erreicht. Clear -> Splash -> Cruise laufen ohne Crash. Aber: Cruise bleibt noch nicht stabil stehen, und der Vario-Ladder rendert als voller schwarzer Block statt proportional.
**Rolle:** Claude Code = Werkmeister. STOP-Gate-Regeln (lesen frei, ausfuehren nur auf "ok").
**Branch:** `aura_kruecke`

---

## 0. Leitregel (gilt fuer die GESAMTE Flug-/Daten-UI)
**Die UI ist durchgehend reines 1-Bit Schwarz/Weiss, voller Kontrast, KEIN Grau.**
- Sonnentauglichkeit + schneller Refresh haben Vorrang vor Eleganz.
- **Hierarchie ueber Groesse und Fettung, nicht ueber Grau:** Labels klein/fett/schwarz, Werte gross/fett/schwarz.
- Graustufen (GC16, langsamer Full-Refresh) **nur** fuer den statischen Boot-Splash (einmaliges Bild).

## Visuelle Spec (verbindlich)
- **Ziel-Look Cruise (S/W):** aura_cruise_flightmode_bw_960x540.svg/.png
- **Layout-Herkunft (nur als Layout-Referenz, NICHT die Graustufen uebernehmen):** aura_vario_epaper_screens_v1.svg (oberster Screen = Cruise).

---

## 1. Display-Stabilitaet (FUNKTIONALER BLOCKER — zuerst)
Cruise wird gezeichnet (CRUISE DONE), bleibt aber nicht stabil stehen.
- **Erst diagnostizieren, dann fixen.** Root-Cause klaeren: Reboot/Reset-Loop? Watchdog waehrend des Refreshs? loop() loescht/ueberschreibt? epd_poweroff() zu frueh? Bild wird nie committet?
- Verdachtsleiste: WDT-Reset waehrend eines langen Full-Refreshs (WDT fuettern / Task-Yield), oder loop() triggert ungewollt einen erneuten Clear, oder Power-Sequenz schaltet die Rail vor dem fertigen Refresh ab.
- **Ziel:** Cruise-Screen steht stabil auf Glas, kein Reset, kein Flackern, kein erneutes Clear.

## 2. Vario-Ladder proportional (der "schwarze Block"-Fix)
Aktuell: ganze Ladder-Flaeche schwarz gefuellt. **Falsch.**
Ziel (siehe Vorlage):
- Schwarz gefuellt **nur von der Null-Linie bis zum aktuellen Vario-Wert** — Fuellhoehe = value / scale (Steigen -> nach oben, Sinken -> nach unten).
- Darueber/darunter: **leere Segmente** (weiss, duenne schwarze Umrandung + Segment-Hairlines) als Skala.
- **Dicke schwarze Null-Linie** in der Mitte.
- Skala-Ticks links: z.B. +4 / +2 / 0 / -2 / -4 (Scale = +/-4 m/s, Werte ausserhalb clampen).
- ø-Vario als kleines schwarzes Dreieck am rechten Ladder-Rand auf Hoehe des Mittelwerts.
- Alles 1-Bit, keine Graustufen-Abstufung mehr.

## 3. Cruise vollstaendig auf 1-Bit S/W bringen
Nach der Vorlage umsetzen, alle Grau-Draw-Calls raus:
- **Statusbar:** Uhr 14:23, Sat-Dots (gefuellt = Fix, hohl = kein Fix), FANET 3, Akku-Symbol + 87% * 25h.
- **Links:** Vario-Ladder (s. §2) + VARIO-Label + grosser Wert +2.3 + m/s * ø20s +1.8.
- **Mitte:** HOEHE MSL + 1847 m (riesig) + QNH 1018 * DGND 412.
- **Mitte/rechts:** SPEED 38 km/h + GLIDE 8.2.
- **Unten:** Heading-Streifen (schwarzes Lineal, zentriert auf aktuellem Kurs, Dreieck-Zeiger + Grad-Zahl) + WIND -> 14 + DEW +4°.
- **Fonts:** kraeftiger, gut lesbarer Sans aus epdiy (fett). Markenschrift NICHT noetig. Lesbar + fett zaehlt.

## 4. epdiy-Refresh-Modi (Performance + Anti-Ghosting)
- **Daten-/Flug-Updates: schneller 1-Bit-Modus** MODE_DU (oder A2) fuer die sich aendernden Felder (Vario, Hoehe, Speed, Heading). KEIN GC16 in der Flug-Schleife.
- **Beim Screen-Eintritt:** einmal sauber clearen, dann nur noch Partial-Updates der Felder.
- **Anti-Ghosting-Disziplin:** DU/A2 sammeln Geister. Zaehler einbauen -> alle ~N Partial-Refreshes (oder bei Screen-Wechsel) einen vollen Clear/Refresh, um Ghosting wegzuraeumen. N als Konstante, spaeter tunebar.

## 5. Offene Entscheidung (Ivo)
**Boot-Splash (Lebensbaum-Logo):** bleibt als statisches Einmal-Bild in **Graustufen** (GC16), ODER strikt auch **1-Bit**?
Default bis zur Entscheidung: **Graustufen lassen** (funktioniert bereits auf Glas). boot_logo.h NICHT anfassen.

---

## GATEs / Abnahme
| Gate | Bedingung |
|---|---|
| **G0** | Display-Stabilitaet: Root-Cause gefunden + gefixt -> Cruise steht stabil auf Glas (kein Reset/Flackern). |
| **G1** | Vario-Ladder **proportional** (fuellt bis Wert, nicht voll); geprueft bei mehreren Test-Werten (+2.3 / 0 / -1.5). |
| **G2** | Cruise komplett **1-Bit S/W**, kein Grau; Layout matcht aura_cruise_flightmode_bw_960x540. |
| **G3** | epdiy MODE_DU/A2-Partial-Refresh fuer Werte-Updates + Anti-Ghosting-Voll-Clear-Zaehler implementiert; Refresh-Gefuehl auf Glas geprueft. |
| **G4 (gated)** | Thermik + Hike and Fly als S/W-Screens — wartet auf S/W-Vorlagen vom Architekt. |

## Stop / Safety / Hygiene
- Nach jedem Gate stoppen + berichten.
- Diagnose vor Fix bei der Stabilitaet — nicht blind patchen.
- Sensor-/Bringup-Logik unangetastet. boot_logo.h unangetastet (s. §5).
- Werte im Screen sind aktuell SIM/Platzhalter.

## Kontext aus vorherigen Sessions
- epdiy 2.0.0 via git (https://github.com/vroland/epdiy.git#2.0.0)
- Board-Def: boards/T5-ePaper-S3.json (memory_type: qio_opi)
- Wire.end() VOR epd_init() noetig (I2C-Driver-Konflikt)
- Reset-Mechanismus: 1200-Baud-Touch auf COM5 (kein physischer Reset-Button)
- epd_draw_pixel arbeitet in nativen Landscape-Koordinaten (Rotation nicht fuer Pixel)
- Pixel-Clock von epdiy automatisch auf 10 MHz reduziert (Arduino cache line < 64B)
- Boot-Splash: boot_logo.h 409x500, GAMMA_LUT[16] S-Curve, MODE_GC16
