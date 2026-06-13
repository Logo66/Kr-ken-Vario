# STATUS-BERICHT — Krücke-Firmware (Aura Vario)
## Stand 2026-06-13 · für den Architekten · Branch `aura-kruecke/2-vario-map`

> **Zweck:** Aktualisierte Bestandsaufnahme — was **läuft**, welche **Außen-Verbindungen** bestehen, was **neu seit 11.06.** ist, und was **gegated / geplant** bleibt.
> **Legende:** ✅ läuft · 🆕 neu seit 11.06. · 🔒 bewusst gesperrt · 🛠 in Vorbereitung · ⚠️ Phase-1-Abweichung · ⚪ verdrahtet/ungenutzt · 📝 geplant.

---

## 0. KURZFASSUNG (Δ seit 11.06.)
Die Krücke ist **Vario + Flugcomputer + Buddy-Empfänger + jetzt voll BLE-konfigurierbar**. Neu seit dem letzten Bericht:
- **🆕 BLE-Konfigurator komplett (M1+M2+M3)** — App schreibt **alle** Einstellungs-Gruppen **und** Flugpläne/Tasks live aufs Gerät; ein versioniertes NVS-JSON-Settings-Modell ist die eine Wahrheit. **End-to-End am echten Gerät verifiziert.**
- **🆕 Buzzer + Vario-Steigton** — Modulino-Buzzer dran, Steig-/Sink-Ton im Flug (war 11.06. noch „fehlt"). Ton-Menü + Test-Knopf.
- **🆕 Umwelt/Wind über BLE** + Wind-Schätzer (Kreisdrift).
- **🆕 Statusleiste mit BLE/WLAN-Icons**, persistenter BLE-Schalter, Menü-Aufruf über den Kapazitiv-Knopf.
- **Hardware:** Beschleunigungssensor (LSM6) beim Löten **defekt → ausgebaut**; Kompass (BMM350) **getestet, misst nicht** (Minimal-Treiber) → geparkt. **Nächste Woche: BNO055** (9-Achs, Onboard-Fusion) ersetzt beide.

---

## 1. WAS LÄUFT — Kern-Funktionen
| Funktion | Stand | Kurz |
|---|---|---|
| **Barometrisches Vario** | ✅ | 2-State-Kalman auf BMP581, ~20 Hz; aktuell + Mittel |
| **AVG-Vario-Fenster** | 🆕✅ | Cruise + Thermik zeigen **dasselbe** Fenster, **per App einstellbar** (`vario.avg_window_s`, 1–120 s, Default 20) |
| **Vario-Steigton (Buzzer)** | 🆕✅ | Modulino ABX00108 (I²C 0x1E); Steigen 2700–3000 Hz (Takt/Höhe mit Steigen), Sink-Warnung 2400 Hz Dauerton; **nur im Flug**; Resonanz-Band gemessen |
| **Ton-Menü** | ✅ | Lautstärke 0–5/Stumm (NVS), **TON-TEST-Knopf** zum Raushören der lautesten Frequenz |
| **Höhe MSL / QNH** | ✅ | ISA, QNH am Gerät + **per App** (`alt.qnh`) |
| **Flugerkennung** | ✅ | Start >20 km/h + Fix + ≥4 Sat (10 s) · Landung <5 km/h + kein Steigen (30 s) |
| **IGC-Logging / Flugbuch** | ✅ | B-Record ~2 s im Flug, stromausfallsicher; 6 jüngste Flüge auf SD |
| **Thermik-Assistent** | ✅ | Kern-Schwerpunkt + Richtung, Kompass-Rose mit Lift-Punkten (N/E/S/W jetzt sauber auf dem Ring) |
| **Endanflug / Ziel** | ✅ | Ankunftshöhe, Distanz, benötigte vs. aktuelle Gleitzahl, Peilung |
| **Karte + Luftraum-Schnitt** | ✅ | CH-Vektor-Pack, verschiebbar/zoombar; Querschnitt + Terrain-Profil |
| **Wind-Schätzer** | 🆕✅ | aus GPS-Kreisdrift (vmax/vmin), Anzeige im Cruise + über BLE |
| **Umwelt** | ✅ | SHT45 Temp/Feuchte/Taupunkt → Basis-Abschätzung + BLE |
| **Einheitliche Statusleiste** | 🆕 | Uhr · Sat · FANET · Buddy · **BLE-Icon · WLAN-Icon** (nur wenn verbunden) · Batterie |
| **G-Kraft-Aufzeichnung** | ⛔ offline | hing am LSM6 — **Sensor defekt/ausgebaut**, kommt mit BNO055 zurück |

---

## 2. AUSSEN-VERBINDUNGEN

### 2.1 FANET (LoRa SX1262) — RX ✅ / TX 🔒
Unverändert: Empfang Typ-1/Typ-7 bis 20 Piloten; **Senden gesperrt (Gate D `FANET_TX_ENABLED=0`)**, Encoder spec-konform + Boot-Selbsttest. „Lieber TX aus als TX falsch."

### 2.2 BLE (NimBLE) — ✅ stark erweitert
**GATT-Service „Aura Vario"** (Service-UUID im Advertising → App erkennt sicher). Notify:
- **Vario-Char …0002** (20 B): Höhe, Vario, Speed, Heading, Sat, Batterie, Flags. ~10 Hz.
- **GPS-Char …0003** (16 B): Lat/Lon. ~1 Hz.
- **Status-Char …0004** (read): Version.
- **🆕 Umwelt/Wind-Char …0005** (24 B): Temp, Feuchte, Taupunkt, Wolkenbasis, Windspeed, Windrichtung.
- **🆕 Konfig/Task-Write-Char …0006** (WRITE + NOTIFY): der **Schreibweg** (siehe §2.6).
- **🆕 Persistenter BLE-Schalter:** Zustand in `/ble.cfg`; **Auto-Start beim Boot/Aufwachen**, wenn an.
- Name + PIN am Gerät und **per App** änderbar (`ble.name/pin/enabled`).

### 2.3 WLAN — ✅
Unverändert: Scan→Connect (Credentials auf SD), drei Verbraucher (Buddy-Server, IGC-Web-Download Port 80, Karten-Pack). **🆕 Robustheit:** Buddy-Heartbeat (TLS) **pausiert während aktiver BLE-Session** → keine Funk-Koexistenz-Störung.

### 2.4 Buddy-Server (`buddy.flightbuddyki.org`) — ✅
Unverändert: Selbst-Registrierung (MAC→Token+Pairing-Code, NVS), Heartbeat alle 5 min, Karten-Index/Download (Bearer, SHA-256). **HEILIG:** Werks-Secret/Token nie ins Repo/Chat.

### 2.5 GPS — ✅
Unverändert: TinyGPSPlus, 9600 Baud, letzte Position alle 30 s in NVS (Karten-Fallback).

### 2.6 🆕 BLE-Schreibweg / Konfigurator (M1+M2+M3) — ✅ verifiziert
**Ein** Schreib-Char `…0006`, **zwei** Inhalte:
- **`kind:settings`** (M2): dotted-path key/value → ins Modell + Live-Anwendung + Echo-Ack. **Alle 13 Vertrags-Gruppen** akzeptiert: `sound/vario/units/display/wifi/ble/fanet/pilot/alt/map/log/buddy/warn`. Live sofort wirksam: Ton, Vario-Schwellen, AVG-Fenster, QNH, BLE-Name/PIN/Schalter; der Rest persistent gespeichert (greift, sobald das Feature ihn liest).
- **`kind:task`** (M3): Flugplan **chunked + CRC32** (Standard IEEE/zlib) → SD `/tasks/<name>.json`, Echo „N WP". 4 KB Puffer, MTU 247.
- **M1 — ein versioniertes NVS-JSON-Modell** (`schema_version` + `updated_at`, Migration der Altpfade Ton/BLE/QNH) ist die eine Wahrheit; unbekannte Keys → `unknown_key`.
- **⚠️ Sicherheits-Abweichung Phase 1:** Schreib-Char ist **offen** (kein `WRITE_ENC`), konsequent mit den ohnehin offenen Lese-Chars. Grund: NimBLE-Bonding hielt nicht (Disconnect unterbrach die Key-Verteilung → „ausstehend"). Disconnect-Fix drin. **Phase-2-Härtung:** Verschlüsselung + Bond konsequent für **alle** Chars.

---

## 3. SENSORIK (I²C, 400 kHz)
| Sensor | Adresse | Zweck | Stand |
|---|---|---|---|
| **BMP581** | 0x47 | Druck → Höhe/Vario | ✅ aktiv |
| **SHT45** | 0x44 | Temp/Feuchte/Taupunkt | ✅ aktiv |
| **PCF85063 (RTC)** | 0x51 | Uhrzeit | ✅ aktiv |
| **BQ25896** | 0x6B | Batterie % | ✅ aktiv |
| **Modulino-Buzzer** | 0x1E | Vario-Ton | 🆕✅ aktiv |
| **LSM6DSO32** | 0x6A | Accel/Gyro | ⛔ **defekt (Löten) → ausgebaut** |
| **BMM350 (Kompass)** | 0x14 | Heading | ⚪ lebt am Bus, **misst nicht** (Minimal-Treiber bleibt „busy") → geparkt |

> **Hardware-Wechsel nächste Woche:** **BNO055** (9-Achs, Onboard-Fusion: Lage-Quaternion + schwerkraftbereinigte Linearbeschleunigung + Heading) **ersetzt BMM350 + LSM6**. Spart Platz + Rechenarbeit; macht die Stufe-1-AHRS-Arbeit der Fusion-Studie überflüssig. Heading kommt bis dahin aus dem GPS-Kurs.

---

## 4. SCREENS & BEDIENUNG
- **Flug-Screens** (Wisch): Cruise · Thermik · Ziel · Karte · Luftraum-Schnitt · Landung.
- **Menüs:** Hauptmenü · QNH · Flugbuch · Funk · WLAN · BLE · Karten-Overlay · Ton.
- **🆕 Menü-Aufruf nur über den Kapazitiv-Knopf** (autonom, unter dem Screen): **kurz = Ton-Menü**, **lang (~0,8 s) = Hauptmenü**. Screen-Langdruck öffnet **kein** Menü mehr (keine Fehlbedienung). Langdruck zeitbasiert ausgewertet (GT911 meldet den Key beim Halten nicht durchgehend).
- **E-Paper:** Flug-Screens 1 Hz Teil-Refresh, Karte/Schnitt voll (GC16) nur bei Eintritt/Zoom/Pan.

---

## 5. SPEICHER & LOGGING
- **SD:** `/maps/`, `/igc/` (+`flugbuch.dat`), `/airspace/`, `/obstacles/`, `/wifi.cfg`, `/ble.cfg`, **🆕 `/tasks/`** (BLE-empfangene Flugpläne).
- **NVS:** `buddy` (Token/Pairing/Status/Position), `sound` (Lautstärke/Ton-Parameter), **🆕 `cfg/model`** (das eine Settings-Modell).

---

## 6. IN VORBEREITUNG / GESPERRT / GEPLANT
- **🆕✅ Buzzer/Ton-Ausgabe — ERLEDIGT** (war 11.06. offen).
- **🆕✅ BLE-Konfigurator M1/M2/M3 — ERLEDIGT** (war im Konfig-Vertrag geplant).
- **Sensor-Fusion (IMU-Vario)** 🛠 — Studie liegt vor, **wartet auf Architekt-OK**; wird auf den **BNO055** umgeschrieben (liefert Lage/Linearbeschleunigung fertig).
- **FANET-Senden** 🔒 — Encoder fertig, Gate D zu bis Validierung durch echte Gegenstelle.
- **Konfigurator-Live-Verdrahtung** 📝 — die gespeicherten Keys `units/display/pilot/fanet/map/log/warn` müssen feature-für-feature noch ausgelesen/angewandt werden (Schreiben + Speichern läuft).
- **M4** 📝 aktiver Task im Flug (Wegpunkt-Navigation) · **M5** 🔒 FANET-TX-Flag nur mit Compile-Gate D.
- **BLE-Sicherheit Phase 2** ⚠️ — Verschlüsselung + Bond für alle Chars.
- **Kleinere offene Punkte:** Landung→Ride/SOS via FANET (Backend, hängt an TX); Flugbuch-Upload via App; Luftraum/Hindernisse auf der Karte rendern; Windrichtungs-Konvention per Flugtest bestätigen.

---

## 7. SICHERHEITS-GATES (bewusst)
| Gate | Zustand | Bedingung zum Öffnen |
|---|---|---|
| **FANET-TX (Gate D)** | 🔒 zu | echte Gegenstelle bestätigt gesendete Position/Höhe |
| **Flugerkennung** | konservativ | kein GPS-Jitter-Fehlstart (künftig IMU-bestätigt mit BNO055) |
| **Werks-Secret / Token** | geheim | nur lokal (NVS / git-ignored), nie in Repo/Doku/Chat |
| **BLE-Schreibweg** | ⚠️ Phase 1 offen | Phase-2: Verschlüsselung + Bond für alle Chars |

---

## 8. NÄCHSTE SCHRITTE (Vorschlag)
1. **BNO055 einbauen** (nächste Woche) → Fusion-Studie darauf umschreiben, Heading/Lage übernehmen.
2. **Konfigurator-Keys live verdrahten** (units/display/pilot/fanet …) — Modell ist da, Features lesen.
3. **FANET-TX validieren** (Gate D).
4. **BLE-Sicherheit Phase 2** (Verschlüsselung + Bond für alle Chars).
5. **M4** aktiver Task im Flug.

---
*Erstellt von der Code-Krücke, Stand `aura-kruecke/2-vario-map` zum 2026-06-13. Quellen: Audit 11.06. + verifizierte Arbeit dieser Session (Commits 00e4a94 … 69a7c12).*
