#pragma once
// sound_screen.h — Flug-Ton-Menue (Touch):
//   Tipp ausserhalb der Tastatur = Lautstaerke eine Stufe weiter (0=STUMM .. 5, Umlauf)
//   Tipp auf eine Klaviertaste    = Ton spielen (Gimmick)
//   Lang druecken = OK / schliessen · schliesst selbst nach 5 s Inaktivitaet
#include "ui_utils.h"
#include "sound_settings.h"
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"
#include "buzzer.h"          // buzzerTone (Klaviertasten)

// === Mini-Klavier (Gimmick) — eine Oktave C7..C8, bewusst im lauten Piezo-Band ===
static const int      KB_X = 60, KB_Y = 326, KB_W = 840, KB_WH = 150;
static const int      KB_NWHITE = 8;
static const uint16_t KB_WHITE[8] = { 2093, 2349, 2637, 2794, 3136, 3520, 3951, 4186 };  // C7 D7 E7 F7 G7 A7 H7 C8
static const char*    KB_WLBL [8] = { "C","D","E","F","G","A","H","C" };
static const int      KB_BAFTER[5] = { 0, 1, 3, 4, 5 };                                   // schwarze nach welcher weissen
static const uint16_t KB_BLACK [5] = { 2217, 2489, 2960, 3322, 3729 };                    // C# D# F# G# A#
static const int      KB_BW = 62, KB_BH = 92;
static inline int kbWW() { return KB_W / KB_NWHITE; }                                     // 105

// Welche Taste am Punkt? -> Frequenz (0 = keine). Schwarze haben Vorrang (liegen oben).
static uint16_t soundPianoFreqAt(int x, int y) {
    if (y < KB_Y || y >= KB_Y + KB_WH || x < KB_X || x >= KB_X + KB_W) return 0;
    int ww = kbWW();
    if (y < KB_Y + KB_BH) {
        for (int i = 0; i < 5; i++) {
            int bx = KB_X + (KB_BAFTER[i]+1)*ww - KB_BW/2;
            if (x >= bx && x < bx + KB_BW) return KB_BLACK[i];
        }
    }
    int wi = (x - KB_X) / ww;
    return (wi >= 0 && wi < KB_NWHITE) ? KB_WHITE[wi] : 0;
}

static void drawPiano(uint8_t *fb) {
    int ww = kbWW();
    for (int i = 0; i < KB_NWHITE; i++) {
        int x = KB_X + i*ww;
        uiBox(x, KB_Y, ww, KB_WH, fb);
        drawHCenter(&ArialBold16, KB_WLBL[i], x, ww, KB_Y + KB_WH - 14, fb);   // Notenname unten
    }
    for (int i = 0; i < 5; i++) {
        int bx = KB_X + (KB_BAFTER[i]+1)*ww - KB_BW/2;
        uiFill(bx, KB_Y, KB_BW, KB_BH, fb);                                    // schwarze Taste
    }
}

static void showSoundScreen(EpdiyHighlevelState *hl, enum EpdDrawMode mode = MODE_DU) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    drawHCenter(&ArialBold28, "TON", 0, 960, 84, fb);
    uiHLine(280, 102, 400, fb);

    if (g_sound.volume == 0) {
        drawHCenter(&ArialBold40, "STUMM", 0, 960, 212, fb);
    } else {
        snprintf(buf, 32, "LAUTSTAERKE %d / %d", g_sound.volume, SND_VOL_MAX);
        drawHCenter(&ArialBold28, buf, 0, 960, 150, fb);
        const int seg = 80, gap = 10;
        const int total = SND_VOL_MAX*seg + (SND_VOL_MAX-1)*gap;
        int bx = (960 - total) / 2, by = 180, bh = 78;
        for (int i = 0; i < SND_VOL_MAX; i++) {
            int x = bx + i*(seg + gap);
            if (i < g_sound.volume) uiFill(x, by, seg, bh, fb);
            else                    uiBox (x, by, seg, bh, fb);
        }
    }

    drawHCenter(&ArialBold16, "Tasten spielen   -   ausserhalb tippen = lauter   -   lang druecken = zu", 0, 960, 300, fb);
    drawPiano(fb);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}
