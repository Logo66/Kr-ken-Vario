#pragma once
#include "ui_utils.h"

static const int MAX_FLIGHTS = 20;

struct FlightRecord {
    uint16_t year; uint8_t month, day, hour, minute;
    uint16_t duration_sec;
    int16_t max_alt, start_alt, max_climb;
    int16_t max_g;              // Max G-Kraft (×100, z.B. 320 = 3.2g)
    uint32_t track_dist_m, straight_dist_m;
    bool valid;
};

class Flugbuch {
public:
    FlightRecord flights[MAX_FLIGHTS];
    int count = 0;
    void addFlight(const FlightRecord &f) {
        if (count < MAX_FLIGHTS) flights[count++] = f;
        else { for(int i=0;i<MAX_FLIGHTS-1;i++) flights[i]=flights[i+1]; flights[MAX_FLIGHTS-1]=f; }
    }
    void addDemoFlights() {
        FlightRecord f1={2026,6,7,14,23,4320,2847,489,340,280,12400,8200,true};
        FlightRecord f2={2026,6,6,11,45,2280,1650,520,280,220,5800,3100,true};
        FlightRecord f3={2026,6,5,15,2,7500,3120,470,420,350,28500,15600,true};
        addFlight(f1); addFlight(f2); addFlight(f3);
    }
};

static void showFlugbuchScreen(EpdiyHighlevelState *hl, const Flugbuch &data) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[64];

    // Titel exakt zentriert
    drawHCenter(&ArialBold28, "FLUGBUCH", 0, 960, 55, fb);
    snprintf(buf, 64, "%d Fluege", data.count);
    drawHCenter(&ArialBold16, buf, 0, 960, 80, fb);
    uiHLine(20, 95, 920, fb);

    // 7 Spalten
    const int COL_W = 130;
    const int COL_X[] = {10, 140, 275, 400, 520, 650, 820};
    const char *HEADERS[] = {"Datum", "Dauer", "MaxAlt", "Climb", "Gmax", "Spur", "Strecke"};
    for (int i = 0; i < 7; i++)
        drawHCenter(&ArialBold16, HEADERS[i], COL_X[i], COL_W, 120, fb);
    uiHLine(20, 133, 920, fb);

    int y = 155;
    int shown = 0;
    for (int i = data.count-1; i >= 0 && shown < 6; i--, shown++) {
        const FlightRecord &f = data.flights[i];
        if (!f.valid) continue;

        snprintf(buf, 64, "%02d.%02d.%02d", f.day, f.month, f.year%100);
        drawHCenter(&ArialBold16, buf, COL_X[0], COL_W, y, fb);

        int dm = f.duration_sec/60;
        snprintf(buf, 64, "%dh%02d", dm/60, dm%60);
        drawHCenter(&ArialBold16, buf, COL_X[1], COL_W, y, fb);

        snprintf(buf, 64, "%dm", f.max_alt);
        drawHCenter(&ArialBold16, buf, COL_X[2], COL_W, y, fb);

        snprintf(buf, 64, "+%.1f", f.max_climb/100.0f);
        drawHCenter(&ArialBold16, buf, COL_X[3], COL_W, y, fb);

        snprintf(buf, 64, "%.1fg", f.max_g/100.0f);
        drawHCenter(&ArialBold16, buf, COL_X[4], COL_W, y, fb);

        snprintf(buf, 64, "%.1fkm", f.track_dist_m/1000.0f);
        drawHCenter(&ArialBold16, buf, COL_X[5], COL_W, y, fb);

        snprintf(buf, 64, "%.1fkm", f.straight_dist_m/1000.0f);
        drawHCenter(&ArialBold16, buf, COL_X[6], COL_W, y, fb);

        y += 30;
        uiHLine(20, y-5, 920, fb, 1);
    }

    if (data.count == 0)
        drawHCenter(&ArialBold28, "Keine Fluege", 0, 960, 280, fb);

    uiHLine(20, 480, 920, fb);
    drawHCenter(&ArialBold16, "Wischen = zurueck    Upload via App (TODO)", 0, 960, 520, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}
