#pragma once
// flight_detect.h — Start-/Lande-Erkennung
// Start: GPS Speed > 15 km/h fuer 5s → "START ERKANNT"
// Landung: GPS Speed < 5 km/h + kein Steigen fuer 30s → Lande-Screen
#include <Arduino.h>

enum FlightState { FLIGHT_GROUND, FLIGHT_FLYING, FLIGHT_LANDED };

class FlightDetector {
public:
    FlightState state = FLIGHT_GROUND;
    unsigned long start_time = 0;      // millis() bei Start
    unsigned long land_time = 0;       // millis() bei Landung
    float max_altitude = 0;            // Max-Hoehe im Flug
    float start_altitude = 0;          // Hoehe bei Start

    // Jeden Loop aufrufen mit aktuellen Daten
    FlightState update(float speed_kmh, float vario, float altitude) {
        switch (state) {
        case FLIGHT_GROUND:
            // Start-Erkennung: Speed > 15 km/h fuer 5s
            if (speed_kmh > 20.0f) {  // 20 km/h statt 15 (GPS-Rauschen)
                if (_fast_since == 0) _fast_since = millis();
                if (millis() - _fast_since > 10000) {  // 10s statt 5s
                    state = FLIGHT_FLYING;
                    start_time = millis();
                    start_altitude = altitude;
                    max_altitude = altitude;
                    _just_started = true;
                    Serial.println("[FLY] >>> START ERKANNT <<<");
                }
            } else {
                _fast_since = 0;
            }
            break;

        case FLIGHT_FLYING:
            if (altitude > max_altitude) max_altitude = altitude;

            // Lande-Erkennung: Speed < 5 km/h + kein Steigen fuer 30s
            if (speed_kmh < 5.0f && vario < 0.3f) {
                if (_slow_since == 0) _slow_since = millis();
                if (millis() - _slow_since > 30000) {
                    state = FLIGHT_LANDED;
                    land_time = millis();
                    Serial.println("[FLY] >>> LANDUNG ERKANNT <<<");
                }
            } else {
                _slow_since = 0;
            }
            break;

        case FLIGHT_LANDED:
            // Bleibt gelandet bis manuell zurueckgesetzt
            break;
        }
        return state;
    }

    // Wurde gerade gestartet? (einmalig true)
    bool justStarted() {
        if (_just_started) { _just_started = false; return true; }
        return false;
    }

    // Flugdauer in Sekunden
    int flightDurationSec() {
        if (state == FLIGHT_FLYING) return (millis() - start_time) / 1000;
        if (state == FLIGHT_LANDED) return (land_time - start_time) / 1000;
        return 0;
    }

    // Hoehengewinn
    float altitudeGained() { return max_altitude - start_altitude; }

    // Reset (nach Lande-Abfrage)
    void reset() { state = FLIGHT_GROUND; _fast_since = 0; _slow_since = 0; }

private:
    unsigned long _fast_since = 0;
    unsigned long _slow_since = 0;
    bool _just_started = false;
};
