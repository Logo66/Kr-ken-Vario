#pragma once
// goal_screen.h — KRUECKE-5 Goal/Final Glide
// ALLE Texte BERECHNET mit measureText + drawHCenter/drawBoxCenter
#include "ui_utils.h"
#include "arialbold72.h"
#include "arialbold32.h"
#include "arialbold24.h"
#include <math.h>

struct GoalData {
    const char *wp_name;
    float distance_km, arrival_m, gr_needed, gr_current;
    float bearing_abs, bearing_rel;
    int rtc_hour, rtc_min, sats, bat_pct, fanet_peers;
    bool buddy_connected;
    const char *buddy_hint;
};

// Dreieck-Formel aus Ticket §2
static void drawTri(int cx, int cy, float ang, int L, int Wd, uint8_t *fb) {
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

// Layout-Felder
static const int GL_LEFT_W = 556;   // Linke Spalte bis Divider
static const int GL_RIGHT_X = 580;  // Rechte Spalte
static const int GL_RIGHT_W = 380;  // Rechte Spalte Breite
// Linke Sub-Felder
static const int GL_LABEL_X = 38;
static const int GL_VALUE_X = 34;
static const int GL_VALUE_W = 522;  // 556-34
// GR Felder (zwei nebeneinander)
static const int GL_GR1_X = 34, GL_GR1_W = 100;
static const int GL_GR2_X = 140, GL_GR2_W = 416;

static void showGoalScreen(EpdiyHighlevelState *hl, const GoalData &d,
                           enum EpdDrawMode mode = MODE_GC16) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];
    int tw, th;

    // === STATUSBAR (Ticket §3, alle Positionen aus Spec) ===
    snprintf(buf,48,"%02d:%02d",d.rtc_hour,d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);

    for(int i=0;i<11;i++){
        int cx=150+i*15;
        if(i<d.sats) fillCircle(cx,29,5,fb);
        else drawCircle(cx,29,5,fb);
    }

    snprintf(buf,48,"FANET %d",d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);

    if(d.buddy_connected) fillCircle(494,29,8,fb);
    else drawCircle(494,29,8,fb);
    drawText(&ArialBold16, "BUDDY", 510, 38, fb);

    uiBox(866,16,58,26,fb);
    uiFill(924,22,7,14,fb);
    uiFill(870,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);

    uiHLine(14, 54, 932, fb);

    // === LINKE SPALTE ===

    // Ziel-Pfeil + Name
    drawTri(50, 82, 90, 30, 22, fb);
    snprintf(buf,48,"ZIEL: %s", d.wp_name);
    // Prüfen ob Name passt (max GL_LEFT_W - 76 = 480px)
    measureText(&ArialBold24, buf, &tw, &th);
    if (tw > GL_LEFT_W - 76) {
        drawText(&ArialBold16, buf, 76, 92, fb);  // Fallback kleiner
    } else {
        drawText(&ArialBold24, buf, 76, 92, fb);
    }

    // Label
    drawText(&ArialBold16, "ANKUNFT UEBER ZIEL", GL_LABEL_X, 142, fb);

    // Vorzeichen-Dreieck
    drawTri(68, 210, (d.arrival_m>=0)?0:180, 56, 46, fb);

    // Ankunftswert — BERECHNET: links bei x=108, prüfe Breite
    snprintf(buf,48,"%+.0f", d.arrival_m);
    measureText(&ArialBold72, buf, &tw, &th);
    if (tw > GL_LEFT_W - 108 - 50) {
        // Zu breit → kleinere Font
        drawText(&ArialBold40, buf, 108, 230, fb);
        measureText(&ArialBold40, buf, &tw, &th);
        drawText(&ArialBold28, "m", 108+tw+8, 230, fb);
    } else {
        drawText(&ArialBold72, buf, 108, 238, fb);
        drawText(&ArialBold32, "m", 108+tw+12, 238, fb);
    }

    uiHLine(GL_LABEL_X, 258, GL_LEFT_W-GL_LABEL_X, fb);

    // Distanz — links, prüfe Breite
    drawText(&ArialBold16, "DISTANZ", GL_LABEL_X, 296, fb);
    snprintf(buf,48,"%.1f km", d.distance_km);
    measureText(&ArialBold40, buf, &tw, &th);
    if (tw > GL_VALUE_W) {
        drawText(&ArialBold28, buf, GL_VALUE_X, 348, fb);
    } else {
        drawText(&ArialBold40, buf, GL_VALUE_X, 352, fb);
    }

    // Gleitzahl — zwei Felder nebeneinander
    drawText(&ArialBold16, "GLEITZAHL NOETIG / IST", GL_LABEL_X, 402, fb);

    snprintf(buf,48,"%.1f", d.gr_needed);
    measureText(&ArialBold32, buf, &tw, &th);
    drawText(&ArialBold32, buf, GL_GR1_X, 442, fb);

    snprintf(buf,48,"/ %.1f", d.gr_current);
    measureText(&ArialBold32, buf, &tw, &th);
    // Platziere rechts vom ersten Wert
    int gr1_tw;
    {char b2[16]; snprintf(b2,16,"%.1f",d.gr_needed); measureText(&ArialBold32,b2,&gr1_tw,&th);}
    drawText(&ArialBold32, buf, GL_GR1_X + gr1_tw + 16, 442, fb);

    // === RECHTE SPALTE ===
    uiVLine(580, 64, 388, fb);

    // Ring + Pfeil
    int rcx=762, rcy=222, rr=118;
    drawCircle(rcx, rcy, rr, fb);
    drawTri(rcx, rcy, d.bearing_rel, 150, 92, fb);

    // Peilung — BERECHNET zentriert
    snprintf(buf,48,"%.0f > %.0f", d.bearing_abs, d.bearing_rel);
    drawHCenter(&ArialBold32, buf, GL_RIGHT_X, GL_RIGHT_W, 392, fb);

    // === BUDDY-BAND (nur wenn connected) ===
    if (d.buddy_connected && d.buddy_hint) {
        uiBox(14, 470, 932, 54, fb);
        uiFill(14, 470, 104, 54, fb);
        drawBoxCenter(&ArialBold16, "BUDDY", 14, 104, 470, 54, fb, 0xFF);
        // Hinweis — prüfe Breite (max 932-132-14 = 786px)
        measureText(&ArialBold16, d.buddy_hint, &tw, &th);
        if (tw < 786) {
            drawText(&ArialBold16, d.buddy_hint, 132, 505, fb);
        } else {
            drawText(&ArialBold16, "...", 132, 505, fb);  // Truncate
        }
    }

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}

static void showDemoGoalScreen(EpdiyHighlevelState *hl) {
    GoalData gd={};
    gd.wp_name="FIESCH"; gd.distance_km=12.4f; gd.arrival_m=340;
    gd.gr_needed=6.8f; gd.gr_current=9.1f;
    gd.bearing_abs=247; gd.bearing_rel=12;
    gd.rtc_hour=14; gd.rtc_min=45; gd.sats=9; gd.bat_pct=78;
    gd.fanet_peers=3; gd.buddy_connected=true;
    gd.buddy_hint="Hans: +2.1 m/s  3.2 km voraus";
    showGoalScreen(hl, gd);
}
