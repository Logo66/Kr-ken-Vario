# AURA-KRUECKE-1 · ESP32-S3 Bring-up + 4 Sensoren via I2C

## Status
- **Erstellt:** 2026-05-18
- **Priorität:** P0 (blockiert alle weiteren AURA-KRUECKE-Tickets)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI (PowerShell) — **gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`**
- **Branch:** `aura-kruecke/1-bringup`
- **Geschätzt:** 4–6 h
- **Voraussetzung:** Sensoren von Ali physisch eingetroffen, Dupont-Kabel vorhanden, T5 E-Paper S3 Pro im USB-Modus erreichbar

> ⚠️ **Workflow:** Lesen jederzeit erlaubt. Jede ausführende Aktion (Datei erstellen, Build, Flash, Commit, Push) braucht **explizites "ok"** von Ivo. Approval-Gates sind unten im Plan markiert mit **🛑 GATE-N**.

---

## Ziel

Neues PlatformIO-Projekt `aura_kruecke` aufsetzen, ESP32-S3 booten, alle vier Zusatz-Sensoren (2× BMP581, LSM6DSO32, BNO085, SHT40) am I2C-Bus initialisieren und Live-Daten via Serial Monitor ausgeben. **Keine Vario-Logik, kein BLE, kein WiFi, keine Display-Anzeige außer "Bring-up OK".** Reines Bring-up-Ticket.

---

## Akzeptanzkriterien

- [ ] PlatformIO-Projekt `aura_kruecke` existiert unter `C:\Users\Ivo\aura_kruecke` mit korrekter `platformio.ini` für das T5 E-Paper S3 Pro
- [ ] Projekt kompiliert ohne Warnings (außer Lib-internen)
- [ ] `pio run -t upload` flasht erfolgreich auf das Board
- [ ] Serial Monitor @ 115200 zeigt **alle 5 Sensoren als "OK"** im Boot-Log (2× BMP581 mit unterschiedlichen Adressen!)
- [ ] Im Loop @ 1 Hz erscheinen über Serial **alle Sensorwerte plausibel**:
  - BMP581 #1 (0x47): Druck ≈ 950–1020 hPa, Temp ≈ 15–25 °C
  - BMP581 #2 (0x46): identische Werte ±0.5 hPa (sitzt direkt daneben)
  - LSM6DSO32 (0x6A): Accel ≈ (0, 0, 9.8) m/s² in Ruhelage, Gyro ≈ (0, 0, 0) °/s
  - BNO085 (0x4A): Quaternion + Game Rotation Vector, plausibel bei Bewegung
  - SHT40 (0x44): Temp ≈ 15–25 °C, Feuchte 30–70 %
- [ ] Onboard-Komponenten initialisiert, keine Adresskonflikte:
  - BQ25896 Charger meldet Akku-Status (Voltage, Stromrichtung)
  - BQ27220 Fuel Gauge meldet SOC (%)
  - PCF85063 RTC erreichbar (Zeit lesen)
- [ ] E-Paper zeigt einmalig beim Boot **"AURA · Bring-up OK"** plus Versions-String — danach Standby (keine weiteren Refreshes)
- [ ] Beim Drücken des Boot-Buttons (GPIO 0) wird ein **I2C-Scan** über Serial ausgegeben und alle erwarteten Adressen sind gefunden:
  ```
  0x20 PCA9535  0x44 SHT40  0x46 BMP581#2  0x47 BMP581#1
  0x4A BNO085   0x51 PCF85063  0x55 BQ27220  0x5D GT911
  0x68 TPS65185  0x6A LSM6DSO32  0x6B BQ25896
  ```
- [ ] Code committed auf Branch `aura-kruecke/1-bringup`, keine `node_modules`/Build-Artefakte im Repo
- [ ] `docs/STATUS.md` enthält 5-Zeilen-Abschlussnotiz

---

## Sacred / Heilig-Liste

Aus dem MASTER-Doc:
- ❌ **NICHTS** in `C:\BuddyServer` oder `C:\Users\Ivo\flight_buddy_ki` ändern
- ❌ Aura-Krücke ist **eigenes Repo** unter `C:\Users\Ivo\aura_kruecke`, komplett isoliert
- ❌ Auf existierende Projekt-Konventionen NICHT zurückgreifen — dies ist ein neues Repo mit eigenen Standards (PlatformIO/Arduino, nicht Flutter)

---

## Aktive Files (zu erstellen)

```
C:\Users\Ivo\aura_kruecke\
├── platformio.ini
├── src\
│   └── main.cpp
├── include\
│   ├── pins.h           ← alle GPIO-Definitionen aus LilyGo-Repo + Sensor-Adressen
│   └── version.h        ← AURA_VERSION = "0.1.0-bringup"
├── lib\
│   └── (leer — Libs via platformio.ini)
├── docs\
│   ├── STATUS.md
│   └── tickets\
│       ├── done\        ← (leer, kommt nach Abschluss)
│       └── AURA-KRUECKE-1.md   ← diese Datei
├── .gitignore           ← .pio, .vscode, *.bin
└── README.md            ← Kurzbeschreibung, Setup-Notes
```

---

## Architektur-Entscheidungen

### platformio.ini
```ini
[env:t5_epaper_s3_pro]
platform = espressif32 @ 6.5.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_size = 16MB
board_build.partitions = default_16MB.csv
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.psram_type = opi

monitor_speed = 115200
upload_speed = 921600

build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -DAURA_VERSION=\"0.1.0-bringup\"
    -mfix-esp32-psram-cache-issue

lib_deps =
    adafruit/Adafruit BMP5xx Library @ ^1.0
    adafruit/Adafruit LSM6DS @ ^4.7
    adafruit/Adafruit BNO08x @ ^1.2
    adafruit/Adafruit SHT4x Library @ ^1.0
    adafruit/Adafruit BusIO @ ^1.16
    lewisxhe/XPowersLib @ ^0.2.3
    mikalhart/TinyGPSPlus @ ^1.1
    bblanchon/ArduinoJson @ ^7.0
```

### pins.h (aus LilyGo T5S3-4.7-e-paper-PRO Repo übernehmen, ergänzt)
```cpp
#pragma once

// I2C — gemeinsam mit Onboard-Chips
#define BOARD_I2C_SDA       39
#define BOARD_I2C_SCL       40
#define I2C_FREQ_HZ         400000

// SPI — gemeinsam mit LoRa + SD
#define BOARD_SPI_MISO      21
#define BOARD_SPI_MOSI      13
#define BOARD_SPI_SCLK      14

// GNSS UART
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43

// LoRa
#define BOARD_LORA_CS       46
#define BOARD_LORA_IRQ      10
#define BOARD_LORA_RST      1
#define BOARD_LORA_BUSY     47

// Button
#define BOARD_BOOT_BTN      0

// I2C-Adressen — Onboard
#define ADDR_PCA9535        0x20
#define ADDR_GT911          0x5D
#define ADDR_PCF85063       0x51
#define ADDR_TPS65185       0x68
#define ADDR_BQ25896        0x6B
#define ADDR_BQ27220        0x55

// I2C-Adressen — Zusatz-Sensoren (AURA-KRUECKE)
#define ADDR_BMP581_PRIMARY    0x47
#define ADDR_BMP581_SECONDARY  0x46
#define ADDR_LSM6DSO32         0x6A
#define ADDR_BNO085            0x4A
#define ADDR_SHT40             0x44
```

### main.cpp · Setup-Reihenfolge mit Approval-Gates

```
🛑 GATE-1 · Repo-Skelett anlegen
  Aktionen:
    - mkdir C:\Users\Ivo\aura_kruecke
    - platformio.ini, src/main.cpp, include/pins.h, include/version.h, .gitignore, README.md erstellen
    - git init
  Auswirkung: lokales Repo, noch kein Remote, noch kein Build

🛑 GATE-2 · Erstes Build (zieht Libs!)
  Aktion: pio run (NICHT upload)
  Auswirkung: ca. 200 MB Dependencies werden gezogen, kompiliert
  Reversibel: ja, .pio/ kann gelöscht werden

Code-Logik in main.cpp implementieren (kein Approval nötig — reines Editieren der Dateien aus GATE-1):
  1. Serial.begin(115200), 500 ms delay
  2. Wire.begin(SDA, SCL, 400000)
  3. Power-Management init (BQ25896 via XPowersLib) — Akku-Status loggen
  4. E-Paper init (siehe Note unten — Minimal-Variante)
  5. Sensoren in dieser Reihenfolge initialisieren:
     - SHT40 (einfachster, gibt erste Sanity)
     - BMP581 #1 (Default-Adresse, Lib-Bring-up)
     - BMP581 #2 (Alternative-Adresse — kritischer Test des ADR-Pins)
     - LSM6DSO32
     - BNO085 (komplexester, eigene SHTP-Init)
  6. Jeden Sensor mit Serial.printf("[OK] %s @ 0x%02X\n", name, addr) loggen
  7. Bei Fehlschlag: Serial.printf("[FAIL] %s ..."), aber WEITER mit den restlichen Sensoren
  8. E-Paper "AURA · Bring-up OK" zeichnen
  9. Loop @ 1 Hz alle Sensorwerte ausgeben
  10. Boot-Button-Press → I2C-Scan-Output

🛑 GATE-3 · Erstes Flashen
  Aktion: pio run -t upload
  Auswirkung: Firmware geht aufs Board, ersetzt was auch immer vorher drauf war
  Reversibel: ja, neu flashen jederzeit möglich

🛑 GATE-4 · Iterations-Loop bis Akzeptanzkriterien erfüllt
  Pro Iteration: Code-Änderung (kein Gate) → Build (kein Gate, GATE-2 bereits gegeben)
  Aber: jedes erneute upload → kurzer "ok für reflash?"-Check, weil Hardware betroffen

🛑 GATE-5 · Commit + Push
  Aktionen:
    - GitHub-Repo aura_kruecke privat anlegen (über gh CLI oder Browser-Hinweis an Ivo)
    - git remote add origin
    - git add . && git commit -m "feat: bring-up + 4 Sensoren auf T5 Pro"
    - git push -u origin aura-kruecke/1-bringup
  Auswirkung: Code geht auf GitHub privat
  Reversibel: ja (force-push erlaubt, Repo ist neu)
```

### E-Paper — pragmatischer Minimal-Ansatz für Ticket 1
Statt sofort die volle epdiy-Integration anzugehen (frisst leicht 2 h), nutzen wir das LilyGo-Display-Example aus dem Repo `Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO`. Daraus übernehmen wir nur die Init-Sequenz und einen einzigen `epd_draw_text()`-Aufruf für den Bring-up-String.

**Wenn LVGL-Setup mehr als 30 Min Zeit kostet:** weglassen, ein einfaches `Serial.println("[OK] E-paper would say: AURA · Bring-up OK")` als Platzhalter. Display-Rendering ist Aufgabe von **AURA-KRUECKE-4**, nicht von diesem Ticket.

---

## Test-Plan

### Bench-Tests (vor Commit)
1. **Cold-Boot:** Power-Cycle → Serial-Output zeigt alle 5 Zusatz-Sensoren + 3 Onboard-Chips als OK
2. **I2C-Scan:** Boot-Button drücken → Liste der 11 erwarteten Adressen
3. **BMP581-Differenz:** beide BMP581 sollten Drücke innerhalb ±0.5 hPa zueinander liefern (sie sitzen physisch nebeneinander). Wenn nicht → ADR-Pin nicht korrekt verdrahtet, beide auf 0x47 → Konflikt
4. **IMU-Plausibilität:** Board flach hinlegen → Accel Z ≈ 9.8, X,Y ≈ 0. Board kippen → Werte ändern sich plausibel
5. **BNO085-Quaternion:** Board langsam um eine Achse drehen → Quaternion-Werte ändern sich smooth, keine NaN
6. **SHT40:** anhauchen → Temp und Feuchte steigen kurz
7. **Akku-Status:** USB ziehen → BQ25896 meldet Discharge, BQ27220 SOC sinkt langsam

### Stabilität
- 30 Min im Loop laufen lassen, kein Reset, keine I2C-Errors im Log

---

## Bekannte Stolpersteine

- **BMP581 Init-Reihenfolge:** Bosch-Datasheet verlangt ~10 ms nach Power-Up und NVM-Ready-Poll vor erstem I2C-Zugriff. Adafruit_BMP5xx::begin() macht das korrekt, aber **nicht** mit `Wire.beginTransmission()` direkt scannen — siehe Adafruit-Forums-Thread "BMP581 breakout DOA" (Mai 2026). Wenn Sensor nicht antwortet auf ersten Versuch: 100 ms warten, nochmal versuchen, dann erst aufgeben.
- **BMP581 #2 Adresse:** Standard 0x47. Adresse 0x46 wird über den **ADR-Pin (SDO)** auf GND gezogen — bei GY-BMP581-Modulen ist das ein Header-Pin, einfach Dupont-Kabel auf GND. Falls Sensor #2 weiterhin auf 0x47 antwortet → verkehrte Verdrahtung, ADR muss WIRKLICH auf GND
- **BNO085 ist SHTP, nicht plain I2C:** Adafruit_BNO08x kümmert sich darum. Bei "no reports received" → Reset-Pin überprüfen (muss high sein während Init)
- **ESP32-S3 PSRAM:** muss in `platformio.ini` aktiviert sein (`board_build.psram_type = opi`), sonst crash beim Boot
- **`-mfix-esp32-psram-cache-issue`** ist nur für ESP32 (nicht S3) nötig, aber schadet auf S3 nicht — drinlassen für Sicherheit

---

## Definition of Done

- Akzeptanzkriterien alle abgehakt
- Code committed auf `aura-kruecke/1-bringup` und nach **GATE-5** auf GitHub Private gepusht
- Screenshot des Serial Monitors mit allen 5 OK-Sensoren in `docs/screenshots/bringup-serial.png`
- `docs/STATUS.md` mit Abschlussnotiz im Stil:
  ```
  ## 2026-05-XX · AURA-KRUECKE-1 abgeschlossen
  - Bring-up auf T5 Pro erfolgreich
  - Alle 5 Sensoren + 3 Onboard-Chips erkannt
  - Bekannte Issue: [falls vorhanden]
  - Nächstes Ticket: AURA-KRUECKE-2 (Vario Core)
  ```
- Diese Ticket-Datei nach `docs/tickets/done/` verschoben

---

## Notes für AURA-KRUECKE-2 (Vario Core)

Nach Bring-up folgt der Vario-Algorithmus auf BMP581 #1. Vorbereitung schon hier:
- BMP581 #1 sollte in Continuous Mode initialisiert werden, 50 Hz Sample-Rate, 4× Oversampling
- Höhenformel: `h = 44330 * (1 - (p/p0)^(1/5.255))` mit p0 = QNH (default 1013.25 hPa)
- Vario-Filter: simple IIR, später durch Kalman ersetzbar
- Ø 20 s Steigwert für Thermal-Mode

Aber das ist **nicht Teil von Ticket 1.** Bring-up zuerst sauber, dann erst Vario.
