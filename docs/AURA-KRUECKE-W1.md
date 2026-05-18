# AURA-KRUECKE-W1 · Lokale Wolkenbasis + Lapse-Rate-Messung

## Status
- **Erstellt:** 2026-05-18
- **Familie:** W (Wetter/Atmosphäre — parallele Track-Familie zu den Haupt-Tickets)
- **Priorität:** P1 (kann direkt nach AURA-KRUECKE-2 starten, blockiert W2 nicht)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI — gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`
- **Branch:** `aura-kruecke/w1-cloudbase-lapse`
- **Geschätzt:** 4–6 h
- **Voraussetzung:** Ticket 2 Phase B abgeschlossen (SHT40 + Dichte-Korrektur laufen, `altitude_kalman` verfügbar)

> ⚠️ **Workflow:** wie immer. Jeder Gate braucht "ok".

---

## Ziel

Mit den lokalen Sensoren (SHT40 + Höhe aus Ticket 2) **vor-Ort-Atmosphären-Wissen** erzeugen, ohne Server-Anbindung. Konkret:

1. **Wolkenbasis-Schätzung live:** aktuelle Höhe + Temp + Taupunkt → vermutete Konvektions-Obergrenze
2. **Persönliche Lapse-Rate-Messung:** in jedem Steig-Etap die tatsächliche ΔT/Δh-Rate erfassen → Aussage "deine Atmosphäre heute" statt "ICAO-Standard"
3. **Atmosphären-Stabilitäts-Indikator:** gemessene Lapse vs trocken-/feucht-adiabatisch → "stabil" / "neutral" / "labil"

Alles ausschliesslich aus Bord-Sensoren. Keine Internet-Anbindung. Keine BuddyServer-Calls. Das ist W1 — das Fundament.

---

## Akzeptanzkriterien

- [ ] `cloud_base_estimate_m` wird live berechnet sobald T und Dew valid sind (~5s nach Boot)
- [ ] Bei einem Steig-Etap (≥ 150 m gewonnen, ≥ 60 s Dauer) wird `measured_lapse_rate_K_per_km` aus den aufgezeichneten T-Höhe-Paaren ermittelt
- [ ] Rolling-Lapse über letzte 3 Steig-Etappen gewichtet (jüngster zuletzt) wird ausgegeben
- [ ] `atmospheric_stability` als Enum `{ very_stable, stable, neutral, labile, very_labile }` basierend auf gemessener Lapse vs Standard-Adiabaten
- [ ] Serial-Output erweitert um die drei neuen Streams
- [ ] Bench-Test plausibel: drinnen mit Heizung → "stabil" (warme Luft oben), draussen kalt → höhere Wolkenbasis erwartet
- [ ] Stair-Walk produziert nutzbare Lapse-Messung (zumindest qualitativ: oben kühler als unten)

---

## Architektur

### Wolkenbasis-Formel

Standard Spread-Methode für konvektive Wolkenbasis (Cumulus):

```
T_dew = SHT40.dew_point_c
T_air = SHT40.temperature_c
spread = T_air - T_dew

// Annahme: Trocken-Adiabate 9.8 K/km bis Sättigung,
// Feucht-Adiabate ~6 K/km darüber. Vereinfacht:
cloud_base_AGL_m = spread / 2.5f * 1000.0f / 9.8f * 9.8f
// = spread / 2.5f * 1000.0f / 1.0f  ← wenn man Standard-Spread benutzt
// Sauberer:
cloud_base_AGL_m = spread * 125.0f;   // grobe Faustformel

// Plus Ground-Referenz:
cloud_base_MSL_m = cloud_base_AGL_m + altitude_at_start_m;
```

**Wichtig:** Spread-Methode funktioniert nur bei *konvektiver* Wolkenbildung. Stratus, Föhn-Wolken, Lee-Wave-Wolken folgen anderen Mechanismen. Output mit Confidence-Flag liefern (z. B. low confidence wenn Wind > 30 km/h).

### Lapse-Rate aus Steig-Etappen

State-Machine pro Etap:

```
state = idle
on vario_avg_20s > 0.5 m/s for 5s:
    state = climbing
    record start: (time, altitude_kalman, temperature_c)
    samples = empty ring buffer (10 Hz aufzeichnen)

while in climbing:
    push (altitude_kalman, temperature_c) into samples

on vario_avg_20s < 0.2 m/s for 10s OR Δh > 150m gewonnen:
    if Δh >= 150m and duration >= 60s:
        # Linear regression T vs h on samples
        slope_K_per_m = linregress(samples).slope
        lapse_K_per_km = -slope_K_per_m * 1000.0
        push lapse_K_per_km to history (max 5 entries)
        emit new measured_lapse
    state = idle
    samples = empty
```

Rolling-Mean über letzte 3 Etappen, gewichtet 0.5/0.3/0.2 (jüngster zuletzt). Wenn weniger als 1 Etap vorhanden → Default = 6.5 K/km (ICAO-Standard) mit `lapse_confidence = forecast`.

### Stabilitäts-Klassifikation

```
measured_lapse_K_per_km < 4.0  → very_stable    // Inversion oder isotherm
                       < 6.5   → stable          // typische Atmosphäre
                       < 8.5   → neutral         // gut für Thermik
                       < 10.0  → labile          // sehr gut für Thermik
                       >= 10.0 → very_labile     // CB-Gefahr, Überentwicklung möglich
```

### Datenstruktur-Erweiterung

`VarioState` aus Ticket 2 wird ergänzt um:

```cpp
// Atmosphären-Block
float dew_point_c;                  // schon in SHT40-Driver berechenbar
float cloud_base_AGL_m;
float cloud_base_MSL_m;
float cloud_base_confidence;        // 0..1
float measured_lapse_K_per_km;
uint8_t lapse_samples_count;        // 0 = forecast, 1-5 = measured
enum AtmoStability stability;
```

---

## Phasen-Plan

```
🛑 GATE-W1-1 · SHT40-Erweiterung + Wolkenbasis
  Aktionen:
    - Dew-Point-Berechnung in SHT40-Driver (Magnus-Formel)
    - src/atmosphere/cloud_base.cpp (Spread-Methode + Confidence)
    - VarioState-Struct erweitern
    - Serial-Output ergänzen
  Auswirkung: Wolkenbasis live im Stream
  OK?

🛑 GATE-W1-2 · Lapse-Rate-State-Machine
  Aktionen:
    - src/atmosphere/lapse_recorder.cpp (Ring-Buffer + State-Machine)
    - src/atmosphere/lapse_history.cpp (5-Entry-History mit Gewichtung)
    - Linear-Regression-Helper (oder polynom_fit aus Eigen-Lite/eigen-quasi)
    - Bench-Test: Stair-Walk mit Temperatur-Differenz erzeugen (Treppenhaus kalt unten/warm oben)
  Auswirkung: erste echte Lapse-Messung
  OK?

🛑 GATE-W1-3 · Stabilitäts-Klassifikation + Validierung
  Aktionen:
    - src/atmosphere/stability.cpp
    - Display-Integration auf Thermal-Screen (Inset-Block)
  Auswirkung: Pilot sieht "Atmosphäre: labil, Basis ~2400 m" auf einen Blick
  Validierung: 30min Outdoor-Test, Plausibilität checken
  OK für Merge?
```

---

## Display-Integration

Auf dem **Thermal-Screen** (aus Mockup): unten links statt nur "Avg climb" auch:

```
Lapse  7.8 K/km · labile
Basis  2'420 m · spread 8°
```

Auf dem **Cruise-Screen**: kleines Info-Element nur Wolkenbasis (1 Zeile):

```
Basis 2420m (in 285m über Pilot)
```

---

## Stolpersteine

- **Spread-Methode funktioniert nur bei Konvektion.** Bei Föhn, Stau, Lee-Wave gibt sie falsche Werte. Confidence-Flag low setzen wenn:
  - Wind > 30 km/h (Föhn/Stau-Verdacht)
  - Relative Feuchte > 85 % auf Boden-Niveau (Stratus/Nebel-Verdacht)
  - Lapse < 4 K/km gemessen (Inversion → keine konvektive Basis)
- **Lapse-Messung in Thermik-Kern selbst ist verrauscht** (Schwankungen durch Auf-/Abwinde mit Temperatur-Differenzen). Lieber nur den Mittelwert *aller* Höhen während des Steigs nehmen, nicht differentielles ΔT/Δh punkt-für-punkt.
- **SHT40-Lag:** der Sensor hat ~3 s Zeitkonstante für Temperatur (Sensirion-Spec). Bei schnellem Steigen entsteht Lag in der Temperatur-Höhe-Kurve. Falls Lapse zu klein gemessen wird → SHT40 hängt nach, dann die Sample-Reihenfolge umdrehen (Bottom-of-climb-T mit kleinerer Höhe als gespeichert paaren).
- **Sensor-Hot-Spot:** wenn SHT40 nah am ESP32 sitzt, misst er die ESP32-Wärme statt Aussenluft. Bench-Test: SHT40 sollte ~Raumtemperatur zeigen, nicht Raumtemperatur + 5°C. Falls letzteres → später in der Hardware-Phase Sensor in Aussen-Luft-Strom positionieren.

---

## Definition of Done

- 3 Gates abgehakt
- Stair-Walk-Test mit gemessener Lapse im Ergebnis-CSV
- Branch gepusht
- Ticket-Datei nach `docs/tickets/done/`
- W1-Daten im STATUS.md kurz dokumentiert: "an Tag X gemessene Lapse: Y K/km, fühlte sich wie: ?"

---

## Notes für W2 (BuddyServer-Forecast-Integration)

W1 produziert die *gemessenen* Daten. W2 holt die *vorhergesagten* Daten und vergleicht. Wenn `measured_lapse` von Forecast-Lapse > 2 K/km abweicht → "Forecast off" Warnung. Wenn `cloud_base_measured` von Forecast > 300 m abweicht → "Atmosphäre anders als geplant".

Aber das ist W2. W1 zuerst sauber.
