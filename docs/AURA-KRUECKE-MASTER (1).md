# AURA-KRUECKE · Master-Doc

## Status
- **Erstellt:** 2026-05-18
- **Hardware:** LilyGo T5 E-Paper S3 Pro (H752-01) + 4 Zusatz-Sensoren via I2C
- **Owner:** Claude (Chef-Architekt)
- **CEO/Pilot:** Ivo
- **Executor:** Claude Code CLI (PowerShell) — **gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`**
- **Repo-Root:** `C:\Users\Ivo\aura_kruecke` *(neu anzulegen, separat von flight_buddy_ki)*
- **Hosting:** GitHub **privat** unter Ivos Account, Repo-Name `aura_kruecke`

> ⚠️ **Vor jedem Schritt:** Executor liest [`AURA-KRUECKE-EXECUTOR-RULES.md`](./AURA-KRUECKE-EXECUTOR-RULES.md). Goldene Regel: **lesen ohne Rückfrage erlaubt, ausführen nur mit explizitem "ok"**.

---

## Vision

Die Aura-Krücke ist die **Go/No-Go-Validierung für das Aura-Vario-Projekt**. Auf bewährter LilyGo-Hardware (ESP32-S3 + ED047TC1 e-Paper + SX1262 + u-blox M10 GNSS) werden die drei Aura-USPs getestet:

1. **Dual BMP581 differential Δp** — Klapper-Vorwarnung 200–500 ms vor dem Ereignis
2. **ML-Vario auf TFLite Micro** — neuronale Steig-/Sink-Klassifikation aus BMP+IMU
3. **FANET SX1262 Schwarm-Awareness** — andere Piloten in der Region sichtbar

Wenn diese drei auf der Krücke überzeugen → Aura-PCB wird gebaut.
Wenn nicht → Krücke wird zum **eigenständigen Produkt** "BuddyVario T5" weiterentwickelt.

**Kein Aufwand für Mechanik, Power-Optimierung oder Custom-PCB-Layout in dieser Phase.** Die Krücke darf 200 mA ziehen, hässlich aussehen und stromhungrig sein.

---

## Hardware-Stand

### T5 E-Paper S3 Pro (eingebaut)
| Komponente | Chip | Adresse / Pin |
|---|---|---|
| MCU | ESP32-S3-WROOM-1 (16M Flash, 8M PSRAM) | — |
| Display | ED047TC1, 4.7", 960×540, 16 Graustufen | parallel via epdiy v7 |
| Touch | GT911 | I2C 0x5D |
| LoRa | SX1262 | SPI (CS=46, IRQ=10, RST=1, BUSY=47) |
| GNSS | u-blox MIA-M10Q (≈ MAX-M10S) | UART2 (RX=44, TX=43) |
| Battery Charger | BQ25896 | I2C 0x6B |
| Fuel Gauge | BQ27220 | I2C 0x55 |
| E-Paper Power | TPS65185 | I2C 0x68 |
| RTC | PCF85063 | I2C 0x51 |
| IO Extender | PCA9535PW | I2C 0x20 |
| Akku | LiPo 3.7 V 1500 mAh | — |

**I2C-Bus:** SDA=39, SCL=40. **SPI:** MISO=21, MOSI=13, SCLK=14.

### Zusatz-Sensoren (Ali, Bestellung im Versand)
| Sensor | Chip | I2C-Adresse | Zweck |
|---|---|---|---|
| BMP581 #1 | Bosch BMP581 | 0x47 (default) | Primärer Druckaufnehmer für Vario/Höhe |
| BMP581 #2 | Bosch BMP581 | 0x46 (ADR-Pin auf GND via Dupont) | Differential Δp für Klapper-Vorwarnung |
| LSM6DSO32 | ST LSM6DSO32 | 0x6A | 6-DoF IMU ±32g für ML-Vario-Training |
| BNO085 | Bosch BNO085 | 0x4A | 9-DoF Fusion-IMU für Heading/Orientation (Kompass-Quelle) |
| SHT40 | Sensirion SHT40 | 0x44 | Temperatur, Feuchte, Taupunkt |

Alle Sensoren am gleichen I2C-Bus (SDA=39, SCL=40). **Keine Adresskonflikte** mit den onboard-Chips geprüft.

---

## Software-Stack

- **Framework:** Arduino on PlatformIO (passt zum LilyGo-Beispielcode aus dem T5S3-4.7-e-paper-PRO Repo)
- **Display:** epdiy v7 (vroland/epdiy) für 16-Graustufen-Output
- **GUI-Layer:** LVGL 8.3 (im LilyGo-Template enthalten, später ggf. ersetzen durch direkte epdiy-Rendering wenn LVGL für e-Paper zu langsam ist)
- **GNSS:** TinyGPSPlus
- **LoRa:** RadioLib 6.5+
- **Sensoren:** Adafruit_BMP5xx, Adafruit_LSM6DSO32, Adafruit_BNO08x, Adafruit_SHT4x
- **Power:** XPowersLib (für BQ25896)
- **TFLite Micro:** TensorFlow Lite for Microcontrollers (esp32-arduino-fork)
- **JSON:** ArduinoJson 7
- **WiFi:** WiFiClientSecure + ArduinoWebsockets

---

## Connectivity-Strategie

**Reihenfolge entschieden 2026-05-18:** zuerst **BLE** (Ticket 5), dann **WiFi** (Ticket 6). BLE muss vollständig laufen und mit der Flutter-App pairen können, bevor WiFi überhaupt angegangen wird. Begründung: BLE ist der bewährte Pfad, validiert das ganze App-Integration-Konzept; WiFi ist Bonus, der erst Sinn macht wenn BLE-Telemetrie steht.

**Zwei parallele Kanäle im Endzustand:**

### Kanal A · BLE GATT-Server (primär)
- **Service:** Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` *(zu verifizieren — Claude Code muss `lib/core/ble/` in flight_buddy_ki READ-ONLY scannen und das tatsächlich verwendete UUID-Schema extrahieren)*
- **Format:** LK8EX1 NMEA-Sätze plus eigene `$AURA,...` Sätze für Aura-spezifische Daten (Δp, ML-Klassifikation, Schwarm-Daten)
- **Update-Rate:** Vario 10 Hz, Höhe 1 Hz, GPS 1 Hz, IMU 50 Hz (subsampled)
- **Power:** BLE 5.0 LE, Long-Range optional
- **Kompatibilität:** Bestehende Flight-Buddy-KI-App muss die Aura ohne Code-Änderung pairen können (Aura erscheint als "vario device")

### Kanal B · WiFi-STA via Handy-Hotspot (sekundär)
- **Modus:** ESP32 verbindet sich mit Hotspot des Pilot-Handys (SSID/PW provisioniert via BLE oder Captive-Portal beim ersten Start)
- **Endpunkt:** `wss://buddy.flightbuddyki.org/aura/telemetry` (WebSocket über Cloudflare Named Tunnel)
- **Fallback:** lokales `ws://192.168.x.x:8001/aura/telemetry` wenn im Home-Netz (BuddyServer-Heimserver)
- **Zweck:**
  - Höhere Bandbreite für IGC-Upload (komplette Aufzeichnungen, nicht nur Live-Stream)
  - Pull von Wetterdaten, Holfuy-Wind, Airspace-Updates direkt am Gerät
  - **Standalone-Modus** — Aura läuft auch ohne aktive Flutter-App, wenn das Handy nur Hotspot bietet
  - Reicht später als Pfad für OTA-Firmware-Updates
- **Auth:** Pre-Shared Token, im NVS gespeichert, bei Erstkonfiguration via BLE übertragen

**Wichtig:** beide Kanäle sind **gleichzeitig aktiv**, redundant. BLE ist Pflicht, WiFi ist Bonus. Wenn Hotspot weg → BLE läuft weiter.

---

## Heilig-Liste · was NICHT angefasst wird

**Lesen ist überall erlaubt** (siehe Executor-Rules). Geschützt ist nur das **Schreiben** in diese Pfade:

- ❌ Schreibend in `C:\BuddyServer\*` — read-only Referenz für API-Endpunkte. Wenn neue Endpunkte gebraucht werden → separater Server-Ticket, NICHT in Aura-Krücke-Tickets reinmischen.
- ❌ Schreibend in `C:\Users\Ivo\flight_buddy_ki\lib\core\buddy_chat\*` — komplett tabu
- ❌ Schreibend in `C:\Users\Ivo\flight_buddy_ki\lib\features\cruise\cruise_screen.dart` — pixel-identisch lassen (Hardware-Logik darunter darf geändert werden, UI-Layout nicht)
- ❌ `BUDDY_AI_MODE=gemini` — sakrosankt, nicht überschreiben
- ❌ Vario-Daten-Rate niemals < 10 Hz, GPS niemals < 1 Hz im Flug

**Explizit erlaubt zum Lesen** (für Protokoll-Verständnis und API-Reverse-Engineering):
- ✅ `C:\Users\Ivo\flight_buddy_ki\lib\core\ble\*` — read-only, um BLE-UUIDs und LK8EX1-Parser-Verhalten zu verstehen
- ✅ `C:\BuddyServer\docs\*` — Architektur-Referenz
- ✅ `C:\BuddyServer\*.py`, `.ts`, `.json` — read-only Code-Inspektion für Endpunkt-Verträge

Aura-Krücke-Repo ist **komplett isoliert** unter `C:\Users\Ivo\aura_kruecke`.

---

## Ticket-Roadmap

| ID | Titel | Status | Geschätzt |
|---|---|---|---|
| AURA-KRUECKE-1 | ESP32-S3 Bring-up + 4 Sensoren via I2C | **Ready** | 4–6 h |
| AURA-KRUECKE-2 | Vario Core: BMP581 → Höhe + Steigwert, ø 20 s | Backlog | 4–8 h |
| AURA-KRUECKE-3 | Dual-BMP581-Δp · Klapper-Vorwarnungs-Algorithmus | Backlog | 8–16 h |
| AURA-KRUECKE-4 | E-Paper Cruise Screen v1 (aus den Mockups) | Backlog | 8–12 h |
| AURA-KRUECKE-5 | BLE-Peripheral mit LK8EX1 + Flight-Buddy-App pairing | Backlog | 6–10 h |
| AURA-KRUECKE-6 | WiFi-Hotspot-Provisioning + WebSocket zu BuddyServer | Backlog | 8–12 h |
| AURA-KRUECKE-7 | LSM6DSO32 + BNO085 Integration, Heading + Kompass | Backlog | 4–6 h |
| AURA-KRUECKE-8 | TFLite-Micro ML-Vario · Training-Datensammlung im Flug | Backlog | 16–24 h |
| AURA-KRUECKE-9 | FANET via SX1262, Schwarm-Empfang | Backlog | 12–20 h |
| AURA-KRUECKE-10 | E-Paper Thermal + H&F + FANET + XC Screens | Backlog | 12–16 h |
| AURA-KRUECKE-11 | IGC-Logger + Upload via WiFi | Backlog | 6–8 h |
| AURA-KRUECKE-12 | Touch-Bedienung (GT911) + Menu-System | Backlog | 8–12 h |

**Reviewing Rule:** nach jedem Ticket schreibt der Executor (Claude Code) ein 5-Zeilen-Update in `aura_kruecke/docs/STATUS.md` und markiert den Ticket-File in `aura_kruecke/docs/tickets/done/`. Nach jedem 3. Ticket Architecture-Review durch Claude (Chef-Architekt) via Browser.

---

## Go/No-Go-Kriterien für Aura-PCB

Nach AURA-KRUECKE-9 (Tickets 1–9 abgeschlossen, ML-Vario + Δp-Algo + FANET getestet) wird entschieden:

- ✅ **Δp erkennt simulierten Klapper** ≥ 200 ms vor dem Ereignis im Bench-Test, ≥ 150 ms im Flug → **PCB bauen**
- ✅ **ML-Vario läuft Echtzeit auf ESP32-S3** (Inferenz < 50 ms, > 95 % Accuracy auf Test-Set) → **PCB bauen**
- ✅ **FANET-Reichweite ≥ 5 km LOS im Test** mit zweitem Node → **PCB bauen**
- ❌ Eines der drei scheitert → **PCB-Projekt killen**, T5 Pro als Plan B "BuddyVario T5" weiterentwickeln (eh schon ein 70 % fertiges Pilot-Gerät)

---

## Wo gehts weiter

1. **Zuerst:** `AURA-KRUECKE-EXECUTOR-RULES.md` lesen lassen — diese Datei definiert *wie* gearbeitet wird (lesen ohne Rückfrage, ausführen nur mit "ok")
2. **Dann:** dieses Master-Doc als Architektur-Kontext
3. **Erstes ausführbares Ticket:** `AURA-KRUECKE-1.md` in `aura_kruecke/docs/tickets/`
