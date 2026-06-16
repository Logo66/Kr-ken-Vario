#pragma once
#include "ui_utils.h"
#include "imu.h"      // ImuCal
#include <math.h>

// SENSOR-KALIBRIERUNG: oben QNH (Hoehe einstellen), unten Kompass (Status + Norden setzen + Speichern).
static const int QM_X=60,   QOK_X=395, QP_X=730, QB_W=170, QB_H=85, QB_Y=140;   // -10 / QNH OK / +10
static const int QNORTH_X=60, QCAL_X=500, QBOT_Y=425, QBOT_W=400, QBOT_H=80;    // NORDEN SETZEN / KAL. SPEICHERN

enum QnhAction { QNH_NONE, QNH_PLUS, QNH_MINUS, QNH_OK, QNH_CALSAVE, QNH_SETNORTH };

static void showQnhScreen(EpdiyHighlevelState *hl, float alt, float qnh, float ppa, ImuCal cal, float heading) {
    (void)ppa;
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[64];

    drawHCenter(&ArialBold28, "SENSOR KALIBRIERUNG", 0, 960, 44, fb);
    uiHLine(40, 66, 880, fb);

    // --- QNH / Hoehe ---
    snprintf(buf, 64, "Hoehe %.0f m       QNH %.0f hPa", alt, qnh);
    drawHCenter(&ArialBold24, buf, 0, 960, 108, fb);
    uiBox(QM_X,  QB_Y, QB_W, QB_H, fb); drawBoxCenter(&ArialBold28, "-10",    QM_X,  QB_Y, QB_W, QB_H, fb);
    uiBox(QOK_X, QB_Y, QB_W, QB_H, fb); drawBoxCenter(&ArialBold28, "OK", QOK_X, QB_Y, QB_W, QB_H, fb);
    uiBox(QP_X,  QB_Y, QB_W, QB_H, fb); drawBoxCenter(&ArialBold28, "+10",    QP_X,  QB_Y, QB_W, QB_H, fb);

    uiHLine(40, 258, 880, fb);

    // --- Kompass ---
    snprintf(buf, 64, "KOMPASS    %.0f Grad", heading);
    drawHCenter(&ArialBold28, buf, 0, 960, 300, fb);
    snprintf(buf, 64, "Sys %d      Gyro %d      Acc %d      Mag %d", cal.sys, cal.gyro, cal.accel, cal.mag);
    drawHCenter(&ArialBold24, buf, 0, 960, 350, fb);
    drawHCenter(&ArialBold16, "Acht-Figur bewegen bis Mag = 3", 0, 960, 392, fb);

    uiBox(QNORTH_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    drawBoxCenter(&ArialBold16, "NORDEN SETZEN", QNORTH_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    uiBox(QCAL_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    drawBoxCenter(&ArialBold16, "KAL. SPEICHERN", QCAL_X, QBOT_Y, QBOT_W, QBOT_H, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static QnhAction checkQnhTap(int tx, int ty) {
    if (tx>=QM_X     && tx<QM_X+QB_W     && ty>=QB_Y   && ty<QB_Y+QB_H)     return QNH_MINUS;
    if (tx>=QP_X     && tx<QP_X+QB_W     && ty>=QB_Y   && ty<QB_Y+QB_H)     return QNH_PLUS;
    if (tx>=QOK_X    && tx<QOK_X+QB_W    && ty>=QB_Y   && ty<QB_Y+QB_H)     return QNH_OK;
    if (tx>=QNORTH_X && tx<QNORTH_X+QBOT_W && ty>=QBOT_Y && ty<QBOT_Y+QBOT_H) return QNH_SETNORTH;
    if (tx>=QCAL_X   && tx<QCAL_X+QBOT_W && ty>=QBOT_Y && ty<QBOT_Y+QBOT_H) return QNH_CALSAVE;
    return QNH_NONE;
}

static float calcQnhFromAlt(float ref_alt, float pressure_pa) {
    float base = 1.0f - ref_alt / 44330.0f;
    return (pressure_pa / powf(base, 5.255f)) / 100.0f;
}
