#pragma once
// contours.h — Hoehenlinien (Konturen) aus dem Region-Pack: Daten + PSRAM-Pool.
// Punkte verweisen in einen einzigen PSRAM-Block (haelt DRAM frei fuer WiFi/BLE).
// Rendering (drawContours) liegt in map_screen.h.
#include <Arduino.h>
#include "esp_heap_caps.h"

struct ContourPt { float lat, lon; };
struct Contour {
    int16_t height_m;
    uint8_t flag;       // 0 = normal, 1 = Index (alle 500 m)
    int pts_off;        // Index in contPool
    int num_pts;
};

static const int CONT_MAX  = 1500;     // max Konturen   (~18 KB DRAM-Index)
static const int CONT_POOL = 60000;    // max Punkte ges. (PSRAM: 60000*8 = 480 KB)
static Contour contours[CONT_MAX];
static ContourPt *contPool = nullptr;
static int contour_count = 0;
static int contPoolUsed = 0;

// Mittelpunkt der zuletzt geladenen Pack-Daten — Karten-Fallback, wenn (noch) kein GPS-Fix.
static double packCenterLat = 0, packCenterLon = 0;

static bool contInit() {
    if (!contPool) {
        contPool = (ContourPt*)heap_caps_malloc((size_t)CONT_POOL * sizeof(ContourPt), MALLOC_CAP_SPIRAM);
        if (!contPool) { Serial.println("[CONT] PSRAM-Pool FAIL"); return false; }
        Serial.printf("[CONT] Pool in PSRAM: %d KB\n", (int)(CONT_POOL*sizeof(ContourPt)/1024));
    }
    contour_count = 0; contPoolUsed = 0;
    return true;
}

// Neue Kontur beginnen; Punkte per contAddPt; mit contEndContour abschliessen.
static void contBegin(int16_t h, uint8_t flag) {
    if (contour_count >= CONT_MAX) return;
    contours[contour_count].height_m = h;
    contours[contour_count].flag = flag;
    contours[contour_count].pts_off = contPoolUsed;
    contours[contour_count].num_pts = 0;
}
static void contAddPt(float lat, float lon) {
    if (!contPool || contour_count >= CONT_MAX || contPoolUsed >= CONT_POOL) return;
    contPool[contPoolUsed].lat = lat;
    contPool[contPoolUsed].lon = lon;
    contPoolUsed++;
    contours[contour_count].num_pts++;
}
static void contEnd() {
    if (contour_count >= CONT_MAX) return;
    if (contours[contour_count].num_pts >= 2) contour_count++;     // gueltig
    else contPoolUsed = contours[contour_count].pts_off;            // verwerfen, Pool zuruecknehmen
}
static inline ContourPt* contPoints(const Contour &c) {
    return contPool ? &contPool[c.pts_off] : nullptr;
}
