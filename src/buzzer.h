#pragma once
// buzzer.h — Arduino Modulino Buzzer (ABX00108) ueber I2C 0x1E.
// Protokoll (aus Arduino_Modulino, ModulinoBuzzer::tone): 8 Byte schreiben, KEIN Register-Byte:
//   [freq u32 LE][dauer_ms u32 LE].  freq=0 -> Stopp. dauer=0 -> Dauerton bis Stopp.
// Hinweis: Piezo = feste Lautstaerke (nur Frequenz/Dauer steuerbar, kein Volume).
#include "driver/i2c.h"

#define BUZZER_ADDR 0x1E

static void buzzerTone(uint32_t freq, uint32_t dur_ms) {
    uint8_t b[8] = {
        (uint8_t)(freq),        (uint8_t)(freq >> 8),    (uint8_t)(freq >> 16),    (uint8_t)(freq >> 24),
        (uint8_t)(dur_ms),      (uint8_t)(dur_ms >> 8),  (uint8_t)(dur_ms >> 16),  (uint8_t)(dur_ms >> 24)
    };
    i2c_master_write_to_device(I2C_NUM_0, BUZZER_ADDR, b, 8, pdMS_TO_TICKS(50));
}

static void buzzerStop() { buzzerTone(0, 0); }

// START-JINGLE — langsamer, geerdeter E-BLUES ueber das ganze Frequenzband (nicht giftig).
// Bogen: warmer tiefer Grundton -> bluesiger Aufstieg (mit Blue Note) -> hoerbare Antwort
// oben im lauten Band -> Aufloesung zurueck nach unten auf den langen Grundton (sicher geerdet).
static void buzzerStartup() {
    struct Note { uint16_t f, d; };   // E-Blues: E G A Bb B  (tief 659 ... laut 3136)
    static const Note jingle[] = {
        { 659, 400},                                  // E5 — warmer Grund
        {   0, 100},
        { 784, 220}, { 880, 220}, { 932, 190}, { 988, 380},   // G A Bb B — bluesiger Aufstieg (Bb = Blue Note)
        {   0, 110},
        {2349, 260}, {2637, 400},                     // D7 -> E7 — hoerbare Antwort oben (lautes Band)
        {3136, 230}, {2637, 360},                     // G7 -> E7 — bluesige Spitze, zurueck zum Grundton
        {   0, 120},
        { 988, 230}, { 880, 230}, { 784, 270},        // B A G — ruhig runter
        { 659, 640}                                   // E5 — tief, lang: sicher geerdet
    };
    for (const Note &n : jingle) {
        if (n.f) buzzerTone(n.f, n.d);
        delay(n.d + 14);                              // sanfte, fast legato Phrasierung (kein Stakkato)
    }
    buzzerStop();
}
