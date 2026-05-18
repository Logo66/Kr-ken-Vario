# AURA-KRUECKE-2 · Vario Core (Kalman-Fusion)

## Status
- **Erstellt:** 2026-05-18
- **Priorität:** P0 (Aura-USP #1 Validierungs-Vorbereitung)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI (PowerShell) — **gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`**
- **Branch:** `aura-kruecke/2-vario-core`
- **Geschätzt:** 13–21 h (4 Phasen + adaptive Kalman-Tuning, Phase D abgespeckt nach Klapper-Streichung)
- **Voraussetzung:** AURA-KRUECKE-1 abgeschlossen, alle 5 Sensoren liefern plausible Daten

> ⚠️ **Workflow:** wie Ticket 1. Jede Phase endet mit einem **🛑 GATE-N** plus Ist-Demo. Wir können nach jeder Phase pausieren und entscheiden ob die nächste Phase wirklich nötig ist, oder ob das aktuelle Niveau reicht.

---

## Ziel

Das **beste mögliche Vario-Signal** aus der vorhandenen Sensor-Suite produzieren. Konkret heisst das:

1. **Latenz < 100 ms** vom physischen Höhenwechsel bis zum Vario-Output (handelsübliche BMP-Varios: 300–800 ms)
2. **Rausch-Robust** auch bei Vibration durch Klapper, Kurbel-Lage-Änderungen
3. **Lage-kompensiert** — keine Phantom-Steigwerte wenn der Pilot pitcht/rollt
4. **Dichte-korrigiert** — korrekte absolute Höhe auch bei extremen Temperatur/Feuchte-Bedingungen
5. **Δp-Stream verfügbar** für AURA-KRUECKE-3 (Klapper-Vorwarnung)
6. **Thermik-Auto-Detection** für Mode-Switching auf den Display-Screens

Alles als 10-Hz-Stream auf Serial output, später (Ticket 5) auch über BLE.

---

## Architektur-Entscheidungen

### FreeRTOS-Tasking

| Task | Core | Rate | Priorität | Job |
|---|---|---|---|---|
| `i2c_sampler` | 0 | 100 Hz | 6 | Sequentielles Lesen aller I2C-Sensoren (BMP×2, IMU×2 via FIFO-Bursts) |
| `sht40_battery` | 0 | 1 Hz | 2 | Langsame Sensoren (SHT40, BQ27220) |
| `kalman_filter` | 1 | 100 Hz | 5 | Sensor-Fusion + State-Update |
| `vario_aggregator` | 1 | 10 Hz | 4 | Climb-Rate-Averages, Thermik-Detection, Δp-Filter |
| `serial_output` | 1 | 10 Hz | 2 | Telemetrie-Stream raus |

I2C läuft sequentiell auf einem Bus — kein paralleler Sensor-Read möglich. Bei 400 kHz I2C-Speed: BMP581 ≈ 2 ms, LSM6DSO32 6-Byte ≈ 1 ms, BNO085 SHTP ≈ 3 ms, total ≈ 8–10 ms pro Sample-Runde → **100 Hz Combined-Sampling realistisch**. LSM6DSO32 läuft intern bei 208 Hz, FIFO gibt uns 2 Samples pro Read.

Datenfluss: `i2c_sampler` schreibt in `g_raw` (mutex-protected struct) → `kalman_filter` liest `g_raw`, schreibt in `g_vario` → `vario_aggregator` liest `g_vario`, schreibt in `g_output` → `serial_output` printet `g_output`.

### Sample-Rates

| Sensor | Native | Read-Rate | Zweck |
|---|---|---|---|
| BMP581 #1 | bis 622 Hz | 50 Hz (FIFO) | Druck → Höhe (Kalman-Measurement #1) |
| BMP581 #2 | bis 622 Hz | 50 Hz (FIFO) | Δp gegen #1 (Ticket 3 + Aggregator) |
| LSM6DSO32 | bis 6.7 kHz | 208 Hz (FIFO Burst) | Schnelle Linear-Acceleration |
| BNO085 | bis 400 Hz | 100 Hz | Quaternion (Welt-Frame-Rotation) |
| SHT40 | bis 1 Hz | 1 Hz | T, RH für Luftdichte |

BMP581-Konfig: Continuous Mode, Oversampling 8× Pressure / 1× Temperature, IIR off (Filterung machen wir selber), FIFO-Watermark 4 Samples.

LSM6DSO32-Konfig: 208 Hz Output-Data-Rate, ±8g Range (Paragliding-Klapper bleiben unter 8g), Anti-Aliasing-Filter ODR/4, FIFO Continuous-Mode.

BNO085-Konfig: Report `SH2_GAME_ROTATION_VECTOR` @ 100 Hz, `SH2_LINEAR_ACCELERATION` @ 100 Hz (für Sanity-Crosscheck mit LSM6DSO32).

**Wichtig:** wir nutzen `SH2_GAME_ROTATION_VECTOR`, nicht `SH2_ROTATION_VECTOR`. Game-Modus nutzt nur Gyro + Accel, kein Magnetometer. Begründung: Karbid-Platten, Funkgerät und Handy im Cockpit verseuchen das Magfeld zuverlässig. Heading-Drift im Game-Mode ist akzeptabel weil wir für die Vario-Welt-Frame-Rotation nur Pitch/Roll brauchen, kein absolutes Heading. Heading kommt später aus GPS-Track wenn nötig.

### Vertikale Beschleunigung im Welt-Frame

Kern-Berechnung pro IMU-Sample:

```cpp
// Eingangsdaten
const float ax = lsm.accel_x;  // m/s² body frame
const float ay = lsm.accel_y;
const float az = lsm.accel_z;
const Quaternion q = bno.rotation;  // {w, x, y, z}

// Rotation Body → World, nur Z-Komponente
const float az_world =
    ax * 2.0f * (q.x*q.z + q.w*q.y) +
    ay * 2.0f * (q.y*q.z - q.w*q.x) +
    az * (q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z);

// Gravity entfernen — wenn BNO085 Z-up Convention liefert
const float a_vertical = az_world - 9.81f;
```

**Verifikation per Bench-Test** (im Test-Plan): Board flach hinlegen, ruhig → `a_vertical` ≈ 0 (±0.05 m/s²). Board um Y-Achse kippen 45° → `a_vertical` ≈ 0 (Lage-Kompensation funktioniert). Board flach anheben 1g push → `a_vertical` ≈ +9.81 kurzzeitig.

Falls BNO085-Quaternion eine andere Convention nutzt (Z-down oder ENU statt NED), Vorzeichen am Ende anpassen — das ist genau dafür da, dass wir empirisch verifizieren.

### Kalman-Filter Design

**3-State Linear Kalman:** Zustand `x = [h, v, a]ᵀ` (Höhe, Vertikalgeschwindigkeit, Vertikalbeschleunigung).

**Process Model** (`dt = 10 ms` bei 100 Hz):
```
F = | 1   dt   0.5·dt² |
    | 0    1     dt    |
    | 0    0      1    |
```

**Measurement Models:**
- Barometer: `z_baro = h`, also `H_baro = [1, 0, 0]`
- IMU vertical: `z_accel = a`, also `H_accel = [0, 0, 1]`

Beide Sensoren gehen als separate Updates rein, jeweils wenn frisches Sample verfügbar.

**Tuning-Startwerte** (in NVS gespeichert, später anpassbar):
```cpp
// Process noise — vertraut dem Bewegungsmodell stark
Q = diag([0.001, 0.01, 0.5]);   // h: 1mm, v: 10cm/s, a: 0.5 m/s² Drift pro Tick

// Measurement noise
R_baro  = 0.25f;                 // (0.5 m)² — BMP581-Rauschen auf Höhe gemappt
R_accel = 0.0025f;               // (0.05 m/s²)² — LSM6DSO32 + BNO085-Drift
```

**Initialisierung:** Beim Boot 100 BMP-Samples mitteln → `x_init = [h_avg, 0, 0]`, `P_init = diag([1, 1, 1])`. Erste 5 Sekunden im Loop = "Warm-up", Output-Flag `vario_valid=false`.

**Implementierung:** Hand-gerollter 3×3 Matrix-Code, keine externe Lib. Mathematisch trivial bei diesem State-Space, ~100 Zeilen C++. Vorbild: OpenVario's vario filter, BlueFly TBD-100.

### Adaptive Kalman-Tuning beim Kurbeln

**Problem:** beim Kurbeln in einer 30°-Schräglage-Kurve wirkt ~1.15 × g effektiv "nach unten" relativ zum Cockpit (Vektor-Addition Gravitation + Zentripetalkraft). Wenn die Orientierungs-Schätzung des BNO085 auch nur 2–3° daneben liegt, leakt ein Teil dieser Zentripetalbeschleunigung in die berechnete Welt-Vertikal-Achse → **Phantom-Steigwerte beim Kurven-Eintritt**, falsche Sinkwerte beim Kurven-Verlassen. Klassisches Problem aller IMU-Varios.

**Lösung:** R_accel wird Yaw-Rate-abhängig dynamisch verändert. Wenn das Gerät stark dreht → Kalman traut dem IMU-Kanal weniger und fällt auf reines Barometer zurück (mit Lag, aber zentripetal-immun).

```cpp
// Im 100-Hz-Kalman-Tick, vor der Accel-Measurement-Update-Stufe:
float yaw_rate = fabsf(bno.gyro_z);   // rad/s

if (yaw_rate > 0.30f) {          // > ~17°/s = enge Thermik-Spirale
    R_accel_runtime = 0.50f;     // Faktor 200 weniger Trust als Cruise
    state.aggressive_turn = true;
} else if (yaw_rate > 0.10f) {   // ~6°/s = sanfte Kurve
    R_accel_runtime = 0.05f;     // Faktor 20 weniger Trust
    state.aggressive_turn = false;
} else {                          // Cruise / Gleitflug
    R_accel_runtime = 0.0025f;   // voller IMU-Trust, niedrigste Latenz
    state.aggressive_turn = false;
}
```

**Trade-off:** während des Kurbelns verlierst du die "spürbar schneller als Skytraxx"-Performance — das Vario läuft wie ein BMP-Only-Vario. Aber: die Vario-Werte sind **wahr** statt fake. Beim Cruise und Thermik-Anflug — die zwei Momente wo Vario-Geschwindigkeit am wichtigsten ist — hast du volle Kalman-Performance.

### In-Flight Bias-Refresh

Sensor-Bias driftet mit Temperatur und Alterung. Statt nur Boot-Kalibrierung machen wir periodisch (alle 10 Min) einen Stille-Detektor:

```cpp
// Bedingung für stabilen Gleitflug:
if (fabsf(yaw_rate) < 0.05f
    && fabsf(state.vario_kalman - state.vario_kalman_prev) < 0.1f
    && state.imu_healthy
    && state.altitude_kalman > GND_REF + 100.0f) {
    // 60 Sekunden lang Bias akkumulieren, dann sanftes Update:
    new_bias = 0.95f * old_bias + 0.05f * measured_bias_during_60s_window;
    nvs_save(new_bias);
}
```

Effekt: Temperatur-Drift der LSM6DSO32 wird im Flug auskompensiert, ohne dass der Pilot was tut. Keine "explizite Kalibrierung in der Luft" — passiert automatisch wenn die Bedingungen passen.

### Datenstruktur

```cpp
struct VarioState {
  // === Raw Sensors ===
  float pressure_pa[2];        // BMP581 #1, #2
  float temperature_c;
  float humidity_rh;
  float air_density_kg_m3;
  Quaternion orientation;      // BNO085
  float accel_body[3];         // LSM6DSO32 body frame
  float accel_vertical_world;  // gravity-removed

  // === Altitude Streams ===
  float altitude_baro_raw;      // ISA standard
  float altitude_baro_density;  // density-corrected
  float altitude_kalman;        // ← primary output

  // === Vario Streams ===
  float vario_baro_iir;         // Phase A baseline (IIR)
  float vario_kalman;           // ← primary output (Phase C)
  float vario_avg_1s;
  float vario_avg_20s;          // ← thermal mode reference

  // === Differential ===
  float dp_raw_pa;              // BMP1.p - BMP2.p
  float dp_filtered_pa;         // high-passed
  bool pressure_transient;

  // === Flags ===
  bool vario_valid;             // false during 5s warmup
  bool in_thermal;
  bool imu_healthy;
  bool baro_healthy;

  // === Config / Meta ===
  float qnh_hpa;                // user-settable, default 1013.25
  uint32_t sample_count;
  uint32_t kalman_updates;
};

extern VarioState g_vario;
extern SemaphoreHandle_t g_vario_mutex;
```

### Sensor-Bias-Kalibrierung beim Boot

Ali-Sensoren haben oft Werks-Bias. Beim ersten Boot (oder via CLI-Command):

- **LSM6DSO32:** Board 5 Sekunden flach + still halten → Bias = average der 1000 Samples. Speichern in NVS als `lsm_bias_xyz`. Bei jedem Boot abziehen.
- **BMP581 #1 vs #2:** mittlerer Druck-Offset über 1000 Samples → in NVS als `bmp_offset_pa`. Falls > 50 Pa → Warnung (möglicher defekter Sensor).
- **BNO085:** hat eingebaute Auto-Kalibrierung (3-stufig). Beim ersten Boot fragen wir den Calibration-Status ab und loggen.

---

## Akzeptanzkriterien (Gesamt-Ticket)

- [ ] Phase A — BMP-Only Baseline funktioniert (vergleichbar mit Skytraxx Lite)
- [ ] Phase B — Dichte-Korrektur aktiv, Δaltitude zum unkorrigierten messbar > 5 m bei warmen Bedingungen
- [ ] Phase C — Kalman-Vario reagiert nachweisbar schneller als IIR-Baseline (siehe Test-Plan)
- [ ] Phase D — Δp-Stream verfügbar, Thermik-Flag schaltet korrekt
- [ ] Alle Phasen mit individuellem **🛑 GATE** abgeschlossen, jede für sich auf `main` (oder Feature-Branch) merge-bar
- [ ] `docs/STATUS.md` enthält Phase-für-Phase-Notizen
- [ ] Eine **Vergleichs-Plot-CSV** wird mitgeliefert: Spalten `time, altitude_baro, altitude_kalman, vario_iir, vario_kalman` — daraus kann ich offline plotten und sehen wie viel Kalman bringt

---

## Phasen-Plan mit Approval-Gates

### Phase A · BMP-Only Baseline · 3–5 h

```
🛑 GATE-A1 · Architektur-Skelett
  Aktionen:
    - Branch aura-kruecke/2-vario-core
    - include/vario_state.h (Datenstruktur)
    - src/sensors/bmp581_driver.cpp (50 Hz Continuous-Mode mit FIFO)
    - src/sensors/i2c_sampler_task.cpp (FreeRTOS-Task auf Core 0)
    - src/vario/altitude.cpp (ISA-Formel)
    - src/vario/iir_filter.cpp (alpha=0.1)
    - src/output/serial_telemetry.cpp (10 Hz)
  Auswirkung: kompiliert, geht aufs Board, Output zeigt baro-only Vario
  OK?

🛑 GATE-A2 · Bench-Validierung
  Aktion: 5-Minuten-Hand-Test, CSV-Log
  Erwartung:
    - Ruhelage: vario stabil bei 0±0.05 m/s
    - Schnelles Heben um 30cm in 0.5s: Peak ~0.5-0.6 m/s, Lag ~400-600ms
    - Stair-Walk: korrekte Höhendifferenz nach 30s
  OK für Phase B?
```

### Phase B · Dichte-Korrektur · 1–2 h

```
🛑 GATE-B1 · SHT40 + Dichte-Formel
  Aktionen:
    - src/sensors/sht40_driver.cpp
    - src/vario/air_density.cpp (CIPM-2007 oder vereinfachte Formel)
    - altitude_baro_density als zweiter Stream parallel zu altitude_baro_raw
  Auswirkung: dritte Altitude-Spalte im Telemetrie-Output
  OK?

🛑 GATE-B2 · Validierung
  Aktion: 10min Vergleich bei Zimmertemperatur, bei 30°C (Heizung) und falls möglich kalt (Garten)
  Erwartung: Δaltitude zwischen raw und density-corrected sichtbar > 2 m bei Temp-Extremen
  OK für Phase C?
```

### Phase C · Kalman-Fusion mit IMU + adaptive Tuning · 8–13 h · **Kern-Innovation**

```
🛑 GATE-C1 · IMU-Pipeline
  Aktionen:
    - src/sensors/lsm6dso32_driver.cpp (208 Hz, FIFO)
    - src/sensors/bno085_driver.cpp (Rotation + Linear Accel)
    - src/vario/world_frame_accel.cpp (Quaternion-Rotation)
    - Bias-Kalibrierung beim Boot (LSM6DSO32, 5s ruhig)
    - Telemetrie-Output erweitert um accel_vertical_world
  Auswirkung: zwei IMUs aktiv, vertical accel im Stream
  OK?

🛑 GATE-C2 · Bench-Validierung der Lage-Kompensation
  Aktion: Board ruhig, dann pitchen/rollen ohne zu heben
  Erwartung: accel_vertical_world bleibt nahe 0 (±0.1 m/s²) trotz Lage-Änderung
  Falls Vorzeichen verkehrt oder Z-Komponente nicht stimmt: anpassen + erneut testen
  OK für C3?

🛑 GATE-C3 · Kalman-Implementation
  Aktionen:
    - src/vario/kalman_3state.cpp (hand-rolled, 3×3 Matrizen)
    - Q/R aus NVS lesen, defaults wie oben
    - Initialisierung mit 100-Sample-Average
    - 5s Warmup-Phase (vario_valid=false)
  Auswirkung: vierte Altitude-Spalte + zweite Vario-Spalte (kalman)
  OK?

🛑 GATE-C4 · Latenz-Vergleichs-Test
  Aktion: 10-Min-CSV mit aggressivem Hand-Pumping (1 Hz Up/Down ±30cm)
  Erwartung:
    - vario_iir Peak verzögert um 300-500ms gegenüber Bewegung
    - vario_kalman Peak verzögert um < 100ms
    - Beide Peak-Amplituden ähnlich (~0.5-0.8 m/s)
    - Im Ruhe-Intervall: beide ≈ 0, Kalman mit weniger Rauschen
  CSV liefern, ich mache Vergleichs-Plot
  OK für C5?

🛑 GATE-C5 · Adaptive Tuning + Bias-Refresh
  Aktionen:
    - R_accel runtime-switching via yaw_rate-Detection
    - aggressive_turn Flag in VarioState
    - In-Flight-Bias-Refresh Task (60s-Stille-Detektor, NVS-Update mit 5% blending)
    - Telemetrie um R_accel_current + aggressive_turn erweitern
  Auswirkung: Kalman lernt zwischen Cruise- und Kurbel-Modus zu unterscheiden
  OK?

🛑 GATE-C6 · Zentripetal-Test (kritisch)
  Aktion: Bench-Test mit Drehstuhl oder Lazy-Susan-Platte:
    - Board auf rotierende Fläche, langsam drehen (0.1 rad/s) → vario_kalman bleibt ≈ 0
    - Schneller drehen (0.4 rad/s) → R_accel-Switch im Log sichtbar, aggressive_turn=true, vario_kalman bleibt ≈ 0
    - Ohne adaptive Tuning würde Phantom-Vario von 0.3-0.8 m/s entstehen
    - Im Flug-Validation später nochmal (Ticket-übergreifend)
  OK für Phase D?
```

### Phase D · Δp-Logging + Thermal Mode · 2–3 h

> ⚠️ **Scope-Änderung 2026-05-18:** ursprünglich enthielt Phase D Δp-Filterung und Threshold-Detektor für Klapper-Vorwarnung. **Das wurde gestrichen.** Klapper-Erkennung ist physikalisch nur sinnvoll mit Sensoren *im Flügel* (siehe Schirm-Nodes-Konzept), nicht am Gurtzeug. Phase D liefert jetzt nur noch **Δp-Roh-Daten zum späteren Offline-Analysieren**, plus Thermik-Auto-Detection.

```
🛑 GATE-D1 · Δp-Roh-Stream
  Aktionen:
    - BMP581 #2 Driver instanzieren (zweite Adresse, gleiche Task-Logik)
    - Offset-Kalibrierung beim Boot (1000 Samples Mittelung, NVS-Speicherung)
    - Δp = BMP1.p - BMP2.p, roh ohne Filter ins Telemetrie-Log
    - 50 Hz Aufzeichnung in IGC-Plus-Stream (für späteren Daten-Pool)
  Auswirkung: dp_raw_pa im Stream, Δp-Daten werden gesammelt
  KEIN Threshold-Detektor, KEIN pressure_transient Flag, KEINE Klapper-Warnung
  OK?

🛑 GATE-D2 · Thermal-Auto-Detection
  Aktionen:
    - src/vario/thermal_detector.cpp
    - State Machine: idle → climbing (avg_short > 0.5 m/s for 5s, Turn-Rate > 12°/s) → idle (avg_short < 0.2 m/s for 10s)
    - Verwendung des kürzeren 8-10s Vario-Averages (siehe Research-Report Sektion 5)
  Auswirkung: in_thermal flag im Stream
  OK?

🛑 GATE-D3 · Phase D Validierung
  Aktion: 5min Test
    - BMP1 + BMP2 nebeneinander: dp_raw stabil um ~0 ±2 Pa
    - Sanity: Sensor-Offset-Drift über 5min < 5 Pa (sonst Hardware-Problem)
    - Stair-Walk hoch: nach 5s mit > 0.5 m/s avg → in_thermal=true
  OK für Merge?
```

**Was nicht in Phase D ist und wann es kommt:**
- Δp-Klapper-Algorithmus → **Schirm-Nodes-Familie** (zukünftig, nach Aura-PCB-Entscheid)
- pressure_transient Flag → ebenfalls Schirm-Nodes
- Klapper-Vorwarnung im UI → erst wenn echte Schirm-Sensor-Daten verfügbar sind

### Final Gate · Merge + Push

```
🛑 GATE-FINAL · Cleanup + Push
  Aktionen:
    - docs/STATUS.md updaten
    - CSV-Vergleichslogs in docs/data/phase-c-comparison.csv
    - Squash-Commit oder logische Commit-History (entscheiden wir gemeinsam)
    - git push origin aura-kruecke/2-vario-core
    - PR-Beschreibung mit kurzem "what changed" auf GitHub
  OK?
```

---

## Sacred / Heilig-Liste

Wie Master-Doc. Aura-Krücke-Repo ist isoliert. Keine Schreib-Operationen ausserhalb `C:\Users\Ivo\aura_kruecke`.

---

## Aktive Files (zu erstellen)

```
aura_kruecke\
├── include\
│   ├── vario_state.h          ← VarioState struct, globale Symbole
│   └── config.h               ← Q/R defaults, sample rates, thresholds
├── src\
│   ├── main.cpp               ← FreeRTOS task creation, glue
│   ├── sensors\
│   │   ├── bmp581_driver.cpp/.h   ← beide Instanzen
│   │   ├── lsm6dso32_driver.cpp/.h
│   │   ├── bno085_driver.cpp/.h
│   │   ├── sht40_driver.cpp/.h
│   │   └── i2c_sampler_task.cpp/.h
│   ├── vario\
│   │   ├── altitude.cpp/.h        ← ISA + density
│   │   ├── air_density.cpp/.h
│   │   ├── iir_filter.cpp/.h
│   │   ├── kalman_3state.cpp/.h
│   │   ├── world_frame_accel.cpp/.h
│   │   ├── dp_filter.cpp/.h
│   │   ├── thermal_detector.cpp/.h
│   │   └── aggregator_task.cpp/.h
│   ├── output\
│   │   └── serial_telemetry.cpp/.h
│   └── calibration\
│       ├── nvs_storage.cpp/.h     ← Bias, Offsets, Q/R
│       └── boot_calibration.cpp/.h
├── docs\
│   ├── STATUS.md (update)
│   ├── data\
│   │   ├── phase-a-hand-test.csv
│   │   ├── phase-c-comparison.csv
│   │   └── phase-d-stair-walk.csv
│   └── tickets\
│       ├── done\AURA-KRUECKE-1.md  (verschieben)
│       └── AURA-KRUECKE-2.md      ← diese Datei
```

---

## Test-Plan

### Phase A
- 5-Minuten-Bench: Ruhe → Mittelwert±Std → erwartet ≤ 0.05 m/s Std
- Hand-Pump-Test: gleichmässig ±30cm @ 0.5 Hz → Peak und Lag dokumentieren
- Stair-Walk: 1 Stockwerk hoch (≈ 3 m) → Endhöhe sollte ±0.5 m stimmen

### Phase B
- Heizung-Test: Board nahe Heizkörper → density-corrected sollte stabiler bleiben
- Sanity: bei Standard-Conditions (~20 °C, 50 % RH) ≤ 1 m Differenz

### Phase C ← *kritischer Test, hier entscheidet sich der Erfolg*
- **Latenz-Test:** Hand-Pump @ 1 Hz, 5 Min Aufzeichnung, beide Vario-Streams loggen
- **Lage-Test:** Board pitchen ±30° ohne zu heben → vario_kalman sollte ≈ 0 bleiben, vario_iir würde fälschlich ausschlagen wenn er Beschleunigung sehen würde (tut er nicht, aber zum Vergleich)
- **Klapper-Sim:** Board kurz schütteln → Kalman sollte das als Rauschen abfangen, nicht als Vario interpretieren

### Phase D
- Druckluft-Test: einen BMP kurz anpusten → Δp-Spike, transient-Flag
- Treppe-mehrere-Stockwerke: in_thermal sollte aktivieren bei kontinuierlichem Steigen

---

## Bekannte Stolpersteine

- **Zentripetalbeschleunigung beim Kurbeln:** der gefährlichste IMU-Vario-Fehler. Falls Phantom-Vario beim Kurven-Eintritt auftritt obwohl adaptive Tuning aktiv: yaw_rate-Threshold senken (z. B. von 0.10 auf 0.05) oder R_accel im aggressive_turn-Mode noch grösser (1.0 statt 0.5)
- **BNO085 Magnetometer-Interferenz:** Karbid, Funkgerät, Handy verseuchen das Magfeld. Deshalb `SH2_GAME_ROTATION_VECTOR` ohne Magfeld. Falls jemand später Heading braucht: aus GPS-Track ableiten, nicht aus BNO085-Mag
- **BNO085 Quaternion-Convention:** Verschiedene Sensor-Fusion-Algorithmen liefern unterschiedliche Frames (NED, ENU, body Z-up vs Z-down). Empirisch verifizieren in GATE-C2.
- **LSM6DSO32 FIFO-Burst:** beim 100-Hz-Polling kommen 2 Samples gleichzeitig — beide müssen mit individuellem Timestamp ins Kalman, nicht gemittelt
- **BMP581 NVM-Init:** kann beim Cold-Boot 30 ms brauchen. Falls Sensor erst nach Warmup antwortet, in Driver-init einen Retry-Loop einbauen
- **Kalman-Tuning ist iterativ:** Erste Q/R-Werte sind Startpunkt, nicht Endpunkt. Phase C kann mehrere Tuning-Runden brauchen. Falls Vario zu nervös → Q kleiner machen. Zu träge → Q grösser oder R_baro grösser
- **I2C-Bus-Sättigung bei 100 Hz:** wenn alle Sensoren zusammen > 10 ms brauchen, müssen wir die LSM-Rate runter oder die FIFO-Bursts ändern. Phase C ist die kritische Stelle für Timing
- **`xPortGetCoreID()` für Pinning:** bei Task-Erstellung explizit Core via `xTaskCreatePinnedToCore` setzen, nicht `xTaskCreate`
- **Mutex-Latenz:** wenn `g_vario_mutex` zu lange gehalten wird, blockieren wir die I2C-Task. Nur kurze Critical-Sections, keine printf inside locks

---

## Erfolgs-Kriterien aus Pilotensicht

Wenn alle 4 Phasen durch sind, sollte sich das Vario im Vergleich zu Ivos Skytraxx so anfühlen:

- **Spürbar schneller** beim Thermik-Einstieg (Bestätigung über Bench-Test, später im Flug)
- **Ruhiger** beim Kurbeln (keine Lage-Phantom-Signale)
- **Stabilere absolute Höhe** auch wenn QNH-Update fehlt (Dichte-Korrektur)
- **Δp-Stream verfügbar** als Rohmaterial für Ticket 3 (Klapper-Algorithmus)

---

## Definition of Done

- Alle vier Phasen mit GATE abgehakt, jede für sich validiert
- Vergleichs-CSV liegt im Repo
- `docs/STATUS.md` mit Phase-für-Phase-Notizen
- Branch `aura-kruecke/2-vario-core` auf GitHub Private gepusht, PR offen
- Ticket-Datei nach `docs/tickets/done/` verschoben

---

## Notes für Folge-Tickets

**Δp-Roh-Daten landen ab Phase D im IGC-Plus-Stream** — auch wenn wir den Klapper-Algorithmus jetzt nicht implementieren, sammeln wir die Daten von Anfang an. Wenn die Schirm-Nodes-Hardware später kommt, hast du ein Trainings-Set bereit.

**AURA-KRUECKE-3 (Klapper-Klassifikator) wird in die Schirm-Nodes-Familie verschoben.** Solange wir nur Gurtzeug-Sensoren haben, ist die Δp-Physik zu schwach für vertrauenswürdige Warnungen. Sobald Schirm-Nodes-Hardware (separate nRF52810-PCB mit BMP581 + ICM-42688-P + Mikrofon, im Bremsen-Ansatz oder am Tragegurt-Ende montiert) verfügbar ist:
- Schirm-Nodes-Familie erhält eigenes Master-Doc
- Klapper-Klassifikator wird darauf aufgebaut, nicht auf Gurtzeug-Δp
- Bis dahin: Δp-Stream aus T2 Phase D wird offline gelogged, für späteres Training
