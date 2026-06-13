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

// Test-Frequenzen (zum Raushoeren der lautesten = Piezo-Resonanz).
static const uint32_t g_testFreqs[] = { 1500, 1800, 2100, 2400, 2700, 3000, 3300, 3700, 4000 };
static const int      g_testFreqCount = 9;
static int            g_testFreqIdx = -1;

// Naechste Test-Frequenz spielen (0.5 s) und zurueckgeben.
static uint32_t buzzerTestNext() {
    g_testFreqIdx = (g_testFreqIdx + 1) % g_testFreqCount;
    uint32_t f = g_testFreqs[g_testFreqIdx];
    buzzerTone(f, 500);
    return f;
}
static uint32_t buzzerTestCurrentFreq() { return (g_testFreqIdx < 0) ? 0 : g_testFreqs[g_testFreqIdx]; }

// START-JINGLE — kurzer rockiger Riff beim Boot, bewusst im lauten Piezo-Band (~2400-3280 Hz).
// Bogen: Gallop rauf -> Peak -> Hammer-Lick zur Spitze -> bluesiger Abstieg -> Doppel-Slam-Finale.
static void buzzerStartup() {
    struct Note { uint16_t f, d; };
    static const Note jingle[] = {
        {2400,  70}, {2400,  70}, {2700,  70}, {3000, 200},   // Gallop rauf -> Peak
        {   0,  60},
        {2850,  70}, {3000,  70}, {3280, 230},                // Hammer-Lick zur Spitze
        {   0,  80},
        {3000,  90}, {2700,  90}, {2550,  70}, {2400, 170},   // bluesiger Abstieg
        {   0,  90},
        {3280, 120}, {0, 45}, {3280, 340}                     // Doppel-Slam-Finale
    };
    for (const Note &n : jingle) {
        if (n.f) buzzerTone(n.f, n.d);
        delay(n.d + 22);                                      // ausklingen + kleine Artikulations-Pause
    }
    buzzerStop();
}
