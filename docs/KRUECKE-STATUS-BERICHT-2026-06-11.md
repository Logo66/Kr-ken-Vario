# STATUS-BERICHT — Krücke-Firmware (Aura Vario)
## Stand 2026-06-11 · für den Architekten · Branch `aura-kruecke/2-vario-map`

> **Zweck:** Vollständige Bestandsaufnahme — was **läuft**, welche **Außen-Verbindungen** bestehen (FANET, BLE, WLAN, Buddy-Server, GPS), und was **in Vorbereitung / gegated / geplant** ist.
> **Legende:** ✅ läuft · 🔒 bewusst gesperrt (Sicherheit) · 🛠 in Vorbereitung (teilweise) · ⚪ verdrahtet aber ungenutzt · 📝 geplant (nur Hook/UI).
> **Hardware:** LilyGo T5-E-Paper-S3-Pro · ESP32-S3 · 960×540 Touch-E-Paper · Sensor-Board (BMP581 / LSM6DSO32 / SHT45) + LoRa SX1262.

---

## 0. KURZFASSUNG

Die Krücke ist als **Vario + Flugcomputer + Buddy-Empfänger** funktionsfähig im Feld. Kern (Vario, Höhe, Flug-Screens, Flugerkennung, IGC-Logging, Karte) läuft. Außen-Verbindungen **FANET-RX, BLE, WLAN, Buddy-Server (Selbst-Registrierung + Karten-Download)** laufen. **FANET-Senden ist bewusst gesperrt** (Sicherheits-Gate, bis eine echte Gegenstelle Position/Höhe bestätigt). In Vorbereitung: **Sensor-Fusion (IMU-Vario)** — Studie liegt vor, wartet auf OK; **Buzzer/Ton-Ausgabe** (Menü + Settings fertig, Lautsprecher noch nicht dran); **Kompass BMM350** (laut Fusion-Studie fürs Vario nicht nötig, nur Anzeige).

---

## 1. WAS LÄUFT — Kern-Funktionen

| Funktion | Stand | Kurz |
|---|---|---|
| **Barometrisches Vario** | ✅ | 2-State-Kalman auf BMP581, ~20 Hz; aktuell + 20-s-Mittel |
| **Höhe MSL** | ✅ | ISA-Formel, QNH am Gerät kalibrierbar (QNH-Screen) |
| **Flugerkennung** | ✅ | Start: >20 km/h + Fix + ≥4 Sat, 10 s gehalten · Landung: <5 km/h + Vario<0.3, 30 s |
| **IGC-Logging** | ✅ | B-Record alle ~2 s im Flug, stromausfallsicher (sofort geschlossen) |
| **Flugbuch** | ✅ | Persistent auf SD, 6 jüngste Flüge (Datum, Dauer, max. Höhe, Steigen, max-G, Distanz) |
| **Thermik-Assistent** | ✅ | Steig-gewichteter Kern-Schwerpunkt + Richtungs-Hinweis, Kompass-Rose mit Lift-Punkten |
| **Endanflug / Ziel** | ✅ | Ankunftshöhe über Ziel, Distanz, benötigte vs. aktuelle Gleitzahl, Peilung |
| **Karte (standortbezogen)** | ✅ | CH-Vektor-Pack (Höhenlinien, Wasser, Gipfel/Orte), Seek-per-Tile-Reader (passt in PSRAM), **verschiebbar + zoombar** |
| **Luftraum-Schnitt** | ✅ | Querschnitt voraus + Terrain-Profil (Relief-Linie) |
| **Einheitliche Statusleiste** | ✅ | Auf allen Flug-Screens gleich: **Uhr · Sat · FANET · Buddy · Batterie** |
| **Ton-Menü (Bedienung)** | ✅ | Lautstärke 0–5 / Stumm, in NVS persistent (Ausgabe siehe §6 — Buzzer fehlt noch) |
| **G-Kraft-Aufzeichnung** | ✅ | LSM6DSO32-Beschleunigungsbetrag, max-G pro Flug ins Flugbuch |
| **Temperatur / Taupunkt** | ✅ | SHT45, fließt in Basis-Abschätzung (Thermik) |

---

## 2. AUSSEN-VERBINDUNGEN

### 2.1 FANET (LoRa SX1262, 868.2 MHz) — RX ✅ / TX 🔒
- **Empfang läuft:** parst Typ-1 (Airborne-Tracking) + Typ-7 (Ground), bis zu 20 Piloten live (Position, Höhe, Steigen, Speed, Heading, Flugzeugtyp). `pilot_count` in der Statusleiste.
- **Senden gesperrt (Gate D):** `FANET_TX_ENABLED = 0`. Der Typ-1-Encoder ist **byte-genau spec-konform** (Selbsttest beim Boot gegen die 3s1d-Referenz). Live-TX bleibt blockiert, bis eine echte Gegenstelle die gesendete Position/Höhe bestätigt. **Leitsatz: lieber TX aus als TX falsch** (falsche Position gefährdet andere Piloten). Test-Sendung über die Funk-UI erzeugt nur einen Log-Eintrag, kein Funk.

### 2.2 BLE (Bluetooth, NimBLE) — ✅
- **GATT-Server „Aura Vario"** (Name am Gerät änderbar), Notify ~1×/s:
  - **Vario-Char:** Höhe, Vario, Speed, Heading, Sat, Batterie, Flags (GPS/Flug/FANET) — 20 Byte.
  - **GPS-Char:** Lat/Lon (double) bei gültigem Fix.
  - **Status-Char:** Versionstext (read-only).
- **Pairing/Sicherheit:** 4-stellige **PIN** (Default 1234, änderbar), Bonding + MITM + Secure Connections erzwungen.
- **Config** (Name + PIN) auf SD `/ble.cfg`. Eigenes Protokoll für die Flight-Buddy-App (kein XCTrack/LK8000-Satz).

### 2.3 WLAN — ✅ (on-demand über UI)
- Scan → SSID-Liste → Passwort (Bildschirm-Tastatur) → Connect (30 s Timeout) → Credentials auf SD `/wifi.cfg`.
- **Drei WLAN-Verbraucher:**
  1. **Buddy-Server** (Registrierung + Heartbeat + Karten-Download) — siehe §2.4.
  2. **IGC-Web-Download:** eingebauter Webserver (Port 80), `GET /` listet `/igc/`, `GET /igc/<datei>.igc` lädt herunter. Läuft automatisch bei WLAN, stoppt bei Trennung.
  3. **Karten-Pack-Download** (über Buddy-Server, Bearer).

### 2.4 Buddy-Server (`https://buddy.flightbuddyki.org`) — ✅
| Endpoint | Methode | Auth | Zweck |
|---|---|---|---|
| `/devices/register` | POST | Werks-Secret | Einmalige Selbst-Registrierung: MAC → **Geräte-Token** + Pairing-Code |
| `/devices/heartbeat` | POST | Bearer | Keep-alive + Firmware-Version, alle 5 min |
| `/maps/index` | GET | Bearer | Karten-Regionen + Versionen + SHA-256 |
| `/maps/<region>/download` | GET | Bearer | Pack laden, SHA-256 prüfen, auf SD ablegen |
- **Selbst-Registrierung:** beim 1. WLAN-Kontakt genau 1×; Token + Pairing-Code in **NVS** (`buddy`). Besitzer koppelt per Pairing-Code in der Buddy-App (`/devices/claim`) → Status `active`. Auth danach immer **`Authorization: Bearer <Token>`** (nicht mehr der alte Master-Key).
- **Status-Indikator:** erfolgreicher Heartbeat (HTTP 200) → der **„Buddy"-Punkt in der Statusleiste** zeigt die Server-Verbindung an.
- **HEILIG:** Werks-Secret + Token **nie** ins Repo/Doku/Chat (nur lokal in git-ignored `secrets.h` bzw. NVS, gehasht in der Server-DB).

### 2.5 GPS — ✅
- TinyGPSPlus über UART2, 9600 Baud, Strom über PCA9535-Expander geschaltet.
- Liest Position, Speed (<3 km/h auf 0 geklemmt), Heading (ab 2 km/h), Sat-Zahl, Datum/Zeit, Höhe (nur für IGC).
- Letzte gültige Position alle 30 s in NVS — Karten-Fallback ohne Fix (überlebt Neustart).

---

## 3. SENSORIK (I²C, 400 kHz)

| Sensor | Adresse | Zweck | Stand |
|---|---|---|---|
| **BMP581 #1** | 0x47 | Druck → Höhe/Vario (Kalman) | ✅ aktiv |
| **BMP581 #2** | 0x46 | Redundanz | ⚪ verdrahtet, nicht initialisiert |
| **LSM6DSO32** | 0x6A | Beschleunigung (max-G) | 🛠 nur Accel-Betrag; **Gyro ungenutzt** |
| **SHT45** | 0x44 | Temperatur/Feuchte/Taupunkt | ✅ aktiv |
| **PCF85063 (RTC)** | 0x51 | Uhrzeit | ✅ aktiv |
| **BQ25896** | 0x6B | Batterie → SoC % | ✅ aktiv |
| **BMM350 (Kompass)** | — | Heading/Lage | ⚪ noch nicht verdrahtet (Heading kommt aus GPS-Kurs) |

> Hinweis Hardware-Revision: Sensor-Board real bestückt = **BMP581 / LSM6DSO32 / SHT45 / BMM350 (Gravity-I²C) + Buzzer-Board**. Ein BMP581 (der mit der Lötbrücke) wird ausgebaut → künftig **1× BMP581**. Kein BNO085 (ältere Doku-Reste bitte ignorieren).

---

## 4. SCREENS & BEDIENUNG

- **Flug-Screens** (Wisch zum Wechseln): Cruise · Thermik · Ziel · Karte · Luftraum-Schnitt. Plus **Landung** (automatisch nach Landung: Gut gelandet / Brauche Ride / Brauche Hilfe).
- **Menü-Screens:** Hauptmenü · QNH · Flugbuch · Funk (WLAN/BLE/FANET) · WLAN · BLE · Karten-Overlay (Downloads) · **Ton**.
- **Touch (GT911-Panel):** Wisch = Screen wechseln, Tipp = Screen-Aktionen (z. B. Karte verschieben/zoomen).
- **Physischer Home-Knopf** (kapazitiver Knopf **unter** dem Screen, GT911-Home-Key — autonom, kein x/y-Touch): **kurz = Ton-Menü**, **lang (~1.5 s) = Hauptmenü**. „Alle Menüs an einem Ort."
- **E-Paper-Strategie:** Flug-Screens 1 Hz Teil-Refresh (schnell), Karte/Schnitt voll (GC16) nur bei Eintritt/Zoom/Pan (gegen Ghosting).

---

## 5. SPEICHER & LOGGING

- **SD-Karte:** `/maps/` (Region-Packs + `active.txt`), `/igc/` (Flüge + `flugbuch.dat`), `/airspace/` (OpenAir), `/obstacles/` (BAZL-GeoJSON + Hotspots), `/wifi.cfg`, `/ble.cfg`.
- **NVS:** `buddy` (Token, Pairing, Status, letzte Position), `sound` (Lautstärke + vorbereitete Ton-Parameter).

---

## 6. IN VORBEREITUNG / GESPERRT / GEPLANT

### 6.1 Sensor-Fusion — IMU-gestütztes Vario (Studie liegt vor) 🛠
- **Stand:** Recherche + Konzept fertig (`docs/STUDIE-SENSORFUSION-ERGEBNIS-2026-06-11.md`). **Wartet auf Architekt-OK** → dann Bau-Ticket Stufe 1.
- **Kern-Ergebnisse:** bestehendes 2-State-Kalman wird inertial (Accel als Steuergröße); **Gyro auslesen** (liegt brach) für 6DOF-Lage (Mahony) → Schwerkraft-Trennung in der Dauerkurbel; **Vario braucht den BMM350-Kompass nicht** (Pitch/Roll aus Accel+Gyro; Buzzer-Störung trifft nur die Kompass-Anzeige). Plus **Startplatz-Ruhe** (IMU-Stillstand/ZUPT) damit GPS-Jitter keinen Fehlstart auslöst. Baro-Vario bleibt jederzeit Fallback.

### 6.2 Ton-Ausgabe / Buzzer 🛠
- **Bedien-Menü + NVS-Settings fertig** (Lautstärke 0–5/Stumm + vorbereitete Boden-Parameter: Steig-Schwelle, Sink-Alarm, Totzone, Tonkurve).
- **Fehlt:** der **Buzzer ist noch nicht angeschlossen** und die **Tonerzeugung** (Frequenz/Beep-Logik) ist noch nicht implementiert. Sobald der Buzzer dran ist, greifen dieselben Settings ohne Umbau.
- **Voll-Konfiguration** (Schwellen/Tonkurve) gehört bewusst auf **Web/App (Boden)**, nicht auf den Touch — Schnittstelle (gemeinsames Settings-Struct in NVS) ist vorbereitet.

### 6.3 FANET-Senden 🔒
- Encoder spec-konform + selbstgetestet; **Live-TX gesperrt (Gate D)** bis Validierung durch echte Gegenstelle. Danach: in-flight alle 5 s die eigene Position senden.

### 6.4 Kompass BMM350 ⚪
- Noch nicht verdrahtet. Laut Fusion-Studie **fürs Vario nicht nötig** — nur als **Kompass-Anzeige** (eigenes, späteres Ticket; Hard/Soft-Iron-Kalibrierung + Buzzer-Abstand).

### 6.5 Kleinere offene Punkte 📝
- **Landung → Ride/SOS via FANET:** Knöpfe da, Backend TODO (hängt an FANET-TX-Freigabe).
- **Flugbuch-Upload via App:** UI-Hinweis, noch kein Upload.
- **Luftraum/Hindernisse auf der Karte rendern:** werden geladen (SD), aber noch nicht in die Kartenansicht gezeichnet (aktuell nur Höhenlinien/Wasser/Gipfel).
- **2. BMP581** als Redundanz/Mittelung — verdrahtet, ungenutzt.
- **Windrichtung im Cruise** ist neu drin (Pfeil + Himmelsrichtung); **Konvention (von/nach) noch per Flugtest zu bestätigen**.

---

## 7. SICHERHEITS-GATES (bewusst)

| Gate | Zustand | Bedingung zum Öffnen |
|---|---|---|
| **FANET-TX (Gate D)** | 🔒 zu | echte Gegenstelle bestätigt gesendete Position/Höhe |
| **Flug-TX/Logging-Auslöser** | konservativ | nur bei gesicherter Flugerkennung (kein GPS-Jitter-Fehlstart; künftig mit IMU-Bestätigung) |
| **Werks-Secret / Token** | geheim | nur lokal (NVS / git-ignored), nie in Repo/Doku/Chat |

---

## 8. NÄCHSTE SCHRITTE (Vorschlag, Reihenfolge offen für Architekt)

1. **Sensor-Fusion Stufe 1** freigeben (Studie liegt vor) — IMU-Vario + Startplatz-Ruhe, Baro als Fallback.
2. **Buzzer anschließen + Tonerzeugung** — Settings sind fertig.
3. **FANET-TX validieren** (Gate D öffnen) — dann Live-Tracking + Ride/SOS.
4. **Web/App-Ton-Konfiguration** — schreibt das vorbereitete Settings-Struct.
5. **Kompass-Anzeige (BMM350)** — separat, unkritisch.

---
*Erstellt von der Code-Krücke. Stand entspricht dem lokalen Branch `aura-kruecke/2-vario-map` zum 2026-06-11; wird mit diesem Commit auf `Logo66/Kr-ken-Vario` gepusht.*
