#pragma once
#include "ui_utils.h"
#include "arialbold32.h"   // fuer die Hoehen-Zahl (eine Stufe kleiner als 40)
#include "imu.h"      // ImuCal
#include <math.h>

// SENSOR-KALIBRIERUNG — sauber in zwei Panels geteilt:
//   Panel 1 (oben):  HOEHE / QNH   -> -10 / +10 / GPS-Hoehe uebernehmen / OK
//   Panel 2 (unten): KOMPASS / IMU -> Status + NORDEN SETZEN + KAL. SPEICHERN
// Button-Reihe Hoehe (Manuell links: -10/+10 | Auto rechts: GPS/OK):
static const int QB_Y=224, QB_H=54;
static const int QM_X=48,   QB_W=180;     // -10
static const int QP_X=240;                // +10  (Breite = QB_W)
static const int QG_X=545,  QG_W=150;     // GPS
static const int QOK_X=712, QOK_W=190;    // OK
// Button-Reihe Kompass (kompakter, mittig):
static const int QBOT_Y=458, QBOT_H=40, QBOT_W=240;
static const int QNORTH_X=190, QCAL_X=530;

enum QnhAction { QNH_NONE, QNH_PLUS, QNH_MINUS, QNH_OK, QNH_CALSAVE, QNH_SETNORTH, QNH_GPS };

// GPS-Hoehe nur als Kal-Quelle freigeben, wenn der Fix wirklich taugt.
static bool qnhGpsReady(bool gps_valid, int sats, float hdop) {
    return gps_valid && sats >= 10 && hdop > 0.0f && hdop <= 2.5f;
}

static void showQnhScreen(EpdiyHighlevelState *hl, float alt, float qnh, float ppa,
                          ImuCal cal, float heading,
                          float gps_alt, int gps_sats, float gps_hdop, bool gps_valid) {
    (void)ppa;
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[72];

    drawHCenter(&ArialBold24, "KALIBRIERUNG", 0, 960, 46, fb);
    uiHLine(40, 64, 880, fb);

    // ===================== Panel 1: HOEHE / QNH =====================
    uiBox(30, 80, 900, 205, fb);
    drawText(&ArialBold16, "HOEHE / QNH", 52, 110, fb);
    snprintf(buf, sizeof(buf), "%.0f m", alt);
    drawText(&ArialBold32, buf, 52, 172, fb);
    snprintf(buf, sizeof(buf), "QNH %.0f hPa", qnh);
    drawText(&ArialBold24, buf, 360, 168, fb);

    // GPS-Qualitaetszeile + Status (zeigt, ob die GPS-Hoehe als Kal taugt)
    bool ready = qnhGpsReady(gps_valid, gps_sats, gps_hdop);
    const char *st = !gps_valid      ? "kein GPS-Fix"
                   : gps_sats < 10   ? "zu wenig Sat"
                   : gps_hdop > 2.5f ? "HDOP zu hoch"
                   :                   "BEREIT";
    if (gps_valid)
        snprintf(buf, sizeof(buf), "GPS  %.0f m    %d Sat    HDOP %.1f    %s",
                 gps_alt, gps_sats, gps_hdop, st);
    else
        snprintf(buf, sizeof(buf), "GPS:  %s", st);
    drawText(&ArialBold16, buf, 52, 208, fb);

    // Buttons Hoehe (Labels bleiben gut lesbar)
    uiBox(QM_X, QB_Y, QB_W, QB_H, fb);  drawBoxCenter(&ArialBold24, "- 10 m", QM_X, QB_Y, QB_W, QB_H, fb);
    uiBox(QP_X, QB_Y, QB_W, QB_H, fb);  drawBoxCenter(&ArialBold24, "+ 10 m", QP_X, QB_Y, QB_W, QB_H, fb);
    if (ready) {                                    // GPS scharf -> gefuellt (invertiert)
        uiFill(QG_X, QB_Y, QG_W, QB_H, fb, 0);
        drawBoxCenter(&ArialBold24, "GPS", QG_X, QB_Y, QG_W, QB_H, fb, 255);
    } else {                                        // nicht bereit -> nur Rahmen
        uiBox(QG_X, QB_Y, QG_W, QB_H, fb);
        drawBoxCenter(&ArialBold24, "GPS", QG_X, QB_Y, QG_W, QB_H, fb);
    }
    uiBox(QOK_X, QB_Y, QOK_W, QB_H, fb);  drawBoxCenter(&ArialBold24, "OK", QOK_X, QB_Y, QOK_W, QB_H, fb);

    // ===================== Panel 2: KOMPASS / IMU =====================
    uiBox(30, 300, 900, 210, fb);
    drawText(&ArialBold16, "KOMPASS / IMU", 52, 330, fb);
    snprintf(buf, sizeof(buf), "Richtung  %.0f Grad", heading);
    drawText(&ArialBold24, buf, 52, 380, fb);
    snprintf(buf, sizeof(buf), "Sys %d    Gyro %d    Acc %d    Mag %d",
             cal.sys, cal.gyro, cal.accel, cal.mag);
    drawText(&ArialBold16, buf, 52, 420, fb);
    drawText(&ArialBold16, "Acht-Figur bewegen bis Mag = 3", 52, 446, fb);

    uiBox(QNORTH_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    drawBoxCenter(&ArialBold16, "NORDEN", QNORTH_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    uiBox(QCAL_X, QBOT_Y, QBOT_W, QBOT_H, fb);
    drawBoxCenter(&ArialBold16, "SPEICHERN", QCAL_X, QBOT_Y, QBOT_W, QBOT_H, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static QnhAction checkQnhTap(int tx, int ty) {
    if (ty >= QB_Y && ty < QB_Y + QB_H) {
        if (tx >= QM_X  && tx < QM_X  + QB_W)  return QNH_MINUS;
        if (tx >= QP_X  && tx < QP_X  + QB_W)  return QNH_PLUS;
        if (tx >= QG_X  && tx < QG_X  + QG_W)  return QNH_GPS;
        if (tx >= QOK_X && tx < QOK_X + QOK_W) return QNH_OK;
    }
    if (ty >= QBOT_Y && ty < QBOT_Y + QBOT_H) {
        if (tx >= QNORTH_X && tx < QNORTH_X + QBOT_W) return QNH_SETNORTH;
        if (tx >= QCAL_X   && tx < QCAL_X   + QBOT_W) return QNH_CALSAVE;
    }
    return QNH_NONE;
}

static float calcQnhFromAlt(float ref_alt, float pressure_pa) {
    float base = 1.0f - ref_alt / 44330.0f;
    return (pressure_pa / powf(base, 5.255f)) / 100.0f;
}
