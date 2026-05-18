# AURA-KRUECKE-2 · Vario Core

- **Erstellt:** 2026-05-18
- **Prioritaet:** P0
- **Owner:** Claude (Chef-Architekt)
- **Branch:** `aura-kruecke/2-vario-core`
- **Geschaetzt:** 13–21 h
- **Voraussetzung:** Ticket 1 GATE-2 (Build OK) ✅, PREP-Ticket ✅

---

## Ziel

Kalman-fusionierter Vario-Algorithmus (BMP581 + BNO085), adaptive Modus-Umschaltung,
temperaturkorrigierte Hoehe, Thermik-Erkennung, und BMP581 #2 Roh-Aufzeichnung.
Offline-Simulator fuer Entwicklung ohne Hardware.

**Explizit NICHT in diesem Ticket:**
- ~~Klapper-Detektor / Δp-Threshold / Pre-Warning-Flags~~ → verschoben auf Schirm-Nodes-Familie (S1-S3)
- ~~Klapper-Filter am Gurtzeug~~ → Physik unzureichend, siehe Research-Report Abschnitt 4
- BLE-Integration (Ticket 5)
- Display-Rendering (Ticket 4)
- Audio-Vario (kein Speaker auf T5 Pro)

---

## Akzeptanzkriterien

### Phase A — Typen + Hoehenformel
- [ ] `vario_types.h` definiert `VarioState`, `ThermalInfo`, `EnvironmentData`
- [ ] `altitude.h/.cpp` implementiert barometrische Hoehe MIT Temperaturkorrektur (SHT40)
- [ ] Structs passen direkt zum BLE-Packet-Format (Ticket 5 braucht nur noch packen)

### Phase B — Kalman KF4D (Clean-Room)
- [ ] `kalman_kf4d.h/.cpp` implementiert 4-State Kalman von der Mathematik
- [ ] State: `[h, v, a, a_bias]`
- [ ] Prediction: IMU Linear-Accel Z @ 100 Hz
- [ ] Measurement Update: Baro-Hoehe @ 50 Hz
- [ ] Adaptive Prozess-Varianz: hoeher bei grosser Beschleunigung
- [ ] **Kein Code aus har-in-air kopiert** (GPL-3.0 — Clean-Room, eigene Variablennamen)

### Phase C — Sensor-Reader + Environment
- [ ] `bmp581_reader.h/.cpp`: Beide BMP581 @ 50 Hz Continuous, 32× OSR, IIR OFF
- [ ] `imu_reader.h/.cpp`: BNO085 Linear-Accel + Game-Rotation-Vector @ 100 Hz, LSM6DSO32 @ 416 Hz High-G Watchdog
- [ ] `environment.h/.cpp`: SHT40 @ 1 Hz (Temp, Feuchte, Taupunkt, Wolkenbasis-Schaetzung)

### Phase D — Thermik + Roh-Aufzeichnung
- [ ] `thermal.h/.cpp`: Thermik-Erkennung via 8s Sliding Window + Turn-Rate-Check
- [ ] Modus-Umschaltung: Cruise (default) ↔ Thermal (auto)
- [ ] Trigger Cruise→Thermal: Climb > 0.5 m/s ODER Turn-Rate > 15°/s
- [ ] Trigger Thermal→Cruise: Kein Steigen + kein Kreisen fuer > 10 s
- [ ] `delta_p.h/.cpp`: BMP581 #2 Rohdaten aufzeichnen (Ringbuffer, kein Filter, kein Alarm)
- [ ] Δp-Offset-Tracking via gleitendem Mittel (τ=60s) — nur fuer spaetere Analyse

### Phase E — Integration + Simulator
- [ ] `main.cpp` erweitert: Sensor-Reader → Kalman → Thermik → Serial-Output
- [ ] Serial-Output @ 10 Hz: `vz`, `intVz`, `pressure`, `temp`, `isFlying`, `isThermik`
- [ ] Serial-Output @ 1 Hz: `batteryPct`, `mode`, `uptime`
- [ ] `pio run` kompiliert ohne Warnings (ausser Lib-interne)
- [ ] PlatformIO `[env:native]` fuer Offline-Simulation
- [ ] `simulator/` Verzeichnis mit 6 CSV-Szenarien + Python-Runner
- [ ] Simulator-Metriken erfuellt (siehe VARIO-SIMULATOR-SPEC.md)

### Phase F — Hardware-Validierung (WARTET AUF BOARD, ~KW 23)
- [ ] Flash + Serial Monitor zeigt Kalman-Vario-Werte
- [ ] BMP581-Differenz ±0.5 hPa zwischen beiden Sensoren (Sanity)
- [ ] Board flach: Vario ≈ 0 m/s (±0.05 m/s nach Einschwingen)
- [ ] Board anheben/senken: Vario reagiert < 200 ms
- [ ] 30 Min Stability-Test: kein Drift > 0.1 m/s, keine I2C-Errors

---

## Code-Architektur

```
src/
├── main.cpp                  ← erweitert aus Ticket 1
├── vario/
│   ├── kalman_kf4d.h/.cpp   ← 4-State Kalman (Clean-Room, von Gleichungen)
│   ├── altitude.h/.cpp       ← Barometrische Hoehe mit SHT40-Temp-Korrektur
│   ├── thermal.h/.cpp        ← 8s Window, Modus-Umschaltung, Turn-Rate-Check
│   └── delta_p.h/.cpp        ← BMP581#2 Roh-Aufzeichnung + Offset-Tracking
├── sensors/
│   ├── bmp581_reader.h/.cpp  ← 50 Hz Continuous, 32× OSR, IIR OFF
│   ├── imu_reader.h/.cpp     ← BNO085 100Hz + LSM6DSO32 416Hz High-G
│   └── environment.h/.cpp    ← SHT40: Temp, Feuchte, Taupunkt, Wolkenbasis
├── include/
│   ├── pins.h                ← aus Ticket 1
│   ├── version.h             ← Bump auf 0.2.0-vario
│   └── vario_types.h         ← VarioState, ThermalInfo, EnvironmentData
└── simulator/                ← PlatformIO native + Python
    ├── scenarios/            ← 6 CSV-Dateien
    ├── ground_truth/         ← 6 Ground-Truth CSVs
    ├── generate_scenarios.py ← Deterministisch, fester Seed
    └── sim_runner.py         ← Liest CSV, fuettert KF4D, plottet
```

---

## Kalman KF4D — Mathematische Spezifikation

**Clean-Room Implementierung** — keine Code-Uebernahme aus har-in-air (GPL-3.0).

### Zustandsvektor (4 × 1)
```
x = [h, v, a, a_bias]ᵀ
```
- h: Hoehe (m)
- v: Vertikalgeschwindigkeit (m/s) — DAS ist der Vario-Wert
- a: Vertikalbeschleunigung (m/s²)
- a_bias: Beschleunigungs-Bias (m/s²) — langsam driftend

### State Transition (Prediction, @ IMU-Rate 100 Hz)
```
dt = 0.01 s

F = | 1  dt  dt²/2  0  |
    | 0   1   dt    0  |
    | 0   0    1    0  |
    | 0   0    0    1  |

x_pred = F · x
P_pred = F · P · Fᵀ + Q
```

### Prozessrauschen Q (adaptiv!)
```
Q = diag(q_h, q_v, q_a, q_bias)

Basis-Werte (Cruise):
  q_h = 0.01, q_v = 0.1, q_a = 1.0, q_bias = 0.0001

Adaptiv: wenn |a_measured| > 0.5 m/s²:
  q_a *= (1 + 10 · |a_measured|)
  → schnelle Reaktion bei Thermik-Eintritt / Turbulenz
```

### Messung Barometer (@ 50 Hz)
```
H_baro = [1, 0, 0, 0]
z_baro = altitude_from_pressure(p, T_sht40)
R_baro = (0.013)²  → aus BMP581 Rauschen bei 32× OSR (1.3 cm)

Innovation: y = z_baro - H_baro · x_pred
S = H_baro · P_pred · H_baroᵀ + R_baro
K = P_pred · H_baroᵀ · S⁻¹
x = x_pred + K · y
P = (I - K · H_baro) · P_pred
```

### Messung IMU (@ 100 Hz)
```
H_accel = [0, 0, 1, -1]    ← a_measured = a_state - a_bias
z_accel = BNO085_linear_accel_z  (Schwerkraft schon abgezogen)
R_accel = (0.3)²  → ~0.3 m/s² Piloten-Rauschen + Sensor-Rauschen

Standard Kalman Update mit H_accel
```

---

## Sensor-Konfiguration

| Sensor | OSR | ODR | Modus | IIR | Besonderheit |
|--------|-----|-----|-------|-----|-------------|
| BMP581 #1 | 32× | 50 Hz | Continuous | OFF | Primaerer Druckaufnehmer |
| BMP581 #2 | 32× | 50 Hz | Continuous | OFF | Roh-Aufzeichnung, DC-Offset-Tracking |
| BNO085 | — | 100 Hz | — | — | Linear Accel + Game Rotation Vector |
| LSM6DSO32 | — | 416 Hz | — | — | ±32g, Threshold-Interrupt bei >4g |
| SHT40 | High Prec. | 1 Hz | — | — | Temp fuer Dichtekorrektur |

---

## Hoehenformel mit Temperaturkorrektur

```cpp
// ISA-korrigierte barometrische Hoehe
// T_actual aus SHT40 (NICHT BMP581 — Selbsterwaermung 1-2°C)
float altitudeCorrected(float pressure_pa, float temp_c, float qnh_pa) {
    float T_kelvin = temp_c + 273.15;
    float p_ratio = pressure_pa / qnh_pa;
    return (T_kelvin / 0.0065) * (1.0 - powf(p_ratio, 0.190284));
}

// Ohne Korrektur: Fehler ~13m/1000m pro 10°C ISA-Abweichung
// Bei 3000m + 10°C: ~40m Fehler!
```

---

## Thermik-Erkennung

```
Sliding Window: 8 s (nicht 20s — Recherche-Ergebnis)
avg_climb = mean(vario, last 8s)
turn_rate = abs(BNO085_gyro_z)  (aus Game Rotation Vector oder direkt)

Cruise → Thermal:
  avg_climb > 0.5 m/s  ODER  turn_rate > 15°/s fuer > 3s

Thermal → Cruise:
  avg_climb < 0.3 m/s  UND  turn_rate < 10°/s  fuer > 10s
```

---

## BMP581 #2 Roh-Aufzeichnung (Phase D)

Keine Filterung, kein Alarm, keine Klapper-Logik. Nur:

1. Rohdruck @ 50 Hz in Ringbuffer (letzte 60 s = 3000 Samples)
2. DC-Offset zum BMP581 #1 via gleitendem Mittel (τ=60s)
3. Δp = baro1 - baro2 - dc_offset → Ringbuffer
4. Serial-Ausgabe @ 1 Hz: `dp_rms_2s` (RMS ueber letzte 2s) als Turbulenz-Indikator
5. Daten stehen spaeter fuer Training/Analyse bereit (Schirm-Nodes-Ticket S2)

---

## Approval Gates

```
🛑 GATE-A · Dateistruktur + vario_types.h + altitude.h/.cpp
   Neue Files anlegen, Structs definieren, Hoehenformel implementieren
   Auswirkung: neue Source-Files, kein Build noetig

🛑 GATE-B · KF4D Kalman (Clean-Room)
   kalman_kf4d.h/.cpp — der mathematische Kern
   Auswirkung: neuer Code, kein Build noetig

🛑 GATE-C · Sensor-Reader + Environment + Thermal + DeltaP
   Alle restlichen Module
   Auswirkung: neuer Code, kein Build noetig

🛑 GATE-D · Integration main.cpp + Build
   Alles zusammenstecken, pio run
   Auswirkung: Build, zieht ggf. neue Lib-Versionen

🛑 GATE-E · Simulator aufsetzen
   PlatformIO native env, CSV-Generierung, Python-Runner
   Auswirkung: neue Dateien, Python-Scripts

🛑 GATE-F · Hardware-Validierung (WARTET AUF BOARD ~KW 23)
   Flash + Bench-Tests
   Auswirkung: Firmware aufs Board
```

---

## Definition of Done

- Alle Akzeptanzkriterien Phase A-E abgehakt
- `pio run` kompiliert fuer ESP32-S3 UND `pio run -e native` fuer Simulator
- Simulator laeuft alle 6 Szenarien, Metriken innerhalb Zielwerte
- Code committed auf `aura-kruecke/2-vario-core`
- `docs/STATUS.md` Update
- Phase F wartet auf Hardware
