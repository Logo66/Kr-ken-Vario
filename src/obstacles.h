#pragma once
// obstacles.h — BAZL-Luftfahrthindernisse: die SCHLECHT SICHTBAREN Gefahren.
// Seile, Hochspannungsleitungen, Seilbahnen, Masten/Antennen, Windraeder, Krane.
// KEINE Berge/Gipfel (die sieht man) und KEIN Flugverkehr (Sichtflug-Pilot weicht selbst aus).
//
// Datei /obstacles/obstacles.txt, je Zeile:  lat1;lon1;lat2;lon2;top_m;type;name
//   - Punkt (Mast/Antenne/Kran/Windrad): lat2/lon2 == lat1/lon1 (oder leer -> kopiert).
//   - Linie (Kabel/Leitung/Seilbahn): zwei Endpunkte je Spannfeld; lange Leitungen
//     werden vom Konverter in einzelne Spannfeld-Segmente zerlegt.
//   - top_m = Oberkante in Meter ueber Meer (ASL).
// Vorkonvertiert aus dem amtlichen BAZL-Datensatz (WGS84) — siehe tools/obstacles/.
#include <Arduino.h>
#include <SD.h>
#include "esp_heap_caps.h"

struct Obstacle {
    float   lat1, lon1, lat2, lon2;   // Punkt: beide Paare gleich; Linie: zwei Enden
    int16_t top_m;                    // Oberkante in m ueber Meer
    uint8_t type;                     // 0=Mast/Antenne 1=Kabel/Leitung 2=Seilbahn 3=Windrad 4=Kran 5=Gebaeude
    char    name[20];
};

static const int OBST_MAX = 6000;     // CH-weit grosszuegig (Spannfeld-Segmente)
static Obstacle *obstacles_arr = nullptr;   // alloc in parseObstacles (PSRAM)
static int obstacle_count = 0;

static const char* obstacleTypeStr(uint8_t t) {
    switch (t) {
        case 1: return "Kabel";    case 2: return "Seilbahn"; case 3: return "Windrad";
        case 4: return "Kran";     case 5: return "Gebaeude"; default: return "Mast";
    }
}

static int parseObstacles(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[OBST] Datei nicht gefunden: %s\n", path); return 0; }
    if (!obstacles_arr) {
        obstacles_arr = (Obstacle*)heap_caps_malloc((size_t)OBST_MAX * sizeof(Obstacle), MALLOC_CAP_SPIRAM);
        if (!obstacles_arr) { Serial.println("[OBST] PSRAM-Alloc fehlgeschlagen!"); f.close(); return 0; }
    }
    obstacle_count = 0;
    Serial.printf("[OBST] Parse %s (%d bytes)...\n", path, f.size());
    while (f.available() && obstacle_count < OBST_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int p[6], last = -1; bool ok = true;            // 6 Semikolons -> 7 Felder
        for (int i = 0; i < 6; i++) { p[i] = line.indexOf(';', last + 1); if (p[i] < 0) { ok = false; break; } last = p[i]; }
        if (!ok) continue;
        Obstacle &o = obstacles_arr[obstacle_count];
        o.lat1  = line.substring(0,        p[0]).toFloat();
        o.lon1  = line.substring(p[0] + 1, p[1]).toFloat();
        o.lat2  = line.substring(p[1] + 1, p[2]).toFloat();
        o.lon2  = line.substring(p[2] + 1, p[3]).toFloat();
        o.top_m = (int16_t)line.substring(p[3] + 1, p[4]).toInt();
        o.type  = (uint8_t)line.substring(p[4] + 1, p[5]).toInt();
        strncpy(o.name, line.substring(p[5] + 1).c_str(), 19);
        o.name[19] = 0;
        if (o.lat2 == 0 && o.lon2 == 0) { o.lat2 = o.lat1; o.lon2 = o.lon1; }   // Punkt
        if (o.lat1 != 0 && o.lon1 != 0) obstacle_count++;
        yield();
    }
    f.close();
    Serial.printf("[OBST] Fertig: %d Hindernisse geladen\n", obstacle_count);
    return obstacle_count;
}
