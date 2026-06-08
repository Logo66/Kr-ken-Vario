#pragma once
// thermal_screen.h — AURA Thermik-Zentrier-Screen
// North-Up Kompass-Rose, Lift-Punkte, Kern-Indikator
// 1-Bit S/W, fette Fonts, MODE_DU/GC16
//
// Layout 960x540:
//   Status:  y 0-48     | THERMIK mm:ss | +245m | BAT
//   Links:   x 0-350    | AVG CLIMB, jetzt, HOEHE, BASE
//   Rechts:  x 360-950  | Kompass-Rose (North-Up, Lift-Punkte)
//   Unten:   y 490-540   | Kern: links · 36m

#include "ui_utils.h"

// Lift-Sample: Position relativ zum Piloten, Steigrate, Alter
struct LiftSample {
    float dx, dy;    // Meter relativ zum Piloten (North-Up: x=Ost, y=Nord)
    float climb;     // m/s
    unsigned long ts; // millis() Zeitstempel
};

struct ThermalData {
    // Vario
    float vario, vario_avg;
    float altitude;
    float base_est;      // geschaetzte Basis (Wolkenuntergrenze)
    float gained;        // Hoehenmeter gewonnen seit Thermik-Eintritt
    unsigned long thermal_start_ms;

    // Pilot
    float heading;       // GPS COG (Grad, North-Up)
    float speed;         // km/h

    // Lift-Punkte
    LiftSample samples[30];
    int sample_count;

    // Kern
    float kern_dx, kern_dy;  // Richtung zum Kern (Meter)
    float kern_dist;         // Distanz (Meter)
    const char *kern_hint;   // "links", "rechts", "vorne", "hinten"

    // Status
    int rtc_hour, rtc_min;
    int bat_pct;
    float bat_hours;
};

// Gefuellter Kreis (Bresenham)
static void fillCircle(int cx, int cy, int r, uint8_t *fb) {
    for (int y=-r; y<=r; y++) {
        int hw = (int)sqrtf(r*r - y*y);
        uiFill(cx-hw, cy+y, 2*hw+1, 1, fb);
    }
}

// Hohler Kreis (duenn)
static void drawCircle(int cx, int cy, int r, uint8_t *fb) {
    for (int a=0; a<360; a+=2) {
        float rad = a * M_PI / 180.0f;
        int x = cx + (int)(r * cosf(rad));
        int y = cy - (int)(r * sinf(rad));
        if (x>=0 && x<960 && y>=0 && y<540)
            uiFill(x, y, 2, 2, fb);
    }
}

// Gedrehtes Dreieck (Pilot-Marker, heading in Grad, North-Up)
static void drawPilotTriangle(int cx, int cy, float heading_deg, int size, uint8_t *fb) {
    float rad = heading_deg * M_PI / 180.0f;
    // Spitze zeigt in Flugrichtung
    float tip_x = cx + sinf(rad) * size;
    float tip_y = cy - cosf(rad) * size;
    // Zwei Ecken hinten
    float back_rad = rad + M_PI;
    float l_x = cx + sinf(back_rad - 0.5f) * size * 0.6f;
    float l_y = cy - cosf(back_rad - 0.5f) * size * 0.6f;
    float r_x = cx + sinf(back_rad + 0.5f) * size * 0.6f;
    float r_y = cy - cosf(back_rad + 0.5f) * size * 0.6f;

    // Gefuelltes Dreieck (Scanline)
    int min_y = (int)fminf(tip_y, fminf(l_y, r_y));
    int max_y = (int)fmaxf(tip_y, fmaxf(l_y, r_y));
    for (int y = min_y; y <= max_y; y++) {
        // Simpel: zeichne Linie von links nach rechts fuer jede Zeile
        int min_x = 960, max_x = 0;
        // Kante tip→l
        float t1 = (l_y != tip_y) ? (float)(y - tip_y) / (l_y - tip_y) : -1;
        if (t1 >= 0 && t1 <= 1) { int x = (int)(tip_x + t1*(l_x-tip_x)); if(x<min_x)min_x=x; if(x>max_x)max_x=x; }
        // Kante tip→r
        float t2 = (r_y != tip_y) ? (float)(y - tip_y) / (r_y - tip_y) : -1;
        if (t2 >= 0 && t2 <= 1) { int x = (int)(tip_x + t2*(r_x-tip_x)); if(x<min_x)min_x=x; if(x>max_x)max_x=x; }
        // Kante l→r
        float t3 = (r_y != l_y) ? (float)(y - l_y) / (r_y - l_y) : -1;
        if (t3 >= 0 && t3 <= 1) { int x = (int)(l_x + t3*(r_x-l_x)); if(x<min_x)min_x=x; if(x>max_x)max_x=x; }
        if (min_x <= max_x && min_x >= 0 && max_x < 960)
            uiFill(min_x, y, max_x-min_x+1, 1, fb);
    }
}

static void showThermalScreen(EpdiyHighlevelState *hl, const ThermalData &d,
                               enum EpdDrawMode mode = MODE_GC16) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    // === STATUS BAR ===
    snprintf(buf,48,"%02d:%02d", d.rtc_hour, d.rtc_min);
    drawText(&ArialBold16, buf, 20, 30, fb);

    unsigned long thermal_s = (millis() - d.thermal_start_ms) / 1000;
    snprintf(buf,48,"THERMIK %d:%02d", (int)(thermal_s/60), (int)(thermal_s%60));
    drawText(&ArialBold16, buf, 120, 30, fb);

    snprintf(buf,48,"+%.0f m", d.gained);
    drawText(&ArialBold16, buf, 380, 30, fb);

    snprintf(buf,48,"%d%%", d.bat_pct);
    drawText(&ArialBold16, buf, 880, 30, fb);

    uiHLine(10, 48, 940, fb);

    // === LINKE SPALTE (Daten) ===
    drawText(&ArialBold16, "AVG CLIMB 20s", 20, 80, fb);
    snprintf(buf,48,"%+.1f", d.vario_avg);
    drawText(&ArialBold40, buf, 30, 150, fb);
    drawText(&ArialBold16, "m/s", 30, 175, fb);

    snprintf(buf,48,"jetzt %+.1f", d.vario);
    drawText(&ArialBold16, buf, 30, 205, fb);

    uiHLine(20, 220, 320, fb);

    drawText(&ArialBold16, "HOEHE", 30, 250, fb);
    snprintf(buf,48,"%.0f m", d.altitude);
    drawText(&ArialBold28, buf, 30, 290, fb);

    uiHLine(20, 310, 320, fb);

    drawText(&ArialBold16, "BASE EST", 30, 340, fb);
    snprintf(buf,48,"%.0f m", d.base_est);
    drawText(&ArialBold28, buf, 30, 380, fb);

    // === VERTIKALER DIVIDER ===
    uiVLine(355, 50, 440, fb);

    // === KOMPASS-ROSE (rechts, North-Up) ===
    int rose_cx = 655;  // Zentrum
    int rose_cy = 270;
    int rose_r = 180;   // Radius

    // Kreis
    drawCircle(rose_cx, rose_cy, rose_r, fb);
    drawCircle(rose_cx, rose_cy, rose_r/2, fb);  // innerer Ring

    // Himmelsrichtungen (fix, North-Up)
    drawText(&ArialBold28, "N", rose_cx-10, rose_cy-rose_r-5, fb);
    drawText(&ArialBold16, "E", rose_cx+rose_r+8, rose_cy+6, fb);
    drawText(&ArialBold16, "S", rose_cx-5, rose_cy+rose_r+20, fb);
    drawText(&ArialBold16, "W", rose_cx-rose_r-25, rose_cy+6, fb);

    // Pilot-Dreieck (dreht sich mit Heading)
    drawPilotTriangle(rose_cx, rose_cy, d.heading, 18, fb);

    // Lift-Punkte
    float scale = rose_r / 200.0f;  // 200m = voller Radius
    unsigned long now = millis();
    for (int i = 0; i < d.sample_count; i++) {
        const LiftSample &s = d.samples[i];
        int px = rose_cx + (int)(s.dx * scale);
        int py = rose_cy - (int)(s.dy * scale);
        if (px < 380 || px > 930 || py < 60 || py > 480) continue;

        int r = 3 + (int)(fabsf(s.climb) * 2);  // Groesse = Steigstaerke
        if (r > 12) r = 12;
        unsigned long age = (now - s.ts) / 1000;

        if (age < 10) {
            fillCircle(px, py, r, fb);       // Frisch: gefuellt
        } else {
            drawCircle(px, py, r, fb);       // Alt: hohl
        }
    }

    // Kern-Indikator (grosses Dreieck Richtung Kern)
    if (d.kern_dist > 5) {
        float angle = atan2f(d.kern_dx, d.kern_dy) * 180.0f / M_PI;
        int kern_px = rose_cx + (int)(d.kern_dx * scale);
        int kern_py = rose_cy - (int)(d.kern_dy * scale);
        // Pfeil vom Zentrum zum Kern
        uiFill(fminf(rose_cx,kern_px), fminf(rose_cy,kern_py),
           abs(kern_px-rose_cx)+2, 2, fb);  // Linie (vereinfacht)
    }

    // === UNTEN: Kern-Hinweis ===
    uiHLine(10, 490, 940, fb);
    if (d.kern_dist > 5) {
        snprintf(buf,48,"Kern: %s  %.0f m", d.kern_hint, d.kern_dist);
    } else {
        snprintf(buf,48,"Kern: zentriert");
    }
    drawText(&ArialBold16, buf, 360, 520, fb);

    // Push
    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}

// Demo-Daten
static void showDemoThermalScreen(EpdiyHighlevelState *hl) {
    ThermalData td = {};
    td.vario = 2.4f;
    td.vario_avg = 1.8f;
    td.altitude = 2847;
    td.base_est = 3120;
    td.gained = 245;
    td.thermal_start_ms = millis() - 134000;  // 2:14
    td.heading = 320;
    td.speed = 35;
    td.rtc_hour = 14;
    td.rtc_min = 31;
    td.bat_pct = 86;

    // Sim Lift-Punkte (Kreis mit variablem Steigen)
    td.sample_count = 12;
    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0f * M_PI / 180.0f;
        float r = 60 + 40 * sinf(angle * 2);
        td.samples[i].dx = cosf(angle) * r + 30;  // Kern leicht rechts
        td.samples[i].dy = sinf(angle) * r + 20;  // Kern leicht vorne
        td.samples[i].climb = 1.0f + 2.0f * fmaxf(0, sinf(angle - 1));
        td.samples[i].ts = millis() - (12-i) * 3000;
    }

    td.kern_dx = 30; td.kern_dy = 20;
    td.kern_dist = 36;
    td.kern_hint = "rechts";

    showThermalScreen(hl, td);
}
