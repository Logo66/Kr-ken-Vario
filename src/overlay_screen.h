#pragma once
// overlay_screen.h — Karte Overlays: Luftraeume + Hindernisse Download
// Braucht WiFi-Verbindung (ueber FUNK → WLAN)
#include <WiFi.h>
#include <HTTPClient.h>
#include "ui_utils.h"
#include "sd_manager.h"
#include "map_pack.h"
#include "pack_reader.h"   // parsePack: geladenes Pack einlesen + rendern (Stufe 3)
#include "obstacle_fetch.h"   // obstacleFetch: Hindernisse nach Standort holen (§5)

// Schweiz: Luftraeume von openAIP, Hindernisse vom BAZL (amtlich, tagesaktuell)
#define URL_AIRSPACE_CH   "https://storage.googleapis.com/29f98e10-a489-4c82-ae5e-489dbcd4912f/ch_asp.txt"
// BAZL Luftfahrthindernisse — GeoJSON, taeglich ab 04:00 aktualisiert
// Antennen, Gebaeude, Kabel, Krane, Seilbahnen, Hochspannung, Windenergie
// Erfassung: >25m unbebaut, >60m bebaut, >40m Mobilkrane
#define URL_OBSTACLES_CH  "https://data.geo.admin.ch/api/stac/v0.9/collections/ch.bazl.luftfahrthindernis/items?limit=5000"
// Hotspots (Thermik) von openAIP
#define URL_HOTSPOTS_CH   "https://storage.googleapis.com/29f98e10-a489-4c82-ae5e-489dbcd4912f/ch_hot.cup"

enum OverlayState {
    OVL_MENU,
    OVL_DOWNLOADING,
    OVL_DONE,
    OVL_ERROR,
    OVL_PACK_LIST      // Stufe 1: Regionen-Index vom Server anzeigen
};

class OverlayScreen {
public:
    OverlayState state = OVL_MENU;
    SDManager *sd = nullptr;
    char status_msg[64] = {0};
    int download_pct = 0;
    double curLat = 0, curLon = 0;   // aktueller Standort (von main gesetzt) — fuer Hindernis-Fetch

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
            bool has_obs = sd && sd->ok && sd->exists("/obstacles/obstacles.txt");
            bool has_hot = sd && sd->ok && sd->exists("/obstacles/ch_hot.cup");

            if (!wifi) {
                drawHCenter(&ArialBold16, "WiFi nicht verbunden — erst unter FUNK > WLAN verbinden",
                            0, 960, 80, fb);
            } else {
                drawHCenter(&ArialBold16, "WiFi verbunden — antippen zum Download",
                            0, 960, 80, fb);
            }

            // 4 Buttons: gleich gross, gleichmaessig verteilt
            int bx = 100, bw = 760, bh = 65, bgap = 18;
            int by0 = 100;
            char lb[48];

            uiBox(bx, by0, bw, bh, fb);
            snprintf(lb, 48, "Luftraeume CH  %s", has_asp ? "[OK]" : "");
            drawBoxCenter(&ArialBold16, lb, bx, by0, bw, bh, fb);

            uiBox(bx, by0+bh+bgap, bw, bh, fb);
            snprintf(lb, 48, "Hindernisse (Standort)  %s", has_obs ? "[OK]" : "");
            drawBoxCenter(&ArialBold16, lb, bx, by0+bh+bgap, bw, bh, fb);

            uiBox(bx, by0+2*(bh+bgap), bw, bh, fb);
            snprintf(lb, 48, "Hotspots CH  %s", has_hot ? "[OK]" : "");
            drawBoxCenter(&ArialBold16, lb, bx, by0+2*(bh+bgap), bw, bh, fb);

            uiBox(bx, by0+3*(bh+bgap), bw, bh, fb);
            drawBoxCenter(&ArialBold16, "Region-Pack laden (WLAN)", bx, by0+3*(bh+bgap), bw, bh, fb);

            if (!sd || !sd->ok) {
                drawHCenter(&ArialBold16, "KEINE SD-KARTE!", 0, 960, 440, fb);
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

        case OVL_PACK_LIST: {
            drawHCenter(&ArialBold24, "REGIONEN (Server)", 0, 960, 88, fb);
            if (mapRegionCount == 0) {
                drawHCenter(&ArialBold16, "Keine Region gemeldet", 0, 960, 200, fb);
            } else {
                int y = 120;
                char row[88];
                for (int i = 0; i < mapRegionCount; i++) {
                    MapRegion &m = mapRegions[i];
                    uiBox(80, y, 800, 54, fb);
                    snprintf(row, 88, "%s   v%d   %u B   %s", m.id, m.latest,
                             (unsigned)m.bytes, m.sha8);
                    drawBoxCenter(&ArialBold16, row, 80, y, 800, 54, fb);
                    y += 64;
                }
                drawHCenter(&ArialBold16, "Region antippen zum Laden (WLAN)",
                            0, 960, y + 12, fb);
            }
            uiBox(350, 460, 260, 52, fb);
            drawBoxCenter(&ArialBold24, "ZURUECK", 350, 460, 260, 52, fb);
            break;
        }
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
                } else if (ty >= 100 && ty < 165) {
                    downloadFile(hl, URL_AIRSPACE_CH, "/airspace/ch_asp.txt", "Luftraeume CH");
                } else if (ty >= 183 && ty < 248) {
                    downloadObstacles(hl);   // §5: Hindernisse NACH STANDORT vom Server
                } else if (ty >= 266 && ty < 331) {
                    downloadFile(hl, URL_HOTSPOTS_CH, "/obstacles/ch_hot.cup", "Hotspots CH");
                } else if (ty >= 349 && ty < 414) {
                    fetchRegionIndex(hl);
                }
            }
            // Zurueck
            if (ty >= 460 && ty < 512 && tx >= 350 && tx < 610) return true;
            break;

        case OVL_PACK_LIST:
            if (ty >= 450) return true;          // ZURUECK / untere Zone → Screen verlassen
            for (int i = 0; i < mapRegionCount; i++) {   // Region-Zeile antippen -> laden
                int ry = 120 + i * 64;
                if (ty >= ry && ty < ry + 54 && tx >= 80 && tx < 880) {
                    downloadRegion(hl, i);
                    return false;
                }
            }
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
    // Stufe 1: /maps/index vom Server holen + Regionen-Liste zeigen (W1)
    void fetchRegionIndex(EpdiyHighlevelState *hl) {
        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "Index laden...");
        download_pct = 0;
        draw(hl);

        char err[48];
        int code = mapPackFetchIndex(err, sizeof(err));
        if (code == 200) {
            state = OVL_PACK_LIST;
        } else {
            snprintf(status_msg, 64, "Index: %s", err);
            state = OVL_ERROR;
        }
        draw(hl);
    }

    static OverlayScreen *s_self;
    static EpdiyHighlevelState *s_hl;
    static void s_dlProgress(int pct) {           // Download-Fortschritt (14.7 MB dauern)
        if (!s_self || !s_hl) return;
        s_self->download_pct = pct;
        if (pct % 10 == 0) s_self->draw(s_hl);    // alle 10% neu zeichnen
    }

    // Stufe 3: Region-Pack laden (Bearer) -> SHA-256-Verify -> Standort-Fenster einlesen -> rendern.
    void downloadRegion(EpdiyHighlevelState *hl, int idx) {
        if (idx < 0 || idx >= mapRegionCount) return;
        MapRegion &reg = mapRegions[idx];
        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "Lade %s  %.1f MB ...", reg.id, reg.bytes / 1048576.0);
        download_pct = 0;
        draw(hl);

        s_self = this; s_hl = hl;
        char err[64];
        int code = mapPackDownload(reg, err, sizeof(err), s_dlProgress);
        if (code == 200) {
            parsePack(mapPackFile);   // SHA ok -> Fenster um Standort einlesen -> Karte rendert
            snprintf(status_msg, 64, "%s: %d Gipfel, %d Konturen", reg.id, peak_count, contour_count);
            state = OVL_DONE;
        } else {
            snprintf(status_msg, 64, "%s", err);   // REJECT/Fehler — klar, kein Crash
            state = OVL_ERROR;
        }
        draw(hl);
    }

    // §5: Hindernisse NACH STANDORT holen (GET /obstacles?lat&lon&r) -> SD -> parseObstacles.
    void downloadObstacles(EpdiyHighlevelState *hl) {
        if (!sd || !sd->ok) { snprintf(status_msg, 64, "Keine SD-Karte"); state = OVL_ERROR; draw(hl); return; }
        if (curLat == 0 && curLon == 0) { snprintf(status_msg, 64, "Kein GPS-Standort"); state = OVL_ERROR; draw(hl); return; }
        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "Hindernisse (Standort, r40km)...");
        download_pct = 0; draw(hl);
        char err[64];
        int c = obstacleFetch(curLat, curLon, 40, err, sizeof(err));
        snprintf(status_msg, 64, "%s", err);
        state = (c == 200 || c == 304) ? OVL_DONE : OVL_ERROR;
        draw(hl);
    }

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

    // OSM Karten-Tiles: Zoom 11-13 fuer Schweiz
    void downloadTiles(EpdiyHighlevelState *hl) {
        if (!sd || !sd->ok) {
            snprintf(status_msg, 64, "Keine SD-Karte");
            state = OVL_ERROR; draw(hl); return;
        }
        if (!SD.exists("/tiles")) SD.mkdir("/tiles");

        // Zoom-Stufen mit Tile-Bereichen (Schweiz 46.0-47.8N, 6.0-10.5E)
        struct ZoomRange { int z, x0, x1, y0, y1; };
        ZoomRange ranges[] = {
            {11, 1060, 1076, 713, 724},   // ~204 Tiles, ganze CH
            {12, 2130, 2150, 1426, 1440},  // ~315 Tiles, CH Mitte+Ost
            {13, 4270, 4296, 2852, 2870},  // ~513 Tiles, Ostschweiz
        };
        int num_ranges = 3;

        // Gesamtzahl berechnen
        int total_tiles = 0;
        for (int r = 0; r < num_ranges; r++) {
            total_tiles += (ranges[r].x1-ranges[r].x0+1) * (ranges[r].y1-ranges[r].y0+1);
        }

        state = OVL_DOWNLOADING;
        snprintf(status_msg, 64, "Karten Zoom 11-13...");
        download_pct = 0;
        draw(hl);

        int done_tiles = 0;
        for (int r = 0; r < num_ranges; r++) {
            int zoom = ranges[r].z;
            int x_min = ranges[r].x0, x_max = ranges[r].x1;
            int y_min = ranges[r].y0, y_max = ranges[r].y1;
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
                    http.setTimeout(5000);  // 5s statt 10s
                    http.addHeader("User-Agent", "AuraVario/1.0");
                    int code = http.GET();
                    if (code == 200) {
                        int sz = http.getSize();
                        WiFiClient *stream = http.getStreamPtr();
                        File f = sd->openWrite(path);
                        if (f) {
                            uint8_t buf[1024];
                            int recv = 0;
                            unsigned long dl_start = millis();
                            while (http.connected() && (sz < 0 || recv < sz)) {
                                int av = stream->available();
                                if (av > 0) {
                                    int rd = stream->readBytes(buf, min(av, 1024));
                                    f.write(buf, rd); recv += rd;
                                }
                                // Timeout: max 8s pro Tile
                                if (millis() - dl_start > 8000) break;
                                delay(1);
                            }
                            f.close();
                        }
                    } else {
                        Serial.printf("[DL] Tile FAIL %d: %s\n", code, path);
                    }
                    http.end();
                    delay(50);  // Rate-Limit
                    yield();
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
        }  // Ende Zoom-Loop

        snprintf(status_msg, 64, "%d Karten-Tiles geladen", done_tiles);
        state = OVL_DONE;
        draw(hl);
    }
};

OverlayScreen*       OverlayScreen::s_self = nullptr;
EpdiyHighlevelState* OverlayScreen::s_hl   = nullptr;
