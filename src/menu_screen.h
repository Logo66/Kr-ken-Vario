#pragma once
#include "ui_utils.h"

enum MenuItem { MENU_NONE, MENU_QNH, MENU_BACKLIGHT, MENU_FLUGBUCH,
                MENU_FUNK, MENU_SLOT5, MENU_AUS };

static const int MBTN=190, MGAP=30, MX0=165, MY0=75;
static int mbX(int c){return MX0+c*(MBTN+MGAP);}
static int mbY(int r){return MY0+r*(MBTN+MGAP);}

static void drawMBtn(int c, int r, const char *label, const char *label2,
                     const char *info, uint8_t *fb) {
    int x = mbX(c), y = mbY(r);
    uiBox(x, y, MBTN, MBTN, fb);

    if (label2) {
        // Zweizeilig: ArialBold16, zentriert mit Abstand
        drawHCenter(&ArialBold16, label, x, MBTN, y + 85, fb);
        drawHCenter(&ArialBold16, label2, x, MBTN, y + 108, fb);
    } else {
        // Einzeilig: zentriert in voller Box-Hoehe
        drawBoxCenter(&ArialBold28, label, x, y, MBTN, MBTN, fb);
    }

    if (info) {
        drawHCenter(&ArialBold16, info, x, MBTN, y + 165, fb);
    }
}

static void showMenuScreen(EpdiyHighlevelState *hl, float qnh, bool bl, int fc) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    drawHCenter(&ArialBold28, "MENU", 0, 960, 55, fb);

    snprintf(buf, 32, "%.0f hPa", qnh);
    drawMBtn(0, 0, "QNH", NULL, buf, fb);
    drawMBtn(1, 0, "LICHT", NULL, bl ? "AN" : "AUS", fb);
    drawMBtn(2, 0, "FLUG", "BUCH", NULL, fb);
    drawMBtn(0, 1, "FUNK", NULL, "FANET/BLE", fb);
    drawMBtn(1, 1, "---", NULL, "(frei)", fb);
    drawMBtn(2, 1, "AUS", NULL, NULL, fb);

    drawHCenter(&ArialBold16, "Antippen | Wischen = zurueck", 0, 960, 530, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static MenuItem checkMenuTap(int tx, int ty) {
    MenuItem items[] = {MENU_QNH, MENU_BACKLIGHT, MENU_FLUGBUCH,
                        MENU_FUNK, MENU_SLOT5, MENU_AUS};
    int cols[] = {0,1,2,0,1,2}, rows[] = {0,0,0,1,1,1};
    for (int i = 0; i < 6; i++) {
        int x = mbX(cols[i]), y = mbY(rows[i]);
        if (tx >= x && tx < x+MBTN && ty >= y && ty < y+MBTN) return items[i];
    }
    return MENU_NONE;
}

static void showCreditsAndShutdown(EpdiyHighlevelState *hl) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    memset(fb, 0x00, epd_width()/2 * epd_height());
    drawHCenter(&ArialBold40, "AURA", 0, 960, 180, fb, 0xFF);
    drawHCenter(&ArialBold28, "Paragliding Vario", 0, 960, 240, fb, 0xFF);
    drawHCenter(&ArialBold16, "KIE Engineering", 0, 960, 310, fb, 0xFF);
    drawHCenter(&ArialBold16, "www.kie-engineering.com", 0, 960, 340, fb, 0xFF);
    drawHCenter(&ArialBold16, "info@kie-engineering.com", 0, 960, 370, fb, 0xFF);
    drawHCenter(&ArialBold16, "Das erste KI-entwickelte Vario", 0, 960, 440, fb, 0xFF);
    epd_poweron(); epd_hl_update_screen(hl, MODE_GC16, 20); epd_poweroff();
    delay(7000);
    epd_poweron(); epd_clear(); epd_poweroff(); delay(500);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}
