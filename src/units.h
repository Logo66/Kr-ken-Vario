#pragma once
// units.h — Anzeige-Einheiten aus dem Settings-Modell (KONFIG-VERTRAG units.*).
// INTERN bleibt alles metrisch (m, km/h, m/s, C) — nur die ANZEIGE wird umgerechnet.
#include "config_model.h"
#include <string.h>

struct UnitsCfg { char alt[4]="m", speed[4]="kmh", vario[6]="ms", temp[2]="c"; };
static UnitsCfg g_units;

static void unitsLoad() {
    const char* s;
    s = g_model["units"]["alt"].as<const char*>();   strncpy(g_units.alt,  (s&&*s)?s:"m",  sizeof(g_units.alt)-1);   g_units.alt[sizeof(g_units.alt)-1]=0;
    s = g_model["units"]["speed"].as<const char*>(); strncpy(g_units.speed,(s&&*s)?s:"kmh",sizeof(g_units.speed)-1); g_units.speed[sizeof(g_units.speed)-1]=0;
    s = g_model["units"]["vario"].as<const char*>(); strncpy(g_units.vario,(s&&*s)?s:"ms", sizeof(g_units.vario)-1); g_units.vario[sizeof(g_units.vario)-1]=0;
    s = g_model["units"]["temp"].as<const char*>();  strncpy(g_units.temp, (s&&*s)?s:"c",  sizeof(g_units.temp)-1);  g_units.temp[sizeof(g_units.temp)-1]=0;
}

// Hoehe (m -> m/ft)
static inline float       uAlt(float m)   { return !strcmp(g_units.alt,"ft") ? m*3.28084f : m; }
static inline const char* uAltL()         { return !strcmp(g_units.alt,"ft") ? "ft" : "m"; }
// Geschwindigkeit (km/h -> km/h / mph / kt)
static inline float       uSpd(float kmh) { if(!strcmp(g_units.speed,"mph")) return kmh*0.621371f; if(!strcmp(g_units.speed,"kt")) return kmh*0.539957f; return kmh; }
static inline const char* uSpdL()         { if(!strcmp(g_units.speed,"mph")) return "mph"; if(!strcmp(g_units.speed,"kt")) return "kt"; return "km/h"; }
// Temperatur (C -> C/F)
static inline float       uTmp(float c)   { return !strcmp(g_units.temp,"f") ? c*1.8f+32.0f : c; }
static inline const char* uTmpL()         { return !strcmp(g_units.temp,"f") ? "F" : "C"; }
