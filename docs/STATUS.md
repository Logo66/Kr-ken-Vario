# AURA Kruecke — Projekt-Status

Letzte Aktualisierung: 2026-06-09

## DONE

### Hardware & Sensoren
- **Kalman-Vario** 2-State (Hoehe/Vario) auf BMP581 @ 20 Hz, live auf Display 1 Hz
- **GPS** L76K @ 9600 Baud, Fix mit 7+ Satelliten, Speed/Heading/Position live
- **Alle 4 Sensoren** via Raw I2C (BMP581, SHT45, LSM6DSO32, GT911)
- **RTC** PCF8563, Software-Clock im Loop
- **Batterie** SoC aus BQ25896 Spannung (4.2V=100%, 3.3V=0%)
- **Touch** GT911, Portrait→Landscape Transform, 50 Hz Polling, Swipe-Schwelle 35px

### FANET (LoRa 868 MHz)
- **SX1262 Init** nach LilyGo Factory (TCXO 2.4V, DIO2 RF-Switch, SPI geteilt mit SD)
- **FANET RX** live: Skytraxx Type 7 (Ground) + Type 1 (Airborne) empfangen
- **FANET TX** Type 1 Tracking im Flug (alle 5s, Duty-Cycle konform)
- **PHY-Parameter** verifiziert: 868.2 MHz, BW250, SF7, Sync 0xF1, Preamble 12, CRC on
- **Pilot-Liste** bis 20 Peers, Count in Statusbar aller Screens

### BLE (NimBLE)
- **GATT Server** "Aura Vario" mit 3 Characteristics (Vario 20B, GPS 16B, Status)
- **PIN-Pairing** (Default 1234, aenderbar im BLE-Screen)
- **1 Hz Notifications** bei verbundenem Client
- **BLE-Screen** unter FUNK → BLE: Name aendern, PIN aendern, AN/AUS Toggle
- **Config** auf SD gespeichert (/ble.cfg), beim Boot geladen

### WiFi & Downloads
- **WiFi Scan** → Liste mit Signal-Balken → Netzwerk antippen → Passwort (Touch-Tastatur QWERTZ)
- **Auto-Connect** bei gespeichertem Netz (/wifi.cfg auf SD)
- **Karten-Download** OSM Tiles Zoom 11+12+13 (Schweiz/Ostschweiz, ~1000 Tiles)
- **Luftraeume** OpenAir CH von openaip.net → /airspace/ch_asp.txt
- **Hindernisse** BAZL Luftfahrthindernisse (GeoJSON, tagesaktuell) → /obstacles/ch_bazl.json
- **Hotspots** Thermik-Hotspots CH von openaip.net → /obstacles/ch_hot.cup
- **Robuster Download** mit 5s Timeout, 8s max/Tile, yield(), Fehler ueberspringen

### Karten-Screen
- **OSM Tiles** als S/W auf E-Paper (Threshold 195, pngle PNG-Decoder)
- **Edge-to-Edge** 960x484px, kein Rahmen
- **Pilot-Marker** weisser Halo (60px) + grosses Dreieck (52px), auf jeder Karte sichtbar
- **Track-Spur** 200-Punkte Ringbuffer, 3px dicke Linie
- **GPS-Projektion** North-Up, m/px aus Zoom-Stufe
- **Zoom** 5 Stufen (500m-10km) → OSM Z11-Z15, Fallback auf vorhandene Tiles
- **Zoom-Buttons** schwebend, doppelter Rahmen, fette Symbole, Handschuh-tauglich
- **1 Hz Overlay** Tile-Cache in PSRAM, nur Pilot+Track+Statusbar updaten (MODE_DU)
- **Tile-Cache** Framebuffer nach Tile-Rendering gespeichert, bei Overlay wiederhergestellt
- **Info-Leiste** unten: Massstab + Koordinaten + Hoehe in einer Zeile
- **Default-Position** Niederneunforn wenn kein GPS-Fix

### Flight-Screens (4 im Karussell, Swipe/Button)
1. **Cruise** — Vario-Ladder, Hoehe, Speed, Heading-Kompass (Outline-Dreiecke 40px, 4px Strich), Wind, Temp, GR
2. **Thermal** — North-Up Kompass-Rose, Lift-Punkte, Kern-Schaetzung (ThermalManager)
3. **Goal/Final-Glide** — Ankunftshoehe, Distanz, GR noetig/ist, Bearing-Ring mit Pfeil
4. **Map** — OSM Tiles + Track + Pilot + Zoom (siehe oben)

### Menu-System
- **6 Buttons** (3x2 Grid, 190px): QNH, LICHT, FLUG/BUCH, FUNK, KARTE, AUS
- **FUNK Sub-Menu** — WLAN / BLE / FANET mit Status-Anzeige
- **BLE Sub-Screen** — Name/PIN aendern mit Touch-Tastatur, AN/AUS
- **KARTE Sub-Screen** — Luftraeume/Hindernisse/Hotspots/Tiles Download mit Fortschrittsbalken
- **QNH-Kalibrierung** via Touch (+/-10m Referenzhoehe)
- **Flugbuch** mit Demo-Daten
- **Landing-Screen** (Gut gelandet / Brauche Ride / Brauche Hilfe)
- **Credits & Shutdown** invertiert (weiss auf schwarz)

### Display-Optimierungen
- **MODE_DU** fuer alle Screen-Wechsel (kein schwarzer Blitz-Balken)
- **MODE_GC16** nur fuer Boot-Splash (Graustufen) und Karten-Tiles
- **Boot** ohne epd_clear (1 Flash statt 3)
- **Start-Screen** "START / ERKANNT / KIE Engineering wuenscht Dir einen schoenen Flug" (ArialBold72)
- **Flug-Erkennung** nur mit GPS-Fix + >=4 Sats (kein Phantom-Start im Buero)

### SD-Karte
- **FAT32** via SPI (CS=12, geteilt mit LoRa CS=46)
- **Verzeichnisse** /airspace/ /obstacles/ /tiles/ /igc/
- **Config-Dateien** /wifi.cfg, /ble.cfg

### Infrastruktur
- **ui_utils.h** exakte Zentrierung via epd_get_text_bounds (measureText, drawHCenter, drawBoxCenter)
- **Font-Tiers** ArialBold 72/40/32/28/24/16pt
- **Touch-Tastatur** QWERTZ mit SHIFT, Backspace, OK, Abbruch

## NAECHSTE SCHRITTE

1. **OpenAir Parser** → Luftraum-Polygone auf Karte + Warnung bei Annaeherung
2. **BAZL Parser** → Hindernis-Punkte auf Karte + Warnung
3. **Flight Buddy App** (PWA + BLE) — Live-Dashboard, Settings, Konfigurator
4. **Produktseite** kie-engineering.com mit integriertem Konfigurator
5. **IGC-Logging** auf SD (GNSS-Track fuer XContest/DHV)
6. **Piezo-Buzzer** (Modulino) — Vario-Ton proportional zur Steigrate
7. **BMM350 Magnetometer** — Tilt-kompensierter Kompass, Hike&Fly Modus
8. **Hike&Fly Screen** — Kompass-Rose, Wegpunkt-Navigation, kein Vario
9. **Regions-Download** in App (AT, IT, CO statt nur CH hardcoded)
10. **Multi-Zoom Tiles** fuer weitere Regionen
