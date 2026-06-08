#pragma once
// thermal_screen.h — Alle Texte BERECHNET, keine Schaetzungen
#include "ui_utils.h"
#include <math.h>

struct LiftSample {
    float dx, dy; float climb; unsigned long ts;
};

struct ThermalData {
    float vario, vario_avg, altitude, base_est, gained;
    unsigned long thermal_start_ms;
    float heading, speed;
    LiftSample samples[30]; int sample_count;
    float kern_dx, kern_dy, kern_dist;
    const char *kern_hint;
    int rtc_hour, rtc_min, bat_pct;
};

static void fillCircle(int cx, int cy, int r, uint8_t *fb) {
    for (int y=-r; y<=r; y++) {
        int hw=(int)sqrtf(r*r-y*y);
        uiFill(cx-hw, cy+y, 2*hw+1, 1, fb);
    }
}
static void drawCircle(int cx, int cy, int r, uint8_t *fb) {
    for (int a=0; a<360; a+=2) {
        float rad=a*M_PI/180.0f;
        int x=cx+(int)(r*cosf(rad)), y=cy-(int)(r*sinf(rad));
        if(x>=0&&x<960&&y>=0&&y<540) uiFill(x,y,2,2,fb);
    }
}
static void drawPilotTriangle(int cx, int cy, float hdg, int sz, uint8_t *fb) {
    float a=hdg*M_PI/180.0f;
    float tx=cx+sinf(a)*sz, ty=cy-cosf(a)*sz;
    float bx=cx-sinf(a)*sz*0.6f, by=cy+cosf(a)*sz*0.6f;
    float lx=bx+cosf(a)*sz*0.4f, ly=by+sinf(a)*sz*0.4f;
    float rx=bx-cosf(a)*sz*0.4f, ry=by-sinf(a)*sz*0.4f;
    int miny=(int)fminf(ty,fminf(ly,ry)), maxy=(int)fmaxf(ty,fmaxf(ly,ry));
    for(int y=miny;y<=maxy;y++){
        int xl=960,xr=0;
        float e[][4]={{tx,ty,lx,ly},{tx,ty,rx,ry},{lx,ly,rx,ry}};
        for(int i=0;i<3;i++){
            float dy=e[i][3]-e[i][1]; if(fabsf(dy)<0.5f)continue;
            float t=(y-e[i][1])/dy; if(t<0||t>1)continue;
            int x=(int)(e[i][0]+t*(e[i][2]-e[i][0]));
            if(x<xl)xl=x; if(x>xr)xr=x;
        }
        if(xl<=xr&&xl>=0&&xr<960) uiFill(xl,y,xr-xl+1,1,fb);
    }
}

// Layout-Felder (definiert, nicht geschaetzt)
// Linke Spalte: x=0, w=350
// Rechte Spalte (Rose): x=360, w=590
// Status: y=0-48
// Vario: y=55-215 in linker Spalte
// Hoehe: y=220-300 in linker Spalte
// Base: y=305-385 in linker Spalte
// Rose: y=55-470 in rechter Spalte
// Kern: y=480-535
static const int TH_LEFT_W = 350;
static const int TH_RIGHT_X = 360;

static void showThermalScreen(EpdiyHighlevelState *hl, const ThermalData &d,
                               enum EpdDrawMode mode = MODE_GC16) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];
    int tw, th;

    // === STATUS BAR (Felder berechnet) ===
    snprintf(buf,48,"%02d:%02d", d.rtc_hour, d.rtc_min);
    drawText(&ArialBold16, buf, 20, 30, fb);  // Links, fix

    unsigned long ts = d.thermal_start_ms ? (millis()-d.thermal_start_ms)/1000 : 0;
    snprintf(buf,48,"THERMIK %d:%02d",(int)(ts/60),(int)(ts%60));
    drawHCenter(&ArialBold16, buf, 120, 260, 30, fb);  // Feld 120-380

    snprintf(buf,48,"+%.0f m", d.gained);
    drawText(&ArialBold16, buf, 400, 30, fb);  // Nach Thermik-Text

    snprintf(buf,48,"%d%%", d.bat_pct);
    measureText(&ArialBold16, buf, &tw, &th);
    drawText(&ArialBold16, buf, 940-tw, 30, fb);  // Rechtsbündig

    uiHLine(10, 48, 940, fb);

    // === LINKE SPALTE: VARIO (Feld x=0, w=350, y=55-215) ===
    drawHCenter(&ArialBold16, "AVG CLIMB 20s", 0, TH_LEFT_W, 78, fb);

    snprintf(buf,48,"%+.1f", d.vario_avg);
    measureText(&ArialBold40, buf, &tw, &th);
    if (tw > TH_LEFT_W - 20) {
        drawHCenter(&ArialBold28, buf, 0, TH_LEFT_W, 140, fb);  // Fallback
    } else {
        drawHCenter(&ArialBold40, buf, 0, TH_LEFT_W, 145, fb);
    }
    drawHCenter(&ArialBold16, "m/s", 0, TH_LEFT_W, 170, fb);

    snprintf(buf,48,"jetzt %+.1f", d.vario);
    drawHCenter(&ArialBold16, buf, 0, TH_LEFT_W, 200, fb);

    uiHLine(10, 218, TH_LEFT_W-10, fb);

    // === HOEHE + BASE EST: 2 exakt gleich grosse Felder ===
    // Feld 1: y=220-308 (88px), Feld 2: y=312-400 (88px)
    const int F1_Y=220, F2_Y=312, FH=88;

    // HOEHE
    drawHCenter(&ArialBold16, "HOEHE", 0, TH_LEFT_W, F1_Y+22, fb);
    snprintf(buf,48,"%.0f m", d.altitude);
    drawBoxCenter(&ArialBold16, buf, 0, F1_Y+30, TH_LEFT_W, FH-30, fb);

    uiHLine(10, F2_Y-2, TH_LEFT_W-10, fb);

    // BASE EST
    drawHCenter(&ArialBold16, "BASE EST", 0, TH_LEFT_W, F2_Y+22, fb);
    snprintf(buf,48,"%.0f m", d.base_est);
    drawBoxCenter(&ArialBold16, buf, 0, F2_Y+30, TH_LEFT_W, FH-30, fb);

    // === DIVIDER ===
    uiVLine(TH_LEFT_W+5, 50, 430, fb);

    // === KOMPASS-ROSE (zentriert in rechter Spalte) ===
    int rose_cx = TH_RIGHT_X + 295;  // 655
    int rose_cy = 265;
    int rose_r = 175;

    drawCircle(rose_cx, rose_cy, rose_r, fb);
    drawCircle(rose_cx, rose_cy, rose_r/2, fb);

    // N/E/S/W — BERECHNET zentriert an den Achsen
    drawHCenter(&ArialBold28, "N", rose_cx-40, 80, rose_cy-rose_r-8, fb);
    drawHCenter(&ArialBold16, "S", rose_cx-30, 60, rose_cy+rose_r+18, fb);

    measureText(&ArialBold16, "E", &tw, &th);
    drawText(&ArialBold16, "E", rose_cx+rose_r+6, rose_cy+th/2, fb);

    measureText(&ArialBold16, "W", &tw, &th);
    drawText(&ArialBold16, "W", rose_cx-rose_r-6-tw, rose_cy+th/2, fb);

    // Pilot-Dreieck
    drawPilotTriangle(rose_cx, rose_cy, d.heading, 18, fb);

    // Lift-Punkte (nur gueltige)
    float scale = rose_r / 200.0f;
    unsigned long now = millis();
    for (int i = 0; i < d.sample_count; i++) {
        const LiftSample &s = d.samples[i];
        if (s.ts == 0) continue;
        unsigned long age = (now - s.ts) / 1000;
        if (age > 60) continue;
        int px = rose_cx + (int)(s.dx * scale);
        int py = rose_cy - (int)(s.dy * scale);
        if (px<TH_RIGHT_X+10 || px>940 || py<60 || py>470) continue;
        int r = 2 + (int)(fabsf(s.climb) * 2);
        if (r > 12) r = 12;
        if (age < 10) fillCircle(px, py, r, fb);
        else drawCircle(px, py, r, fb);
    }

    // Kern-Linie
    if (d.kern_dist > 5 && d.kern_dist < 500) {
        int kpx = rose_cx + (int)(d.kern_dx * scale);
        int kpy = rose_cy - (int)(d.kern_dy * scale);
        if (kpx>=TH_RIGHT_X+10 && kpx<=940 && kpy>=60 && kpy<=470) {
            int dx=kpx-rose_cx, dy=kpy-rose_cy;
            int steps=max(abs(dx),abs(dy));
            if(steps>0) for(int s=0;s<=steps;s++) {
                int lx=rose_cx+dx*s/steps, ly=rose_cy+dy*s/steps;
                uiFill(lx-1,ly-1,3,3,fb);
            }
        }
    }

    // === KERN-HINWEIS (zentriert auf volle Breite) ===
    uiHLine(10, 480, 940, fb);
    if (d.kern_dist > 5) {
        snprintf(buf,48,"Kern: %s  %.0f m", d.kern_hint, d.kern_dist);
    } else if (d.kern_hint) {
        snprintf(buf,48,"Kern: %s", d.kern_hint);
    } else {
        snprintf(buf,48,"Kern: ---");
    }
    drawHCenter(&ArialBold16, buf, 0, 960, 515, fb);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}

static void showDemoThermalScreen(EpdiyHighlevelState *hl) {
    ThermalData td = {};
    td.vario=2.4f; td.vario_avg=1.8f; td.altitude=2847;
    td.base_est=3120; td.gained=245;
    td.thermal_start_ms=millis()-134000;
    td.heading=320; td.speed=35;
    td.rtc_hour=14; td.rtc_min=31; td.bat_pct=86;
    td.sample_count=0; td.kern_dist=0; td.kern_hint="zu wenig Daten";
    showThermalScreen(hl, td);
}
