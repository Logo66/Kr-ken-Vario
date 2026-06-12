#pragma once
// vario_sound.h — Steigton ueber den Modulino-Buzzer, gesteuert vom Vario-Wert.
//   Steigen >= climb_th : Beep-Ton, Tonhoehe UND Takt steigen mit dem Steigen.
//   Sinken  <= sink_th  : tiefer Dauerton (Warnung).
//   Totzone dazwischen / Stumm (Lautstaerke 0) : still.
// Modulino-Piezo hat KEINE Lautstaerke -> wir bleiben im lauten oberen Frequenzband
// (Resonanz, per Boot-Sweep gefunden). FREQ_LOW/FREQ_HIGH ggf. nach Gehoer anpassen.
#include "buzzer.h"

class VarioSound {
public:
    // Lautes Steig-Band (Piezo-Resonanz ~2-3 kHz). Nach dem Boot-Sweep ggf. nachziehen.
    static const uint32_t FREQ_LOW  = 2700;   // schwaches Steigen (gemessene Resonanz)
    static const uint32_t FREQ_HIGH = 3000;   // starkes Steigen (lauteste gemessene Freq)
    static const uint32_t FREQ_SINK = 2400;   // Sink-Warnung (Dauerton, unterster der lauten Toene)

    void update(float vario, bool muted, float climb_th, float sink_th) {
        if (muted) { silence(); return; }

        if (vario >= climb_th) {
            if (_state == 2) buzzerStop();                 // war Sink-Dauerton -> aus
            _state = 1;
            float c = vario; if (c > 6.0f) c = 6.0f;       // 0..6 m/s gemappt
            float k = c / 6.0f;
            uint32_t freq = FREQ_LOW + (uint32_t)((FREQ_HIGH - FREQ_LOW) * k);
            unsigned long period = (unsigned long)(620 - 480 * k); // 620ms (schwach) .. 140ms (stark)
            if (period < 130) period = 130;
            if (millis() - _lastBeep >= period) {
                _lastBeep = millis();
                uint32_t len = period / 2; if (len > 130) len = 130;
                buzzerTone(freq, len);
            }
        } else if (vario <= sink_th) {
            if (_state != 2) { buzzerTone(FREQ_SINK, 0); _state = 2; }  // 0 = Dauerton bis Stopp
        } else {
            silence();                                                 // Totzone
        }
    }

private:
    int _state = 0;                 // 0=still, 1=Steig-Beep, 2=Sink-Dauerton
    unsigned long _lastBeep = 0;
    void silence() { if (_state) { buzzerStop(); _state = 0; } }
};
