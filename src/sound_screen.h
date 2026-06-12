#pragma once
// sound_screen.h — Flug-Ton-Menue (Touch). Bewusst MINIMAL (Ticket §2):
//   Tipp        = Lautstaerke eine Stufe weiter (0=STUMM .. 5, mit Umlauf)
//   Lang druecken = OK / schliessen
//   schliesst selbst nach 5 s Inaktivitaet
// Volle Konfig (Schwellen/Tonkurve/Totzone) gehoert NICHT hierher -> Web/App (Boden).
#include "ui_utils.h"
#include "sound_settings.h"
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"
#include "buzzer.h"          // Ton-Test (Frequenzen durchtippen, lauteste raushoeren)

// Ton-Test-Button-Zone (unten) — Tippen spielt die naechste Test-Frequenz
static const int SND_TEST_X = 250, SND_TEST_Y = 330, SND_TEST_W = 460, SND_TEST_H = 70;
static bool inSoundTest(int x, int y) {
    return x >= SND_TEST_X && x < SND_TEST_X+SND_TEST_W && y >= SND_TEST_Y && y < SND_TEST_Y+SND_TEST_H;
}

static void showSoundScreen(EpdiyHighlevelState *hl, enum EpdDrawMode mode = MODE_DU) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    // Titel
    drawHCenter(&ArialBold28, "TON", 0, 960, 96, fb);
    uiHLine(280, 116, 400, fb);

    if (g_sound.volume == 0) {
        drawHCenter(&ArialBold40, "STUMM", 0, 960, 250, fb);
    } else {
        snprintf(buf, 32, "LAUTSTAERKE %d / %d", g_sound.volume, SND_VOL_MAX);
        drawHCenter(&ArialBold28, buf, 0, 960, 178, fb);
        // Balken: SND_VOL_MAX Segmente, mittig
        const int seg = 80, gap = 10;
        const int total = SND_VOL_MAX*seg + (SND_VOL_MAX-1)*gap;
        int bx = (960 - total) / 2, by = 210, bh = 92;
        for (int i = 0; i < SND_VOL_MAX; i++) {
            int x = bx + i*(seg + gap);
            if (i < g_sound.volume) uiFill(x, by, seg, bh, fb);   // gefuellt
            else                    uiBox (x, by, seg, bh, fb);   // leer
        }
    }

    // === TON-TEST-Button — Frequenzen durchtippen, lauteste raushoeren ===
    uiBox(SND_TEST_X, SND_TEST_Y, SND_TEST_W, SND_TEST_H, fb);
    uint32_t tf = buzzerTestCurrentFreq();
    if (tf) snprintf(buf, 32, "TON-TEST  %lu Hz", (unsigned long)tf);
    else    snprintf(buf, 32, "TON-TEST  (tippen)");
    drawBoxCenter(&ArialBold24, buf, SND_TEST_X, SND_TEST_Y, SND_TEST_W, SND_TEST_H, fb);

    // Hinweise (kompakt)
    drawHCenter(&ArialBold16, "oben tippen = lauter  .  lang druecken = OK / zu", 0, 960, 432, fb);
    drawHCenter(&ArialBold16, "TON-TEST tippen = naechste Frequenz hoeren",       0, 960, 464, fb);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}
