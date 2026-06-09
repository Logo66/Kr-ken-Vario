#pragma once
// peaks.h — Gipfel-Layer (OSM natural=peak): Daten + Parser.
// Rendering (drawPeaks) liegt in map_screen.h, wo Projektion/Clip verfuegbar sind.
// SD-Datei /peaks/peaks.txt, je Zeile: "lat;lon;ele;name"  (ASCII, transliteriert)
#include <Arduino.h>
#include <SD.h>
#include "esp_heap_caps.h"

struct Peak {
    float lat, lon;
    int16_t ele;
    char name[26];
};

static const int PEAK_MAX = 1200;   // ~1100 Gipfel CH, etwas Reserve
static Peak *peaks_arr = nullptr;    // alloc in parsePeaks (PSRAM) — haelt DRAM frei
static int peak_count = 0;

static int parsePeaks(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[PEAK] Datei nicht gefunden: %s\n", path); return 0; }
    if (!peaks_arr) {
        peaks_arr = (Peak*)heap_caps_malloc((size_t)PEAK_MAX * sizeof(Peak), MALLOC_CAP_SPIRAM);
        if (!peaks_arr) { Serial.println("[PEAK] PSRAM-Alloc fehlgeschlagen!"); f.close(); return 0; }
    }
    peak_count = 0;
    Serial.printf("[PEAK] Parse %s (%d bytes)...\n", path, f.size());
    while (f.available() && peak_count < PEAK_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int s1 = line.indexOf(';');
        int s2 = line.indexOf(';', s1 + 1);
        int s3 = line.indexOf(';', s2 + 1);
        if (s1 < 0 || s2 < 0 || s3 < 0) continue;
        Peak &p = peaks_arr[peak_count];
        p.lat = line.substring(0, s1).toFloat();
        p.lon = line.substring(s1 + 1, s2).toFloat();
        p.ele = (int16_t)line.substring(s2 + 1, s3).toInt();
        strncpy(p.name, line.substring(s3 + 1).c_str(), 25);
        p.name[25] = 0;
        if (p.lat != 0 && p.lon != 0) peak_count++;
        yield();
    }
    f.close();
    Serial.printf("[PEAK] Fertig: %d Gipfel geladen\n", peak_count);
    return peak_count;
}
