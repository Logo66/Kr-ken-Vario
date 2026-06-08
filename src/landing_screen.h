#pragma once
// landing_screen.h — Lande-Abfrage nach automatischer Lande-Erkennung
// Grosse Touch-Buttons: Gut gelandet / Brauche Ride / Brauche Hilfe
// 1-Bit S/W, fette Fonts
#include "epdiy.h"
#include "epd_highlevel.h"
#include <string.h>
#include <stdio.h>
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

enum LandingChoice { LAND_NONE, LAND_OK, LAND_RIDE, LAND_HELP };

// Touch-Bereiche (fuer Tap-Erkennung)
struct TouchArea { int x, y, w, h; };
static const TouchArea AREA_OK   = {30,  100, 900, 100};
static const TouchArea AREA_RIDE = {30,  230, 900, 100};
static const TouchArea AREA_HELP = {30,  360, 900, 100};

static bool inArea(int tx, int ty, const TouchArea &a) {
    return tx >= a.x && tx < a.x+a.w && ty >= a.y && ty < a.y+a.h;
}

// Draw-Helpers
static void LT(const EpdFont *f, const char *s, int x, int y, uint8_t *fb) {
    int cx=x, cy=y; EpdFontProperties p=epd_font_properties_default(); p.fg_color=0;
    epd_write_string(f,s,&cx,&cy,fb,&p);
}
static void LH(int x,int y,int w,uint8_t *fb) { EpdRect r={x,y,w,2}; epd_fill_rect(r,0,fb); }
static void LF(int x,int y,int w,int h,uint8_t *fb) { EpdRect r={x,y,w,h}; epd_fill_rect(r,0,fb); }
static void LB(int x,int y,int w,int h,uint8_t *fb) {
    // Dicker Rahmen (4px)
    LF(x,y,w,4,fb); LF(x,y+h-4,w,4,fb); LF(x,y,4,h,fb); LF(x+w-4,y,4,h,fb);
}

static void showLandingScreen(EpdiyHighlevelState *hl, float altitude,
                               int hours, int minutes) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    // Titel
    LT(&ArialBold28, "GELANDET", 350, 50, fb);
    snprintf(buf, 48, "%.0f m  %02d:%02d", altitude, hours, minutes);
    LT(&ArialBold16, buf, 370, 80, fb);

    LH(30, 90, 900, fb);

    // === Button 1: GUT GELANDET ===
    LB(AREA_OK.x, AREA_OK.y, AREA_OK.w, AREA_OK.h, fb);
    LT(&ArialBold40, "GUT GELANDET", 200, 165, fb);

    // === Button 2: BRAUCHE RIDE ===
    LB(AREA_RIDE.x, AREA_RIDE.y, AREA_RIDE.w, AREA_RIDE.h, fb);
    LT(&ArialBold40, "BRAUCHE RIDE", 200, 295, fb);
    LT(&ArialBold16, "Mitfahrgelegenheit via FANET", 280, 320, fb);

    // === Button 3: BRAUCHE HILFE ===
    // Invertiert: schwarzer Hintergrund, weisser Text
    LF(AREA_HELP.x, AREA_HELP.y, AREA_HELP.w, AREA_HELP.h, fb);
    EpdFontProperties wp = epd_font_properties_default();
    wp.fg_color = 0xFF;  // Weiss auf schwarz
    int cx=200, cy=430;
    epd_write_string(&ArialBold40, "BRAUCHE HILFE", &cx, &cy, fb, &wp);
    cx=300; cy=450;
    epd_write_string(&ArialBold16, "Notruf via FANET", &cx, &cy, fb, &wp);

    // Footer
    LH(30, 490, 900, fb);
    LT(&ArialBold16, "Antippen zum Waehlen", 340, 520, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

// Pruefe ob Tap in einem Button-Bereich liegt
static LandingChoice checkLandingTap(int tx, int ty) {
    if (inArea(tx, ty, AREA_OK))   return LAND_OK;
    if (inArea(tx, ty, AREA_RIDE)) return LAND_RIDE;
    if (inArea(tx, ty, AREA_HELP)) return LAND_HELP;
    return LAND_NONE;
}
