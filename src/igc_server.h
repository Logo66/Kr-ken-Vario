#pragma once
// igc_server.h — kleiner WLAN-Webserver: listet /igc/ und laedt IGC-Dateien im Browser herunter.
// Laeuft nur wenn WLAN verbunden ist. Eingebauter WebServer (keine neue Lib).
// "Upload via App"-Ersatz, bis die Buddy-PWA steht: Datei-Export ueber WLAN.
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include "pins.h"

static WebServer igcServer(80);
static bool igcServerStarted = false;

static void igcHandleRoot() {
    digitalWrite(BOARD_LORA_CS, HIGH);   // LoRa deselektieren waehrend SD-Zugriff
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>Aura Kruecke</title><style>body{font-family:sans-serif;margin:24px;background:#f7f6f2}"
                  "h2{color:#2a2826}a{display:block;padding:12px;margin:6px 0;background:#fff;border-radius:8px;"
                  "text-decoration:none;color:#1f1d19;border:1px solid #ddd}a:hover{background:#eee}</style></head><body>";
    html += "<h2>Aura Kruecke — Flugaufzeichnungen</h2>";
    File dir = SD.open("/igc");
    int n = 0;
    if (dir && dir.isDirectory()) {
        File e = dir.openNextFile();
        while (e) {
            String name = String(e.name());
            if (!e.isDirectory() && name.endsWith(".igc")) {
                html += "<a href='/igc/" + name + "' download>&#11015; " + name +
                        "  <small>(" + String((uint32_t)e.size()) + " B)</small></a>";
                n++;
            }
            e = dir.openNextFile();
        }
    }
    if (n == 0) html += "<p>Noch keine IGC-Fluege aufgezeichnet.</p>";
    html += "<hr><small>Aura Kruecke &middot; KIE Engineering</small></body></html>";
    igcServer.send(200, "text/html; charset=utf-8", html);
}

// alles unter /igc/... als Datei-Download ausliefern
static void igcHandleFile() {
    String uri = igcServer.uri();   // z.B. /igc/2026-06-10_1423.igc
    if (!uri.startsWith("/igc/")) { igcServer.send(404, "text/plain", "Nicht gefunden"); return; }
    digitalWrite(BOARD_LORA_CS, HIGH);
    if (!SD.exists(uri)) { igcServer.send(404, "text/plain", "Datei nicht gefunden"); return; }
    File f = SD.open(uri, FILE_READ);
    if (!f) { igcServer.send(500, "text/plain", "Lesefehler"); return; }
    String name = uri.substring(uri.lastIndexOf('/') + 1);
    igcServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    igcServer.streamFile(f, "application/octet-stream");
    f.close();
}

// jeden Loop aufrufen: startet den Server bei Verbindung, bedient Anfragen
static void igcServerLoop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!igcServerStarted) {
            igcServer.on("/", igcHandleRoot);
            igcServer.onNotFound(igcHandleFile);
            igcServer.begin();
            igcServerStarted = true;
            Serial.printf("[IGCWEB] Server laeuft: http://%s/\n", WiFi.localIP().toString().c_str());
        }
        igcServer.handleClient();
    } else if (igcServerStarted) {
        igcServer.stop();
        igcServerStarted = false;
    }
}
