#pragma once
// thermal_manager.h — Sammelt Lift-Samples + berechnet Kern
// Fuettert ThermalData fuer den Thermik-Screen
#include <Arduino.h>
#include <math.h>
#include "thermal_screen.h"

class ThermalManager {
public:
    ThermalData data = {};
    bool active = false;

    void start(float start_alt) {
        active = true;
        data = {};
        data.thermal_start_ms = millis();
        _start_alt = start_alt;
        _sample_idx = 0;
        _last_lat = 0;
        _last_lon = 0;
    }

    void stop() { active = false; }

    // Jeden Loop aufrufen mit aktuellen Daten
    void update(float lat, float lon, float heading, float speed_kmh,
                float vario, float vario_avg, float altitude,
                float temp, float dewpoint, int bat_pct,
                int rtc_hour, int rtc_min) {

        data.vario = vario;
        data.vario_avg = vario_avg;
        data.altitude = altitude;
        data.heading = heading;
        data.speed = speed_kmh;
        data.gained = altitude - _start_alt;
        data.rtc_hour = rtc_hour;
        data.rtc_min = rtc_min;
        data.bat_pct = bat_pct;

        // Basis-Schaetzung (Wolkenuntergrenze aus Spread)
        // Spread = Temp - Dewpoint, Basis = Altitude + Spread * 125
        float spread = temp - dewpoint;
        data.base_est = altitude + spread * 125.0f;

        // Lift-Sample hinzufuegen (alle 2s, nur bei GPS-Bewegung)
        if (speed_kmh > 5.0f && lat != 0 && lon != 0 &&
            millis() - _last_sample_ms > 2000) {

            _last_sample_ms = millis();

            // Relative Position berechnen (Meter, North-Up)
            if (_last_lat != 0) {
                float dlat = lat - _last_lat;
                float dlon = lon - _last_lon;
                // Grob: 1° lat ≈ 111320m, 1° lon ≈ 111320 * cos(lat)
                float dy = dlat * 111320.0f;  // Nord
                float dx = dlon * 111320.0f * cosf(lat * M_PI / 180.0f);  // Ost

                // Sample in Ringpuffer
                int idx = _sample_idx % 30;
                data.samples[idx].dx = dx;
                data.samples[idx].dy = dy;
                data.samples[idx].climb = vario;
                data.samples[idx].ts = millis();
                _sample_idx++;
                if (data.sample_count < 30) data.sample_count++;
            }

            _last_lat = lat;
            _last_lon = lon;
        }

        // Kern berechnen (climb-gewichteter Schwerpunkt)
        calcKern();
    }

private:
    float _start_alt = 0;
    float _last_lat = 0, _last_lon = 0;
    unsigned long _last_sample_ms = 0;
    int _sample_idx = 0;

    void calcKern() {
        if (data.sample_count < 3) {
            data.kern_dist = 0;
            data.kern_hint = "zu wenig Daten";
            return;
        }

        float sum_dx = 0, sum_dy = 0, sum_w = 0;
        unsigned long now = millis();

        for (int i = 0; i < data.sample_count; i++) {
            const LiftSample &s = data.samples[i];
            if (now - s.ts > 60000) continue;  // Aelter als 60s ignorieren
            if (s.climb <= 0) continue;          // Nur Steigen zaehlt

            float w = s.climb;  // Gewicht = Steigrate
            sum_dx += s.dx * w;
            sum_dy += s.dy * w;
            sum_w += w;
        }

        if (sum_w < 0.1f) {
            data.kern_dist = 0;
            data.kern_hint = "kein Steigen";
            return;
        }

        data.kern_dx = sum_dx / sum_w;
        data.kern_dy = sum_dy / sum_w;
        data.kern_dist = sqrtf(data.kern_dx * data.kern_dx + data.kern_dy * data.kern_dy);

        // Richtungshinweis relativ zum Heading
        float kern_bearing = atan2f(data.kern_dx, data.kern_dy) * 180.0f / M_PI;
        float rel = kern_bearing - data.heading;
        while (rel > 180) rel -= 360;
        while (rel < -180) rel += 360;

        if (rel > -45 && rel < 45) data.kern_hint = "vorne";
        else if (rel >= 45 && rel < 135) data.kern_hint = "rechts";
        else if (rel >= -135 && rel < -45) data.kern_hint = "links";
        else data.kern_hint = "hinten";
    }
};
