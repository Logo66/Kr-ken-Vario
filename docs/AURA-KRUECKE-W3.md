# AURA-KRUECKE-W3 · Wind-Schichtung + Rotor-Warning

## Status
- **Erstellt:** 2026-05-18
- **Familie:** W (Wetter)
- **Priorität:** P2 (Hochwertfeature, kein Blocker)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI — gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`
- **Branch:** `aura-kruecke/w3-wind-layering`
- **Geschätzt:** 6–8 h
- **Voraussetzungen:**
  - W2 abgeschlossen (Forecast-Profil verfügbar, inkl. wind_dir/wind_speed pro Höhe)
  - Solider GPS-Fix mit ≥ 1 Hz und ground-speed/track aus TinyGPSPlus

---

## Ziel

Den Pilot vor dem **gefährlichsten unsichtbaren Phänomen** der Atmosphäre warnen — Windscherung und Rotoren — und dabei nebenbei XC-Optimierung anbieten.

Drei Outputs:

1. **Live-Wind pro Höhenband** (200 m Bänder): aus GPS-Track + Heading-Schätzung
2. **Rotor-Warning** wenn Wind-Scherung in benachbarten Bändern > 15 km/h
3. **Best-XC-Altitude**: welche Höhe in den nächsten 1000 m über dem Pilot hat den günstigsten Wind für seinen aktuellen Kurs

---

## Akzeptanzkriterien

- [ ] Live-Wind wird pro 200 m-Höhenband berechnet aus GPS-Track-Drift während Kurbel-Kreisen (3 vollständige Kreise = 1 Wind-Sample)
- [ ] Wind-Bänder werden in `WindLayerArray` gehalten, max 30 Min Alterung pro Band, dann verworfen
- [ ] Bei Scherung > 15 km/h zwischen Nachbar-Bändern → `rotor_warning_layer` Flag + Audio-Alert beim Durchqueren der Schicht
- [ ] Best-XC-Altitude wird relativ zum aktuellen GPS-Bearing berechnet (Rückenwind-Vorteil über nächste 1000 m)
- [ ] Vergleich live-Wind vs Forecast-Wind als zweite Spalte angezeigt (Forecast-Validation)
- [ ] XC-Screen bekommt neues Wind-Spalten-Element (siehe Mockup-Update unten)

---

## Architektur

### GPS-derived Wind aus Kreisflug

Wenn der Pilot in Thermik kreist (yaw_rate > 0.3 rad/s, ~17°/s, 3+ vollständige Kreise):

1. Sammle alle (GPS-track, GPS-speed) Samples während ≥ 3 Vollkreisen
2. Mittel-Position der Punkte = vermutlicher "wahrer" Pilot-Pfad-Schwerpunkt
3. Drift dieses Schwerpunktes über die Zeit = Wind-Vektor
4. Höhenband = Mittel der altitude_kalman während der Messung

```cpp
struct WindLayer {
    float altitude_band_m;     // 200m bands, z. B. 1800-2000
    float wind_dir_deg;
    float wind_speed_kmh;
    uint32_t measured_at_unix;
    float confidence;           // 0..1, abhängig von #Kreise
};

struct WindProfile {
    WindLayer layers[24];       // bis 24 Bänder = 4800 m Abdeckung
    uint8_t count;
};
```

**Alternative bei langem Gleitflug** (kein Kreisen): aus track − heading × ground_speed Differenz schätzen. Weniger genau, aber funktioniert wenn keine Thermik gekurbelt wird. Heading kommt aus GPS-track-Glättung (nicht aus BNO085-Mag wegen Cockpit-Interferenz — siehe Ticket 2).

### Rotor-Detection

```cpp
for (i = 1; i < wind.count; i++) {
    float dv = abs(wind.layers[i].wind_speed - wind.layers[i-1].wind_speed);
    float dd = angular_diff(wind.layers[i].wind_dir, wind.layers[i-1].wind_dir);
    float h_diff = abs(wind.layers[i].altitude - wind.layers[i-1].altitude);

    // Scherung: Vektor-Differenz pro 100m
    float shear = vector_diff(wind.layers[i], wind.layers[i-1]) / h_diff * 100;

    if (shear > 15.0 && h_diff < 400.0) {
        rotor_warning.altitude_low = wind.layers[i-1].altitude;
        rotor_warning.altitude_high = wind.layers[i].altitude;
        rotor_warning.severity = (shear > 25.0) ? HIGH : MEDIUM;
    }
}
```

Audio-Alert wenn `altitude_kalman` in dieser Gefahren-Schicht ist UND nicht aktiv vermieden wird.

### Best-XC-Altitude

Wenn im XC-Modus mit aktivem Wegpunkt:

```cpp
float bearing_to_wp = compute_bearing(my_pos, next_wp);
WindLayer* best = null;
float best_tailwind = -INFINITY;

for each layer above current altitude, up to current + 1000m:
    float tailwind_component = layer.wind_speed * cos(layer.wind_dir - bearing_to_wp);
    if (tailwind_component > best_tailwind) {
        best_tailwind = tailwind_component;
        best = &layer;
    }

if (best && best.altitude - current_altitude > 200) {
    recommendation = "Klettere auf X m, dort +Y km/h Rückenwind";
}
```

Anzeige nur wenn echter Gewinn > 5 km/h und Höhendifferenz < 1500 m (sonst nicht praktikabel).

---

## Phasen-Plan

```
🛑 GATE-W3-1 · Live-Wind-Berechnung aus Kreisflug
  Aktionen:
    - src/atmosphere/wind_estimator.cpp
    - Detector für "wir kreisen jetzt" (yaw_rate-basiert, mit Geometrie-Check 3 vollständige Kreise)
    - GPS-Position-Aggregator
    - WindLayer-Output
  Bench: kann nur in der Luft validiert werden. Synthetic-Test: simulierte GPS-Position auf Kreis mit Wind-Offset → erwarteter Wind extrahiert
  OK?

🛑 GATE-W3-2 · Wind-Profile-Storage + Aging
  Aktionen:
    - src/atmosphere/wind_profile.cpp (24-Band-Array, 30-Min-Alterung)
    - Forecast-Wind-Vergleich (W2-Daten als Referenz)
    - Telemetrie-Output
  OK?

🛑 GATE-W3-3 · Rotor-Detection + Audio-Alert
  Aktionen:
    - src/atmosphere/rotor_detector.cpp
    - Audio-Driver für CUI CPT-9019S Buzzer (falls schon angeschlossen, sonst LED-only)
    - Pattern: Rotor = 3× kurzer Beep beim Eintritt in Gefahren-Schicht
  OK?

🛑 GATE-W3-4 · Best-XC-Altitude-Empfehlung
  Aktionen:
    - src/atmosphere/xc_altitude_optimizer.cpp
    - Display-Integration auf XC-Screen
    - Nur aktiv im XC-Modus mit gesetztem Wegpunkt
  OK für Merge?
```

---

## Display-Integration

### XC-Screen — neue Wind-Spalte rechts

```
Wind-Schichten
2400 m  ↗ 22  ← here +5 km/h besser
2200 m  ↗ 18
2000 m  → 14   PILOT
1800 m  → 11
1600 m  → 12   ⚠ rotor 1700-1900
```

Bänder farblich kodiert nach Vergleich mit aktuellem Pilot-Track:
- dunkles Grau: Gegenwind
- mittleres Grau: neutral / Seitenwind
- schwarz mit "+km/h"-Hint: Rückenwind besser als aktuell

### Cruise-Screen — kompakte Wind-Trend-Anzeige

Bottom-Strip ergänzt:
```
Wind  ↗ 14 km/h · oben 22  · unten 11
```

Zeigt aktuelle Höhe ± 400 m, gibt Pilot Awareness ohne XC-Screen-Wechsel.

---

## Stolpersteine

- **Wind-Schätzung aus 3 Kreisen braucht ~60 s Kurbeln.** Bei kurzen Thermiken (< 100 m gewonnen) kommt keine Messung zusammen. Confidence-Flag low, Forecast als Fallback nutzen.
- **GPS-Genauigkeit bei langsamem Flug:** beim Hike & Fly (Marsch zu Fuss) ist GPS-Track verrauscht. Wind-Estimator nur im Flugmodus aktivieren (Erkennung: ground_speed > 10 km/h ODER altitude_AGL > 50 m).
- **Magnetkompass nicht verfügbar:** wir nutzen `SH2_GAME_ROTATION_VECTOR` (Ticket 2) — daher kein absolutes Heading aus IMU. Heading wird aus GPS-Track-Glättung abgeleitet (5-Sample-Mittel). Im Schwebezustand keine Heading-Info — dann auch keine Live-Wind-Messung möglich.
- **Audio-Alert nicht spammen:** wenn Pilot in Rotor-Schicht herumfliegt → Alert maximal alle 60 s wiederholen, nicht im 1 Hz Loop nerven.
- **XC-Empfehlung nicht aufdrängen:** Pilot weiss meist warum er gerade nicht 200 m höher steigen will (Wolkenbasis, Luftraum, Müdigkeit). Empfehlung ist Info, nicht Befehl. Keine Audio-Alert, nur Display.

---

## Definition of Done

- 4 Gates abgehakt
- Synthetic-Test für Wind-Estimator dokumentiert (Code + Erwartungswert)
- Validierung mit echtem Flug-Log (nach-träglich falls Bench-Test gut aussieht)
- Branch gepusht
- W-Familie damit komplett (W1+W2+W3)

---

## Notes für Folge-Tickets

Wenn alle W-Tickets durch sind, sind interessante Folge-Themen:

- **W4 — Konvergenz-Detection:** entgegengesetzte Winde in benachbarten Höhen + Inversion = klassische Konvergenz. Mit W2+W3-Daten direkt ableitbar.
- **W5 — Wave-Detection:** Lee-Wave-Charakteristik (regelmässige Schwingung, sehr glatte Steigwerte, konstante Drift). Brauchen Mustererkennung über mehrere Minuten.
- **W-Swarm — Atmosphären-Mapping über FANET:** wenn 5+ Aura-Geräte gleichzeitig in der Region fliegen, kann der BuddyServer aus den geteilten Live-Daten ein 3D-Atmosphären-Bild bauen. Sehr ambitioniert, wäre ein eigenes Master-Ticket.

Aber alles erstmal W1-3 sauber.
