#pragma once
// config_model.h — M1: EIN versioniertes Settings-Modell als JSON-Blob im NVS (KONFIG-VERTRAG Teil 2).
// Eine Wahrheit fuer alle Editoren (Touch/BLE/Web). schema_version + updated_at sind Pflicht (Cloud-Sync Ph.2).
// Live-wirksame Keys (Ton/Vario/QNH/BLE) werden in applySettingKV (main.cpp) ZUSAETZLICH sofort angewandt
// (ueber ihre bestehenden Stores) — hier liegt die persistente Vertrags-Wahrheit fuer ALLE Keys.
#include <ArduinoJson.h>
#include <Preferences.h>

static JsonDocument g_model;
static Preferences  cfgPrefs;

// Vertrags-Gruppen (Top-Level). Nur Keys aus diesen Gruppen werden akzeptiert.
static bool modelKeyKnown(const char *k) {
    static const char *groups[] = {"sound","vario","units","display","wifi","ble","fanet",
                                   "pilot","alt","map","log","buddy","warn"};
    for (auto g : groups) { size_t n = strlen(g); if (!strncmp(k, g, n) && k[n] == '.') return true; }
    return false;
}

static void modelSave() {
    char buf[2048];
    serializeJson(g_model, buf, sizeof(buf));
    cfgPrefs.begin("cfg", false);
    cfgPrefs.putString("model", buf);
    cfgPrefs.end();
}

// Vertrags-Defaults erzeugen.
static void modelDefaults() {
    g_model.clear();
    g_model["schema_version"] = 1;
    g_model["updated_at"]     = 0;
    JsonObject snd = g_model["sound"].to<JsonObject>();   snd["volume"]=3; snd["muted"]=false;
    JsonObject var = g_model["vario"].to<JsonObject>();   var["climb_threshold"]=0.2; var["sink_alarm"]=-3.0; var["deadband"]=0.1; var["tone_curve"]=0; var["sink_tone"]=true; var["avg_window_s"]=20;
    JsonObject uni = g_model["units"].to<JsonObject>();   uni["alt"]="m"; uni["speed"]="kmh"; uni["vario"]="ms"; uni["temp"]="c";
    JsonObject dis = g_model["display"].to<JsonObject>(); dis["backlight"]=false; dis["brightness"]=80;
    JsonObject wif = g_model["wifi"].to<JsonObject>();    wif["ssid"]=""; wif["pass"]="";
    JsonObject bl  = g_model["ble"].to<JsonObject>();     bl["name"]="Aura Vario"; bl["pin"]=1234; bl["enabled"]=false;
    JsonObject fan = g_model["fanet"].to<JsonObject>();   fan["enabled"]=true; fan["aircraft"]=1; fan["pilot_name"]=""; fan["online_tracking"]=true; fan["tx_enabled"]=false;
    JsonObject pil = g_model["pilot"].to<JsonObject>();   pil["name"]=""; pil["glider"]=""; pil["weight_kg"]=95;
    JsonObject alt = g_model["alt"].to<JsonObject>();     alt["qnh"]=1013.25;
    JsonObject mp  = g_model["map"].to<JsonObject>();     mp["region"]="ch_v1";
    JsonObject lay = mp["layers"].to<JsonObject>();       lay["contours"]=true; lay["water"]=true; lay["airspace"]=true; lay["obstacles"]=true; lay["track"]=true;
    JsonObject lg  = g_model["log"].to<JsonObject>();     lg["igc"]=true; lg["imu_raw"]=false;
    JsonObject bud = g_model["buddy"].to<JsonObject>();   bud["pairing_code"]="";
    JsonObject war = g_model["warn"].to<JsonObject>();    war["buffer_h"]=500; war["buffer_v"]=150; war["airspace"]=true; war["obstacle"]=true; war["sphere_outer_m"]=300; war["sphere_inner_m"]=100;
}

// Beim Boot: aus NVS laden; fehlt es -> Defaults + Migration der 4 Altpfade (Ton-NVS, /ble.cfg, QNH) -> speichern.
static void modelInit(uint8_t curVolume, const char *bleName, uint32_t blePin, bool bleEnabled, float qnh_hpa) {
    cfgPrefs.begin("cfg", true);
    String s = cfgPrefs.getString("model", "");
    cfgPrefs.end();
    if (s.length() > 2 && deserializeJson(g_model, s) == DeserializationError::Ok) {
        Serial.printf("[CFG] Modell aus NVS geladen (%u B, schema=%d)\n", (unsigned)s.length(), (int)(g_model["schema_version"]|0));
        return;
    }
    modelDefaults();                                  // Erstinit
    g_model["sound"]["volume"] = curVolume;           // Migration der Altpfade
    g_model["ble"]["name"]     = bleName;
    g_model["ble"]["pin"]      = blePin;
    g_model["ble"]["enabled"]  = bleEnabled;
    g_model["alt"]["qnh"]      = qnh_hpa;
    modelSave();
    Serial.println("[CFG] Modell neu erstellt (Defaults + Migration)");
}

// Einen dotted-path-Key ins Modell setzen (legt verschachtelte Objekte an), updated_at hoch, speichern.
static void modelSet(const char *dotted, JsonVariantConst val) {
    char buf[64]; strncpy(buf, dotted, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    JsonObject obj = g_model.as<JsonObject>();
    char *save=nullptr; char *part = strtok_r(buf, ".", &save);
    while (part) {
        char *next = strtok_r(nullptr, ".", &save);
        if (!next) obj[part] = val;                                                       // Blatt setzen (kopiert)
        else obj = obj[part].is<JsonObject>() ? obj[part].as<JsonObject>() : obj[part].to<JsonObject>();
        part = next;
    }
    g_model["updated_at"] = (int)(g_model["updated_at"]|0) + 1;                            // monotone Version (Sync Ph.2)
    modelSave();
}
