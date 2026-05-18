# Vario Offline-Simulator Spec

**Zweck:** Kalman-Filter (KF4D) und Thermik-Detektor entwickeln und tunen, OHNE auf Hardware zu warten.
**Laeuft auf:** PC (Python oder direkt als C++ Unit im PlatformIO native-Env)
**Input:** CSV-Streams mit simulierten Sensordaten
**Output:** Vario-Werte, Hoehe, Thermik-Status — vergleichbar mit Ground-Truth

---

## Architektur

```
simulator/
├── scenarios/
│   ├── 01_still.csv          ← Ruhe auf dem Tisch
│   ├── 02_ramp.csv           ← Linearer Aufstieg 0→3 m/s ueber 10s
│   ├── 03_sine.csv           ← Sinusfoermiges Steigen/Sinken (Wellenlift-Modell)
│   ├── 04_thermal.csv        ← Realistischer Thermik-Einstieg + Kurbeln
│   ├── 05_collapse_reject.csv← Turbulenz-Burst, kein echter Klapper (false-positive Test)
│   └── 06_centripetal.csv    ← Kurbeln mit Zentripetal-Beschleunigung (IMU-Stoerung)
├── ground_truth/
│   ├── 01_still_truth.csv
│   ├── ...
│   └── 06_centripetal_truth.csv
├── sim_runner.py             ← Liest CSV, fuettert KF4D, vergleicht mit Ground-Truth
└── README.md
```

---

## CSV-Format (alle Szenarien)

Einheitliches Format, eine Zeile pro Sample bei **100 Hz** (10 ms Intervall):

```csv
t_ms,baro_pa,accel_z_ms2,gyro_z_dps,temp_c,baro2_pa
```

| Feld | Einheit | Beschreibung |
|------|---------|--------------|
| t_ms | ms | Zeitstempel ab Start |
| baro_pa | Pa | BMP581 #1 Druckmessung (mit Rauschen) |
| accel_z_ms2 | m/s² | BNO085 Linear Acceleration Z-Achse (Erd-Frame, ohne Schwerkraft) |
| gyro_z_dps | °/s | Turn-Rate um Hochachse (fuer Thermik-Modus-Erkennung) |
| temp_c | °C | SHT40 Temperatur |
| baro2_pa | Pa | BMP581 #2 Druckmessung (fuer Δp, mit eigenem Offset+Rauschen) |

**Rauschmodell:**
- Baro: Gauss N(0, 0.16 Pa) bei 32× OSR + langsamer Random-Walk (0.01 Pa/s)
- Accel: Gauss N(0, 0.02 m/s²) + Bias-Drift (0.001 m/s²/min)
- Gyro: Gauss N(0, 0.1 °/s)
- Baro2: Wie Baro1, aber +3.2 Pa statischer Offset (Sensor-zu-Sensor)

**Ground-Truth CSV:**
```csv
t_ms,alt_m,vario_ms,is_thermal
```

---

## Szenario-Definitionen

### 01 · Ruhe (Still)
- **Dauer:** 60 s
- **Physik:** Sensor liegt auf dem Tisch, keine Bewegung
- **Baro:** Konstant 101325 Pa ± Rauschen
- **Accel:** 0 m/s² ± Rauschen (Schwerkraft schon abgezogen durch BNO085)
- **Gyro:** 0 °/s ± Rauschen
- **Ground-Truth:** alt = 0 m, vario = 0 m/s, is_thermal = false
- **Prueft:** Filter-Konvergenz, Rausch-Unterdrueckung, kein falscher Vario-Ausschlag
- **Akzeptanz:** Vario-RMS < 0.05 m/s nach 5s Einschwingen

### 02 · Rampe (Linear Climb)
- **Dauer:** 30 s
- **Physik:** Linearer Aufstieg von 0 auf 3 m/s ueber 10 s, dann 10 s konstant 3 m/s, dann 10 s Abbremsen auf 0
- **Baro:** Fallender Druck gemaess barometrischer Hoehenformel
- **Accel:** Beschleunigungspuls waehrend Rampe-auf und Rampe-ab (±0.3 m/s²)
- **Ground-Truth:** Trapezfoermiges Vario-Profil
- **Prueft:** Latenz bei Step-Response, Ueberschwingen
- **Akzeptanz:** Latenz < 200 ms (Vario erreicht 50% des Zielwerts), Ueberschwingen < 10%

### 03 · Sinus (Wellenlift)
- **Dauer:** 120 s
- **Physik:** Sinusfoermiges Steigen/Sinken, Amplitude 2 m/s, Periode 20 s
- **Baro:** Sinusfoermiger Druckverlauf
- **Accel:** Ableitung des Sinus (Kosinus-Phase)
- **Ground-Truth:** Sinus-Vario
- **Prueft:** Phasentreue, Amplitudengenauigkeit
- **Akzeptanz:** Phase-Lag < 100 ms, Amplitude ±5%

### 04 · Thermik (Realistic Thermal Entry + Coring)
- **Dauer:** 180 s
- **Phasen:**
  1. 0-30 s: Geradeausflug, Sinken -1.2 m/s (Polare bei Trimmspeed)
  2. 30-35 s: Thermik-Eintritt, Steigen rampt auf +2.5 m/s
  3. 35-150 s: Kurbeln bei ~20°/s Turn-Rate, Steigen variiert sinusfoermig 1.5-3.5 m/s (Kern nicht zentriert → asymmetrisches Profil)
  4. 150-160 s: Thermik verlassen, Steigen faellt auf 0
  5. 160-180 s: Geradeausflug, Sinken -1.2 m/s
- **Accel:** Zentripetal-Komponente beim Kurbeln (~0.5 m/s² lateral), Burst bei Thermik-Eintritt
- **Gyro:** 0 im Geradeausflug, ~20°/s beim Kurbeln
- **Ground-Truth:** Vario + is_thermal Flag (true ab 35 s bis 155 s)
- **Prueft:** Thermik-Erkennung (Latenz, false-positive im Sinken), Modus-Umschaltung, Vario-Genauigkeit im Kurbeln
- **Akzeptanz:** Thermik erkannt innerhalb 5 s nach Eintritt, kein False-Positive im Sinken

### 05 · Turbulenz-Burst (Collapse Reject)
- **Dauer:** 60 s
- **Physik:** Normaler Gleitflug mit zwei kurzen Turbulenz-Bursts (t=20s und t=40s)
- **Baro:** Leichtes Sinken -1.0 m/s, kurze ±3 Pa Spikes (200 ms Dauer) bei Turbulenz
- **Accel:** Kurze ±2 m/s² Spikes (100 ms Dauer), aber KEIN nachhaltiges Steigen
- **Gyro:** Kurze ±5°/s Bursts
- **Ground-Truth:** Vario bleibt bei ~-1.0 m/s, is_thermal = false durchgehend
- **Prueft:** Robustheit gegen Turbulenz-Spikes, kein falscher Thermik-Alarm
- **Akzeptanz:** Vario-Spike durch Turbulenz < 0.5 m/s Amplitude, Thermik bleibt false

### 06 · Zentripetal (Coring Centripetal Rejection)
- **Dauer:** 120 s
- **Physik:** Kurbeln in 2.0 m/s Steigen bei verschiedenen Turn-Rates (10, 20, 30 °/s)
- **Accel:** Vertikale Komponente (Steigen) + Zentripetal-Stoerung die in die Vertikale koppelt wenn Pilot in Schieflage ist (~0.3-0.8 m/s² je nach Bank-Winkel)
- **Gyro:** Konstante Turn-Rate pro Segment
- **Ground-Truth:** Vario = 2.0 m/s konstant (Zentripetal darf NICHT in den Vario-Wert einfliessen)
- **Prueft:** Korrekte Gravitations-Subtraktion durch BNO085, Zentripetal-Rejection
- **Akzeptanz:** Vario-Fehler durch Zentripetal < 0.2 m/s bei allen Turn-Rates

---

## Ground-Truth Generierung

Die Ground-Truth wird analytisch berechnet (keine Simulation noetig):
1. Vario-Profil definieren (stueckweise Funktionen)
2. Hoehe = Integral(vario, dt)
3. Druck = inverse barometrische Formel mit Temp-Korrektur
4. Accel = Ableitung(vario) + Zentripetal + Rauschen
5. Baro = Druck + Gauss-Rauschen + Random-Walk

Python-Script `generate_scenarios.py` erzeugt alle CSVs deterministisch (fester Random-Seed fuer Reproduzierbarkeit).

---

## Simulator-Runner

**Option A (empfohlen): Python**
- Liest CSV zeilenweise
- Fuettert C-exportierten KF4D (via ctypes oder pybind11) ODER Python-Reimplementierung
- Vergleicht Output mit Ground-Truth
- Plottet Ergebnis (matplotlib)
- Gibt Metriken aus: Latenz, RMS-Fehler, Max-Fehler, Thermik-Detection-Delay

**Option B: PlatformIO native**
- `[env:native]` in platformio.ini
- Kompiliert den C++ Vario-Code fuer x86
- Liest CSV als stdin, gibt Ergebnisse auf stdout
- Vorteil: identischer Code wie auf dem ESP32, kein Port noetig
- Nachteil: kein Plotting, braucht separates Analyse-Script

**Empfehlung:** Option B (PlatformIO native) fuer den Algorithmus-Code + Python-Wrapper fuer Analyse und Plots. So ist der C++ Code 1:1 identisch mit dem ESP32-Code.

---

## Metriken pro Szenario

| Metrik | Definition | Ziel |
|--------|-----------|------|
| Latenz (ms) | Zeit bis Vario 50% des Step-Response erreicht | < 200 ms |
| RMS-Fehler (m/s) | RMS(vario_output - ground_truth) nach Einschwingen | < 0.1 m/s |
| Max-Fehler (m/s) | Max absolute Abweichung | < 0.5 m/s |
| Ueberschwingen (%) | (Max-Vario - Target-Vario) / Target-Vario × 100 | < 10% |
| Thermik-Delay (s) | Zeit von Ground-Truth is_thermal=true bis Detektor-Output=true | < 5 s |
| False-Positive-Rate | Anteil der Zeit wo Detektor=true aber Ground-Truth=false | 0% |
| Zentripetal-Rejection (m/s) | Vario-Fehler der durch Kurbeln entsteht | < 0.2 m/s |
