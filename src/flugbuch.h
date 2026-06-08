#pragma once
// flugbuch.h — Flugbuch-Verwaltung + Anzeige
// Speichert Flug-Zusammenfassungen in NVS, IGC auf SD (spaeter)
// Orientiert an Flight Buddy V5
#include "epdiy.h"
#include "epd_highlevel.h"
#include <string.h>
#include <stdio.h>
#include "arialbold40.h"
#include "arialbold28.h"
#include "arialbold16.h"

// Max Fluege im Speicher
static const int MAX_FLIGHTS = 20;

struct FlightRecord {
    uint16_t year;
    uint8_t month, day, hour, minute;
    uint16_t duration_sec;      // Flugdauer in Sekunden
    int16_t  max_alt;           // Max Hoehe MSL (m)
    int16_t  start_alt;         // Starthoehe (m)
    int16_t  max_climb;         // Max Steigen (cm/s → /100 = m/s)
    uint32_t track_dist_m;      // GPS-Spur Distanz (m)
    uint32_t straight_dist_m;   // Luftlinie Start→Landung (m)
    bool     valid;
};

class Flugbuch {
public:
    FlightRecord flights[MAX_FLIGHTS];
    int count = 0;

    // Flug hinzufuegen
    void addFlight(const FlightRecord &f) {
        if (count < MAX_FLIGHTS) {
            flights[count++] = f;
        } else {
            // Aeltesten ueberschreiben (Ringpuffer)
            for (int i = 0; i < MAX_FLIGHTS-1; i++) flights[i] = flights[i+1];
            flights[MAX_FLIGHTS-1] = f;
        }
        // TODO: in NVS speichern
    }

    // Demo-Daten fuer Anzeige
    void addDemoFlights() {
        FlightRecord f1 = {2026,6,7, 14,23, 4320, 2847, 489, 340, 12400, 8200, true};
        FlightRecord f2 = {2026,6,6, 11,45, 2280, 1650, 520, 280, 5800, 3100, true};
        FlightRecord f3 = {2026,6,5, 15,2,  7500, 3120, 470, 420, 28500, 15600, true};
        addFlight(f1); addFlight(f2); addFlight(f3);
    }
};

// === Flugbuch-Screen ===
static void _ft(const EpdFont *f, const char *s, int x, int y, uint8_t *fb) {
    int cx=x, cy=y; EpdFontProperties p=epd_font_properties_default(); p.fg_color=0;
    epd_write_string(f,s,&cx,&cy,fb,&p);
}
static void _ff(int x,int y,int w,int h,uint8_t *fb) { EpdRect r={x,y,w,h}; epd_fill_rect(r,0,fb); }

static void showFlugbuchScreen(EpdiyHighlevelState *hl, const Flugbuch &fb_data) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[64];

    // Titel
    snprintf(buf, 64, "FLUGBUCH  %d Fluege", fb_data.count);
    _ft(&ArialBold28, buf, 250, 35, fb);
    _ff(20, 50, 920, 2, fb);

    // Spalten-Header
    _ft(&ArialBold16, "Datum", 30, 75, fb);
    _ft(&ArialBold16, "Dauer", 200, 75, fb);
    _ft(&ArialBold16, "MaxAlt", 330, 75, fb);
    _ft(&ArialBold16, "MaxClimb", 460, 75, fb);
    _ft(&ArialBold16, "Spur km", 620, 75, fb);
    _ft(&ArialBold16, "Strecke", 780, 75, fb);
    _ff(20, 90, 920, 1, fb);

    // Fluege (neueste zuerst, max 5 auf Screen)
    int y = 110;
    int shown = 0;
    for (int i = fb_data.count - 1; i >= 0 && shown < 5; i--, shown++) {
        const FlightRecord &f = fb_data.flights[i];
        if (!f.valid) continue;

        // Datum
        snprintf(buf, 64, "%02d.%02d.%04d", f.day, f.month, f.year);
        _ft(&ArialBold16, buf, 30, y, fb);

        // Dauer
        int dur_min = f.duration_sec / 60;
        snprintf(buf, 64, "%dh%02dm", dur_min/60, dur_min%60);
        _ft(&ArialBold28, buf, 190, y+5, fb);

        // Max Hoehe
        snprintf(buf, 64, "%dm", f.max_alt);
        _ft(&ArialBold28, buf, 330, y+5, fb);

        // Max Steigen
        snprintf(buf, 64, "+%.1f", f.max_climb / 100.0f);
        _ft(&ArialBold28, buf, 470, y+5, fb);

        // GPS-Spur
        snprintf(buf, 64, "%.1f", f.track_dist_m / 1000.0f);
        _ft(&ArialBold28, buf, 620, y+5, fb);

        // Strecke (Luftlinie)
        snprintf(buf, 64, "%.1f", f.straight_dist_m / 1000.0f);
        _ft(&ArialBold28, buf, 790, y+5, fb);

        y += 75;
        _ff(20, y-5, 920, 1, fb);  // Trennlinie
    }

    if (fb_data.count == 0) {
        _ft(&ArialBold28, "Keine Fluege aufgezeichnet", 220, 250, fb);
    }

    // Footer-Buttons
    _ff(20, 480, 920, 2, fb);
    // TODO: Upload/Loeschen Buttons
    _ft(&ArialBold16, "Wischen = zurueck    Upload via App (TODO)", 200, 520, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}
