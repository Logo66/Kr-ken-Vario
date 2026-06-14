#pragma once
// obstacle_fetch.h — Hindernisse NACH STANDORT vom Buddy-Server holen (Ticket §5).
//   GET {BUDDY_BASE_URL}/obstacles?lat=&lon=&r=  (Authorization: Bearer <device-token>)
//   -> text/plain obstacles.txt -> SD /obstacles/obstacles.txt -> parseObstacles().
// Cache: ETag (If-None-Match -> 304) + SHA-256-Verify (X-SHA256). Meta in obstacles.meta.
// TLS: setInsecure — Integritaet ueber SHA-256 wie bei den Map-Packs.
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include "mbedtls/sha256.h"
#include "pins.h"
#include "device_registry.h"   // BUDDY_BASE_URL, deviceToken, deviceHasToken()
#include "obstacles.h"         // parseObstacles, obstacle_count

// Cache-Meta des letzten Fetches (fuer Drift-Entscheidung + 304-Revalidierung).
static double g_obstFetchLat = 0, g_obstFetchLon = 0;
static int    g_obstFetchR   = 0;
static char   g_obstEtag[48] = {0};

static void obstacleLoadMeta() {
    if (!SD.exists("/obstacles/obstacles.meta")) return;   // erster Boot: noch kein Cache
    File f = SD.open("/obstacles/obstacles.meta", FILE_READ);
    if (!f) return;
    String s = f.readStringUntil('\n'); f.close(); s.trim();
    int p1 = s.indexOf(';'), p2 = s.indexOf(';', p1 + 1), p3 = s.indexOf(';', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) return;
    g_obstFetchLat = s.substring(0, p1).toDouble();
    g_obstFetchLon = s.substring(p1 + 1, p2).toDouble();
    g_obstFetchR   = s.substring(p2 + 1, p3).toInt();
    strncpy(g_obstEtag, s.substring(p3 + 1).c_str(), sizeof(g_obstEtag) - 1);
    g_obstEtag[sizeof(g_obstEtag) - 1] = 0;
}
static void obstacleSaveMeta(double lat, double lon, int r, const char *etag) {
    g_obstFetchLat = lat; g_obstFetchLon = lon; g_obstFetchR = r;
    strncpy(g_obstEtag, etag ? etag : "", sizeof(g_obstEtag) - 1); g_obstEtag[sizeof(g_obstEtag) - 1] = 0;
    File f = SD.open("/obstacles/obstacles.meta", FILE_WRITE);
    if (!f) return;
    f.printf("%.6f;%.6f;%d;%s\n", lat, lon, r, g_obstEtag);
    f.close();
}

// Holt Hindernisse fuer (lat,lon,r km). 200=neu geladen+geparst, 304=Cache aktuell,
// <0/HTTP-Code = Fehler (alter Cache bleibt gueltig). errbuf: Klartext fuer den Screen.
static int obstacleFetch(double lat, double lon, char *errbuf, size_t errlen, void (*progress)(int) = nullptr) {
    if (WiFi.status() != WL_CONNECTED) { snprintf(errbuf, errlen, "Kein WLAN"); return -1; }
    if (!deviceHasToken())             { snprintf(errbuf, errlen, "Kein Geraete-Token"); return -2; }
    if (lat == 0 && lon == 0)          { snprintf(errbuf, errlen, "Kein Standort"); return -3; }
    digitalWrite(BOARD_LORA_CS, HIGH);   // SPI-Bus: LoRa-CS los (SD + LoRa teilen den Bus)

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    char url[160];
    // Pro-Land-Endpoint (Server bestimmt das Land per BBox aus dem Standort) -> ganze Land-Datei
    snprintf(url, sizeof(url), "%s/obstacles/country?lat=%.6f&lon=%.6f", BUDDY_BASE_URL, lat, lon);
    if (!http.begin(client, url)) { snprintf(errbuf, errlen, "begin() fehlgeschlagen"); return -4; }
    http.addHeader("Authorization", String("Bearer ") + deviceToken);
    if (g_obstEtag[0]) http.addHeader("If-None-Match", g_obstEtag);   // Neustart-/Update-Abgleich -> 304
    const char *collect[] = { "ETag", "X-SHA256", "X-Country" };
    http.collectHeaders(collect, 3);
    http.setTimeout(20000);

    int code = http.GET();
    if (code == 304) { http.end(); snprintf(errbuf, errlen, "aktuell (304), %d Hind.", obstacle_count); return 304; }
    if (code == 404) { http.end(); snprintf(errbuf, errlen, "Land nicht abgedeckt"); return 404; }
    if (code != 200) {
        if (code == 401)   snprintf(errbuf, errlen, "401 Token/Device");
        else if (code < 0) snprintf(errbuf, errlen, "Netzfehler %d", code);
        else               snprintf(errbuf, errlen, "HTTP %d", code);
        Serial.printf("[OBST] /obstacles/country -> %d\n", code);
        http.end(); return code;
    }
    String etag = http.header("ETag");
    String wantSha = http.header("X-SHA256");
    String country = http.header("X-Country");
    int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    if (!SD.exists("/obstacles")) SD.mkdir("/obstacles");
    const char *tmp = "/obstacles/_dl.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) { snprintf(errbuf, errlen, "SD Schreibfehler"); http.end(); return -5; }

    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
    uint8_t buf[1024]; int received = 0, lastPct = -1; unsigned long tData = millis();
    while (http.connected() && (total < 0 || received < total)) {
        int avail = stream->available();
        if (avail > 0) {
            int rd = stream->readBytes(buf, min(avail, 1024));
            f.write(buf, rd); mbedtls_sha256_update(&sha, buf, rd); received += rd; tData = millis();
            if (progress && total > 0) { int p = (int)(100.0f * received / total); if (p != lastPct) { lastPct = p; progress(p); } }
        } else if (millis() - tData > 15000) {
            break;   // 15s ohne Daten -> WLAN weg: Temp wird unten verworfen
        }
        delay(1);
    }
    f.close(); http.end();

    uint8_t hash[32]; mbedtls_sha256_finish(&sha, hash); mbedtls_sha256_free(&sha);
    char hex[65]; for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", hash[i]); hex[64] = 0;

    if (total > 0 && received != total) { SD.remove(tmp); snprintf(errbuf, errlen, "unvollstaendig %d/%d", received, total); return -6; }
    if (wantSha.length() == 64 && !wantSha.equalsIgnoreCase(hex)) { SD.remove(tmp); snprintf(errbuf, errlen, "SHA mismatch verworfen"); return -7; }

    SD.remove("/obstacles/obstacles.txt");
    if (!SD.rename(tmp, "/obstacles/obstacles.txt")) { SD.remove(tmp); snprintf(errbuf, errlen, "Rename fehlgeschlagen"); return -8; }

    obstacleSaveMeta(lat, lon, 0, etag.c_str());
    int n = parseObstacles("/obstacles/obstacles.txt");
    Serial.printf("[OBST] Fetch OK %s %.5f,%.5f -> %d B, %d Hindernisse, sha %.8s\n",
                  country.length() ? country.c_str() : "?", lat, lon, received, n, hex);
    snprintf(errbuf, errlen, "%s OK: %d Hindernisse", country.length() ? country.c_str() : "Land", n);
    return 200;
}
