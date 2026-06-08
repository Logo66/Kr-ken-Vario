#pragma once
// overlay_screen.h — Karte Overlays: Luftraeume + Hindernisse Download
// Braucht WiFi-Verbindung (ueber FUNK → WLAN)
#include <WiFi.h>
#include <HTTPClient.h>
#include "ui_utils.h"
#include "sd_manager.h"

// OpenAIP URLs (Schweiz) — kein Auth noetig, woechentlich aktualisiert
#define URL_AIRSPACE_CH "https://storage.googleapis.com/29f98e10-a489-4c82-ae5e-489dbcd4912f/ch_asp.txt"
#define URL_OBSTACLES_CH "https://storage.googleapis.com/29f98e10-a489-4c82-ae5e-489dbcd4912f/ch_obs.txt"

enum OverlayState {
    OVL_MENU,
    OVL_DOWNLOADING,
    OVL_DONE,
    OVL_ERROR
};

class OverlayScreen {
public:
    OverlayState state = OVL_MENU;
    SDManager *sd = nullptr;
    char status_msg[64] = {0};
    int download_pct = 0;

    void begin(SDManager *sdm) {
        sd = sdm;
        state = OVL_MENU;
    }

    void draw(EpdiyHighlevelState *hl) {
        uint8_t *fb = epd_hl_get_framebuffer(hl);
        epd_hl_set_all_white(hl);

        drawHCenter(&ArialBold28, "KARTE", 0, 960, 44, fb);
        uiHLine(14, 58, 932, fb);

        switch (state) {
        case OVL_MENU: {
            bool wifi = (WiFi.status() == WL_CONNECTED);
            bool has_asp = sd && sd->ok && sd->exists("/airspace/ch_asp.txt");
            bool has_obs = sd && sd->ok && sd->exists("/obstacles/ch_obs.txt");

            if (!wifi) {
                drawHCenter(&ArialBold16, "WiFi nicht verbunden — erst unter FUNK > WLAN verbinden",
                            0, 960, 80, fb);
            } else {
                drawHCenter(&ArialBold16, "WiFi verbunden — antippen zum Download",
                            0, 960, 80, fb);
            }

            // Luftraeume — zentriert in Box
            uiBox(100, 120, 760, 70, fb);
            char asp_label[48];
            snprintf(asp_label, 48, "Luftraeume CH  %s", has_asp ? "[OK]" : "");
            drawBoxCenter(&ArialBold24, asp_label, 100, 120, 760, 70, fb);

            // Hindernisse — zentriert in Box
            uiBox(100, 210, 760, 70, fb);
            char obs_label[48];
            snprintf(obs_label, 48, "Hindernisse CH  %s", has_obs ? "[OK]" : "");
            drawBoxCenter(&ArialBold24, obs_label, 100, 210, 760, 70, fb);

            // Karten-Tiles
            uiBox(100, 300, 760, 70, fb);
            drawBoxCenter(&ArialBold24, "Karten Download", 100, 300, 760, 70, fb);

            // Status SD
            if (!sd || !sd->ok) {
                drawHCenter(&ArialBold16, "KEINE SD-KARTE!", 0, 960, 340, fb);
            }

            // Zurueck
            uiBox(350, 460, 260, 52, fb);
            drawBoxCenter(&ArialBold24, "ZURUECK", 350, 460, 260, 52, fb);
            break;
        }

        case OVL_DOWNLOADING: {
            drawHCenter(&ArialBold40, "DOWNLOAD", 0, 960, 180, fb);
            drawHCenter(&ArialBold16, status_msg, 0, 960, 230, fb);
            uiBox(100, 270, 760, 40, fb);
            if (download_pct > 0) uiFill(110, 280, (int)(740.0f * download_pct / 100.0f), 20, fb);
            char buf[16];
            snprintf(buf, 16, "%d%%", download_pct);
            drawHCenter(&ArialBold28, buf, 0, 960, 350, fb);
            break;
        }

        case OVL_DONE:
            drawHCenter(&ArialBold40, "FERTIG", 0, 960, 240, fb);
            drawHCenter(&ArialBold24, status_msg, 0, 960, 310, fb);
            uiBox(350, 380, 260, 52, fb);
            drawBoxCenter(&ArialBold24, "OK", 350, 380, 260, 52, fb);
            break;

        case OVL_ERROR:
            drawHCenter(&ArialBold40, "FEHLER", 0, 960, 240, fb);
            drawHCenter(&ArialBold16, status_msg, 0, 960, 310, fb);
            uiBox(350, 380, 260, 52, fb);
            drawBoxCenter(&ArialBold24, "OK", 350, 380, 260, 52, fb);
            break;
        }

        epd_poweron();
        epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
        epd_poweroff();
    }

    // true = Screen verlassen
    bool handleTap(int tx, int ty, EpdiyHighlevelState *hl) {
        switch (state) {
        case OVL_MENU:
            // Zurueck (grosszuegig: ganze untere Zone)
            if (ty >= 440) return true;
            if (tx >= 100 && tx < 860) {
                if (WiFi.status() != WL_CONNECTED) {
                    // Fehlermeldung kurz anzeigen, dann zurück
                    snprintf(status_msg, 64, "Erst FUNK > WLAN verbinden!");
                    state = OVL_ERROR; draw(hl);
                    return false;
                } else if (ty >= 120 && ty < 190) {
                    downloadFile(hl, URL_AIRSPACE_CH, "/airspace/ch_asp.txt", "Luftraeume CH");
                } else if (ty >= 210 && ty < 280) {
                    downloadFile(hl, URL_OBSTACLES_CH, "/obstacles/ch_obs.txt", "Hindernisse CH");
                } else if (ty >= 300 && ty < 370) {
                    downloadTiles(hl);
                }
            }
            // Zurueck
            if (ty >= 460 && ty < 512 && tx >= 350 && tx < 610) return true;
            break;

        case OVL_DONE:
        case OVL_ERROR:
            // Jeder Tap → zurueck zu OVL_MENU
            state = OVL_MENU;
            draw(hl);
            break;
        default: break;
        }
        return false;
    }

private:
    void downloadFile(EpdiyHighlevelState *hl, const char *url,
                      const char *path, const char *label) {
        if (!sd || !sd->ok) {
            snprintf(status_msg, 64, "Keine SD-Karte");
            state = OVL_ERROR; draw(hl); return;
        }
        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "%s...", label);
        download_pct = 0;
        draw(hl);

        HTTPClient http;
        http.begin(url);
        http.setTimeout(15000);
        int httpCode = http.GET();
        if (httpCode != 200) {
            snprintf(status_msg, 64, "HTTP %d — %s", httpCode, label);
            state = OVL_ERROR; draw(hl); http.end(); return;
        }

        int total = http.getSize();
        WiFiClient *stream = http.getStreamPtr();
        File f = sd->openWrite(path);
        if (!f) {
            snprintf(status_msg, 64, "SD Schreibfehler");
            state = OVL_ERROR; draw(hl); http.end(); return;
        }

        uint8_t buf[1024];
        int received = 0;
        while (http.connected() && (total < 0 || received < total)) {
            int avail = stream->available();
            if (avail > 0) {
                int rd = stream->readBytes(buf, min(avail, 1024));
                f.write(buf, rd);
                received += rd;
                if (total > 0) {
                    int pct = (int)(100.0f * received / total);
                    if (pct / 10 != download_pct / 10) {
                        download_pct = pct;
                        draw(hl);
                    }
                }
            }
            delay(1);
        }
        f.close(); http.end();

        Serial.printf("[DL] %d bytes → %s\n", received, path);
        snprintf(status_msg, 64, "%s: %d KB", label, received / 1024);
        state = OVL_DONE;
        draw(hl);
    }

    // OSM Karten-Tiles fuer Schweiz (Zoom 10-13, ~47.0-47.8N, 6.0-10.5E)
    void downloadTiles(EpdiyHighlevelState *hl) {
        if (!sd || !sd->ok) {
            snprintf(status_msg, 64, "Keine SD-Karte");
            state = OVL_ERROR; draw(hl); return;
        }
        if (!SD.exists("/tiles")) SD.mkdir("/tiles");

        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "Karten-Tiles Zoom 11...");
        download_pct = 0;
        draw(hl);

        // Zoom 11: Schweiz ca. x=1060-1075, y=720-730 (10 * 10 = ~100 Tiles)
        int zoom = 11;
        int x_min = 1060, x_max = 1075;
        int y_min = 720, y_max = 730;
        int total_tiles = (x_max - x_min + 1) * (y_max - y_min + 1);
        int done_tiles = 0;

        for (int x = x_min; x <= x_max; x++) {
            for (int y = y_min; y <= y_max; y++) {
                char url[128], path[64];
                // OSM Tile-Server (a/b/c round-robin)
                snprintf(url, 128, "https://a.tile.openstreetmap.org/%d/%d/%d.png", zoom, x, y);
                snprintf(path, 64, "/tiles/%d_%d_%d.png", zoom, x, y);

                if (!sd->exists(path)) {
                    HTTPClient http;
                    http.begin(url);
                    http.setTimeout(10000);
                    http.addHeader("User-Agent", "AuraVario/1.0");
                    int code = http.GET();
                    if (code == 200) {
                        int sz = http.getSize();
                        WiFiClient *stream = http.getStreamPtr();
                        File f = sd->openWrite(path);
                        if (f) {
                            uint8_t buf[1024];
                            int recv = 0;
                            while (http.connected() && (sz < 0 || recv < sz)) {
                                int av = stream->available();
                                if (av > 0) {
                                    int rd = stream->readBytes(buf, min(av, 1024));
                                    f.write(buf, rd); recv += rd;
                                }
                                delay(1);
                            }
                            f.close();
                        }
                    }
                    http.end();
                    delay(100);  // Rate-Limit respektieren
                }

                done_tiles++;
                int pct = (int)(100.0f * done_tiles / total_tiles);
                if (pct != download_pct) {
                    download_pct = pct;
                    snprintf(status_msg, 64, "Tile %d/%d", done_tiles, total_tiles);
                    if (pct % 5 == 0) draw(hl);
                }
            }
        }

        snprintf(status_msg, 64, "%d Karten-Tiles geladen", done_tiles);
        state = OVL_DONE;
        draw(hl);
    }
};
