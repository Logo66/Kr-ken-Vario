#pragma once
// wind_estimator.h — Windschaetzung aus der GPS-Kreisdrift beim Kurbeln (XCSoar-Prinzip).
//   Im Kreis ist die Bodengeschwindigkeit MAX im Rueckenwind, MIN im Gegenwind:
//     wind_speed = (v_max - v_min) / 2
//     Wind kommt AUS (Kurs bei v_max + 180 Grad)   [meteorologische Konvention: "woher"]
// HEILIG: reine Schaetzung fuer Anzeige + BLE — fliesst NICHT ins Vario/Kalman.
#include <math.h>

class WindEstimator {
public:
    float wind_speed = 0;   // km/h
    float wind_dir   = 0;   // Grad, Richtung WOHER der Wind kommt
    bool  valid      = false;

    // Bei jedem NEUEN GPS-Fix aufrufen (~1 Hz). course in Grad (0..360), speed in km/h.
    void update(float course, float speed, bool gps_fix) {
        if (!gps_fix || speed < 8.0f) return;       // nur in Fahrt (sonst GPS-Kursrauschen im Stand)
        crs[idx] = course; spd[idx] = speed;
        idx = (idx + 1) % N; if (n < N) n++;
        if (n < 8) return;

        float sweep = 0, vmax = -1, vmin = 1e9f, cAtMax = 0, prev = 0;
        for (int i = 0; i < n; i++) {
            int a = (idx - n + i + N) % N;          // chronologische Reihenfolge
            float s = spd[a], c = crs[a];
            if (s > vmax) { vmax = s; cAtMax = c; }
            if (s < vmin) vmin = s;
            if (i > 0) { float d = c - prev; while (d > 180) d -= 360; while (d < -180) d += 360; sweep += fabsf(d); }
            prev = c;
        }
        // Mindestens ~ein voller Kreis im Puffer + echte Geschwindigkeitsdifferenz
        if (sweep >= 340.0f && vmax > vmin + 1.0f) {
            float ws = (vmax - vmin) * 0.5f;
            float wd = cAtMax + 180.0f; while (wd >= 360) wd -= 360; while (wd < 0) wd += 360;
            if (!valid) { wind_speed = ws; wind_dir = wd; valid = true; }
            else {
                wind_speed += 0.3f * (ws - wind_speed);                 // EMA Betrag
                float a = wind_dir * (float)M_PI / 180.0f, b = wd * (float)M_PI / 180.0f;
                float sx = 0.7f*sinf(a) + 0.3f*sinf(b), cx = 0.7f*cosf(a) + 0.3f*cosf(b);
                wind_dir = atan2f(sx, cx) * 180.0f / (float)M_PI;       // EMA Winkel (ueber sin/cos)
                if (wind_dir < 0) wind_dir += 360;
            }
        }
    }

private:
    static const int N = 40;     // ~40 s @1 Hz -> deckt 1-2 Kreise
    float crs[N], spd[N];
    int n = 0, idx = 0;
};
