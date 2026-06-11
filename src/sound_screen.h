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

    // Hinweise (es fehlt die Haptik -> klar beschriften)
    drawHCenter(&ArialBold16, "Tippen = lauter   (nach 5 wieder STUMM)", 0, 960, 384, fb);
    drawHCenter(&ArialBold16, "Lang druecken = OK / schliessen",          0, 960, 418, fb);
    drawHCenter(&ArialBold16, "schliesst selbst nach 5 s",                0, 960, 452, fb);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}
