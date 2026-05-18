# AURA-KRUECKE · Master-Doc

## Status
- **Erstellt:** 2026-05-18
- **Letzte Aktualisierung:** 2026-05-18 (BLE verifiziert, Klapper→Schirm-Nodes, T3 gestrichen)
- **Hardware:** LilyGo T5 E-Paper S3 Pro (H752-01) + 4 Zusatz-Sensoren via I2C
- **Owner:** Claude (Chef-Architekt)
- **CEO/Pilot:** Ivo
- **Executor:** Claude Code CLI (PowerShell) — **gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`**
- **Repo-Root:** `C:\Users\Ivo\aura_kruecke`
- **Hosting:** GitHub **privat** unter Ivos Account, Repo-Name `aura_kruecke`

> ⚠️ **Vor jedem Schritt:** Executor liest [`AURA-KRUECKE-EXECUTOR-RULES.md`](./AURA-KRUECKE-EXECUTOR-RULES.md). Goldene Regel: **lesen ohne Rueckfrage erlaubt, ausfuehren nur mit explizitem "ok"**.

---

## Vision

Die Aura-Kruecke ist die **Go/No-Go-Validierung fuer das Aura-Vario-Projekt**. Auf bewaehrter LilyGo-Hardware (ESP32-S3 + ED047TC1 e-Paper + SX1262 + u-blox M10 GNSS) werden die drei Aura-USPs getestet:

1. **KF4D IMU-Baro-Fusion** — Vario-Latenz ~150 ms (XCTracer-Niveau), adaptive Daempfung
2. **ML-Vario auf TFLite Micro** — neuronale Steig-/Sink-Klassifikation aus BMP+IMU
3. **FANET SX1262 Schwarm-Awareness** — andere Piloten in der Region sichtbar

Wenn diese drei auf der Kruecke ueberzeugen → Aura-PCB wird gebaut.
Wenn nicht → Kruecke wird zum **eigenstaendigen Produkt** "BuddyVario T5" weiterentwickelt.

**Kein Aufwand fuer Mechanik, Power-Optimierung oder Custom-PCB-Layout in dieser Phase.** Die Kruecke darf 200 mA ziehen, haesslich aussehen und stromhungrig sein.

---

## Hardware-Stand

### T5 E-Paper S3 Pro (eingebaut)
| Komponente | Chip | Adresse / Pin |
|---|---|---|
| MCU | ESP32-S3-WROOM-1 (16M Flash, 8M PSRAM) | — |
| Display | ED047TC1, 4.7", 960x540, 16 Graustufen | parallel via epdiy v7 |
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

### Zusatz-Sensoren (Ali, Lieferung erwartet ~KW 23 / 2026-06-01)
| Sensor | Chip | I2C-Adresse | Zweck |
|---|---|---|---|
| BMP581 #1 | Bosch BMP581 | 0x47 (default) | Primaerer Druckaufnehmer fuer Vario/Hoehe |
| BMP581 #2 | Bosch BMP581 | 0x46 (ADR-Pin auf GND) | Roh-Aufzeichnung fuer spaetere Δp-Analyse |
| LSM6DSO32 | ST LSM6DSO32 | 0x6A | 6-DoF IMU ±32g, High-G Watchdog fuer Klapper-Erkennung |
| BNO085 | Bosch BNO085 | 0x4A | 9-DoF Fusion-IMU: Orientierung, Gravity-Vektor, Linear-Accel |
| SHT40 | Sensirion SHT40 | 0x44 | Temperatur (Dichtekorrektur), Feuchte, Taupunkt |

---

## Software-Stack

- **Framework:** Arduino on PlatformIO (espressif32 @ 6.5.0)
- **Display:** epdiy v7 (vroland/epdiy) fuer 16-Graustufen-Output
- **GUI-Layer:** LVGL 8.3 (im LilyGo-Template enthalten)
- **GNSS:** TinyGPSPlus
- **LoRa:** RadioLib 6.5+
- **Sensoren:** Adafruit_BMP5xx, Adafruit_LSM6DSO32, Adafruit_BNO08x, Adafruit_SHT4x
- **Power:** XPowersLib / PowersBQ25896
- **TFLite Micro:** TensorFlow Lite for Microcontrollers (esp32-arduino-fork)
- **JSON:** ArduinoJson 7
- **WiFi:** WiFiClientSecure + ArduinoWebsockets

### Lib-API (verifiziert 2026-05-18 in Ticket 1)
- BMP5xx: `#include <Adafruit_BMP5xx.h>`, `begin(addr, &Wire)`, Enums `BMP5XX_*`
- LSM6DSO32: `#include <Adafruit_LSM6DSO32.h>`, `begin_I2C(addr, &Wire)`
- BNO08x: `#include <Adafruit_BNO08x.h>`, `begin_I2C(addr, &Wire)`
- SHT4x: `#include <Adafruit_SHT4x.h>`, `begin(&Wire)` — KEIN Adress-Parameter
- BQ25896: `#include "PowersBQ25896.tpp"`, `getChargeStatusString()`
- PIO-Pfad: `C:\Users\Ivo\.platformio\penv\Scripts\pio.exe`

---

## BLE-Protokoll (verifiziert 2026-05-18 via PREP-Ticket)

**Protokoll: AURA Binary v1.0** — NICHT NMEA/LK8EX1. Verifiziert in `aura_binary_parser.dart`.

### Service UUID
```
E7F5A3B1-2C8D-4E6F-9A0B-3D1C5E7F9A2B
```

### Characteristics

| UUID (letzte 4 Bytes) | Name | Property | Rate | Groesse |
|---|---|---|---|---|
| `...9A2C` | VARIO | NOTIFY | 10 Hz | 12 Bytes |
| `...9A2D` | GPS | NOTIFY | 1 Hz | 16 Bytes |
| `...9A2E` | STATUS | NOTIFY | 1 Hz | 6 Bytes |
| `...9A2F` | CONFIG | WRITE/INDICATE | — | geplant (v1.1) |

### Packet-Formate (Little-Endian)

**VARIO (0xAA, 12 B):** `[header][vz:i16÷100][intVz:i16÷100][pressure:u32 Pa][temp:i16÷100][flags:u8]`
Flags: bit0=isFlying, bit1=isThermik, bit2=gpsOk

**GPS (0xBB, 16 B):** `[header][lat:i32÷1e7][lon:i32÷1e7][alt:i16 m][speed:u16÷10 km/h][heading:u16÷10°][sats:u8]`

**STATUS (0xCC, 6 B):** `[header][battery:u8 %][mode:u8][fanet:u8][uptime:u16 min]`

---

## Connectivity-Strategie

**Reihenfolge:** zuerst **BLE** (Ticket 5), dann **WiFi** (Ticket 6).

### Kanal A · BLE GATT-Server (primaer)
- Custom AURA UUID, Binary Packets, 10 Hz Vario

### Kanal B · WiFi-STA via Handy-Hotspot (sekundaer)
- `wss://buddy.flightbuddyki.org/aura/telemetry`
- Fallback: `ws://192.168.x.x:8001/aura/telemetry`
- IGC-Upload, Wetterdaten, OTA-Updates, Standalone-Modus

**Beide Kanaele gleichzeitig aktiv.** BLE ist Pflicht, WiFi ist Bonus.

---

## Heilig-Liste

- ❌ Schreibend in `C:\BuddyServer\*`
- ❌ Schreibend in `C:\Users\Ivo\flight_buddy_ki\lib\core\buddy_chat\*`
- ❌ Schreibend in `cruise_screen.dart`
- ❌ `BUDDY_AI_MODE=gemini`
- ❌ Vario < 10 Hz, GPS < 1 Hz

---

## Ticket-Roadmap

### Haupt-Track

| ID | Titel | Status | Geschaetzt |
|---|---|---|---|
| AURA-KRUECKE-1 | ESP32-S3 Bring-up + 4 Sensoren via I2C | **GATE-2 done** (wartet auf HW) | 4–6 h |
| AURA-KRUECKE-2-PREP | Verifikation: BLE, Lizenz, Simulator-Spec | **✅ DONE** | 1–2 h |
| AURA-KRUECKE-2 | Vario Core: KF4D Kalman, Thermik, Dichtekorr. | **Ready** | 13–21 h |
| ~~AURA-KRUECKE-3~~ | ~~Dual-BMP581-Δp Klapper-Vorwarnung am Gurtzeug~~ | **GESTRICHEN** | — |
| AURA-KRUECKE-4 | E-Paper Cruise Screen v1 | Backlog | 8–12 h |
| AURA-KRUECKE-5 | BLE-Peripheral: AURA Binary Protocol + App Pairing | Backlog | 6–10 h |
| AURA-KRUECKE-6 | WiFi-Hotspot-Provisioning + WebSocket | Backlog | 8–12 h |
| AURA-KRUECKE-7 | *(frei — IMU jetzt in T2 integriert)* | — | — |
| AURA-KRUECKE-8 | TFLite-Micro ML-Vario · Training-Datensammlung | Backlog | 16–24 h |
| AURA-KRUECKE-9 | FANET via SX1262, Schwarm-Empfang | Backlog | 12–20 h |
| AURA-KRUECKE-10 | E-Paper Thermal + H&F + FANET + XC Screens | Backlog | 12–16 h |
| AURA-KRUECKE-11 | IGC-Logger + Upload via WiFi | Backlog | 6–8 h |
| AURA-KRUECKE-12 | Touch-Bedienung (GT911) + Menu-System | Backlog | 8–12 h |

#### Warum T3 gestrichen ist

> **Recherche-Ergebnis (2026-05-18):** Dual-BMP581 Δp am **Gurtzeug** hat keinen Praezedenfall. Die einzigen funktionierenden Systeme (ParaBaro/Aviometrics, FLYSENS) platzieren Sensoren **im Fluegel**. Signal am Gurtzeug: 5–20 Pa bei SNR ~30:1 — theoretisch messbar, aber Windboeen, Koerperbewegungen und thermische Drift dominieren. **Klapper-Vorwarnung am Gurtzeug ist Wunschdenken, nicht Physik.**
>
> BMP581 #2 zeichnet stattdessen Rohdaten auf (T2, Phase D) als Baseline fuer die Schirm-Nodes-Familie.

### W-Familie (Wetter/Atmosphaere)

| ID | Titel | Status | Geschaetzt |
|---|---|---|---|
| AURA-KRUECKE-W1 | Wolkenbasis + Lapse-Rate (SHT40) | Ready nach T2 | 4–6 h |
| AURA-KRUECKE-W2 | Forecast-Vergleich + Inversions-Detection (WiFi) | Backlog | 6–10 h |
| AURA-KRUECKE-W3 | Wind-Schichtung + Rotor-Warning | Backlog | 6–8 h |

### S-Familie (Schirm-Nodes) — NEU

Klapper-Vorwarnung gehoert auf den **Fluegel**, nicht ans Gurtzeug.

| ID | Titel | Status | Geschaetzt |
|---|---|---|---|
| AURA-KRUECKE-S1 | Schirm-Node HW: BMP581 + ESP32-C3 + BLE, <5g | Backlog | 12–20 h |
| AURA-KRUECKE-S2 | BLE-Mesh: Schirm-Nodes → Kruecke Gateway, Δp-Fusion | Backlog | 16–24 h |
| AURA-KRUECKE-S3 | Klapper-Detektor auf echten Fluegel-Δp-Daten | Backlog | 12–16 h |

**Voraussetzung:** Fluegel-integrierte Sensoren (Custom-PCB, ~10x15 mm). Fruehestens nach Go/No-Go.

---

## Go/No-Go-Kriterien fuer Aura-PCB

- ✅ **KF4D-Vario Latenz < 200 ms** im Bench-Test → **PCB bauen**
- ✅ **ML-Vario Echtzeit** (Inferenz < 50 ms, > 95% Accuracy) → **PCB bauen**
- ✅ **FANET ≥ 5 km LOS** → **PCB bauen**
- ❌ Eines scheitert → T5 Pro als "BuddyVario T5" weiterentwickeln

Schirm-Nodes (S-Familie) sind **kein** Go/No-Go-Kriterium.

---

## Reviewing Rule

Nach jedem Ticket: 5-Zeilen-Update in `docs/STATUS.md`, Ticket nach `docs/tickets/done/`.
Nach jedem 3. Ticket: Architecture-Review.
