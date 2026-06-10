#pragma once
// map_pack.h — Karten-Pack ueber WLAN vom Buddy-Server (/maps/*), Ticket C Stufe 3.
//   GET /maps/index               -> Regionen + Version + Bytes + SHA-256
//   GET /maps/<region>/download   -> Pack-Datei -> SHA-256-Verify -> SD -> Reader rendert
// Auth: Authorization: Bearer <device-token> (Ticket C; X-Buddy-Key ist Legacy, raus).
// Endpoint /maps/* (NICHT /api/v1/maps/* = OTM-Web-Tiles).
// TLS: setInsecure — Integritaet wird ueber SHA-256 gegen das Server-Manifest geprueft.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include "mbedtls/sha256.h"
#include "pins.h"
#include "device_registry.h"   // deviceToken, deviceHasToken(), BUDDY_BASE_URL

#define MAPPACK_MAX_REGIONS 12

struct MapRegion {
    char     id[24];        // region_id, z.B. "hoernli"
    int      latest;        // neueste Pack-Version
    uint32_t bytes;         // latest_bytes
    char     sha8[9];       // erste 8 Hex von latest_sha256 (Anzeige)
    char     sha_full[65];  // volle SHA-256 (Verify)
};

static MapRegion mapRegions[MAPPACK_MAX_REGIONS];
static int  mapRegionCount   = 0;
static char mapPackFile[48]  = {0};   // Pfad des zuletzt verifizierten + geladenen Packs

// GET /maps/index (Bearer) -> mapRegions[]. 200 = ok, sonst HTTP-Code (>0)/Fehler (<0). errbuf: Klartext.
static int mapPackFetchIndex(char *errbuf, size_t errlen) {
    mapRegionCount = 0;
    if (WiFi.status() != WL_CONNECTED) { snprintf(errbuf, errlen, "Kein WLAN"); return -1; }
    if (!deviceHasToken())             { snprintf(errbuf, errlen, "Kein Geraete-Token"); return -2; }

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String url = String(BUDDY_BASE_URL) + "/maps/index";
    if (!http.begin(client, url)) { snprintf(errbuf, errlen, "begin() fehlgeschlagen"); return -3; }
    http.addHeader("Authorization", String("Bearer ") + deviceToken);
    http.setTimeout(15000);

    int code = http.GET();
    if (code != 200) {
        if (code == 401)   snprintf(errbuf, errlen, "401 - Token/Device blocked");
        else if (code < 0) snprintf(errbuf, errlen, "Netzfehler %d", code);
        else               snprintf(errbuf, errlen, "HTTP %d", code);
        Serial.printf("[PACKWLAN] /maps/index -> %d\n", code);
        http.end(); return code;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { snprintf(errbuf, errlen, "JSON-Fehler"); return -4; }
    JsonArray regions = doc["regions"].as<JsonArray>();
    for (JsonObject r : regions) {
        if (mapRegionCount >= MAPPACK_MAX_REGIONS) break;
        MapRegion &m = mapRegions[mapRegionCount];
        strncpy(m.id, r["region_id"] | "?", sizeof(m.id)-1); m.id[sizeof(m.id)-1] = 0;
        m.latest = r["latest"]       | 0;
        m.bytes  = r["latest_bytes"] | 0;
        const char *sha = r["latest_sha256"] | "";
        strncpy(m.sha_full, sha, 64); m.sha_full[64] = 0;
        strncpy(m.sha8, sha, 8);      m.sha8[8] = 0;
        Serial.printf("[PACKWLAN]   %s v%d  %u B  %s\n", m.id, m.latest, (unsigned)m.bytes, m.sha8);
        mapRegionCount++;
    }
    snprintf(errbuf, errlen, "%d Region(en)", mapRegionCount);
    return 200;
}

// GET /maps/<region>/download (Bearer) -> SD-Temp -> SHA-256 vs Manifest -> /maps/region_<r>_v<v>.pack
// 200 = ok (Pfad in mapPackFile), -5 unvollstaendig, -6 SHA-Mismatch (beide verworfen). errbuf: Klartext.
static int mapPackDownload(const MapRegion &reg, char *errbuf, size_t errlen,
                           void (*progress)(int) = nullptr) {
    mapPackFile[0] = 0;
    if (WiFi.status() != WL_CONNECTED) { snprintf(errbuf, errlen, "Kein WLAN"); return -1; }
    if (!deviceHasToken())             { snprintf(errbuf, errlen, "Kein Geraete-Token"); return -2; }
    digitalWrite(BOARD_LORA_CS, HIGH);

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String url = String(BUDDY_BASE_URL) + "/maps/" + reg.id + "/download";
    if (reg.latest > 0) url += "?v=" + String(reg.latest);
    if (!http.begin(client, url)) { snprintf(errbuf, errlen, "begin() fehlgeschlagen"); return -3; }
    http.addHeader("Authorization", String("Bearer ") + deviceToken);
    http.setTimeout(20000);

    int code = http.GET();
    if (code != 200) {
        if (code == 401)   snprintf(errbuf, errlen, "401 - Token/Device blocked");
        else if (code < 0) snprintf(errbuf, errlen, "Netzfehler %d", code);
        else               snprintf(errbuf, errlen, "HTTP %d", code);
        http.end(); return code;
    }
    int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    const char *tmp = "/maps/_dl.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) { snprintf(errbuf, errlen, "SD Schreibfehler"); http.end(); return -4; }

    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
    uint8_t buf[1024]; int received = 0, lastPct = -1;
    unsigned long tData = millis();
    while (http.connected() && (total < 0 || received < total)) {
        int avail = stream->available();
        if (avail > 0) {
            int rd = stream->readBytes(buf, min(avail, 1024));
            f.write(buf, rd);
            mbedtls_sha256_update(&sha, buf, rd);
            received += rd;
            if (progress && total > 0) { int p = (int)(100.0f*received/total); if (p != lastPct) { lastPct = p; progress(p); } }
            tData = millis();
        } else if (millis() - tData > 15000) {
            break;   // 15s ohne Daten -> WLAN weg (W4): Temp wird unten verworfen
        }
        delay(1);
    }
    f.close(); http.end();

    uint8_t hash[32]; mbedtls_sha256_finish(&sha, hash); mbedtls_sha256_free(&sha);
    char hexsha[65]; for (int i = 0; i < 32; i++) sprintf(hexsha + i*2, "%02x", hash[i]); hexsha[64] = 0;

    if (total > 0 && received != total) {
        Serial.printf("[PACKWLAN] REJECT: unvollstaendig %d/%d\n", received, total);
        SD.remove(tmp); snprintf(errbuf, errlen, "Abbruch %d/%d B verworfen", received, total); return -5;
    }
    if (reg.sha_full[0] && strcmp(hexsha, reg.sha_full) != 0) {
        Serial.printf("[PACKWLAN] REJECT: SHA mismatch\n  got %s\n  exp %s\n", hexsha, reg.sha_full);
        SD.remove(tmp); snprintf(errbuf, errlen, "SHA mismatch verworfen"); return -6;
    }

    char finalp[48];
    snprintf(finalp, sizeof(finalp), "/maps/region_%s_v%d.pack", reg.id, reg.latest > 0 ? reg.latest : 1);
    SD.remove(finalp);
    if (!SD.rename(tmp, finalp)) { SD.remove(tmp); snprintf(errbuf, errlen, "Rename fehlgeschlagen"); return -7; }

    strncpy(mapPackFile, finalp, sizeof(mapPackFile)-1); mapPackFile[sizeof(mapPackFile)-1] = 0;
    File af = SD.open("/maps/active.txt", FILE_WRITE);   // Boot laedt kuenftig dieses Pack (echtes Pack)
    if (af) { af.print(finalp); af.close(); }
    Serial.printf("[PACKWLAN] OK %s %d B, SHA verifiziert -> %s\n", reg.id, received, finalp);
    snprintf(errbuf, errlen, "OK %d B, SHA ok", received);
    return 200;
}
