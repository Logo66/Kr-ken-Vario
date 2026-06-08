#pragma once
// goal_screen.h — AURA XC GOAL / Final Glide Screen
// KRUECKE-5: Pixelgenaue Umsetzung der Koordinaten-Tabelle
// Alle Positionen 1:1 aus dem Ticket, KEINE Schaetzungen
#include "ui_utils.h"
#include "arialbold72.h"
#include "arialbold32.h"
#include "arialbold24.h"
#include <math.h>

// Font-Tiers (aus Ticket §1)
// T1 HERO = ArialBold72  (~72px Versalhoehe)
// T2 XL   = ArialBold40  (~40px)
// T3 L    = ArialBold32  (~32px)
// T4 TITLE= ArialBold24  (~24px)
// T5 LABEL= ArialBold16  (~15px)
// T6 STATUS=ArialBold16  (~17px)

struct GoalData {
    // Ziel
    const char *wp_name;      // "FIESCH"
    float distance_km;        // km zum Ziel
    float arrival_m;          // Ankunftshoehe ueber/unter Ziel (m, mit Vorzeichen)
    float gr_needed;          // Gleitzahl noetig
    float gr_current;         // Gleitzahl aktuell
    float bearing_abs;        // Absolute Peilung (Grad)
    float bearing_rel;        // Relative Peilung (Grad, 0=geradeaus)

    // Status
    int rtc_hour, rtc_min;
    int sats, bat_pct;
    int fanet_peers;
    bool buddy_connected;
    const char *buddy_hint;   // NULL wenn kein Buddy
};

// === Dreieck-Formel aus Ticket §2 ===
// tri(cx, cy, ang, L, Wd) — ang in Grad, 0=Norden, im Uhrzeigersinn
static void drawTri(int cx, int cy, float ang_deg, int L, int Wd, uint8_t *fb) {
    float a = ang_deg * M_PI / 180.0f;
    float sa = sinf(a), ca = cosf(a);

    float tip_x = cx + 0.6f * L * sa;
    float tip_y = cy - 0.6f * L * ca;
    float bc_x  = cx - 0.4f * L * sa;
    float bc_y  = cy + 0.4f * L * ca;
    float bl_x  = bc_x + 0.5f * Wd * ca;
    float bl_y  = bc_y + 0.5f * Wd * sa;
    float br_x  = bc_x - 0.5f * Wd * ca;
    float br_y  = bc_y - 0.5f * Wd * sa;

    // Scanline-Fill des Dreiecks
    int min_y = (int)fminf(tip_y, fminf(bl_y, br_y));
    int max_y = (int)fmaxf(tip_y, fmaxf(bl_y, br_y));
    for (int y = min_y; y <= max_y; y++) {
        int lx = 960, rx = 0;
        // 3 Kanten pruefen
        float edges[][4] = {
            {tip_x, tip_y, bl_x, bl_y},
            {tip_x, tip_y, br_x, br_y},
            {bl_x, bl_y, br_x, br_y}
        };
        for (int e = 0; e < 3; e++) {
            float y1 = edges[e][1], y2 = edges[e][3];
            if ((y >= y1 && y <= y2) || (y >= y2 && y <= y1)) {
                float dy = y2 - y1;
                if (fabsf(dy) < 0.5f) continue;
                float t = (y - y1) / dy;
                int x = (int)(edges[e][0] + t * (edges[e][2] - edges[e][0]));
                if (x < lx) lx = x;
                if (x > rx) rx = x;
            }
        }
        if (lx <= rx && lx >= 0 && rx < 960 && y >= 0 && y < 540)
            uiFill(lx, y, rx - lx + 1, 1, fb);
    }
}

// === Statusbar (identisch auf allen Screens, aus Ticket §3) ===
static void drawGoalStatusbar(uint8_t *fb, const GoalData &d) {
    char buf[32];
    // Uhr
    snprintf(buf, 32, "%02d:%02d", d.rtc_hour, d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);

    // Sat-Dots
    for (int i = 0; i < 11; i++) {
        int cx = 150 + i * 15, cy = 29;
        if (i < d.sats) fillCircle(cx, cy, 5, fb);
        else drawCircle(cx, cy, 5, fb);
    }

    // FANET
    snprintf(buf, 32, "FANET %d", d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);

    // Buddy
    if (d.buddy_connected) {
        fillCircle(494, 29, 8, fb);
    } else {
        drawCircle(494, 29, 8, fb);
    }
    drawText(&ArialBold16, "BUDDY", 510, 38, fb);

    // Akku
    uiBox(866, 16, 58, 26, fb);
    uiFill(924, 22, 7, 14, fb);
    int fill_w = (int)(42.0f * d.bat_pct / 100.0f);
    uiFill(870, 20, fill_w, 18, fb);

    // Trennlinie
    uiHLine(14, 54, 932, fb);
}

// === GOAL SCREEN ===
static void showGoalScreen(EpdiyHighlevelState *hl, const GoalData &d,
                           enum EpdDrawMode mode = MODE_GC16) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    drawGoalStatusbar(fb, d);

    // === LINKE SPALTE ===

    // Ziel-Pfeil (rechts zeigend)
    drawTri(50, 82, 90, 30, 22, fb);

    // Zielname
    snprintf(buf, 48, "ZIEL: %s", d.wp_name);
    drawText(&ArialBold24, buf, 76, 92, fb);

    // Label
    drawText(&ArialBold16, "ANKUNFT UEBER ZIEL", 38, 142, fb);

    // Vorzeichen-Dreieck (hoch wenn >=0, runter wenn <0)
    float tri_ang = (d.arrival_m >= 0) ? 0 : 180;
    drawTri(68, 210, tri_ang, 56, 46, fb);

    // Ankunftswert (T1 HERO, linkbuendig bei x=108)
    snprintf(buf, 48, "%+.0f", d.arrival_m);
    drawText(&ArialBold72, buf, 108, 238, fb);

    // Einheit "m" — x = rechtes Ende der Zahl + 12px (gemessen!)
    int tw = 0, th = 0;
    measureText(&ArialBold72, buf, &tw, &th);
    drawText(&ArialBold32, "m", 108 + tw + 12, 238, fb);

    // Trennlinie
    uiHLine(38, 258, 518, fb);

    // Distanz
    drawText(&ArialBold16, "DISTANZ", 38, 296, fb);
    snprintf(buf, 48, "%.1f km", d.distance_km);
    drawText(&ArialBold40, buf, 34, 352, fb);

    // Gleitzahl
    drawText(&ArialBold16, "GLEITZAHL NOETIG / IST", 38, 402, fb);
    snprintf(buf, 48, "%.1f", d.gr_needed);
    drawText(&ArialBold32, buf, 34, 442, fb);
    snprintf(buf, 48, "/ %.1f", d.gr_current);
    drawText(&ArialBold32, buf, 134, 442, fb);

    // === RECHTE SPALTE ===

    // Vertikale Trennlinie
    uiVLine(580, 64, 388, fb);

    // Richtungs-Ring
    int ring_cx = 762, ring_cy = 222, ring_r = 118;
    drawCircle(ring_cx, ring_cy, ring_r, fb);

    // Grosser Ziel-Pfeil (dreht mit REL_BEARING)
    drawTri(ring_cx, ring_cy, d.bearing_rel, 150, 92, fb);

    // Peilung Text (zentriert unter dem Ring)
    snprintf(buf, 48, "%.0f%c %.0f%c",
             d.bearing_abs, (d.bearing_rel >= 0) ? '>' : '<',
             fabsf(d.bearing_rel), (d.bearing_rel >= 0) ? '>' : '<');
    drawHCenter(&ArialBold32, buf, 580, 380, 392, fb);

    // === BUDDY-BAND (nur wenn connected + hint) ===
    if (d.buddy_connected && d.buddy_hint) {
        uiBox(14, 470, 932, 54, fb);
        uiFill(14, 470, 104, 54, fb);  // Tag gefuellt
        // "BUDDY" weiss auf schwarz
        EpdFontProperties wp = epd_font_properties_default();
        wp.fg_color = 0xFF;
        int cx = 40, cy = 505;
        epd_write_string(&ArialBold16, "BUDDY", &cx, &cy, fb, &wp);
        // Hinweistext
        drawText(&ArialBold16, d.buddy_hint, 132, 505, fb);
    }

    epd_poweron();
    epd_hl_update_screen(hl, mode, (int)epd_ambient_temperature());
    epd_poweroff();
}

// Demo
static void showDemoGoalScreen(EpdiyHighlevelState *hl) {
    GoalData gd = {};
    gd.wp_name = "FIESCH";
    gd.distance_km = 12.4f;
    gd.arrival_m = 340;
    gd.gr_needed = 6.8f;
    gd.gr_current = 9.1f;
    gd.bearing_abs = 247;
    gd.bearing_rel = 12;
    gd.rtc_hour = 14;
    gd.rtc_min = 45;
    gd.sats = 9;
    gd.bat_pct = 78;
    gd.fanet_peers = 3;
    gd.buddy_connected = true;
    gd.buddy_hint = "Hans: +2.1 m/s  3.2 km voraus";
    showGoalScreen(hl, gd);
}
