# DEBUG-SERIAL-CDC — USB-CDC Serial Output reparieren

**Status:** offen · **Prioritaet:** blocker (GATE 0 haengt davon ab)
**Repo:** `C:\Users\Ivo\aura_kruecke` · **Board:** LilyGo T5 E-Paper S3 Pro auf COM5

---

## Symptom

- Firmware flasht erfolgreich (`pio run -t upload` auf COM5, keine Fehler)
- USB-Geraet enumeriert: `Serielles USB-Geraet (COM5)` / VID:303A PID:1001
- **Kein Serial-Output** — 0 Bytes via PIO Monitor, .NET SerialPort, pyserial
- E-Paper zeigt altes Factory-Bild (persistent, kein Bug — EPD behaelt letztes Bild)
- Touch reagiert nicht (unser Code hat keinen Touch-Handler)

## Bewiesene Fakten

- Board-Definition `boards/T5-ePaper-S3.json` ist korrekt (memory_type: qio_opi)
- PSRAM 8 MB und Flash 16 MB wurden **einmal** erfolgreich gelesen via Serial
- BQ25896 Charger antwortet (Vbat=4.10V wurde einmal gelesen)
- Alte PIO-Monitor-Hintergrundprozesse haben COM5 blockiert — wurden gekillt
- Problem persistiert auch nach kill + erase + clean build + reflash

## Schritt 0: ZUERST RECHERCHIEREN — nicht raten

**Bevor du irgendetwas ausfuehrst, lies und verstehe:**

1. **Git-History dieses Repos:**
   ```bash
   git log --oneline --all
   git diff HEAD~1    # letzte Aenderungen
   git show HEAD      # was wurde zuletzt gemacht
   ```
   Verstehe was funktioniert hat und was geaendert wurde.

2. **LilyGo Referenz-Repo** (das funktionierende Original):
   https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
   - `platformio.ini` — welche Build-Flags, welches Board, welche USB-Konfiguration
   - `examples/factory/main/` — wie initialisieren die Serial? Welche USB-Settings?
   - `boards/T5-ePaper-S3.json` — Board-Definition (bereits lokal in `boards/`)
   - `examples/factory/main/utilities.h` — Pin-Definitionen

3. **ESP32-S3 USB-CDC Dokumentation:**
   - Wie funktioniert HWCDC vs USB-OTG auf dem ESP32-S3?
   - Was bedeutet `ARDUINO_USB_MODE=1` vs `=0`?
   - Was bedeutet `ARDUINO_USB_CDC_ON_BOOT=1`?
   - Braucht der Host DTR assertion damit HWCDC sendet?

4. **Bestehende Projekt-Docs:**
   - `docs/AURA-KRUECKE-MASTER.md` — Hardware-Uebersicht
   - `docs/AURA-KRUECKE-EXECUTOR-RULES.md` — Regeln
   - `include/pins.h` — Pin-Definitionen
   - `boards/T5-ePaper-S3.json` — Board-Config

**Erst wenn du verstanden hast wie das LilyGo-Repo Serial konfiguriert,
darfst du mit Schritt 1 beginnen. Kein Trial-and-Error ohne Verstaendnis.**

---

## Aufgabe

### Schritt 1: Serial-Verbindung debuggen

```bash
# Im PlatformIO-Terminal:

# 1. Pruefen ob alte Prozesse COM5 blockieren
# Windows: tasklist | findstr "pio python monitor"
# Falls ja: taskkill

# 2. Aktuelle Firmware ist ein Ultra-Minimal-Test in src/main.cpp
#    (nur Serial.println tick). Flashen:
pio run -t upload

# 3. Serial Monitor starten (NACH dem Flash, nicht gleichzeitig!)
pio device monitor -b 115200

# 4. RESET-Button auf dem Board druecken
# 5. Erwarteter Output: "tick 1", "tick 2", ...
```

### Schritt 2: Falls kein Output

Moegliche Ursachen in Reihenfolge der Wahrscheinlichkeit:

**A) HWCDC braucht DTR-Signal vom Host:**
ESP32-S3 HWCDC sendet nur wenn der Host DTR assertet.
PIO Monitor macht das normalerweise automatisch.
→ Falls PIO Monitor nichts zeigt: `pio device monitor -b 115200 --dtr 1`

**B) Serial geht auf UART0 statt HWCDC:**
Pruefen ob `-DARDUINO_USB_CDC_ON_BOOT=1` in den effektiven Build-Flags ist:
```bash
pio run -v 2>&1 | findstr "USB_CDC"
```
Muss `-DARDUINO_USB_CDC_ON_BOOT=1` enthalten.

**C) USB-Treiber-Problem (Windows):**
Geraete-Manager → COM5 → Treiber deinstallieren → USB-Kabel ab/an → neu enumerieren lassen.

**D) Falscher COM-Port:**
```bash
pio device list
```
Alle Ports pruefen. Vielleicht ist ein zweiter Port erschienen.

### Schritt 3: Wenn Serial funktioniert

Firmware zurueck auf die Vollversion (GATE 0+1):
Die Datei `src/main.cpp` muss wiederhergestellt werden auf die Version mit:
- I2C-Scanner (ohne BNO085)
- Sensor-Init (BMP581x2, LSM6DSO32, SHT40)
- Boot-Banner seriell
- 1-Hz Sensor-Loop

Die EPD-Integration (FastEPD) kommt DANACH — erst Serial stabil.

```bash
# Vollversion flashen
pio run -t upload

# Monitor
pio device monitor -b 115200

# RESET druecken → erwarteter Output:
# ========================================
#   AURA Kruecke v0.2.0-bringup
#   MINIMAL BOOT TEST (no EPD)
# ========================================
#   Free heap: ~368000 bytes
#   PSRAM size: 8386295 bytes
#   ...
# === I2C Scan ===
#   0x20 PCA9535    [FOUND]  
#   0x44 SHT40      [MISS]   (aura) ← kein Sensorboard
#   ...
```

### Schritt 4: GATE 0+1 Report

Wenn Serial laeuft, Report erstellen:
- Welche I2C-Adressen antworten (ohne Sensorboard: nur Onboard)
- PSRAM/Flash/Heap Werte
- Vbat
→ Damit ist GATE 1 (I2C-Scanner seriell, ohne Sensoren) bestanden.

## Konfiguration

**platformio.ini** (aktuell):
```ini
[env:t5_epaper_s3_pro]
platform = espressif32 @ 6.5.0
board = T5-ePaper-S3
framework = arduino
board_build.partitions = default_16MB.csv
board_build.psram_type = opi
monitor_speed = 115200
upload_speed = 921600
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DAURA_VERSION=\"0.2.0-bringup\"
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    adafruit/Adafruit BMP5xx Library @ ^1.0
    adafruit/Adafruit LSM6DS @ ^4.7
    adafruit/Adafruit SHT4x Library @ ^1.0
    adafruit/Adafruit BusIO @ ^1.16
    lewisxhe/XPowersLib @ ^0.2.3
    bblanchon/ArduinoJson @ ^7.0
    mikalhart/TinyGPSPlus @ ^1.0
    https://github.com/bitbank2/FastEPD.git
```

**Board-Definition** `boards/T5-ePaper-S3.json`: aus LilyGo-Repo, memory_type=qio_opi.
Nicht aendern — damit bootet das Board korrekt.

**Aktuelle main.cpp**: Ultra-Minimal-Test (nur tick-Output). 
Vollversion liegt in git history (commit vor den Debug-Aenderungen).

## Regeln
- Nur lesen ohne Rueckfrage. Ausfuehren/committen nur mit explizitem "ok".
- STOP + Report wenn Serial funktioniert oder wenn alle Optionen A-D durchprobiert sind.
