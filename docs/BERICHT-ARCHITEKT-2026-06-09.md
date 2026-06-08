# Bericht an Architekt — AURA Kruecke Stand 09.06.2026

**Von:** Werkmeister (Claude Code)
**An:** Architekt (Ivo Eichenberger, CEO/Pilot, KIE Engineering)
**Projekt:** AURA Kruecke — KI-entwickeltes Paragliding-Vario
**Hardware:** LilyGo T5 E-Paper S3 Pro (ESP32-S3, ED047TC1 4.7" 960x540, SX1262, L76K GPS)

---

## 1. EXECUTIVE SUMMARY

Die AURA Kruecke ist vom Konzept zum **funktionierenden Fluggeraet** geworden.
Alle Kern-Subsysteme laufen: Vario, GPS, Display, Touch, FANET, BLE, WiFi, SD, Karte.
Das Geraet kann autonom fliegen — ohne Handy, ohne Internet, ohne Bodenstation.

**Wichtigste Meilensteine dieser Session (08.-09.06.2026):**
- FANET-Empfang vom Skytraxx verifiziert (erste LoRa-Pakete!)
- OSM-Karte live auf E-Paper (weltweit erste Implementation?)
- BLE GATT Server mit PIN-Pairing
- WiFi Scan + Touch-Tastatur + SD-Downloads
- Karten-Tiles fuer die Schweiz auf SD

---

## 2. SYSTEM-ARCHITEKTUR

```
┌─────────────────────────────────────────────────────────┐
│  AURA KRUECKE (ESP32-S3-WROOM-1 N16R8)                 │
│                                                         │
│  ┌─────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ BMP581  │  │ SHT40    │  │ LSM6DSO32│  │ L76K GPS│ │
│  │ Druck   │  │ Temp/Hum │  │ Accel/Gyr│  │ 9600 Bd │ │
│  │ 20 Hz   │  │ 1 Hz     │  │ (ready)  │  │ UART2   │ │
│  └────┬────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
│       │ I2C Raw     │ I2C Raw     │ I2C Raw      │      │
│  ┌────▼─────────────▼─────────────▼──────────────▼────┐ │
│  │              MAIN LOOP (50 Hz)                     │ │
│  │  Kalman-Vario → Screens → Touch → FANET → BLE     │ │
│  └──────────────────┬────────────────────────────────┘ │
│                     │                                   │
│  ┌──────────────────▼────────────────────────────────┐ │
│  │  ED047TC1 E-Paper 960x540 16-grey (epdiy 2.0)    │ │
│  │  MODE_DU 1 Hz Refresh (75ms) / GC16 fuer Tiles   │ │
│  └───────────────────────────────────────────────────┘ │
│                                                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ SX1262   │  │ NimBLE   │  │ WiFi     │              │
│  │ FANET    │  │ GATT     │  │ STA      │              │
│  │ 868.2MHz │  │ PIN 1234 │  │ Scan+DL  │              │
│  │ RX+TX    │  │ 3 Chars  │  │ SD-Save  │              │
│  └──────────┘  └──────────┘  └──────────┘              │
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  SD-Karte (FAT32, SPI CS=12)                     │  │
│  │  /tiles/     OSM Tiles Z11-Z13 (~1000 PNGs)      │  │
│  │  /airspace/  OpenAir Luftraeume CH                │  │
│  │  /obstacles/ BAZL Hindernisse (GeoJSON)           │  │
│  │  /igc/       (vorbereitet fuer Fluglog)           │  │
│  │  /wifi.cfg   SSID + Passwort                      │  │
│  │  /ble.cfg    Geraetename + PIN                    │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## 3. SCREEN-ARCHITEKTUR

```
BOOT → Splash (GC16) → Cruise
                            │
    ┌───────────────────────┼───────────────────────┐
    │ Swipe/Button          │                       │
    ▼                       ▼                       ▼
 Cruise ◄──► Thermal ◄──► Goal ◄──► Map
    │                                    │
    │ Long-Tap                           │ 1 Hz Overlay
    ▼                                    │ (Tile-Cache
  MENU                                  │  + Pilot)
    │
    ├── QNH (Touch +/-)
    ├── LICHT (Toggle)
    ├── FLUG/BUCH (Liste)
    ├── FUNK ──┬── WLAN (Scan → Liste → Passwort → Connect)
    │          ├── BLE (Name/PIN aendern, AN/AUS)
    │          └── FANET (Status)
    ├── KARTE ──── Luftraeume / Hindernisse / Hotspots / Tiles Download
    └── AUS → Credits → Deep Sleep
```

---

## 4. FUNK-SUBSYSTEME

### 4.1 FANET (868 MHz LoRa)
- **Status:** VERIFIZIERT — Skytraxx 4.0 empfangen (Type 7, Mfr 0x11, RSSI -68 dBm)
- **PHY:** 868.2 MHz, BW250, SF7, Sync 0xF1→0xF4/0x14, Preamble 12, CRC on
- **RX:** Continuous, DIO1 IRQ, bis 20 Peers in Liste
- **TX:** Type 1 Tracking im Flug, alle 5s, 14 dBm
- **Erkenntnisse:**
  - SPI geteilt mit SD → CS-Management kritisch
  - TCXO 2.4V (Board-spezifisch, nicht 1.6V wie GXAirCom)
  - Init NACH epdiy (GPIO ISR Konflikt)
  - Skytraxx sendet Type 7 (Ground) am Boden, nicht Type 1

### 4.2 BLE (NimBLE GATT)
- **Status:** FUNKTIONIERT — Geraet sichtbar, PIN-Pairing
- **Service:** Custom UUID, 3 Characteristics (Vario, GPS, Status)
- **Sicherheit:** Bond + MITM + SC, Display-Only IO
- **Config:** Name + PIN auf SD, aenderbar via Touch-Screen

### 4.3 WiFi
- **Status:** FUNKTIONIERT — Scan, Connect, Download, Auto-Reconnect
- **Nutzung:** Nur fuer Downloads (Tiles, Luftraeume, Hindernisse)
- **Config:** SSID + Passwort auf SD gespeichert

---

## 5. KARTEN-SYSTEM

### 5.1 Tile-Rendering Pipeline
```
SD (/tiles/Z_X_Y.png)
  → pngle PNG-Decoder (Streaming, Pixel-Callback)
    → RGB→Grey Threshold (grey<195 → schwarz)
      → epd_draw_pixel(x, y, color, framebuffer)
        → epd_hl_update_screen(MODE_GC16)
```

### 5.2 Kritische Erkenntnisse (fuer Architekt wichtig)
1. **back_fb muss weiss** — epdiy Highlevel nutzt differenzielles Rendering.
   Ohne `memset(hl->back_fb, 0xFF)` werden Tile-Pixel ignoriert.
2. **Threshold statt Graustufen** — E-Paper 4-bit kann OSM-Farben nicht
   sinnvoll darstellen. Binaer S/W mit Threshold 195 ist optimal.
3. **Tile-Cache in PSRAM** — 259 KB Framebuffer-Kopie nach Tile-Rendering,
   wird bei 1 Hz Overlay-Updates wiederhergestellt.
4. **4x3 Tiles + yield()** — 12 Tiles mit Watchdog-Feeding, kein Crash.

### 5.3 Download-System
- OSM Tile-Server (a.tile.openstreetmap.org)
- 5s HTTP-Timeout, 8s max/Tile, Fehler ueberspringen
- Bereits geladene Tiles werden nicht nochmal geladen
- Zoom 11 (ganze CH), 12 (CH Mitte+Ost), 13 (Ostschweiz)

---

## 6. DISPLAY-WISSEN (FUER ALLE ZUKUENFTIGEN SCREENS)

| Situation | Modus | Warum |
|---|---|---|
| 1 Hz Flight-Refresh | MODE_DU | Kein Flash, 75ms, nur S/W |
| Screen-Wechsel (Swipe) | MODE_DU | Kein schwarzer Balken |
| Karten-Tiles | MODE_GC16 | Braucht Graustufen-Waveform |
| Boot-Splash | MODE_GC16 | Graustufen-Logo |
| Credits/Shutdown | MODE_GC16 | Invertiert (weiss auf schwarz) |

**Goldene Regel:** `epd_hl_set_all_white()` setzt NUR front_fb.
Fuer Bilder/Tiles: `memset(hl->back_fb, 0xFF, fb_size)` ZUSAETZLICH.

---

## 7. I2C-ARCHITEKTUR (FUER NEUE SENSOREN)

```
Wire.begin(39, 40) → Sensor-Init (BMP581, SHT40, LSM6DSO32, GT911, RTC, BQ25896)
Wire.end()         → epdiy uebernimmt I2C-Driver
                   → Ab hier NUR raw I2C: i2c_master_write_read_device(I2C_NUM_0, ...)
```

**Neue Sensoren (Buzzer Modulino, BMM350):**
- Muessen via Raw I2C angesprochen werden (NACH epdiy init)
- ODER: zweiten I2C-Bus auf anderen Pins (falls verfuegbar)
- I2C-Adresse pruefen gegen bestehende Geraete (0x20-0x6B belegt)

---

## 8. OFFENE PUNKTE / TECHNISCHE SCHULDEN

1. **Zoom 14+15 Tiles** fehlen (nur 11-13 geladen)
2. **Regions-Download** hardcoded auf CH — muss flexibel werden
3. **OpenAir Parser** noch nicht implementiert (Datei auf SD, aber nicht geparst)
4. **BAZL Parser** nur STAC-Index geladen (2.8 KB), nicht die vollen Daten
5. **IGC-Logging** vorbereitet (/igc/ Verzeichnis) aber nicht implementiert
6. **FANET TX Frame** vereinfacht — Altitude/Speed/Climb Encoding nicht 100% Spec-konform
7. **Tile-Download** langsam (~50 Tiles/Minute) — koennte parallel oder per Batch
8. **Map Zoom** funktioniert erst wenn Tiles fuer den Zoom-Level vorhanden sind
9. **Kein Anti-Ghosting** — nach vielen MODE_DU Updates sammelt sich Ghosting

---

## 9. RESOURCE-VERBRAUCH

| Resource | Genutzt | Verfuegbar | Auslastung |
|---|---|---|---|
| Flash | 1.6 MB | 6.5 MB | 25% |
| RAM | 55 KB | 328 KB | 17% |
| PSRAM | ~520 KB | 8 MB | 6.5% |
| SD-Karte | ~35 MB | 119 GB | 0.03% |

Viel Luft fuer OpenAir-Polygone, Hindernis-Daten, IGC-Logs und die App.

---

## 10. FAZIT

Die AURA Kruecke validiert das Konzept: **ein KI-entwickeltes Paragliding-Vario
auf Consumer-Hardware ist machbar.** Alle kritischen Subsysteme funktionieren.
Die naechste Phase ist die Flight Buddy App (PWA/BLE) und die Airspace-Integration.

Respekt an den Piloten fuer die Geduld bei 40+ Iterationen auf der Karte.
Das Ding fliegt.

**KIE Engineering — Das erste KI-entwickelte Vario**
