#pragma once
#include "ui_utils.h"

enum LandingChoice { LAND_NONE, LAND_OK, LAND_RIDE, LAND_HELP };

static const int LA_X=30, LA_W=900, LA_H=100;
static const int LA_OK_Y=100, LA_RIDE_Y=230, LA_HELP_Y=360;

static void showLandingScreen(EpdiyHighlevelState *hl, float alt, int h, int m) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    drawHCenter(&ArialBold28, "GELANDET", 0, 960, 55, fb);
    snprintf(buf, 48, "%.0f m  %02d:%02d", alt, h, m);
    drawHCenter(&ArialBold16, buf, 0, 960, 80, fb);
    uiHLine(30, 90, 900, fb);

    // Button 1: GUT GELANDET
    uiBox(LA_X, LA_OK_Y, LA_W, LA_H, fb);
    drawBoxCenter(&ArialBold28, "GUT GELANDET", LA_X, LA_OK_Y, LA_W, LA_H, fb);

    // Button 2: BRAUCHE RIDE
    uiBox(LA_X, LA_RIDE_Y, LA_W, LA_H, fb);
    drawBoxCenter(&ArialBold28, "BRAUCHE RIDE", LA_X, LA_RIDE_Y, LA_W, 70, fb);
    drawHCenter(&ArialBold16, "Mitfahrgelegenheit via FANET", LA_X, LA_W, LA_RIDE_Y+85, fb);

    // Button 3: BRAUCHE HILFE (invertiert)
    uiFill(LA_X, LA_HELP_Y, LA_W, LA_H, fb, 0);
    drawBoxCenter(&ArialBold28, "BRAUCHE HILFE", LA_X, LA_HELP_Y, LA_W, 70, fb, 0xFF);
    drawHCenter(&ArialBold16, "Notruf via FANET", LA_X, LA_W, LA_HELP_Y+85, fb, 0xFF);

    uiHLine(30, 480, 900, fb);
    drawHCenter(&ArialBold16, "Antippen zum Waehlen", 0, 960, 520, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

static LandingChoice checkLandingTap(int tx, int ty) {
    if (tx>=LA_X && tx<LA_X+LA_W && ty>=LA_OK_Y && ty<LA_OK_Y+LA_H) return LAND_OK;
    if (tx>=LA_X && tx<LA_X+LA_W && ty>=LA_RIDE_Y && ty<LA_RIDE_Y+LA_H) return LAND_RIDE;
    if (tx>=LA_X && tx<LA_X+LA_W && ty>=LA_HELP_Y && ty<LA_HELP_Y+LA_H) return LAND_HELP;
    return LAND_NONE;
}
