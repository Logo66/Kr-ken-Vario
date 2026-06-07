#pragma once
// kalman_vario.h — 2-State Kalman Filter fuer barometrisches Vario
//
// State: [altitude, vario]
// Measurement: barometrische Hoehe (aus BMP581)
//
// Tuning-Parameter:
//   Q_alt:  Prozessrauschen Hoehe (wie schnell aendert sich h unabhaengig?)
//   Q_var:  Prozessrauschen Vario (wie schnell aendert sich Steigrate?)
//   R:      Messrauschen Barometer (wie genau ist die Hoehenmessung?)
//
// Kleines R / grosses Q = schnelle Reaktion, rauschig
// Grosses R / kleines Q = langsame Reaktion, glatt
//
// Typisch fuer Paragliding-Vario:
//   R = 0.5 (BMP581 ~0.5m Rauschen bei 16x Oversampling)
//   Q_alt = 0.02, Q_var = 0.5 (responsive, ~150ms Latenz)

#include <Arduino.h>

class KalmanVario {
public:
    float altitude = 0;
    float vario = 0;

    void init(float initial_alt) {
        altitude = initial_alt;
        vario = 0;
        P[0][0] = 1.0f;  P[0][1] = 0;
        P[1][0] = 0;      P[1][1] = 1.0f;
        last_ms = millis();
    }

    // Predict + Update in einem Schritt (Barometer-only, ~10-50 Hz)
    void update(float measured_alt) {
        unsigned long now = millis();
        float dt = (now - last_ms) / 1000.0f;
        last_ms = now;

        if (dt <= 0 || dt > 2.0f) {
            // Erster Aufruf oder zu lange Pause — Reset
            altitude = measured_alt;
            vario = 0;
            return;
        }

        // === PREDICT ===
        // x_pred = F * x = [alt + vario*dt, vario]
        altitude += vario * dt;

        // P_pred = F*P*F' + Q
        float p00 = P[0][0] + dt * (P[1][0] + P[0][1] + dt * P[1][1]) + Q_alt;
        float p01 = P[0][1] + dt * P[1][1];
        float p10 = P[1][0] + dt * P[1][1];
        float p11 = P[1][1] + Q_var;

        // === UPDATE (Measurement: z = measured_alt) ===
        float innovation = measured_alt - altitude;
        float S = p00 + R;  // Innovation covariance

        // Kalman gain
        float K0 = p00 / S;
        float K1 = p10 / S;

        // State update
        altitude += K0 * innovation;
        vario += K1 * innovation;

        // Covariance update (Joseph form fuer numerische Stabilitaet)
        float t00 = (1.0f - K0) * p00;
        float t01 = (1.0f - K0) * p01;
        float t10 = p10 - K1 * p00;
        float t11 = p11 - K1 * p01;

        P[0][0] = t00;
        P[0][1] = t01;
        P[1][0] = t10;
        P[1][1] = t11;
    }

    // Tuning-Parameter (oeffentlich fuer spaetere NVS-Konfiguration)
    float Q_alt = 0.02f;   // Prozessrauschen Hoehe
    float Q_var = 0.5f;    // Prozessrauschen Vario (hoeher = schnellere Reaktion)
    float R = 0.5f;        // Messrauschen Barometer

private:
    float P[2][2] = {{1,0},{0,1}};
    unsigned long last_ms = 0;
};
