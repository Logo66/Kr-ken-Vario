#pragma once
// device_registry.h — Ticket C Stufe 1: Self-Registration + NVS-Device-Token.
// Das Geraet meldet sich mit Werks-Secret + ESP32-MAC am Buddy-Server an, bekommt
// einen EIGENEN Bearer-Token (persistent in NVS) + einen Pairing-Code (der Besitzer
// koppelt das Geraet ueber die Buddy-Web-App /devices/claim an seinen Account).
//
// Auth ab jetzt: Authorization: Bearer <device-token>  (NICHT mehr X-Buddy-Key).
// HEILIG: Werks-Secret + Token NIE ins Repo/Doku/Chat. Token nur in NVS (Geraet)
//         + gehasht in der Server-DB.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Werks-Secret aus lokalem, git-ignored src/secrets.h. Fehlt es → leeres Secret
// (kompiliert, Registrierung gibt dann sauber 401 statt Crash).
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef BUDDY_DEVICE_FACTORY_SECRET
#define BUDDY_DEVICE_FACTORY_SECRET ""
#endif

#ifndef BUDDY_BASE_URL
#define BUDDY_BASE_URL "https://buddy.flightbuddyki.org"
#endif
#define DEV_HB_INTERVAL_MS 300000UL        // Heartbeat alle 5 Min (sofort beim 1. WLAN-Kontakt)

static Preferences devicePrefs;
static char deviceMac[13]        = {0};   // 12 hex + NUL
static char deviceToken[80]      = {0};   // 64 hex + NUL (Bearer)
static char devicePairingCode[8] = {0};   // 6 + NUL
static char deviceStatus[16]     = {0};   // unclaimed / active / blocked
static bool deviceRegTried       = false; // genau 1 Versuch pro Boot (kein Retry-Sturm)
static unsigned long deviceLastHb = 0;     // Heartbeat-Takt (0 = noch nicht gesendet)
static bool deviceServerOk       = false;  // Server-Verbindung (letzter Heartbeat 200 + WLAN up) -> Statusleiste

static void deviceReadMac() {
    uint8_t m[6];
    WiFi.macAddress(m);                    // STA-MAC aus efuse (kein WLAN noetig)
    snprintf(deviceMac, sizeof(deviceMac), "%02x%02x%02x%02x%02x%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static bool deviceHasToken() { return deviceToken[0] != 0; }

static void deviceLoadNvs() {
    devicePrefs.begin("buddy", true);      // read-only
    String t = devicePrefs.getString("devtoken", "");
    String p = devicePrefs.getString("pairing", "");
    String s = devicePrefs.getString("status", "");
    devicePrefs.end();
    strncpy(deviceToken,       t.c_str(), sizeof(deviceToken)-1);
    strncpy(devicePairingCode, p.c_str(), sizeof(devicePairingCode)-1);
    strncpy(deviceStatus,      s.c_str(), sizeof(deviceStatus)-1);
}

static void deviceSaveNvs() {
    devicePrefs.begin("buddy", false);
    devicePrefs.putString("devtoken", deviceToken);
    devicePrefs.putString("pairing",  devicePairingCode);
    devicePrefs.putString("status",   deviceStatus);
    devicePrefs.end();
}

// Letzte bekannte GPS-Position in NVS — Karten-Fallback ohne Fix (ueberlebt Neustart, K2).
static void deviceSaveLastPos(double lat, double lon) {
    if (lat == 0 && lon == 0) return;
    devicePrefs.begin("buddy", false);
    devicePrefs.putDouble("lastlat", lat);
    devicePrefs.putDouble("lastlon", lon);
    devicePrefs.end();
}
static bool deviceLoadLastPos(double *lat, double *lon) {
    devicePrefs.begin("buddy", true);
    double la = devicePrefs.getDouble("lastlat", 0);
    double lo = devicePrefs.getDouble("lastlon", 0);
    devicePrefs.end();
    if (la != 0 || lo != 0) { *lat = la; *lon = lo; return true; }
    return false;
}

// POST /devices/register {mac, factory_secret, firmware_ver} -> {token, pairing_code, status}
// return: 200 ok (Token in NVS), sonst HTTP-Code (>0) / Fehler (<0). errbuf: Klartext.
static int deviceRegister(char *errbuf, size_t errlen) {
    if (WiFi.status() != WL_CONNECTED) { snprintf(errbuf, errlen, "Kein WLAN"); return -1; }
    if (deviceMac[0] == 0) deviceReadMac();

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(BUDDY_BASE_URL) + "/devices/register";
    if (!http.begin(client, url)) { snprintf(errbuf, errlen, "begin() fehlgeschlagen"); return -2; }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    JsonDocument body;
    body["mac"]            = deviceMac;
    body["factory_secret"] = BUDDY_DEVICE_FACTORY_SECRET;
    body["firmware_ver"]   = AURA_VERSION;
    String payload;
    serializeJson(body, payload);

    int code = http.POST(payload);
    if (code != 200) {
        if (code == 401)      snprintf(errbuf, errlen, "401 Werks-Secret falsch/fehlt");
        else if (code < 0)    snprintf(errbuf, errlen, "Netzfehler %d", code);
        else                  snprintf(errbuf, errlen, "HTTP %d", code);
        Serial.printf("[DEV] /devices/register -> %d\n", code);
        http.end();
        return code;
    }
    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp)) { snprintf(errbuf, errlen, "JSON-Fehler"); return -3; }
    const char *tok = doc["token"]        | "";
    const char *pc  = doc["pairing_code"] | "";
    const char *st  = doc["status"]       | "unclaimed";
    if (!tok[0]) { snprintf(errbuf, errlen, "Kein Token in Antwort"); return -4; }

    strncpy(deviceToken,       tok, sizeof(deviceToken)-1);       deviceToken[sizeof(deviceToken)-1] = 0;
    strncpy(devicePairingCode, pc,  sizeof(devicePairingCode)-1); devicePairingCode[sizeof(devicePairingCode)-1] = 0;
    strncpy(deviceStatus,      st,  sizeof(deviceStatus)-1);      deviceStatus[sizeof(deviceStatus)-1] = 0;
    deviceSaveNvs();

    Serial.printf("[DEV] registriert: mac=%s status=%s pairing=%s token=%.6s...(NVS)\n",
                  deviceMac, deviceStatus, devicePairingCode, deviceToken);
    snprintf(errbuf, errlen, "OK Pairing %s", devicePairingCode);
    return 200;
}

// POST /devices/heartbeat (Bearer-Token) {firmware_ver} -> Server aktualisiert last_seen/firmware.
// 200 = ok (Server kennt Geraet), 403 = Device blocked, 401 = Token ungueltig. errbuf: Klartext.
static int deviceHeartbeat(char *errbuf, size_t errlen) {
    if (WiFi.status() != WL_CONNECTED) { snprintf(errbuf, errlen, "kein WLAN"); return -1; }
    if (!deviceHasToken())             { snprintf(errbuf, errlen, "kein Token"); return -2; }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(BUDDY_BASE_URL) + "/devices/heartbeat";
    if (!http.begin(client, url)) { snprintf(errbuf, errlen, "begin() fehlgeschlagen"); return -3; }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + deviceToken);   // Geraete-Token (NICHT X-Buddy-Key)
    http.setTimeout(15000);

    JsonDocument body;
    body["firmware_ver"] = AURA_VERSION;
    String payload;
    serializeJson(body, payload);

    int code = http.POST(payload);
    http.end();
    if (code == 200)       snprintf(errbuf, errlen, "OK");
    else if (code == 403)  snprintf(errbuf, errlen, "403 Device blocked");
    else if (code == 401)  snprintf(errbuf, errlen, "401 Token ungueltig");
    else if (code < 0)     snprintf(errbuf, errlen, "Netzfehler %d", code);
    else                   snprintf(errbuf, errlen, "HTTP %d", code);
    return code;
}

// Beim Boot: MAC + NVS-Token laden.
static void deviceInit() {
    deviceReadMac();
    deviceLoadNvs();
    Serial.printf("[DEV] MAC=%s token=%s status=%s\n",
                  deviceMac, deviceHasToken() ? "vorhanden(NVS)" : "KEINER",
                  deviceStatus[0] ? deviceStatus : "-");
}

// Im Loop: sobald WLAN da ist und KEIN Token existiert -> genau 1 Registrierung.
static void deviceLoop() {
    if (WiFi.status() != WL_CONNECTED) { deviceServerOk = false; return; }  // WLAN weg -> Server-Punkt aus

    if (!deviceHasToken()) {
        if (deviceRegTried) return;        // genau 1 Register-Versuch pro Boot (kein Retry-Sturm)
        deviceRegTried = true;
        char err[48];
        int code = deviceRegister(err, sizeof(err));
        Serial.printf("[DEV] Register-Versuch -> %d (%s)\n", code, err);
        return;
    }

    // Token vorhanden -> Heartbeat: sofort beim 1. WLAN-Kontakt, danach alle DEV_HB_INTERVAL_MS.
    if (deviceLastHb == 0 || millis() - deviceLastHb > DEV_HB_INTERVAL_MS) {
        deviceLastHb = millis();
        char err[48];
        int code = deviceHeartbeat(err, sizeof(err));
        deviceServerOk = (code == 200);    // Server-Verbindung fuer die Statusleiste
        Serial.printf("[DEV] Heartbeat -> %d (%s)\n", code, err);
    }
}

// Status-Zeile fuer den FUNK-Screen (leer = nicht registriert).
static const char *deviceFunkStatus() {
    static char line[40];
    if (!deviceHasToken()) { line[0] = 0; return line; }
    if (strcmp(deviceStatus, "active") == 0)        snprintf(line, sizeof(line), "Geraet gekoppelt");
    else if (strcmp(deviceStatus, "blocked") == 0)  snprintf(line, sizeof(line), "Geraet GESPERRT");
    else snprintf(line, sizeof(line), "Pairing: %s  (in Buddy-App koppeln)", devicePairingCode);
    return line;
}
