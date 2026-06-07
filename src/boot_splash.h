#pragma once
// boot_splash.h — AURA boot logo INVERTIERT (weiss auf schwarz)
#include "epdiy.h"
#include "epd_highlevel.h"
#include "boot_logo.h"

static constexpr int LOGO_X = (960 - BOOT_LOGO_W) / 2;
static constexpr int LOGO_Y = (540 - BOOT_LOGO_H) / 2;

// Gamma S-Kurve (invertiert: 0=weiss auf Panel, 15=schwarz auf Panel)
static const uint8_t GAMMA_INV[16] = {
    255, 244, 230, 213, 194, 174, 153, 132, 110, 90, 72, 55, 38, 22, 10, 0
};

/// Invertierter Splash: schwarzer Hintergrund, weisses Logo
static void showBootSplash(EpdiyHighlevelState *hl, const char *version) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);

    // Ganzen Framebuffer SCHWARZ fuellen (0x00)
    int fb_size = 960 / 2 * 540;  // 4bpp packed
    memset(fb, 0x00, fb_size);
    // Auch den Back-Buffer schwarz setzen (verhindert Negativ-Flash beim Uebergang)
    // epd_hl_set_all_white setzt beide auf weiss — wir brauchen beide auf schwarz
    // Trick: set_all_white + memset front = nur front ist schwarz
    // Besser: direkt beide Buffer setzen
    epd_hl_set_all_white(hl);     // back = weiss
    memset(fb, 0x00, fb_size);    // front = schwarz

    // Logo invertiert blitten (weiss auf schwarz)
    for (int y = 0; y < BOOT_LOGO_H; y++) {
        for (int x = 0; x < BOOT_LOGO_W; x++) {
            uint8_t lvl = boot_logo[y * BOOT_LOGO_W + x];
            uint8_t grey = GAMMA_INV[lvl];  // invertiert: dunkel→hell
            epd_draw_pixel(LOGO_X + x, LOGO_Y + y, grey, fb);
        }
    }

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}
