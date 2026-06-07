# AURA-KRUECKE Sensor Board V1 — STATUS

## Phase 0: Forensik + Library-Check

**Status:** COMPLETE  
**Zeit:** ~25 min  
**Datum:** 2026-05-24

### Forensik — Was war beim Vorgänger falsch?

1. **Falscher Chip:** Vorgänger verwendete BMP**585** (LGA-9, 3.25x3.25mm) statt BMP**581** (LGA-10, 2x2mm). Komplett anderes Package und Pinout.
2. **BNO085-Pinout falsch:** Vorgänger-Symbol hatte korrekte Pin-Namen aber FALSCHE Pin-Nummern. Beispiel: SDA war Pin 11 statt korrekt Pin 20. Hätte non-funktionales Board ergeben.
3. **Custom-Lib-Spirale:** Weil KiCad-Default-Libs die Sensor-Symbole nicht hatten, wurden Custom-Libs erstellt — mit falschen Daten.
4. **Kein Datenblatt-Verifikation:** Die Eagle-Referenz-Schematics von Adafruit (BMP5xx, BNO08x) lagen im Repo, wurden aber nicht als Pinout-Quelle genutzt.

### Library-Inventur

| Bauteil | Symbol | Footprint | Quelle |
|---------|--------|-----------|--------|
| BMP581 | `aura_sensors:BMP581` (NEU) | `aura_sensors:BMP581_LGA-10_2x2mm` (NEU) | Pinout verifiziert vs. Adafruit BMP581 Rev B Eagle |
| LSM6DSO32 | `SparkFun-Sensor:LSM6DSOX` (pin-kompatibel) | `Package_LGA:Bosch_LGA-14_3x2.5mm_P0.5mm` (KiCad default) | SparkFun KiCad Libs |
| BNO085 | `aura_sensors:BNO085` (NEU) | `Package_LGA:LGA-28_5.2x3.8mm_P0.5mm` (KiCad default) | Pinout verifiziert vs. Adafruit BNO08x Rev C Eagle |
| SHT40 | `Sensor_Humidity:SHT4x` (KiCad default) | `Sensor_Humidity:Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad` | KiCad 10.0.1 default |
| JST-SH 4-pin | `Connector_Generic:Conn_01x04` (KiCad default) | `Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal` | KiCad 10.0.1 default |
| Pin Header 2x3 | `Connector_Generic:Conn_02x03_Odd_Even` (KiCad default) | `Connector_PinHeader_1.27mm:PinHeader_2x03_P1.27mm_Vertical` | KiCad 10.0.1 default |
| R 0402 | `Device:R` | `Resistor_SMD:R_0402_1005Metric` | KiCad 10.0.1 default |
| C 0402 | `Device:C` | `Capacitor_SMD:C_0402_1005Metric` | KiCad 10.0.1 default |
| C 0805 | `Device:C` | `Capacitor_SMD:C_0805_2012Metric` | KiCad 10.0.1 default |

### Korrektur zum Ticket

- BMP581 ist **LGA-10** (nicht LGA-8). 10 Pads: 8 perimeter + 2 GND. Body bleibt 2x2mm.
- Adafruit BMP581 Board bestätigt 10-pin Layout, identisch zum BMP388-Footprint.
- BNO085 KiCad-Default-Footprint (`LGA-28_5.2x3.8mm_P0.5mm`) hat identische Pad-Nummerierung wie Adafruit-Referenz.

### Archiviert

Vorgänger-Dateien verschoben nach `hardware/sensor_board/archive/_pre_v1_20260524/`

### Bekannte Issues

- Keine

## Phase 1: Schematic

**Status:** AWAITING GATE-HW-1 (Ivo: open in KiCad, run ERC)  
**Zeit:** ~35 min  
**Datum:** 2026-05-24

### Generated Files

| File | Purpose |
|------|---------|
| `project/sensor_board.kicad_pro` | KiCad project (4-layer stackup, ENIG, design rules) |
| `project/sensor_board.kicad_sch` | Schematic (18 components, 201 elements) |
| `project/sym-lib-table` | Symbol library refs (aura_sensors, SparkFun-Sensor) |
| `project/fp-lib-table` | Footprint library refs (aura_sensors, SparkFun-Sensor) |

### Components (18 total)

| Ref | Part | I2C Addr | Notes |
|-----|------|----------|-------|
| J1 | Qwiic JST-SH 4-pin | — | GND/3V3/SDA/SCL |
| U1 | BMP581 | 0x47 | SDO=VDD |
| U2 | BMP581 | 0x46 | SDO=GND |
| U3 | LSM6DSO32 | 0x6A | SDO=GND, CS=VDD |
| U4 | BNO085 | 0x4A | HSA0=GND, I2C mode |
| U5 | SHT40 | 0x44 | — |
| R1,R2 | 4.7k | — | I2C pullups |
| R3 | 10k | — | BNO085 BOOTN pullup |
| R4 | 10k | — | BNO085 RSTN pullup |
| C1-C5 | 100nF 0402 | — | Sensor decoupling |
| C6 | 10uF 0805 | — | Bulk cap |
| C7 | 100nF 0402 | — | BNO085 CAP pin |
| J2 | 2x3 header 1.27mm | — | INT_BNO, INT_LSM, RST_BNO |

### Design Decisions

- BNO085 PS0/PS1 hardwired to GND (I2C mode), no pulldown R needed
- R4 added as RSTN pullup (prevents float when J2 unpopulated)
- C7 added for BNO085 CAP pin (100nF to GND, per datasheet)
- BNO085 secondary I2C (S_SCL/S_SDA) and XIN32 left NC

### Bekannte Issues

- Schematic layout is functional, not polished — rearrange in GUI
- ERC may flag LSM6DSOX hidden GND pin 7 — safe to suppress
