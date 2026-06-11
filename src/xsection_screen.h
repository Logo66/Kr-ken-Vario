#pragma once
// xsection_screen.h — AURA-KRUECKE-7: Luftraum-Schnitt (Seitenansicht)
// Stufe 1: Hoehe/Distanz/Luftraum/Gleitpfad/Konflikt. Stufe 2: Terrain-Profil (Silhouette).
#include "ui_utils.h"
#include "openair_parser.h"
#include "contours.h"     // Stufe 2: Geländehöhe aus Höhenlinien (Lookup)
#include <math.h>

// === TERRAIN-LOOKUP (Stufe 2) — Geländehöhe aus den geladenen Höhenlinien ===
// Pack hat kein Höhenraster -> Weg (b): naechste Terrain-Kontur (flag 0/1) liefert die Höhe.
// "Hilfe, keine Gewähr" (Daten ab 25 m, nicht vollstaendig). Gilt nur im geladenen Tile-Fenster.
static float terrainElevAt(double plat, double plon) {
    if (!contPool || contour_count == 0) return -9999.0f;
    // Die zwei naechsten Kontur-Punkte UNTERSCHIEDLICHER Hoehe -> dazwischen interpolieren.
    float d1 = 1e18f, d2 = 1e18f;
    int16_t h1 = -32000, h2 = -32000;
    for (int c = 0; c < contour_count; c++) {
        const Contour &ct = contours[c];
        if (ct.flag >= 2) continue;                 // nur Terrain (kein Wasser/Strasse)
        int16_t h = ct.height_m;
        const ContourPt *pts = contPoints(ct);
        for (int i = 0; i < ct.num_pts; i++) {
            float dlat = (float)(pts[i].lat - plat);
            float dlon = (float)(pts[i].lon - plon);
            float dsq = dlat*dlat + dlon*dlon;
            if (dsq < d1) {
                if (h != h1) { d2 = d1; h2 = h1; }   // alter Bester -> Zweiter (andere Hoehe)
                d1 = dsq; h1 = h;
            } else if (dsq < d2 && h != h1) {
                d2 = dsq; h2 = h;
            }
        }
    }
    if (h1 == -32000 || d1 > 1.6e-4f) return -9999.0f;    // >~1.3 km weg -> keine Daten
    if (h2 == -32000) return (float)h1;                   // nur eine Hoehe gefunden
    float dd1 = sqrtf(d1), dd2 = sqrtf(d2);
    return (float)h1 + ((float)h2 - (float)h1) * (dd1 / (dd1 + dd2));  // interpolieren
}

// Profil entlang des Heading-Strahls (0..10 km), gecacht (nur bei Bewegung/Drehung neu).
static const int TP_N = 50;
static int16_t terrainProfile[TP_N];
static double  tpLat = 0, tpLon = 0; static float tpHdg = -999.0f;
static void terrainProfileCompute(double lat, double lon, float heading) {
    double dlat=(lat-tpLat)*111000.0, dlon=(lon-tpLon)*111000.0*cos(lat*M_PI/180.0);
    if (tpHdg>-900.0f && dlat*dlat+dlon*dlon < 300.0*300.0 && fabsf(heading-tpHdg)<12.0f) return; // Cache
    tpLat=lat; tpLon=lon; tpHdg=heading;
    float hrad = heading * (float)M_PI/180.0f;
    double clat = cos(lat*M_PI/180.0);
    for (int s = 0; s < TP_N; s++) {
        float distKm = (s + 0.5f) * (10.0f / TP_N);
        double plat = lat + (distKm * cosf(hrad)) / 111.0;
        double plon = lon + (distKm * sinf(hrad)) / (111.0 * clat);
        float e = terrainElevAt(plat, plon);
        terrainProfile[s] = (e > -9999.0f) ? (int16_t)e : -9999;
    }
}

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

    // === EINHEITLICHE STATUSLEISTE (Uhr | Sat | FANET | Buddy | Server | Batterie) ===
    drawStatusBar(fb);

    // === TITEL ===
    snprintf(buf,48,"LUFTRAUM VORAUS  %d", (int)d.heading);
    drawText(&ArialBold28, buf, 40, 100, fb);                        // Dreieck geloescht, Titel etwas nach links

    // === CHART-RAHMEN + ACHSEN ===
    uiBox(14, 112, 610, 415, fb);                                    // rechts bis zur Daten-Trennlinie -> "10" frei, keine Doppellinie
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

    // === TERRAIN-PROFIL (Stufe 2) — FETTE RELIEF-LINIE (kein Fuellen -> schlank, kein Brei) ===
    terrainProfileCompute(d.lat, d.lon, d.heading);
    int tpx = -1, tpy = 0;
    for (int s = 0; s < TP_N; s++) {
        if (terrainProfile[s] <= -9999) continue;                    // Luecke -> ueberbruecken (Linie durch)
        float distKm = (s + 0.5f) * (10.0f / TP_N);
        int sx = xsX(distKm);
        int sy = xsY((float)terrainProfile[s]); if (sy<118) sy=118; if (sy>492) sy=492;
        if (tpx >= 0) xsLine(tpx, tpy, sx, sy, 4, false, fb);         // fette, durchgezogene Relief-Linie
        tpx = sx; tpy = sy;
    }

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
        drawText(&ArialBold28, airspaceClassStr(airspaces[ah.idx].cls), lblx, yceil+54, fb);  // tiefer rein, weg von der oberen Linie
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
    drawText(&ArialBold28,buf,636,200,fb);
    drawText(&ArialBold16,"FLOOR",638,240,fb);
    if(ah.found) snprintf(buf,48,"%d m",(int)ah.floor_m); else snprintf(buf,48,"--");
    drawText(&ArialBold28,buf,636,288,fb);
    drawText(&ArialBold16,"@ GRENZE",638,328,fb);
    if(ah.found) snprintf(buf,48,"%d m",(int)glideAtBorder); else snprintf(buf,48,"--");
    drawText(&ArialBold28,buf,636,376,fb);
    if(ah.found && conflict){
        uiFill(624,442,300,66,fb);                                    // schwarzer Alarm-Kasten (bleibt: Warnung)
        snprintf(buf,48,"KONFLIKT +%d m",(int)(glideAtBorder-ah.floor_m));
        drawBoxCenter(&ArialBold24, buf, 624,442,300,66, fb, 255);
    } else if (ah.found){
        snprintf(buf,48,"FREI -%d m",(int)(ah.floor_m-glideAtBorder));
        drawBoxCenter(&ArialBold24, buf, 624,442,300,66, fb, 0);      // kleiner, KEIN Kasten
    } else {
        drawBoxCenter(&ArialBold16, "KEIN LUFTRAUM", 624,442,300,66, fb, 0);   // klein, zentriert, ohne Kasten
    }

    // === GND-Platzhalter (Terrain = Stufe 2) ===
    uiHLine(96, 494, 490, fb, 1);

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}
