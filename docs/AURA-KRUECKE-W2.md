# AURA-KRUECKE-W2 · Forecast-Vergleich + Inversions-Detection

## Status
- **Erstellt:** 2026-05-18
- **Familie:** W (Wetter)
- **Priorität:** P2 (braucht WiFi-Anbindung — siehe Voraussetzungen)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI — gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`
- **Branch:** `aura-kruecke/w2-forecast-inversion`
- **Geschätzt:** 6–10 h (Krücke-Seite + BuddyServer-Endpunkt-Spec)
- **Voraussetzungen:**
  - W1 abgeschlossen (lokale Lapse-Messung verfügbar)
  - Ticket 6 abgeschlossen (WiFi-STA-Modus läuft, BuddyServer erreichbar)
  - **BuddyServer-Endpunkt-Ticket parallel erstellt** — siehe Abschnitt "Server-Seite"

> ⚠️ **Wichtig:** dieses Ticket berührt den BuddyServer nur **lesend**. Die Server-Seite (neuer Endpunkt) ist ein **separates Ticket** im BuddyServer-Repo. Nicht in der Krücke einen Server-Endpunkt implementieren.

---

## Ziel

Den auf BuddyServer schon vorhandenen Wetter-Forecast (ICON-CH oder gleichwertig) für die Position des Piloten **on-device verfügbar machen**, mit drei konkreten Anwendungen:

1. **Inversions-Detection live:** gemessene T bei Pilot-Höhe vs Forecast-T → wenn deutlich wärmer → Inversion → Thermik gekappt
2. **CAPE-Light:** Surface-T + vertikales Forecast-Profil → erwartete Trigger-Temperatur, erwartete Top-of-Climb
3. **Forecast-Confidence:** rolling-Differenz zwischen Forecast und Messung der letzten 60 Min → Pilot sieht ob Plan stimmt

---

## Server-Seite — Endpunkt-Vertrag (separates BuddyServer-Ticket)

Vor diesem Ticket muss in `C:\BuddyServer` (separates Ticket im Server-Repo) folgender Endpunkt existieren:

```
GET /atmosphere/profile
  ?lat={lat}&lon={lon}
  &altitudes={alt1,alt2,alt3,...}        # MSL, in m, optional, default: 0..5000 in 200m-Schritten
  &time={iso8601}                         # default: now

Response 200:
{
  "model": "icon-ch1-eps",
  "issued_at": "2026-05-18T06:00:00Z",
  "valid_at": "2026-05-18T13:00:00Z",
  "position": {"lat": 47.123, "lon": 8.456, "elevation_m": 412},
  "profile": [
    {"altitude_m": 400, "temperature_c": 18.2, "dewpoint_c": 11.4, "wind_dir_deg": 250, "wind_speed_kmh": 8},
    {"altitude_m": 600, "temperature_c": 17.1, "dewpoint_c": 10.9, "wind_dir_deg": 252, "wind_speed_kmh": 11},
    ...
  ],
  "surface": {
    "temperature_c": 18.5,
    "dewpoint_c": 11.5,
    "pressure_hpa": 1015.2,
    "expected_trigger_temp_c": 19.5,
    "expected_thermal_top_m": 2400
  },
  "confidence": 0.78
}
```

Krücke-Seite konsumiert nur — keine Berechnungen die in den Server gehören.

---

## Akzeptanzkriterien (Krücke)

- [ ] Beim ersten WiFi-Connect (oder alle 30 Min im Flug) wird `/atmosphere/profile` mit aktueller GPS-Position abgerufen
- [ ] Forecast-Daten im RAM-Cache gehalten, NVS-Persistenz für letztes Profil
- [ ] `inversion_detected` Flag wird gesetzt wenn `T_measured - T_forecast(altitude) > 3.0 K` für mindestens 30 s
- [ ] `cape_top_estimate_m` aus Surface-T + Trocken-Adiabate → erste Sättigung
- [ ] `forecast_confidence_score` aus rolling-Vergleich letzter 60 Min (RMSE T_measured vs T_forecast)
- [ ] Bei fehlendem WiFi: cached Profil bis 6 h alt akzeptieren, dann "Forecast stale" Flag
- [ ] Display-Integration auf Thermal-Screen: kleines Element "Top: 2400m · Inv@2100m"

---

## Architektur

### Datenstruktur-Erweiterung

```cpp
struct AtmosphereForecast {
  uint32_t issued_at_unix;
  uint32_t valid_at_unix;
  float profile_alt_m[32];       // bis 32 Höhen-Stützstellen
  float profile_temp_c[32];
  float profile_dew_c[32];
  float profile_wind_dir[32];
  float profile_wind_kmh[32];
  uint8_t profile_count;
  float surface_temp_c;
  float surface_dewpoint_c;
  float surface_expected_trigger_c;
  float surface_expected_top_m;
  float forecast_confidence;
};

extern AtmosphereForecast g_forecast;

// In VarioState ergänzen:
bool inversion_detected;
float inversion_altitude_m;       // wo die Inversion sitzt
float cape_top_estimate_m;
float forecast_match_score;        // 0..1, wie gut Forecast stimmt
bool forecast_stale;
```

### Inversion-Detection-Algorithmus

```cpp
void check_inversion(VarioState& v, const AtmosphereForecast& f) {
    // Interpolate forecast T at current altitude
    float t_forecast = interpolate_linear(f.profile_alt_m, f.profile_temp_c,
                                          f.profile_count, v.altitude_kalman);

    float delta = v.temperature_c - t_forecast;

    if (delta > 3.0f) {
        v.inversion_counter += dt;
        if (v.inversion_counter > 30.0f) {
            v.inversion_detected = true;
            v.inversion_altitude_m = v.altitude_kalman;
        }
    } else {
        v.inversion_counter = 0;
        v.inversion_detected = false;
    }
}
```

### CAPE-Light

Trocken-adiabatisches Hochrechnen vom Boden bis Sättigung:

```cpp
float compute_cape_top(const AtmosphereForecast& f) {
    float t_surface = f.surface_temp_c;
    float dew_surface = f.surface_dewpoint_c;
    float surface_alt = f.position_elevation_m;  // oder GND_REF

    // Trocken-Adiabate: -9.8 K/km bis Sättigung
    // Sättigung erreicht bei: spread / 8.0 km (Faustformel)
    float spread = t_surface - dew_surface;
    float lcl_m = surface_alt + spread * 125.0f;  // Lifting Condensation Level

    // Vergleich mit Forecast-Profil: wo schneidet die Adiabate die Umgebungs-Temp?
    // (simple Variante — präzise CAPE wäre Integral)
    float parcel_temp = t_surface;
    float h = surface_alt;
    while (h < surface_alt + 5000.0f) {
        h += 100.0f;
        parcel_temp -= 0.98f;  // -9.8 K/km
        float env_temp = interpolate_linear(f.profile_alt_m, f.profile_temp_c,
                                             f.profile_count, h);
        if (parcel_temp < env_temp) {
            return h;  // Parcel kühler als Umgebung → Stop
        }
    }
    return surface_alt + 5000.0f;  // kein Stop gefunden, sehr labil
}
```

### Forecast-Confidence

Rolling RMSE über letzte 60 Min:

```cpp
// Pro Minute: speichere (t_measured, t_forecast_at_my_alt) in 60-Sample Ring
// Confidence = 1 - normalize(rmse, max_rmse=5K)
float rmse = sqrt(sum((m-f)^2) / 60);
forecast_match_score = clamp(1.0 - rmse / 5.0, 0.0, 1.0);
```

---

## Phasen-Plan

```
🛑 GATE-W2-1 · HTTP-Client für Profile-Endpoint
  Voraussetzung: Ticket 6 abgeschlossen, ESPHttpClient läuft, TLS-Zertifikat im Filesystem
  Aktionen:
    - src/network/forecast_fetcher.cpp
    - JSON-Parsing mit ArduinoJson
    - NVS-Persistenz für letztes Profil
    - Fetch beim Boot + alle 30 Min im Flug
  Auswirkung: g_forecast ist befüllt nach erstem Fetch
  OK?

🛑 GATE-W2-2 · Inversion-Detection
  Aktionen:
    - src/atmosphere/inversion_detector.cpp
    - Hysterese (30 s über Threshold, 60 s drunter zum Reset)
    - Telemetrie-Output
  Bench: auf Heizung legen → simuliert lokale Temp-Spike → Inversion-Flag triggert
  OK?

🛑 GATE-W2-3 · CAPE-Light + Confidence-Score
  Aktionen:
    - src/atmosphere/cape_light.cpp
    - src/atmosphere/forecast_match.cpp (rolling RMSE)
    - Display-Integration Thermal-Screen
  OK?

🛑 GATE-W2-4 · Stale-Forecast-Handling
  Aktionen:
    - Wenn WiFi >6 h weg → forecast_stale=true
    - UI-Indikator (Symbol oder Text "Forecast 7h alt")
    - Cached Profil bleibt nutzbar bis dahin
  OK für Merge?
```

---

## Display-Integration

Thermal-Screen, kompaktes Atmo-Element ersetzt "Lapse" aus W1 mit erweiterten Daten:

```
Atmo  Gemessen 7.8 K/km · Forecast 7.2  ✓
Top   ~2400 m (CAPE)  · Inversion @ 2100 m
Conf  Forecast 78%
```

Wenn Inversion detected → eigene Zeile prominent:

```
⚠ INVERSION @ 2100m — Top heute begrenzt
```

---

## Stolpersteine

- **WiFi-Verbindungs-Latenz:** der erste Fetch beim Boot kann 5-20 s dauern (DNS, TLS-Handshake, ICON-Antwort). Nicht den Boot blockieren — async im Background-Task. Solange `g_forecast.issued_at_unix == 0` → "Forecast pending" anzeigen, kein Crash, kein Default-Wert ohne Flag.
- **GPS-Position muss valid sein** bevor erster Fetch sinnvoll ist. Wenn GPS-Fix erst nach 30 s kommt → erst dann Fetch starten.
- **Forecast-Resolution:** ICON-CH hat 1 km horizontal, COSMO 2 km. Bei längeren XC-Flügen kann die Pilot-Position aus dem Forecast-Grid wandern — nach 50 km Strecke neu fetchen.
- **TLS-Zertifikat-Pinning:** für `buddy.flightbuddyki.org` brauchen wir das Cloudflare-Root-Cert im SPIFFS oder hard-coded. Wenn das expired (alle 1-2 Jahre) → OTA-Update nötig.
- **CAPE-Light ist VEREINFACHT.** Echte CAPE integriert positive Auftriebs-Fläche über alle Höhen. Unser simpler "wo schneidet Adiabate Umgebung" ist eine grobe Schätzung. Wenn ein Pilot mal genauer CAPE will → später dazubauen oder Server-side rechnen.

---

## Definition of Done

- 4 Gates abgehakt
- BuddyServer-seitiger `/atmosphere/profile`-Endpunkt funktioniert (separates Ticket im Server-Repo, verlinkt)
- Cache-Verhalten getestet: WiFi-Abschaltung simulieren, stale-Flag erscheint nach 6h
- Branch gepusht
- Inversions-Detection an einem realen Flug-Tag mit Inversions-Bedingungen validiert (kann nach-träglich in W2-followup-Ticket landen)

---

## Notes für W3 (Wind-Schichtung)

W2 zieht das vertikale **Temperatur**-Profil. W3 nutzt **dasselbe Endpunkt-Response** (das `wind_dir_deg` und `wind_speed_kmh` pro Höhe schon enthält) plus GPS-derived Live-Wind. Wenn W2 fertig ist, kann W3 den HTTP-Fetch-Code wiederverwenden — nur die Verarbeitung ist anders.
