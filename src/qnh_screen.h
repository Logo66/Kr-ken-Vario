#pragma once
#include "ui_utils.h"
#include <math.h>

// Buttons: -10 links, +10 rechts, freie Mitte x=240-720 (480px)
static const int QM_X=50, QP_X=740, QB_W=160, QB_H=120, QB_Y=200;
static const int QOK_X=380, QOK_Y=420, QOK_W=200, QOK_H=70;
// Mitte fuer Hoehe/QNH-Anzeige
static const int QMID_X=240, QMID_W=480;

enum QnhAction { QNH_NONE, QNH_PLUS, QNH_MINUS, QNH_OK };

static void showQnhScreen(EpdiyHighlevelState *hl, float alt, float qnh, float ppa) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    // Titel exakt zentriert
    drawHCenter(&ArialBold28, "QNH EINSTELLEN", 0, 960, 55, fb);
    uiHLine(30, 70, 900, fb);
    drawHCenter(&ArialBold16, "Bekannte Hoehe einstellen:", 0, 960, 100, fb);

    // [-10] Button
    uiBox(QM_X, QB_Y, QB_W, QB_H, fb);
    drawBoxCenter(&ArialBold28, "-10", QM_X, QB_Y, QB_W, QB_H, fb);

    // [+10] Button
    uiBox(QP_X, QB_Y, QB_W, QB_H, fb);
    drawBoxCenter(&ArialBold28, "+10", QP_X, QB_Y, QB_W, QB_H, fb);

    // Hoehe exakt zentriert in der Mitte (zwischen -10 und +10)
    snprintf(buf, 48, "%.0f m", alt);
    drawHCenter(&ArialBold28, buf, QMID_X, QMID_W, 200, fb);

    // QNH exakt zentriert
    snprintf(buf, 48, "QNH: %.0f hPa", qnh);
    drawHCenter(&ArialBold16, buf, QMID_X, QMID_W, 280, fb);

    // Druck exakt zentriert
    snprintf(buf, 48, "Druck: %.0f hPa", ppa / 100.0f);
    drawHCenter(&ArialBold16, buf, QMID_X, QMID_W, 310, fb);

    // [OK] Button exakt zentriert
    uiBox(QOK_X, QOK_Y, QOK_W, QOK_H, fb);
    drawBoxCenter(&ArialBold28, "OK", QOK_X, QOK_Y, QOK_W, QOK_H, fb);

    uiHLine(30, 510, 900, fb);
    drawHCenter(&ArialBold16, "+/- 10m pro Tap    OK = Speichern", 0, 960, 535, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

static QnhAction checkQnhTap(int tx, int ty) {
    if (tx>=QM_X && tx<QM_X+QB_W && ty>=QB_Y && ty<QB_Y+QB_H) return QNH_MINUS;
    if (tx>=QP_X && tx<QP_X+QB_W && ty>=QB_Y && ty<QB_Y+QB_H) return QNH_PLUS;
    if (tx>=QOK_X && tx<QOK_X+QOK_W && ty>=QOK_Y && ty<QOK_Y+QOK_H) return QNH_OK;
    return QNH_NONE;
}

static float calcQnhFromAlt(float ref_alt, float pressure_pa) {
    float base = 1.0f - ref_alt / 44330.0f;
    return (pressure_pa / powf(base, 5.255f)) / 100.0f;
}
