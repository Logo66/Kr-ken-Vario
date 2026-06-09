#pragma once
// xsection_screen.h — AURA-KRUECKE-7: Luftraum-Schnitt (Seitenansicht), Stufe 1
// Layout: Y-Achse mit Rand (nicht abgeschnitten), Chart schmaler, Daten-Spalte rechts ohne Kollision.
#include "ui_utils.h"
#include "openair_parser.h"
#include <math.h>

struct XSectionData {
    double lat, lon;
    float altitude;   // m MSL
    float heading;    // Grad
    float speed;      // km/h
    float gr;         // Gleitzahl (>0.5 gueltig, sonst level)
    int rtc_hour, rtc_min, sats, bat_pct, fanet_peers;
    bool gps_fix;
};

// === Projektion: X 0..10 km -> 96..586 ; Y 0..3000 m -> 494..161 ===
static inline int xsX(float km) { return 96 + (int)(km * 49.0f); }
static inline int xsY(float m)  { return 494 - (int)(m * 0.111f); }

static void xsCircFill(int cx, int cy, int r, uint8_t *fb) {
    for (int dy=-r; dy<=r; dy++) for (int dx=-r; dx<=r; dx++)
        if (dx*dx+dy*dy <= r*r) uiFill(cx+dx, cy+dy, 1, 1, fb);
}
static void xsCircRing(int cx, int cy, int r, uint8_t *fb) {
    for (int dy=-r; dy<=r; dy++) for (int dx=-r; dx<=r; dx++) {
        int dd=dx*dx+dy*dy; if (dd<=r*r && dd>=(r-2)*(r-2)) uiFill(cx+dx, cy+dy, 1, 1, fb);
    }
}
static void xsTriRight(int tipx, int cy, int sz, uint8_t *fb) {       // kleines Dreieck (Titel)
    for (int dx=0; dx<=sz; dx++) { int h=sz-dx; uiFill(tipx-sz+dx, cy-h, 1, 2*h+1, fb); }
}
static void xsSeg(int x0,int y0,int x1,int y1,uint8_t *fb){           // ungeclippte Linie
    int dx=abs(x1-x0),dy=abs(y1-y0),steps=(dx>dy)?dx:dy; if(steps<1)steps=1;
    for(int i=0;i<=steps;i++) uiFill(x0+(x1-x0)*i/steps, y0+(y1-y0)*i/steps, 1,1, fb);
}
// Gleitschirm-Symbol (Seitenansicht): gewoelbte Kappe + Leinen + Pilot — 2.5x, fett
static void xsGlider(int px, int py, uint8_t *fb) {
    int x0=px-46, x1=px+30;                                           // Kappe ~76 px breit
    for (int x=x0; x<=x1; x++) {                                      // gewoelbter Bogen, fett
        float f = sinf((float)(x-x0)/(float)(x1-x0) * 3.14159f);
        int y = py - 50 - (int)(18.0f * f);
        uiFill(x, y, 3, 6, fb);
    }
    for(int o=-1;o<=1;o++){                                           // Leinen (fett: 3 nebeneinander)
        xsSeg(x0, py-50, px+o, py, fb);
        xsSeg(x1, py-50, px+o, py, fb);
    }
    xsCircFill(px, py, 7, fb);                                        // Pilot (gross)
}
static void xsLine(int x0,int y0,int x1,int y1,int thick,bool dash,uint8_t *fb){
    int dx=abs(x1-x0), dy=abs(y1-y0);
    int steps=(dx>dy)?dx:dy; if(steps<1)steps=1; if(steps>3000)return;
    int ht=thick/2;
    for(int i=0;i<=steps;i++){
        if(dash && ((i/7)%2)) continue;
        int x=x0+(x1-x0)*i/steps, y=y0+(y1-y0)*i/steps;
        if(x>=96 && x<=586 && y>=116 && y<=492) uiFill(x-ht,y-ht,thick,thick,fb);
    }
}

static void showXSectionScreen(EpdiyHighlevelState *hl, const XSectionData &d,
                               enum EpdDrawMode mode = MODE_GC16) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    // === STATUSBAR ===
    snprintf(buf,48,"%02d:%02d", d.rtc_hour, d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);
    for (int i=0;i<11;i++){ int cx=150+i*15; if(i<d.sats) xsCircFill(cx,29,5,fb); else xsCircRing(cx,29,5,fb); }
    snprintf(buf,48,"FANET %d", d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);
    uiBox(866,16,58,26,fb); uiFill(924,22,7,14,fb);
    uiFill(870,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);
    uiHLine(14, 54, 932, fb);

    // === TITEL ===
    xsTriRight(46, 92, 14, fb);
    snprintf(buf,48,"LUFTRAUM VORAUS  %d", (int)d.heading);
    drawText(&ArialBold28, buf, 54, 100, fb);

    // === CHART-RAHMEN + ACHSEN ===
    uiBox(20, 112, 580, 415, fb);
    const int yv[]={3000,2000,1000,0};
    const char* yl[]={"3000","2000","1000","GND"};
    for(int k=0;k<4;k++){
        int yy=xsY(yv[k]);
        uiHLine(88, yy, 8, fb, 1);
        int tw,th; measureText(&ArialBold16, yl[k], &tw, &th);
        drawText(&ArialBold16, yl[k], 84-tw, yy+6, fb);               // rechtsbuendig bei x84 -> Rand frei
    }
    for(int km=0;km<=10;km+=2){
        int xx=xsX(km);
        snprintf(buf,48,"%d",km);
        int tw,th; measureText(&ArialBold16, buf, &tw, &th);
        drawText(&ArialBold16, buf, xx-tw/2, 513, fb);
    }
    drawText(&ArialBold16, "km", 330, 522, fb);                      // einmal, mittig unter den Zahlen

    // === LUFTRAUM VORAUS ===
    AheadResult ah; ah.found=false; ah.idx=-1; ah.dist_km=0; ah.floor_m=0; ah.ceil_m=0;
    if (d.lat != 0) ah = airspaceAhead(d.lat, d.lon, d.heading);
    float gr = (d.gr > 0.5f && d.gr < 100.0f) ? d.gr : 0.0f;
    float glideAtBorder = 0; bool conflict = false;

    if (ah.found) {
        int bx0=xsX(ah.dist_km); if(bx0<96)bx0=96; if(bx0>560)bx0=560;
        int yfloor=xsY(ah.floor_m); if(yfloor>492)yfloor=492; if(yfloor<116)yfloor=116;
        int yceil=xsY(ah.ceil_m); if(yceil<116)yceil=116; if(yceil>492)yceil=492;
        if (yfloor>yceil) uiBox(bx0, yceil, 586-bx0, yfloor-yceil, fb);
        uiFill(bx0, yfloor-2, 586-bx0, 4, fb);
        for(int hx=bx0; hx<582; hx+=12)
            for(int s=0;s<9;s++){ int px=hx+s, py=yfloor+3+s; if(px<586 && py<492) uiFill(px,py,1,1,fb); }
        int lblx=bx0+10; if(lblx>430)lblx=430;
        drawText(&ArialBold28, airspaceClassStr(airspaces[ah.idx].cls), lblx, yceil+34, fb);
        snprintf(buf,48,"Floor %d", (int)ah.floor_m);
        drawText(&ArialBold16, buf, lblx, yfloor-8, fb);
    }

    // === JETZT (Hoehe) + Gleitschirm ===
    int yNow=xsY(d.altitude);
    for(int x=96;x<586;x+=10) uiFill(x, yNow, 5, 1, fb);             // gestrichelte Referenz
    xsGlider(165, yNow, fb);                                          // eigene Position = Gleitschirm
    snprintf(buf,48,"%d", (int)d.altitude);                           // nur die Hoehe, kein "JETZT"
    drawText(&ArialBold16, buf, 200, yNow+5, fb);                     // nah am Schirm

    // === GLEITPFAD ===
    float altEnd = (gr>0) ? d.altitude - 10000.0f/gr : d.altitude;
    xsLine(xsX(0), yNow, xsX(10), xsY(altEnd), 2, true, fb);

    // === KONFLIKT ===
    if (ah.found) {
        glideAtBorder = (gr>0) ? d.altitude - (ah.dist_km*1000.0f)/gr : d.altitude;
        conflict = (glideAtBorder > ah.floor_m);
        if (conflict) {
            float kmExit = (gr>0) ? (d.altitude - ah.floor_m)*gr/1000.0f : 10.0f;
            if (kmExit>10) kmExit=10; if (kmExit<ah.dist_km) kmExit=ah.dist_km;
            xsLine(xsX(ah.dist_km), xsY(glideAtBorder), xsX(kmExit), xsY(ah.floor_m), 6, false, fb);
            xsCircRing(xsX(ah.dist_km), xsY(glideAtBorder), 9, fb);
        }
    }

    // === DATEN-SPALTE (rechts, kollisionsfrei) ===
    uiVLine(624,120,405,fb,2);
    drawText(&ArialBold16,"GRENZE IN",638,152,fb);
    if(ah.found) snprintf(buf,48,"%.1f km",ah.dist_km); else snprintf(buf,48,"--");
    drawText(&ArialBold28,buf,636,190,fb);
    drawText(&ArialBold16,"FLOOR",638,240,fb);
    if(ah.found) snprintf(buf,48,"%d m",(int)ah.floor_m); else snprintf(buf,48,"--");
    drawText(&ArialBold28,buf,636,278,fb);
    drawText(&ArialBold16,"@ GRENZE",638,328,fb);
    if(ah.found) snprintf(buf,48,"%d m",(int)glideAtBorder); else snprintf(buf,48,"--");
    drawText(&ArialBold28,buf,636,366,fb);
    if(ah.found && conflict){
        uiFill(624,442,300,66,fb);                                    // schwarzer Alarm-Kasten
        snprintf(buf,48,"KONFLIKT +%d m",(int)(glideAtBorder-ah.floor_m));
        drawBoxCenter(&ArialBold28, buf, 624,442,300,66, fb, 255);
    } else if (ah.found){
        uiBox(624,442,300,66,fb);
        snprintf(buf,48,"FREI -%d m",(int)(ah.floor_m-glideAtBorder));
        drawBoxCenter(&ArialBold28, buf, 624,442,300,66, fb, 0);
    } else {
        drawBoxCenter(&ArialBold16, "KEIN LUFTRAUM", 624,442,300,66, fb, 0);   // klein, zentriert, ohne Kasten
    }

    // === GND-Platzhalter (Terrain = Stufe 2) ===
    uiHLine(96, 494, 490, fb, 1);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}
