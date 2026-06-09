#pragma once
// openair_parser.h — OpenAir Luftraum-Parser
// Liest .txt Dateien im OpenAir-Format von SD → Polygon-Structs im RAM
// Befehle: AC, AN, AH, AL, DP, DC, DA, DB, V X=, V D=
// Referenz: http://www.winpilot.com/UsersGuide/UserAirspace.asp
#include <Arduino.h>
#include <SD.h>
#include "esp_heap_caps.h"
#include <math.h>

// Luftraum-Klassen
enum AirspaceClass {
    ASP_A, ASP_B, ASP_C, ASP_D, ASP_E, ASP_F, ASP_G,
    ASP_CTR, ASP_R, ASP_Q, ASP_P, ASP_TMA, ASP_TMZ,
    ASP_GLIDER, ASP_WAVE, ASP_OTHER
};

// Einzelner Polygon-Punkt
struct AirPoint {
    float lat, lon;
};

// Ein Luftraum (max 64 Punkte pro Polygon)
static const int ASP_MAX_POINTS = 64;
struct Airspace {
    char name[32];
    char upper[16];      // z.B. "FL100", "2500 MSL"
    char lower[16];      // z.B. "GND", "1500 MSL"
    AirspaceClass cls;
    AirPoint pts[ASP_MAX_POINTS];
    int num_pts;
    bool active;         // Anzeigen ja/nein (konfigurierbar)
};

// Globaler Speicher: max 200 Luftraeume — in PSRAM, damit DRAM-Heap fuer WiFi/BLE frei bleibt
static const int ASP_MAX = 200;
static Airspace *airspaces = nullptr;   // alloc in parseOpenAir (PSRAM)
static int airspace_count = 0;

// Hilfsfunktion: "47:35:12 N" oder "47:35.2 N" → Dezimalgrad
static float parseCoord(const char *s) {
    // Formate: DD:MM:SS N/S/E/W oder DD:MM.MM N/S/E/W
    float deg = 0, min = 0, sec = 0;
    char dir = 'N';
    // Versuche DD:MM:SS
    int n = sscanf(s, "%f:%f:%f %c", &deg, &min, &sec, &dir);
    if (n < 3) {
        // Versuche DD:MM.MM
        sec = 0;
        sscanf(s, "%f:%f %c", &deg, &min, &dir);
    }
    float result = deg + min / 60.0f + sec / 3600.0f;
    if (dir == 'S' || dir == 'W') result = -result;
    return result;
}

// Parse "47:13:4.0080 N 008:55:26.0040 E" (Leerzeichen-Format von openaip.net) -> lat, lon
// Fallback: Komma-getrennt "lat, lon". Reine Stack-Puffer, Laengen-Guards.
static bool parseLatLon(const char *s, float *lat, float *lon) {
    // Suche N/S als Trennzeichen zwischen Lat und Lon
    const char *sep = nullptr;
    for (const char *p = s; *p; p++) {
        if ((*p == 'N' || *p == 'S') && (p > s) && (*(p+1) == ' ' || *(p+1) == '\t')) {
            sep = p;
            break;
        }
    }
    if (sep) {
        // Leerzeichen-getrennt: "lat N lon E"
        int lat_len = (int)(sep - s + 1);
        char lat_str[64], lon_str[64];
        if (lat_len > 63) return false;
        strncpy(lat_str, s, lat_len);
        lat_str[lat_len] = 0;
        const char *lon_start = sep + 1;
        while (*lon_start == ' ' || *lon_start == '\t') lon_start++;
        strncpy(lon_str, lon_start, 63);
        lon_str[63] = 0;
        *lat = parseCoord(lat_str);
        *lon = parseCoord(lon_str);
        return true;
    }
    // Fallback: Komma-getrennt
    const char *comma = strchr(s, ',');
    if (comma) {
        char lat_str[64], lon_str[64];
        int lat_len = (int)(comma - s);
        if (lat_len > 63) return false;
        strncpy(lat_str, s, lat_len);
        lat_str[lat_len] = 0;
        strncpy(lon_str, comma + 1, 63);
        lon_str[63] = 0;
        *lat = parseCoord(lat_str);
        *lon = parseCoord(lon_str);
        return true;
    }
    return false;
}

// OpenAir Datei parsen
static int parseOpenAir(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[OA] Datei nicht gefunden: %s\n", path);
        return 0;
    }

    if (!airspaces) {
        airspaces = (Airspace*)heap_caps_malloc((size_t)ASP_MAX * sizeof(Airspace), MALLOC_CAP_SPIRAM);
        if (!airspaces) { Serial.println("[OA] PSRAM-Alloc fehlgeschlagen!"); f.close(); return 0; }
        Serial.printf("[OA] Luftraum-Array in PSRAM: %d KB\n", (int)(ASP_MAX*sizeof(Airspace)/1024));
    }
    airspace_count = 0;
    Airspace *cur = nullptr;
    float center_lat = 0, center_lon = 0;
    int direction = 1;  // +1 = clockwise

    Serial.printf("[OA] Parse %s (%d bytes)...\n", path, f.size());

    while (f.available() && airspace_count < ASP_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '*') continue;  // Leer/Kommentar

        // AC — Airspace Class (neuer Luftraum beginnt)
        if (line.startsWith("AC ")) {
            if (cur && cur->num_pts > 0) {
                airspace_count++;  // Vorherigen abschliessen
            }
            if (airspace_count >= ASP_MAX) break;
            cur = &airspaces[airspace_count];
            memset(cur, 0, sizeof(Airspace));
            cur->active = true;
            String cls = line.substring(3);
            cls.trim();
            if (cls == "A") cur->cls = ASP_A;
            else if (cls == "B") cur->cls = ASP_B;
            else if (cls == "C") cur->cls = ASP_C;
            else if (cls == "D") cur->cls = ASP_D;
            else if (cls == "E") cur->cls = ASP_E;
            else if (cls == "CTR" || cls == "MCTR") cur->cls = ASP_CTR;
            else if (cls == "R") cur->cls = ASP_R;
            else if (cls == "Q") cur->cls = ASP_Q;
            else if (cls == "P") cur->cls = ASP_P;
            else if (cls == "GP") cur->cls = ASP_GLIDER;
            else if (cls == "W") cur->cls = ASP_WAVE;
            else if (cls == "TMA" || cls == "CTA" || cls == "TRA" || cls == "TSA") cur->cls = ASP_TMA;
            else if (cls == "TMZ" || cls == "RMZ") cur->cls = ASP_TMZ;
            else cur->cls = ASP_OTHER;
        }

        if (!cur) continue;

        // AN — Airspace Name
        if (line.startsWith("AN ")) {
            strncpy(cur->name, line.substring(3).c_str(), 31);
            cur->name[31] = 0;
        }

        // AH — Upper limit
        else if (line.startsWith("AH ")) {
            strncpy(cur->upper, line.substring(3).c_str(), 15);
            cur->upper[15] = 0;
        }

        // AL — Lower limit
        else if (line.startsWith("AL ")) {
            strncpy(cur->lower, line.substring(3).c_str(), 15);
            cur->lower[15] = 0;
        }

        // V X= — Center point (fuer Kreise/Boegen)
        else if (line.startsWith("V X=")) {
            String coord = line.substring(4);
            coord.trim();
            parseLatLon(coord.c_str(), &center_lat, &center_lon);
        }

        // V D= — Direction
        else if (line.startsWith("V D=")) {
            direction = (line[4] == '-') ? -1 : 1;
        }

        // DP — Polygon point
        else if (line.startsWith("DP ")) {
            if (cur->num_pts < ASP_MAX_POINTS) {
                String coord = line.substring(3);
                coord.trim();
                float plat, plon;
                if (parseLatLon(coord.c_str(), &plat, &plon)) {
                    cur->pts[cur->num_pts].lat = plat;
                    cur->pts[cur->num_pts].lon = plon;
                    cur->num_pts++;
                }
            }
        }

        // DC — Circle (radius in NM)
        else if (line.startsWith("DC ")) {
            float radius_nm = line.substring(3).toFloat();
            float radius_deg = radius_nm / 60.0f;  // 1 NM ≈ 1/60 Grad
            // Kreis als 24-Punkt-Polygon approximieren
            int steps = 24;
            for (int i = 0; i < steps && cur->num_pts < ASP_MAX_POINTS; i++) {
                float angle = i * 2.0f * M_PI / steps;
                cur->pts[cur->num_pts].lat = center_lat + radius_deg * cosf(angle);
                cur->pts[cur->num_pts].lon = center_lon + radius_deg * sinf(angle) / cosf(center_lat * M_PI / 180.0f);
                cur->num_pts++;
            }
        }

        // DA — Arc (radius, start_angle, end_angle)
        else if (line.startsWith("DA ")) {
            // Format: DA radius, angleStart, angleEnd
            float r_nm, a_start, a_end;
            if (sscanf(line.c_str() + 3, "%f,%f,%f", &r_nm, &a_start, &a_end) == 3) {
                float r_deg = r_nm / 60.0f;
                // Winkel normalisieren
                if (direction > 0 && a_end < a_start) a_end += 360;
                if (direction < 0 && a_end > a_start) a_start += 360;
                float step = direction * 5.0f;  // 5° Schritte
                for (float a = a_start; direction > 0 ? a <= a_end : a >= a_end; a += step) {
                    if (cur->num_pts >= ASP_MAX_POINTS) break;
                    float ar = a * M_PI / 180.0f;
                    cur->pts[cur->num_pts].lat = center_lat + r_deg * cosf(ar);
                    cur->pts[cur->num_pts].lon = center_lon + r_deg * sinf(ar) / cosf(center_lat * M_PI / 180.0f);
                    cur->num_pts++;
                }
            }
        }

        // DB — Bogen zwischen zwei Punkten (auf Kreis um V X=)
        // Vereinfacht: beide Endpunkte als Stuetzpunkte (Bogen ~ Sehne). Koordinaten korrekt geparst.
        else if (line.startsWith("DB ")) {
            String coord = line.substring(3);
            coord.trim();
            int comma = coord.indexOf(',');
            float la, lo;
            if (comma > 0) {
                String p1 = coord.substring(0, comma); p1.trim();
                String p2 = coord.substring(comma + 1); p2.trim();
                if (parseLatLon(p1.c_str(), &la, &lo) && cur->num_pts < ASP_MAX_POINTS) {
                    cur->pts[cur->num_pts].lat = la; cur->pts[cur->num_pts].lon = lo; cur->num_pts++;
                }
                if (parseLatLon(p2.c_str(), &la, &lo) && cur->num_pts < ASP_MAX_POINTS) {
                    cur->pts[cur->num_pts].lat = la; cur->pts[cur->num_pts].lon = lo; cur->num_pts++;
                }
            } else {
                if (parseLatLon(coord.c_str(), &la, &lo) && cur->num_pts < ASP_MAX_POINTS) {
                    cur->pts[cur->num_pts].lat = la; cur->pts[cur->num_pts].lon = lo; cur->num_pts++;
                }
            }
        }

        yield();  // Watchdog fuettern
    }

    // Letzten Luftraum abschliessen
    if (cur && cur->num_pts > 0) {
        airspace_count++;
    }

    f.close();
    Serial.printf("[OA] Fertig: %d Luftraeume geparst\n", airspace_count);
    return airspace_count;
}

// Klasse als String
static const char* airspaceClassStr(AirspaceClass cls) {
    switch (cls) {
        case ASP_A: return "A";
        case ASP_B: return "B";
        case ASP_C: return "C";
        case ASP_D: return "D";
        case ASP_E: return "E";
        case ASP_CTR: return "CTR";
        case ASP_R: return "R";
        case ASP_Q: return "Q";
        case ASP_P: return "P";
        case ASP_TMA: return "TMA";
        case ASP_TMZ: return "TMZ";
        case ASP_GLIDER: return "GP";
        case ASP_WAVE: return "W";
        default: return "?";
    }
}

// AL/AH-String -> Meter (MSL-Naeherung). Deckt FL, ft (OpenAir-Default), GND/SFC, m ab.
static float parseAltM(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0.0f;
    if (strncmp(s, "GND", 3) == 0 || strncmp(s, "SFC", 3) == 0) return 0.0f;
    if ((s[0]=='F'||s[0]=='f') && (s[1]=='L'||s[1]=='l')) return (float)atof(s + 2) * 30.48f;  // FL*100ft
    float v = (float)atof(s);
    const char *p = s;
    while ((*p>='0'&&*p<='9') || *p=='.' || *p=='-' || *p=='+') p++;
    while (*p == ' ') p++;
    if (*p == 'm' || (*p == 'M' && strncmp(p, "MSL", 3) != 0)) return v;  // schon Meter
    return v * 0.3048f;  // Fuss
}

// Naechster Luftraum VORAUS entlang Heading. Strahl-Polygon-Schnitt in lokalen Metern.
struct AheadResult {
    int idx;
    float dist_km;
    float floor_m, ceil_m;
    bool found;
};

static AheadResult airspaceAhead(double myLat, double myLon, float headingDeg) {
    AheadResult r = { -1, 0, 0, 0, false };
    if (!airspaces) return r;
    float best = 1e9f;
    float hr = headingDeg * (float)M_PI / 180.0f;
    float dE = sinf(hr), dN = cosf(hr);                 // Strahlrichtung (Ost, Nord)
    float coslat = cosf((float)myLat * (float)M_PI / 180.0f);
    for (int a = 0; a < airspace_count; a++) {
        Airspace &asp = airspaces[a];
        if (!asp.active || asp.num_pts < 3) continue;
        float entry = 1e9f;
        for (int i = 0; i < asp.num_pts; i++) {
            int j = (i + 1) % asp.num_pts;
            float ax = (float)(asp.pts[i].lon - myLon) * 111320.0f * coslat;
            float ay = (float)(asp.pts[i].lat - myLat) * 111320.0f;
            float bx = (float)(asp.pts[j].lon - myLon) * 111320.0f * coslat;
            float by = (float)(asp.pts[j].lat - myLat) * 111320.0f;
            float ex = bx - ax, ey = by - ay;
            float det = ex * dN - dE * ey;
            if (fabsf(det) < 1e-6f) continue;
            float t  = (ex * ay - ey * ax) / det;          // Distanz entlang Strahl (m)
            float ss = (dE * ay - dN * ax) / det;          // Param auf Kante [0,1]
            if (t >= 0 && ss >= 0 && ss <= 1 && t < entry) entry = t;
        }
        if (entry < best && entry <= 10000.0f) {   // nur im Chart-Fenster (0..10 km)
            best = entry;
            r.idx = a; r.dist_km = entry / 1000.0f;
            r.floor_m = parseAltM(asp.lower);
            r.ceil_m  = parseAltM(asp.upper);
            r.found = true;
        }
    }
    return r;
}
