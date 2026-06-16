#pragma once
#include "ui_utils.h"
#include "imu.h"      // ImuCal (Kalibrier-Status)
#include <math.h>

// QNH-Einstellung + Sensor-(Kompass-)Kalibrierung an EINEM Ort.
// Oben QNH (-10 / Hoehe / +10), unten Kompass-Kalibrierstatus + Speichern.
static const int QM_X=40,   QP_X=770, QB_W=150, QB_H=95, QB_Y=85;   // -10 / +10
static const int QOK_X=120, QOK_Y=420, QOK_W=250, QOK_H=80;         // QNH OK
static const int QCAL_X=590, QCAL_W=250;                            // KAL. SPEICHERN (gleiche Y/H wie OK)
static const int QMID_X=200, QMID_W=560;                            // Mitte fuer Hoehe/QNH

enum QnhAction { QNH_NONE, QNH_PLUS, QNH_MINUS, QNH_OK, QNH_CALSAVE };

static void showQnhScreen(EpdiyHighlevelState *hl, float alt, float qnh, float ppa, ImuCal cal) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[64];

    drawHCenter(&ArialBold28, "QNH + KALIBRIERUNG", 0, 960, 42, fb);
    uiHLine(30, 62, 900, fb);

    // --- QNH ---
    uiBox(QM_X, QB_Y, QB_W, QB_H, fb);
    drawBoxCenter(&ArialBold28, "-10", QM_X, QB_Y, QB_W, QB_H, fb);
    uiBox(QP_X, QB_Y, QB_W, QB_H, fb);
    drawBoxCenter(&ArialBold28, "+10", QP_X, QB_Y, QB_W, QB_H, fb);
    snprintf(buf, 64, "%.0f m", alt);
    drawHCenter(&ArialBold28, buf, QMID_X, QMID_W, 110, fb);
    snprintf(buf, 64, "QNH %.0f hPa", qnh);
    drawHCenter(&ArialBold16, buf, QMID_X, QMID_W, 152, fb);
    snprintf(buf, 64, "Druck %.0f hPa", ppa / 100.0f);
    drawHCenter(&ArialBold16, buf, QMID_X, QMID_W, 182, fb);

    // --- Kompass-Kalibrierung ---
    uiHLine(30, 250, 900, fb);
    drawHCenter(&ArialBold16, "KOMPASS: Acht-Figur bewegen bis Mag = 3", 0, 960, 278, fb);
    snprintf(buf, 64, "Sys %d    Gyro %d    Acc %d    Mag %d", cal.sys, cal.gyro, cal.accel, cal.mag);
    drawHCenter(&ArialBold28, buf, 0, 960, 335, fb);
    uiHLine(30, 395, 900, fb);

    // --- Buttons ---
    uiBox(QOK_X, QOK_Y, QOK_W, QOK_H, fb);
    drawBoxCenter(&ArialBold28, "QNH OK", QOK_X, QOK_Y, QOK_W, QOK_H, fb);
    uiBox(QCAL_X, QOK_Y, QCAL_W, QOK_H, fb);
    drawBoxCenter(&ArialBold24, "KAL. SPEICHERN", QCAL_X, QOK_Y, QCAL_W, QOK_H, fb);

    drawHCenter(&ArialBold16, "QNH OK = speichern + zurueck   .   Mag=3 -> Kal. speichern", 0, 960, 520, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static QnhAction checkQnhTap(int tx, int ty) {
    if (tx>=QM_X   && tx<QM_X+QB_W    && ty>=QB_Y  && ty<QB_Y+QB_H)   return QNH_MINUS;
    if (tx>=QP_X   && tx<QP_X+QB_W    && ty>=QB_Y  && ty<QB_Y+QB_H)   return QNH_PLUS;
    if (tx>=QOK_X  && tx<QOK_X+QOK_W  && ty>=QOK_Y && ty<QOK_Y+QOK_H) return QNH_OK;
    if (tx>=QCAL_X && tx<QCAL_X+QCAL_W && ty>=QOK_Y && ty<QOK_Y+QOK_H) return QNH_CALSAVE;
    return QNH_NONE;
}

static float calcQnhFromAlt(float ref_alt, float pressure_pa) {
    float base = 1.0f - ref_alt / 44330.0f;
    return (pressure_pa / powf(base, 5.255f)) / 100.0f;
}
