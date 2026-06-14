#pragma once
// obstacles.h — BAZL-Luftfahrthindernisse: die SCHLECHT SICHTBAREN Gefahren.
// Seile, Hochspannungsleitungen, Seilbahnen, Masten/Antennen, Windraeder, Krane.
// KEINE Berge/Gipfel (die sieht man) und KEIN Flugverkehr (Sichtflug-Pilot weicht selbst aus).
//
// XC-Modell (wie die Lufträume): ganzes LAND liegt auf der SD, wird beim Boot in PSRAM
// geladen und IM FLUG OFFLINE abgefragt (kein Netz noetig, bis 400 km Strecke).
// Datei /obstacles/obstacles.txt, je Zeile:  lat1;lon1;lat2;lon2;top_m;type[;rest]
//   - Punkt (Mast/Antenne/Kran/Windrad): lat2/lon2 == lat1/lon1 (oder 0 -> kopiert).
//   - Linie (Kabel/Leitung/Seilbahn): zwei Endpunkte je Spannfeld.
//   - top_m = Oberkante in Meter ueber Meer (ASL).
// Vorkonvertiert aus dem amtlichen BAZL-Datensatz (WGS84) — siehe tools/obstacles/.
//
// Schneller Loader: readBytesUntil + sscanf (KEIN Arduino-String pro Zeile) -> ganze
// CH (~104k Segmente, 4.7 MB) in wenigen Sekunden. Kapazitaet aus Dateigroesse geschaetzt.
#include <Arduino.h>
#include <SD.h>
#include "esp_heap_caps.h"

struct Obstacle {                     // schlank: 19 Byte (kein Name) -> ~2 MB fuer ganze CH
    float   lat1, lon1, lat2, lon2;   // Punkt: beide Paare gleich; Linie: zwei Enden
    int16_t top_m;                    // Oberkante in m ueber Meer
    uint8_t type;                     // 0=Mast/Antenne 1=Kabel/Leitung 2=Seilbahn 3=Windrad 4=Kran
};

static const int OBST_MAX = 130000;   // Obergrenze (ganze CH ~104k; Reserve fuer groessere Laender)
static Obstacle *obstacles_arr = nullptr;
static int obstacle_count = 0;
static int obstacle_cap   = 0;

static const char* obstacleTypeStr(uint8_t t) {
    switch (t) {
        case 1: return "Kabel";    case 2: return "Seilbahn"; case 3: return "Windrad";
        case 4: return "Kran";     default: return "Mast";
    }
}

static int parseObstacles(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[OBST] Datei nicht gefunden: %s\n", path); return 0; }
    long est = f.size() / 40 + 16;                 // ~40 Byte/Zeile -> Kapazitaet schaetzen
    int cap = est > OBST_MAX ? OBST_MAX : (int)est;
    if (cap < 1) cap = 1;
    if (obstacles_arr && cap > obstacle_cap) { heap_caps_free(obstacles_arr); obstacles_arr = nullptr; }
    if (!obstacles_arr) {
        obstacles_arr = (Obstacle*)heap_caps_malloc((size_t)cap * sizeof(Obstacle), MALLOC_CAP_SPIRAM);
        if (!obstacles_arr) { Serial.println("[OBST] PSRAM-Alloc fehlgeschlagen!"); f.close(); return 0; }
        obstacle_cap = cap;
    }
    obstacle_count = 0;
    Serial.printf("[OBST] Parse %s (%u bytes, cap %d)...\n", path, (unsigned)f.size(), cap);
    unsigned long t0 = millis();
    char line[96];
    while (obstacle_count < obstacle_cap && f.available()) {
        int len = f.readBytesUntil('\n', (uint8_t*)line, sizeof(line) - 1);
        if (len <= 0) continue;
        line[len] = 0;
        if (line[0] == '#') continue;
        float a, b, c, d; int top, type;
        if (sscanf(line, "%f;%f;%f;%f;%d;%d", &a, &b, &c, &d, &top, &type) != 6) continue;
        Obstacle &o = obstacles_arr[obstacle_count];
        o.lat1 = a; o.lon1 = b; o.lat2 = c; o.lon2 = d;
        o.top_m = (int16_t)top; o.type = (uint8_t)type;
        if (o.lat2 == 0 && o.lon2 == 0) { o.lat2 = o.lat1; o.lon2 = o.lon1; }   // Punkt
        if (o.lat1 != 0 && o.lon1 != 0) obstacle_count++;
        if ((obstacle_count & 8191) == 0) yield();      // Watchdog bei grossen Dateien
    }
    f.close();
    Serial.printf("[OBST] Fertig: %d Hindernisse in %lu ms\n", obstacle_count, millis() - t0);
    return obstacle_count;
}
