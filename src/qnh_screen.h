#pragma once
// qnh_screen.h — QNH-Kalibrierung ueber Referenzhoehe
// Pilot stellt bekannte Hoehe ein (+/- 10m), QNH berechnet sich
#include "epdiy.h"
#include "epd_highlevel.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

// Touch-Bereiche
static const int QNH_MINUS_X=80,  QNH_MINUS_Y=200, QNH_BTN_W=180, QNH_BTN_H=140;
static const int QNH_PLUS_X=700,  QNH_PLUS_Y=200;
static const int QNH_OK_X=350,    QNH_OK_Y=420,   QNH_OK_W=260, QNH_OK_H=80;

enum QnhAction { QNH_NONE, QNH_PLUS, QNH_MINUS, QNH_OK };

static void QT(const EpdFont *f, const char *s, int x, int y, uint8_t *fb) {
    int cx=x, cy=y; EpdFontProperties p=epd_font_properties_default(); p.fg_color=0;
    epd_write_string(f,s,&cx,&cy,fb,&p);
}
static void QF(int x,int y,int w,int h,uint8_t *fb) { EpdRect r={x,y,w,h}; epd_fill_rect(r,0,fb); }
static void QB(int x,int y,int w,int h,uint8_t *fb) {
    QF(x,y,w,4,fb); QF(x,y+h-4,w,4,fb); QF(x,y,4,h,fb); QF(x+w-4,y,4,h,fb);
}

static void showQnhScreen(EpdiyHighlevelState *hl, float ref_alt, float qnh_hpa, float pressure_pa) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    QT(&ArialBold28, "QNH EINSTELLEN", 280, 40, fb);
    QF(30, 55, 900, 2, fb);

    QT(&ArialBold16, "Bekannte Hoehe einstellen:", 300, 90, fb);

    // Hoehe gross zentriert zwischen -10 und +10 Buttons (x=260-700)
    snprintf(buf, 48, "%.0f m", ref_alt);
    QT(&ArialBold40, buf, 420, 180, fb);

    // QNH zentriert
    snprintf(buf, 48, "QNH %.0f hPa", qnh_hpa);
    QT(&ArialBold28, buf, 384, 260, fb);

    snprintf(buf, 48, "P: %.0f hPa", pressure_pa / 100.0f);
    QT(&ArialBold16, buf, 410, 295, fb);

    // [-] Button (links)
    QB(QNH_MINUS_X, QNH_MINUS_Y, QNH_BTN_W, QNH_BTN_H, fb);
    QT(&ArialBold40, "-10", QNH_MINUS_X+40, QNH_MINUS_Y+85, fb);

    // [+] Button (rechts)
    QB(QNH_PLUS_X, QNH_PLUS_Y, QNH_BTN_W, QNH_BTN_H, fb);
    QT(&ArialBold40, "+10", QNH_PLUS_X+35, QNH_PLUS_Y+85, fb);

    // [OK] Button (unten mitte, Text zentriert)
    QB(QNH_OK_X, QNH_OK_Y, QNH_OK_W, QNH_OK_H, fb);
    QT(&ArialBold40, "OK", QNH_OK_X+105, QNH_OK_Y+55, fb);

    // Footer
    QF(30, 520, 900, 2, fb);
    QT(&ArialBold16, "+/- 10m pro Tap    OK = Speichern", 260, 535, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

static QnhAction checkQnhTap(int tx, int ty) {
    if (tx >= QNH_MINUS_X && tx < QNH_MINUS_X+QNH_BTN_W &&
        ty >= QNH_MINUS_Y && ty < QNH_MINUS_Y+QNH_BTN_H) return QNH_MINUS;
    if (tx >= QNH_PLUS_X && tx < QNH_PLUS_X+QNH_BTN_W &&
        ty >= QNH_PLUS_Y && ty < QNH_PLUS_Y+QNH_BTN_H) return QNH_PLUS;
    if (tx >= QNH_OK_X && tx < QNH_OK_X+QNH_OK_W &&
        ty >= QNH_OK_Y && ty < QNH_OK_Y+QNH_OK_H) return QNH_OK;
    return QNH_NONE;
}

// QNH aus Referenzhoehe + aktuellem Druck berechnen
static float calcQnhFromAlt(float ref_alt, float pressure_pa) {
    float base = 1.0f - ref_alt / 44330.0f;
    return (pressure_pa / powf(base, 5.255f)) / 100.0f;  // hPa
}
