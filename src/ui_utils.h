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
