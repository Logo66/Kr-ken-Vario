#pragma once
// cruise_screen.h â€” AURA Vario Cruise Display v7 FINAL
// Exaktes Grid, manuell zentriert, keine Berechnung die schiefgehen kann
//
// GRID (960x540):
//   Status:     y  0- 48
//   Row Hoehe:  y 50-168  | x 310-950 (full width)
//   Row Spd/Gl: y 170-284 | x 310-628 / 632-950 (2 gleiche Haelften)
//   Row Wnd/Tmp:y 286-400 | x 310-628 / 632-950 (2 gleiche Haelften)
//   Kompass:    y 410-530
//   Vario:      y 50-400  | x  10-305

#include "epdiy.h"
#include "epd_highlevel.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

struct CruiseData {
    float altitude, vario, vario_avg, speed, heading, glide;
    float qnh, delta_gnd, wind_speed, wind_dir, temp, dewpoint;
    int bat_pct; float bat_hours;
    bool gps_fix; int sats, fanet_peers, avg_seconds;
};

static void T(const EpdFont *f, const char *s, int x, int y, uint8_t *fb) {
    int cx=x, cy=y;
    EpdFontProperties p = epd_font_properties_default();
    p.fg_color = 0;
    epd_write_string(f, s, &cx, &cy, fb, &p);
}
static void H(int x, int y, int w, uint8_t *fb) {
    EpdRect r={x,y,w,2}; epd_fill_rect(r,0,fb);
}
static void V(int x, int y, int h, uint8_t *fb) {
    EpdRect r={x,y,2,h}; epd_fill_rect(r,0,fb);
}
static void FB(int x, int y, int w, int h, uint8_t *fb) {
    EpdRect r={x,y,w,h}; epd_fill_rect(r,0,fb);
}
static void B(int x, int y, int w, int h, uint8_t *fb) {
    H(x,y,w,fb); H(x,y+h-1,w,fb); V(x,y,h,fb); V(x+w-1,y,h,fb);
}

// LADDER
static void drawLadder(float vario, float avg, int lx, int ly, int lw, int lh, uint8_t *fb) {
    const float SC=4.0f; const int NS=4;
    int hf=lh/2, sg=hf/NS, zy=ly+hf;
    for(int i=0;i<NS;i++) { B(lx,zy-(i+1)*sg,lw,sg,fb); B(lx,zy+i*sg,lw,sg,fb); }
    FB(lx-6,zy-2,lw+12,5,fb);
    float cl=fmaxf(-SC,fminf(SC,vario));
    int fp=(int)(fabsf(cl)/SC*hf);
    if(cl>0) FB(lx+2,zy-fp,lw-4,fp,fb);
    else if(cl<0) FB(lx+2,zy,lw-4,fp,fb);
    char b[8];
    for(int i=1;i<=NS;i++) {
        snprintf(b,8,"+%d",i); T(&ArialBold16,b,lx+lw+4,zy-i*sg+sg/2+6,fb);
        snprintf(b,8,"-%d",i); T(&ArialBold16,b,lx+lw+4,zy+i*sg-sg/2+6,fb);
    }
    float ac=fmaxf(-SC,fminf(SC,avg));
    int ay=zy-(int)(ac/SC*hf);
    if(fabsf(avg)>0.05f) FB(lx-8,ay-4,7,8,fb);
}

// KOMPASS — Doppelpfeil + Buchstaben auf GLEICHER Hoehe, keine extra Linie
static void drawCompass(float hdg, int sx, int sy, int sw, uint8_t *fb) {
    int cx = sx + sw/2;
    int base_y = sy + 60;  // Vertikal zentriert in Zone (400-540, Mitte=470)

    // Doppelpfeil an der Heading-Position (gross, 22px halb, 22px hoch)
    // Oberes Dreieck ▼ (Spitze bei base_y)
    for(int r=0; r<22; r++) {
        int hw = 22 - r*22/22;
        if(hw>0) FB(cx-hw, base_y-22+r, 2*hw+1, 1, fb);
    }
    // Unteres Dreieck ▲ (Spitze bei base_y)
    for(int r=0; r<22; r++) {
        int hw = 22 - r*22/22;
        if(hw>0) FB(cx-hw, base_y+22-r, 2*hw+1, 1, fb);
    }

    // Kleine Ticks an der Baseline (duenne Linie nur als Tick-Referenz)
    const char* lb[]={"N","NE","E","SE","S","SW","W","NW"};
    int dg[]={0,45,90,135,180,225,270,315};
    float vs=240.0f, pp=(float)sw/vs;

    for(int i=0; i<8; i++) {
        float df=(float)dg[i]-hdg;
        while(df>180) df-=360;
        while(df<-180) df+=360;
        if(fabsf(df)>vs/2) continue;
        int px = cx + (int)(df*pp);
        if(px<sx+15 || px>sx+sw-15) continue;

        // Tick (kleine vertikale Marke)
        FB(px, base_y-3, 2, 6, fb);

        // Buchstabe — Baseline = gleiche Hoehe wie Pfeil-Mitte
        // ArialBold28 hat ~28px Hoehe, Baseline bei base_y bedeutet Text von base_y-28 bis base_y
        if(dg[i]%90==0)
            T(&ArialBold28, lb[i], px-12, base_y+2, fb);
        else
            T(&ArialBold16, lb[i], px-10, base_y, fb);
    }

    // 10er-Grad-Ticks (fein, ueber der Baseline)
    for(int d=0; d<360; d+=10) {
        float df=(float)d-hdg;
        while(df>180) df-=360;
        while(df<-180) df+=360;
        if(fabsf(df)>vs/2) continue;
        int px = cx + (int)(df*pp);
        if(px<sx+5 || px>sx+sw-5) continue;
        FB(px, base_y-2, 1, 4, fb);
    }
}

static void showCruiseScreen(EpdiyHighlevelState *hl, const CruiseData &d) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    // ============ GRID KONSTANTEN ============
    // Rechte Haelfte: 3 Reihen x 2 Spalten
    const int RX=310, RW=640, RHW=318;     // rechts: Start, Breite, Halbbreite
    const int RMX=RX+RHW+4;                // rechte Spalte Start = 632
    const int R1T=50, R1B=168;              // Reihe 1 (Hoehe)
    const int R2T=170, R2B=284;             // Reihe 2 (Speed/Glide)
    const int R3T=286, R3B=400;             // Reihe 3 (Wind/Temp)
    const int DX=308;                       // Vertikaler Haupt-Divider
    const int CT=410;                       // Kompass Top

    // === STATUS BAR (y: 0-48) ===
    T(&ArialBold16,"14:23",20,30,fb);
    for(int i=0;i<d.sats&&i<12;i++) FB(160+i*12,22,6,6,fb);
    snprintf(buf,48,"%d sat",d.sats); T(&ArialBold16,buf,320,30,fb);
    snprintf(buf,48,"FANET %d",d.fanet_peers); T(&ArialBold16,buf,440,30,fb);
    B(720,18,36,18,fb); FB(756,23,4,8,fb);
    FB(723,21,(int)(30.0f*d.bat_pct/100.0f),12,fb);
    snprintf(buf,48,"%d%% %.0fh",d.bat_pct,d.bat_hours); T(&ArialBold16,buf,768,30,fb);
    H(10,48,940,fb);

    // === VARIO LADDER (x: 18, y: 55-395, h=340) ===
    drawLadder(d.vario, d.vario_avg, 18, 55, 50, 340, fb);
    int zy = 55 + 170; // Null-Linie = 225

    // === VARIO WERTE (x: 110-300) ===
    T(&ArialBold16,"VARIO",155,75,fb);
    // Aktuell: zentriert in oberer Haelfte (y 80-220)
    snprintf(buf,48,"%+.1f",d.vario);
    T(&ArialBold40,buf,130,160,fb);
    T(&ArialBold16,"m/s",165,182,fb);
    // Trennlinie auf Null-Hoehe
    H(110,zy,195,fb);
    // Integriert: zentriert in unterer Haelfte (y 230-390)
    snprintf(buf,48,"%+.1f",d.vario_avg);
    T(&ArialBold40,buf,130,310,fb);
    snprintf(buf,48,"avg %ds",d.avg_seconds);
    T(&ArialBold16,buf,140,332,fb);

    // === HAUPT-DIVIDER ===
    V(DX,50,R3B-50,fb);

    // === REIHE 1: HOEHE (x: 310-950, y: 50-168) ===
    T(&ArialBold16,"HOEHE MSL",530,72,fb);
    snprintf(buf,48,"%.0f",d.altitude);
    T(&ArialBold40,buf,480,130,fb);
    T(&ArialBold28,"m",680,130,fb);
    snprintf(buf,48,"QNH %.0f  GND +%.0f m",d.qnh,d.delta_gnd);
    T(&ArialBold16,buf,430,158,fb);
    H(RX,R1B,RW,fb);

    // === REIHE 2: SPEED | GLIDE (y: 170-284) ===
    // Speed: Feld x310-628 (318px breit)
    // Zentriert: Label bei x+100, Wert bei x+120
    T(&ArialBold16,"SPEED",410,195,fb);
    snprintf(buf,48,"%.0f",d.speed);
    T(&ArialBold40,buf,420,250,fb);
    T(&ArialBold16,"km/h",420,274,fb);
    // Divider
    V(RX+RHW,R2T,R2B-R2T,fb);
    // Glide: Feld x632-950 (318px breit)
    T(&ArialBold16,"GLIDE",730,195,fb);
    if(d.vario<-0.1f&&d.speed>5.0f) snprintf(buf,48,"%.1f",d.glide);
    else if(d.vario>0.1f) snprintf(buf,48,"+++");
    else snprintf(buf,48,"---");
    T(&ArialBold40,buf,720,250,fb);
    H(RX,R2B,RW,fb);

    // === REIHE 3: WIND | TEMP (y: 286-400) ===
    // Wind: Feld x310-628
    T(&ArialBold16,"WIND",410,310,fb);
    snprintf(buf,48,"%.0f km/h",d.wind_speed);
    T(&ArialBold28,buf,390,360,fb);
    // Divider
    V(RX+RHW,R3T,R3B-R3T,fb);
    // Temp+Dew: Feld x632-950
    snprintf(buf,48,"%.1f C",d.temp);
    T(&ArialBold28,buf,680,340,fb);
    snprintf(buf,48,"Dew %+.0f C",d.dewpoint);
    T(&ArialBold16,buf,680,370,fb);

    // === UNTERKANTE HAUPTBEREICH ===
    H(10,R3B,940,fb);

    // === KOMPASS (y: 410-530) ===
    // Zone KOMPLETT weiss fuellen (loescht Geister von vorherigen Renders)
    EpdRect cz = {0, R3B+3, 960, 540-R3B-3};
    epd_fill_rect(cz, 0xFF, fb);
    drawCompass(d.heading, 20, CT, 920, fb);

    // Push
    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

static void showDemoCruiseScreen(EpdiyHighlevelState *hl) {
    CruiseData sim={
        .altitude=2847,.vario=2.4f,.vario_avg=1.8f,.speed=47,
        .heading=340,.glide=8.2f,.qnh=1018,.delta_gnd=412,
        .wind_speed=14,.wind_dir=45,.temp=25.3f,.dewpoint=17.0f,
        .bat_pct=87,.bat_hours=25,.gps_fix=true,
        .sats=11,.fanet_peers=3,.avg_seconds=20,
    };
    showCruiseScreen(hl, sim);
}

