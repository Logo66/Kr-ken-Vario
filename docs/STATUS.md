# AURA Kruecke — Projekt-Status

Letzte Aktualisierung: 2026-06-07

---

## DONE

- **Toolchain / Flash / HWCDC stabil**
  - `espressif32 @ 6.5.0` (arduino-esp32 v2.0.14)
  - **epdiy 2.0.0 direkt** (FastEPD entfernt — falscher Power-Pfad war Brownout-Root-Cause)
  - Board-Def: `boards/T5-ePaper-S3.json` (memory_type: qio_opi — kritisch!)
  - Upload auf COM5 (VID:303A:1001), Reset via 1200-Baud-Touch (kein phys. Reset-Button)
  - `monitor_rts = 0 / monitor_dtr = 0` in platformio.ini

- **FIRST LIGHT ERREICHT (2026-06-07)**
  - epd_clear() + Boot-Splash (16-grey MODE_GC16) + Cruise-Screen auf Glas
  - Root-Cause Brownout: FastEPD nutzte PCA9535-Power-Pfad (EPDiyV7EinkPower)
    statt native epdiy epd_board_v7 Power-Sequenz → I2C-Konflikt + falscher HV-Pfad
  - Fix: epdiy direkt, Wire.end() vor epd_init(), kein FastEPD

- **I2C-Bus + Onboard-Peripherie OK**
  - Bus: SDA=GPIO39, SCL=GPIO40, 400 kHz
  - Bestaetigte Adressen: 0x20 PCA9535, 0x51 PCF85063, 0x55 BQ27220, 0x5D GT911, 0x6B BQ25896
  - TPS65185 @ 0x68: MISSING im Kalt-Scan (schlaeft bis epdiy ihn weckt — normal)

- **BNO085 komplett entfernt** (gestrichen aus Sensorboard-Design)

- **Boot-Logo Asset integriert**
  - boot_logo.h: 409x500, 16-grey Floyd-Steinberg dither
  - GAMMA_LUT S-Curve fuer E-Paper Kontrast-Anpassung
  - Generator: tools/gen_boot_asset.py (Pillow-only, Windows-tauglich)

- **Vbat = 4.10-4.20 V** — Akku voll, BQ25896 Charger funktioniert

---

## OFFEN — GATE 1: alle Sensoren

Alle 4 Sensor-Boards physisch stecken, dann neuer Scan:

| Adresse | Geraet    | Ziel   |
|---------|-----------|--------|
| 0x44    | SHT40     | [OK]   |
| 0x46    | BMP581#2  | [OK]   |
| 0x47    | BMP581#1  | [OK]   |
| 0x6A    | LSM6DSO32 | [OK]   |

Scan muss **4/4** zeigen. I2C-Hinweis: Wire.begin() nach epdiy crasht (I2C-Driver-Konflikt).

---

## NAECHSTES TICKET

**AURA-KRUECKE-3:** Cruise-Screen 1-Bit S/W + Display-Stabilitaet
- docs/tickets/AURA-KRUECKE-3.md
- Prioritaet: G0 Stabilitaet → G1 Ladder proportional → G2 1-Bit Layout → G3 DU/A2 Refresh

---

## BUG — BUG-KRUECKE-001-pressure-unit: Druck-Faktor-100

**Symptom:** BMP581#2 zeigt P=9.61 hPa statt ~961 hPa.
Vermutung: Adafruit_BMP5xx::pressure liefert bereits hPa, Code teilt nochmal durch 100.
Vor Fix verifizieren. Eigenes Ticket: docs/tickets/BUG-KRUECKE-001-pressure-unit.md

---

## Bekannte Issues / Workarounds

1. Kein physischer Reset-Button → Reset via 1200-Baud-Touch auf COM5
2. Wire.end() vor epd_init() noetig (epdiy installiert eigenen I2C-Driver)
3. Wire.begin() nach epdiy crasht (i2c_driver_install error) — Sensor-Loop braucht Fix
4. Pixel-Clock: epdiy reduziert automatisch auf 10 MHz (Arduino cache line < 64B)
5. Cruise-Screen wird gezeichnet aber bleibt nicht stabil stehen → KRUECKE-3 G0

---

## Erledigte Tickets

- AURA-KRUECKE-1B: Board-Bringup (GATE 0-1 seriell bestanden)
- AURA-KRUECKE-2A: Boot-Logo Asset (G0-G2 bestanden)
- AURA-KRUECKE-2B: Battery-only EPD Test (gescheitert → Root-Cause = FastEPD Power-Pfad)
- AURA-KRUECKE-2C: epdiy-Migration (First Light erreicht!)
