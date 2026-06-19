#pragma once
#include "ui_utils.h"

enum MenuItem { MENU_NONE, MENU_QNH, MENU_BACKLIGHT, MENU_FLUGBUCH,
                MENU_FUNK, MENU_KARTE, MENU_AUS };

// 2 Spalten x 3 Reihen, breite Kaesten -> Text passt locker rein.
static const int MBW=410, MBH=125, MGX=40, MGY=20, MX0=50, MY0=90;
static int mbX(int c){return MX0 + c*(MBW+MGX);}
static int mbY(int r){return MY0 + r*(MBH+MGY);}

static void drawMBtn(int c, int r, const char *label, const char *info, uint8_t *fb) {
    int x = mbX(c), y = mbY(r);
    uiBox(x, y, MBW, MBH, fb);
    if (info) {                                          // Label + kleine Info, mittig als Block
        drawHCenter(&ArialBold24, label, x, MBW, y + MBH/2 - 6, fb);
        drawHCenter(&ArialBold16, info,  x, MBW, y + MBH/2 + 28, fb);
    } else {                                             // nur Label, voll zentriert
        drawBoxCenter(&ArialBold24, label, x, y, MBW, MBH, fb);
    }
}

static void showMenuScreen(EpdiyHighlevelState *hl, float qnh, bool bl, int fc,
                           enum EpdDrawMode mode = MODE_DU) {
    (void)qnh; (void)fc;
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);

    drawHCenter(&ArialBold28, "MENU", 0, 960, 48, fb);
    uiHLine(40, 66, 880, fb);

    drawMBtn(0, 0, "SENSOR KALIB.", NULL, fb);
    drawMBtn(1, 0, "LICHT", bl ? "AN" : "AUS", fb);
    drawMBtn(0, 1, "FLUGBUCH", NULL, fb);
    drawMBtn(1, 1, "FUNK", "WLAN/BLE", fb);
    drawMBtn(0, 2, "KARTE", NULL, fb);
    drawMBtn(1, 2, "AUS", NULL, fb);

    drawHCenter(&ArialBold16, "Antippen  |  Wischen = zurueck", 0, 960, 525, fb);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}

static MenuItem checkMenuTap(int tx, int ty) {
    MenuItem items[] = {MENU_QNH, MENU_BACKLIGHT, MENU_FLUGBUCH,
                        MENU_FUNK, MENU_KARTE, MENU_AUS};
    int cols[] = {0,1,0,1,0,1}, rows[] = {0,0,1,1,2,2};
    for (int i = 0; i < 6; i++) {
        int x = mbX(cols[i]), y = mbY(rows[i]);
        if (tx >= x && tx < x+MBW && ty >= y && ty < y+MBH) return items[i];
    }
    return MENU_NONE;
}

static void showCreditsAndShutdown(EpdiyHighlevelState *hl) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);   // INVERTIERT: weisser Hintergrund (E-Paper sauber, kein schwarzes Ghosting)
    drawHCenter(&ArialBold40, "AURA", 0, 960, 180, fb);   // schwarze Schrift (Default 0x00)
    drawHCenter(&ArialBold28, "Paragliding Vario", 0, 960, 240, fb);
    drawHCenter(&ArialBold16, "KIE Engineering", 0, 960, 310, fb);
    drawHCenter(&ArialBold16, "www.kie-engineering.com", 0, 960, 340, fb);
    drawHCenter(&ArialBold16, "info@kie-engineering.com", 0, 960, 370, fb);
    drawHCenter(&ArialBold16, "Das erste KI-entwickelte Vario", 0, 960, 440, fb);
    epd_poweron(); epd_hl_update_screen(hl, MODE_GC16, 20); epd_poweroff();
    delay(7000);
    epd_poweron(); epd_clear(); epd_poweroff(); delay(500);
    // Power-Off (Ship-Mode) macht der Aufrufer in main.cpp (MENU_AUS) -- braucht den BQ25896 (ppm),
    // der hier nicht erreichbar ist (menu_screen.h wird vor der ppm-Deklaration inkludiert).
}
