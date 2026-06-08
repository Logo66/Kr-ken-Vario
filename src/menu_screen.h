#pragma once
// menu_screen.h — 6-Button Menu (3x2 Grid, 20x20mm pro Button)
// Display: 960x540px = 100.8x56.7mm → 9.52 px/mm
// Button: 190x190px = 20x20mm, Gap: 30px = 3mm
#include "epdiy.h"
#include "epd_highlevel.h"
#include "esp_sleep.h"
#include <string.h>
#include <stdio.h>
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

enum MenuItem { MENU_NONE, MENU_QNH, MENU_BACKLIGHT, MENU_FLUGBUCH,
                MENU_SLOT4, MENU_SLOT5, MENU_AUS };

static const int MBTN = 190, MGAP = 30;
static const int MX0 = 165, MY0 = 70;

struct MBtn { int col, row; MenuItem item; const char *line1; const char *line2; };
static const MBtn MBTNS[] = {
    {0, 0, MENU_QNH,       "QNH",      NULL},
    {1, 0, MENU_BACKLIGHT,  "LICHT",    NULL},
    {2, 0, MENU_FLUGBUCH,   "FLUG-",    "BUCH"},
    {0, 1, MENU_SLOT4,      "---",      NULL},
    {1, 1, MENU_SLOT5,      "---",      NULL},
    {2, 1, MENU_AUS,        "AUS",      NULL},
};
static const int MBTN_COUNT = 6;

static int mbtnX(int col) { return MX0 + col * (MBTN + MGAP); }
static int mbtnY(int row) { return MY0 + row * (MBTN + MGAP); }

static void _mt(const EpdFont *f, const char *s, int x, int y, uint8_t *fb) {
    int cx=x, cy=y; EpdFontProperties p=epd_font_properties_default(); p.fg_color=0;
    epd_write_string(f,s,&cx,&cy,fb,&p);
}
static void _mf(int x,int y,int w,int h,uint8_t *fb) { EpdRect r={x,y,w,h}; epd_fill_rect(r,0,fb); }
// Zentrierter Text in Box (approximiert)
static void _mc(const EpdFont *f, const char *s, int bx, int bw, int y, uint8_t *fb) {
    int approx_w = strlen(s) * 22; // ArialBold40 ~22px/char
    if (f == &ArialBold28) approx_w = strlen(s) * 16;
    if (f == &ArialBold16) approx_w = strlen(s) * 10;
    int cx = bx + (bw - approx_w) / 2;
    _mt(f, s, cx, y, fb);
}

static void showMenuScreen(EpdiyHighlevelState *hl, float current_qnh,
                            bool bl_on, int flight_count) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    _mc(&ArialBold28, "MENU", 0, 960, 45, fb);

    for (int i = 0; i < MBTN_COUNT; i++) {
        int x = mbtnX(MBTNS[i].col);
        int y = mbtnY(MBTNS[i].row);

        // Rahmen (4px)
        _mf(x,y,MBTN,4,fb); _mf(x,y+MBTN-4,MBTN,4,fb);
        _mf(x,y,4,MBTN,fb); _mf(x+MBTN-4,y,4,MBTN,fb);

        // Zeile 1 (gross, zentriert)
        if (MBTNS[i].line2) {
            // Zweizeilig
            _mc(&ArialBold28, MBTNS[i].line1, x, MBTN, y + 80, fb);
            _mc(&ArialBold28, MBTNS[i].line2, x, MBTN, y + 115, fb);
        } else {
            _mc(&ArialBold40, MBTNS[i].line1, x, MBTN, y + 105, fb);
        }

        // Spezial-Info unterhalb
        if (MBTNS[i].item == MENU_QNH) {
            snprintf(buf, 32, "%.0f hPa", current_qnh);
            _mc(&ArialBold16, buf, x, MBTN, y + 140, fb);
        }
        if (MBTNS[i].item == MENU_BACKLIGHT) {
            _mc(&ArialBold16, bl_on ? "AN" : "AUS", x, MBTN, y + 140, fb);
        }
        if (MBTNS[i].item == MENU_FLUGBUCH) {
            snprintf(buf, 32, "%d Fluege", flight_count);
            _mc(&ArialBold16, buf, x, MBTN, y + 145, fb);
        }
        if (MBTNS[i].item == MENU_SLOT4 || MBTNS[i].item == MENU_SLOT5) {
            _mc(&ArialBold16, "(frei)", x, MBTN, y + 140, fb);
        }
    }

    _mc(&ArialBold16, "Antippen | Wischen = zurueck", 0, 960, 530, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

static MenuItem checkMenuTap(int tx, int ty) {
    for (int i = 0; i < MBTN_COUNT; i++) {
        int x = mbtnX(MBTNS[i].col);
        int y = mbtnY(MBTNS[i].row);
        if (tx >= x && tx < x+MBTN && ty >= y && ty < y+MBTN)
            return MBTNS[i].item;
    }
    return MENU_NONE;
}

// Credits + Shutdown
static void showCreditsAndShutdown(EpdiyHighlevelState *hl) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    int fs = epd_width()/2 * epd_height();
    memset(fb, 0x00, fs);
    EpdFontProperties wp = epd_font_properties_default(); wp.fg_color = 0xFF;
    int cx, cy;
    cx=350; cy=150; epd_write_string(&ArialBold40, "AURA", &cx, &cy, fb, &wp);
    cx=260; cy=210; epd_write_string(&ArialBold28, "Paragliding Vario", &cx, &cy, fb, &wp);
    cx=280; cy=290; epd_write_string(&ArialBold16, "KIE Engineering", &cx, &cy, fb, &wp);
    cx=240; cy=320; epd_write_string(&ArialBold16, "www.kie-engineering.com", &cx, &cy, fb, &wp);
    cx=240; cy=350; epd_write_string(&ArialBold16, "info@kie-engineering.com", &cx, &cy, fb, &wp);
    cx=220; cy=430; epd_write_string(&ArialBold16, "Das erste KI-entwickelte Vario", &cx, &cy, fb, &wp);
    cx=260; cy=460; epd_write_string(&ArialBold16, "Entwickelt mit Claude Code", &cx, &cy, fb, &wp);
    epd_poweron(); epd_hl_update_screen(hl, MODE_GC16, 20); epd_poweroff();
    delay(7000);
    epd_poweron(); epd_clear(); epd_poweroff();
    delay(500);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}
