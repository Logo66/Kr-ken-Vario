#pragma once
// map_screen.h — KRUECKE-6 Phase 1: Geruest
// Pixelgenau nach Ticket-Koordinaten, KEINE Schaetzungen
// Clip: x14 y56 w778 h470 | Steuer: x806 | Pilot: (400,288)
#include "ui_utils.h"
#include "arialbold32.h"
#include "arialbold24.h"
#include <math.h>

// === Dreieck-Formel aus Ticket §2 (identisch zu goal_screen) ===
static void mapTri(int cx, int cy, float ang, int L, int Wd, uint8_t *fb) {
    float a=ang*M_PI/180.0f, sa=sinf(a), ca=cosf(a);
    float tx=cx+0.6f*L*sa, ty=cy-0.6f*L*ca;
    float bx=cx-0.4f*L*sa, by=cy+0.4f*L*ca;
    float blx=bx+0.5f*Wd*ca, bly=by+0.5f*Wd*sa;
    float brx=bx-0.5f*Wd*ca, bry=by-0.5f*Wd*sa;
    int miny=(int)fminf(ty,fminf(bly,bry)), maxy=(int)fmaxf(ty,fmaxf(bly,bry));
    for(int y=miny;y<=maxy;y++){
        int xl=960,xr=0;
        float e[][4]={{tx,ty,blx,bly},{tx,ty,brx,bry},{blx,bly,brx,bry}};
        for(int i=0;i<3;i++){
            float dy=e[i][3]-e[i][1]; if(fabsf(dy)<0.5f)continue;
            float t=(y-e[i][1])/dy; if(t<0||t>1)continue;
            int x=(int)(e[i][0]+t*(e[i][2]-e[i][0]));
            if(x<xl)xl=x; if(x>xr)xr=x;
        }
        if(xl<=xr&&xl>=0&&xr<960&&y>=0&&y<540) uiFill(xl,y,xr-xl+1,1,fb);
    }
}

// === Layout-Konstanten (exakt aus Ticket) ===
static const int MAP_CLIP_X=14, MAP_CLIP_Y=56, MAP_CLIP_W=778, MAP_CLIP_H=470;
static const int MAP_PILOT_X=400, MAP_PILOT_Y=288;
static const int MAP_BTN_X=806, MAP_BTN_W=128, MAP_BTN_H=104, MAP_BTN_RX=10;
static const int MAP_BTN_PLUS_Y=66, MAP_BTN_MINUS_Y=182, MAP_BTN_CENTER_Y=298;

// Zoom-Stufen (Index 0..4 → Kartenbreite in Metern)
static const float ZOOM_M[] = {500, 1000, 2000, 5000, 10000};
static const char* ZOOM_LABEL[] = {"0.5 km", "1 km", "2 km", "5 km", "10 km"};
static int mapZoomIdx = 2;  // Start: 2 km

enum MapAction { MAP_NONE, MAP_ZOOM_IN, MAP_ZOOM_OUT, MAP_RECENTER };

struct MapData {
    float heading;
    int rtc_hour, rtc_min, sats, bat_pct, fanet_peers;
    bool buddy_connected;
};

static void showMapScreen(EpdiyHighlevelState *hl, const MapData &d) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    // === STATUSBAR (Akku bei x700, nicht x866 — Ticket §3 Karte) ===
    snprintf(buf,32,"%02d:%02d",d.rtc_hour,d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);
    for(int i=0;i<11;i++){
        int cx=150+i*15;
        if(i<d.sats) fillCircle(cx,29,5,fb);
        else drawCircle(cx,29,5,fb);
    }
    snprintf(buf,32,"FANET %d",d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);

    // Akku bei x700 (Ticket: Karte hat Tasten rechts)
    uiBox(700,16,58,26,fb);
    uiFill(758,22,7,14,fb);
    uiFill(704,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);
    snprintf(buf,32,"%d%%",d.bat_pct);
    drawText(&ArialBold16, buf, 770, 38, fb);

    uiHLine(14, 54, 932, fb);

    // === KARTEN-CLIP-RAHMEN (x14 y56 w778 h470, stroke 2) ===
    uiHLine(MAP_CLIP_X, MAP_CLIP_Y, MAP_CLIP_W, fb);
    uiHLine(MAP_CLIP_X, MAP_CLIP_Y+MAP_CLIP_H-2, MAP_CLIP_W, fb);
    uiVLine(MAP_CLIP_X, MAP_CLIP_Y, MAP_CLIP_H, fb);
    uiVLine(MAP_CLIP_X+MAP_CLIP_W-2, MAP_CLIP_Y, MAP_CLIP_H, fb);

    // === PILOT-DREIECK (400,288) heading=0 Phase1, tri()-Formel ===
    mapTri(MAP_PILOT_X, MAP_PILOT_Y, d.heading, 46, 34, fb);

    // === NORDPFEIL (Linie 56,118→56,78 + Dreieck) ===
    uiVLine(56, 78, 40, fb, 3);  // Linie stroke 3
    mapTri(56, 74, 0, 24, 18, fb);  // Pfeil nach Norden
    drawText(&ArialBold16, "N", 46, 140, fb);

    // === MASSSTAB (Linie 40,506→160,506 + Endmarken) ===
    uiHLine(40, 506, 120, fb, 3);  // Hauptlinie stroke 3
    uiVLine(40, 500, 12, fb, 2);   // Endmarke links
    uiVLine(158, 500, 12, fb, 2);  // Endmarke rechts
    drawText(&ArialBold16, ZOOM_LABEL[mapZoomIdx], 66, 494, fb);

    // === STEUER-SPALTE (3 Touch-Buttons) ===

    // [+] Taste (x806 y66 w128 h104 rx10)
    uiBox(MAP_BTN_X, MAP_BTN_PLUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    drawBoxCenter(&ArialBold32, "+", MAP_BTN_X, MAP_BTN_PLUS_Y, MAP_BTN_W, MAP_BTN_H, fb);

    // [-] Taste (x806 y182 w128 h104)
    uiBox(MAP_BTN_X, MAP_BTN_MINUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    drawBoxCenter(&ArialBold32, "-", MAP_BTN_X, MAP_BTN_MINUS_Y, MAP_BTN_W, MAP_BTN_H, fb);

    // [Re-Center] Taste (x806 y298 w128 h104) — Fadenkreuz
    uiBox(MAP_BTN_X, MAP_BTN_CENTER_Y, MAP_BTN_W, MAP_BTN_H, fb);
    int ccx=870, ccy=350;
    drawCircle(ccx, ccy, 26, fb);         // Kreis
    uiVLine(ccx, ccy-38, 76, fb, 3);     // Vertikale Linie
    uiHLine(ccx-38, ccy, 76, fb, 3);     // Horizontale Linie

    // Zoom-Text
    drawHCenter(&ArialBold16, "ZOOM", MAP_BTN_X, MAP_BTN_W, 448, fb);
    drawHCenter(&ArialBold32, ZOOM_LABEL[mapZoomIdx], MAP_BTN_X, MAP_BTN_W, 486, fb);

    // === RENDER ===
    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

// Touch-Handler
static MapAction checkMapTap(int tx, int ty) {
    if (tx>=MAP_BTN_X && tx<MAP_BTN_X+MAP_BTN_W) {
        if (ty>=MAP_BTN_PLUS_Y && ty<MAP_BTN_PLUS_Y+MAP_BTN_H) return MAP_ZOOM_IN;
        if (ty>=MAP_BTN_MINUS_Y && ty<MAP_BTN_MINUS_Y+MAP_BTN_H) return MAP_ZOOM_OUT;
        if (ty>=MAP_BTN_CENTER_Y && ty<MAP_BTN_CENTER_Y+MAP_BTN_H) return MAP_RECENTER;
    }
    return MAP_NONE;
}
