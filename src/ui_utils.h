#pragma once
// ui_utils.h — Shared UI-Hilfsfunktionen mit EXAKTER Textzentrierung
// Nutzt epd_get_text_bounds() — KEINE Schaetzungen
#include "epdiy.h"
#include "epd_highlevel.h"
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

// === Exakte Text-Messung ===
// Gibt die Pixelbreite und -hoehe eines Strings zurueck
static void measureText(const EpdFont *font, const char *text, int *w, int *h) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    EpdFontProperties p = epd_font_properties_default();
    epd_get_text_bounds(font, text, &x0, &y0, &x1, &y1, w, h, &p);
}

// === Text zeichnen an exakter Position ===
static void drawText(const EpdFont *font, const char *text,
                     int x, int y, uint8_t *fb, uint8_t color = 0) {
    int cx = x, cy = y;
    EpdFontProperties p = epd_font_properties_default();
    p.fg_color = color;
    epd_write_string(font, text, &cx, &cy, fb, &p);
}

// === Horizontal zentriert in einem Feld ===
// field_x, field_w: linker Rand und Breite des Feldes
// y: Baseline des Textes
static void drawHCenter(const EpdFont *font, const char *text,
                        int field_x, int field_w, int y, uint8_t *fb, uint8_t color = 0) {
    int tw = 0, th = 0;
    measureText(font, text, &tw, &th);
    int x = field_x + (field_w - tw) / 2;
    drawText(font, text, x, y, fb, color);
}

// === Horizontal + Vertikal zentriert in einer Box ===
// bx, by, bw, bh: Box-Koordinaten
static void drawBoxCenter(const EpdFont *font, const char *text,
                          int bx, int by, int bw, int bh, uint8_t *fb, uint8_t color = 0) {
    int tw = 0, th = 0;
    measureText(font, text, &tw, &th);
    int x = bx + (bw - tw) / 2;
    int y = by + (bh + th) / 2;  // Baseline = Mitte + halbe Texthoehe
    drawText(font, text, x, y, fb, color);
}

// === Geometrie-Hilfen ===
static void uiHLine(int x, int y, int w, uint8_t *fb, int thick = 2) {
    EpdRect r = {x, y, w, thick};
    epd_fill_rect(r, 0, fb);
}

static void uiVLine(int x, int y, int h, uint8_t *fb, int thick = 2) {
    EpdRect r = {x, y, thick, h};
    epd_fill_rect(r, 0, fb);
}

static void uiFill(int x, int y, int w, int h, uint8_t *fb, uint8_t color = 0) {
    EpdRect r = {x, y, w, h};
    epd_fill_rect(r, color, fb);
}

// Box mit Rahmen (4px)
static void uiBox(int x, int y, int w, int h, uint8_t *fb) {
    uiFill(x, y, w, 4, fb);           // oben
    uiFill(x, y + h - 4, w, 4, fb);   // unten
    uiFill(x, y, 4, h, fb);           // links
    uiFill(x + w - 4, y, 4, h, fb);   // rechts
}

// === EINHEITLICHE STATUSLEISTE (alle Flug-Screens) ===========================
// Reihenfolge: Uhr | Sat | FANET | Buddy | Batterie
// "Buddy" = Verbindung zum Buddy-Server (eine Verbindung, ein Kreis).
// Eigener Dot-Helfer (sbDot) -> kein Konflikt mit den per-Screen fillCircle/drawCircle.
static void sbDot(int cx, int cy, int r, bool filled, uint8_t *fb) {
    for (int y=-r; y<=r; y++)
        for (int x=-r; x<=r; x++) {
            int dd = x*x + y*y;
            if (filled ? (dd <= r*r) : (dd <= r*r && dd >= (r-2)*(r-2)))
                uiFill(cx+x, cy+y, 1, 1, fb);
        }
}

// Gemeinsamer Status-Zustand — main.cpp fuellt ihn 1x pro Loop, jeder Screen liest ihn.
// "server" = Verbindung zum Buddy-Server (treibt den Buddy-Kreis).
struct StatusBarState { int hh=0, mm=0, sats=0, fanet=0, bat=0; bool server=false, ble=false, wifi=false; };
static StatusBarState g_status;

static void statusBarSet(int hh, int mm, int sats, int fanet, bool server, int bat,
                         bool ble=false, bool wifi=false) {
    g_status.hh=hh; g_status.mm=mm; g_status.sats=sats; g_status.fanet=fanet;
    g_status.server=server; g_status.bat=bat; g_status.ble=ble; g_status.wifi=wifi;
}

// Zeichnet die EINHEITLICHE Statusleiste oben — auf JEDEM Screen identisch.
static void drawStatusBar(uint8_t *fb) {
    char b[24];
    snprintf(b, 24, "%02d:%02d", g_status.hh, g_status.mm);
    drawText(&ArialBold16, b, 22, 38, fb);                                  // Uhr (links)
    snprintf(b, 24, "Sat %d", g_status.sats);
    drawText(&ArialBold16, b, 150, 38, fb);                                 // Sat (Zahl, keine Punkte-Reihe)
    snprintf(b, 24, "FANET %d", g_status.fanet);
    drawText(&ArialBold16, b, 332, 38, fb);                                 // FANET
    // Buddy-Server-Verbindung = EINE Verbindung -> nur "Buddy" + ein Kreis
    sbDot(494, 29, 8, g_status.server, fb); drawText(&ArialBold16, "BUDDY", 510, 38, fb);
    // BLE + WLAN — nur sichtbar, wenn tatsaechlich verbunden (sonst gar nichts)
    if (g_status.ble)  drawText(&ArialBold16, "BLE",  600, 38, fb);
    if (g_status.wifi) drawText(&ArialBold16, "WLAN", 686, 38, fb);
    uiBox(866,16,58,26,fb); uiFill(924,22,7,14,fb);
    uiFill(870,20,(int)(42.0f*g_status.bat/100.0f),18,fb);                  // Batterie (rechts)
    uiHLine(14, 48, 932, fb);                                               // Trennlinie (kompakt: Platz fuer Cruise/Thermik)
}
