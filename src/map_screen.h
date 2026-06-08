#pragma once
// map_screen.h — KRUECKE-6 Phase 1: Geruest
// Pixelgenau nach Ticket-Koordinaten, KEINE Schaetzungen
// Clip: x14 y56 w778 h470 | Steuer: x806 | Pilot: (400,288)
#include "ui_utils.h"
#include "arialbold32.h"
#include "arialbold24.h"
#include <math.h>
#include <SD.h>
#include <pngle.h>

// === Dreieck-Formel aus Ticket §2 (identisch zu goal_screen) ===
static void mapTri(int cx, int cy, float ang, int L, int Wd, uint8_t *fb) {
    float a=ang*M_PI/180.0f, sa=sinf(a), ca=cosf(a);
    float tx=cx+0.6f*L*sa, ty=cy-0.6f*L*ca;
    float bx=cx-0.4f*L*sa, by=cy+0.4f*L*ca;
    float blx=bx+0.5f*Wd*ca, bly=by+0.5f*Wd*sa;
    float brx=bx-0.5f*Wd*ca, bry=by-0.5f*Wd*sa;
    int miny=(int)fminf(ty,fminf(bly,bry)), maxy=(int)fmaxf(ty,fmaxf(bly,bry));
    for(int y=miny;y<=maxy;y++){
        int xl=960,xr=0;
        float e[][4]={{tx,ty,blx,bly},{tx,ty,brx,bry},{blx,bly,brx,bry}};
        for(int i=0;i<3;i++){
            float dy=e[i][3]-e[i][1]; if(fabsf(dy)<0.5f)continue;
            float t=(y-e[i][1])/dy; if(t<0||t>1)continue;
            int x=(int)(e[i][0]+t*(e[i][2]-e[i][0]));
            if(x<xl)xl=x; if(x>xr)xr=x;
        }
        if(xl<=xr&&xl>=0&&xr<960&&y>=0&&y<540) uiFill(xl,y,xr-xl+1,1,fb);
    }
}

// === Layout-Konstanten (exakt aus Ticket) ===
static const int MAP_CLIP_X=14, MAP_CLIP_Y=56, MAP_CLIP_W=778, MAP_CLIP_H=470;
static const int MAP_PILOT_X=400, MAP_PILOT_Y=288;
static const int MAP_BTN_X=806, MAP_BTN_W=128, MAP_BTN_H=104, MAP_BTN_RX=10;
static const int MAP_BTN_PLUS_Y=66, MAP_BTN_MINUS_Y=182, MAP_BTN_CENTER_Y=298;

// Zoom-Stufen (Index 0..4 → Kartenbreite in Metern)
static const float ZOOM_M[] = {500, 1000, 2000, 5000, 10000};
static const char* ZOOM_LABEL[] = {"0.5 km", "1 km", "2 km", "5 km", "10 km"};
static int mapZoomIdx = 2;  // Start: 2 km

enum MapAction { MAP_NONE, MAP_ZOOM_IN, MAP_ZOOM_OUT, MAP_RECENTER };

// === Projektion §5: Welt → Bildschirm (North-Up) ===
// Eigene Position fix in (400, 288). m_per_px = zoom_m / 778
static void projectToScreen(double myLat, double myLon, double pLat, double pLon,
                             int zoomIdx, int *sx, int *sy) {
    float m_per_px = ZOOM_M[zoomIdx] / (float)MAP_CLIP_W;
    float dx_m = (float)(pLon - myLon) * 111320.0f * cosf((float)myLat * M_PI / 180.0f);
    float dy_m = (float)(pLat - myLat) * 111320.0f;
    *sx = MAP_PILOT_X + (int)(dx_m / m_per_px);
    *sy = MAP_PILOT_Y - (int)(dy_m / m_per_px);  // Norden = oben
}

static bool inClip(int x, int y) {
    return x >= MAP_CLIP_X+2 && x <= MAP_CLIP_X+MAP_CLIP_W-4 &&
           y >= MAP_CLIP_Y+2 && y <= MAP_CLIP_Y+MAP_CLIP_H-4;
}

// === OSM Tile Rendering ===
// OSM Tiles: Zoom 11, 256x256 PNG → Graustufen auf Framebuffer
// Tile-Nummer aus Lat/Lon: x = floor((lon+180)/360 * 2^z), y = floor((1-log(tan(lat)+1/cos(lat))/pi)/2 * 2^z)
static int lon2tile(double lon, int z) { return (int)floor((lon + 180.0) / 360.0 * (1 << z)); }
static int lat2tile(double lat, int z) {
    double lr = lat * M_PI / 180.0;
    return (int)floor((1.0 - log(tan(lr) + 1.0/cos(lr)) / M_PI) / 2.0 * (1 << z));
}
// Pixel-Position innerhalb eines Tiles
static double lon2pix(double lon, int z) {
    double t = (lon + 180.0) / 360.0 * (1 << z);
    return (t - floor(t)) * 256.0;
}
static double lat2pix(double lat, int z) {
    double lr = lat * M_PI / 180.0;
    double t = (1.0 - log(tan(lr) + 1.0/cos(lr)) / M_PI) / 2.0 * (1 << z);
    return (t - floor(t)) * 256.0;
}

// PNG-Decode Callback Kontext
struct TileCtx {
    uint8_t *fb;
    int draw_x, draw_y;
    int clip_x, clip_y, clip_w, clip_h;
    int pixels_drawn;  // Debug-Zaehler
};

static void tileDrawCb(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const uint8_t rgba[4]) {
    TileCtx *ctx = (TileCtx *)pngle_get_user_data(pngle);
    int px = ctx->draw_x + (int)x;
    int py = ctx->draw_y + (int)y;
    // Clip-Check
    if (px < ctx->clip_x || px >= ctx->clip_x + ctx->clip_w) return;
    if (py < ctx->clip_y || py >= ctx->clip_y + ctx->clip_h) return;
    // RGB → Graustufen (0=schwarz, 255=weiss)
    uint8_t grey = (uint8_t)(0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2]);
    epd_draw_pixel(px, py, grey, ctx->fb);
    ctx->pixels_drawn++;
}

static void drawTiles(double lat, double lon, int zoom, uint8_t *fb) {
    if (lat == 0 && lon == 0) return;

    int center_tx = lon2tile(lon, zoom);
    int center_ty = lat2tile(lat, zoom);
    int px_in_tile_x = (int)lon2pix(lon, zoom);
    int px_in_tile_y = (int)lat2pix(lat, zoom);

    // Pilot sitzt bei MAP_PILOT_X, MAP_PILOT_Y
    // Offset: wo das Center-Tile hingezeichnet wird
    int tile_origin_x = MAP_PILOT_X - px_in_tile_x;
    int tile_origin_y = MAP_PILOT_Y - px_in_tile_y;

    // 3x3 Tiles um den Center-Tile zeichnen
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int tx = center_tx + dx;
            int ty = center_ty + dy;
            int ox = tile_origin_x + dx * 256;
            int oy = tile_origin_y + dy * 256;

            // Sichtbarkeits-Check
            if (ox + 256 < MAP_CLIP_X || ox > MAP_CLIP_X + MAP_CLIP_W) continue;
            if (oy + 256 < MAP_CLIP_Y || oy > MAP_CLIP_Y + MAP_CLIP_H) continue;

            char path[48];
            snprintf(path, 48, "/tiles/%d_%d_%d.png", zoom, tx, ty);
            File f = SD.open(path, FILE_READ);
            if (!f) {
                Serial.printf("[MAP] Tile fehlt: %s\n", path);
                continue;
            }
            Serial.printf("[MAP] Tile laden: %s (%d bytes) → (%d,%d)\n", path, f.size(), ox, oy);

            pngle_t *pngle = pngle_new();
            TileCtx ctx = {fb, ox, oy, MAP_CLIP_X+2, MAP_CLIP_Y+2, MAP_CLIP_W-4, MAP_CLIP_H-4, 0};
            pngle_set_user_data(pngle, &ctx);
            pngle_set_draw_callback(pngle, tileDrawCb);

            // LoRa CS HIGH waehrend SD-Zugriff (geteilter SPI)
            digitalWrite(BOARD_LORA_CS, HIGH);

            uint8_t buf[512];
            int total_fed = 0;
            while (f.available()) {
                int rd = f.read(buf, sizeof(buf));
                if (rd > 0) {
                    int ret = pngle_feed(pngle, buf, rd);
                    total_fed += rd;
                    if (ret < 0) {
                        Serial.printf("[MAP] pngle error: %s\n", pngle_error(pngle));
                        break;
                    }
                }
            }
            f.close();
            Serial.printf("[MAP] Tile %d_%d: fed=%d pixels=%d\n", tx, ty, total_fed, ctx.pixels_drawn);
            pngle_destroy(pngle);
        }
    }
}

// === Track-Buffer (letzte 200 GPS-Positionen) ===
static const int TRACK_MAX = 200;
struct TrackPoint { double lat, lon; };
static TrackPoint trackBuf[TRACK_MAX];
static int trackCount = 0;
static int trackHead = 0;

static void trackAdd(double lat, double lon) {
    if (lat == 0 && lon == 0) return;
    trackBuf[trackHead] = {lat, lon};
    trackHead = (trackHead + 1) % TRACK_MAX;
    if (trackCount < TRACK_MAX) trackCount++;
}

static void drawTrack(double myLat, double myLon, int zoomIdx, uint8_t *fb) {
    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < trackCount; i++) {
        int idx = (trackHead - trackCount + i + TRACK_MAX) % TRACK_MAX;
        int sx, sy;
        projectToScreen(myLat, myLon, trackBuf[idx].lat, trackBuf[idx].lon, zoomIdx, &sx, &sy);
        if (inClip(sx, sy)) {
            if (prev_x >= 0 && inClip(prev_x, prev_y)) {
                // Linie von prev zu current (dicke Linie, stroke 3.5→4px)
                int dx = sx - prev_x, dy = sy - prev_y;
                int steps = max(abs(dx), abs(dy));
                if (steps > 0 && steps < 500) {
                    for (int s = 0; s <= steps; s++) {
                        int lx = prev_x + dx * s / steps;
                        int ly = prev_y + dy * s / steps;
                        uiFill(lx-1, ly-1, 3, 3, fb);
                    }
                }
            }
            prev_x = sx; prev_y = sy;
        } else {
            prev_x = -1; prev_y = -1;
        }
    }
}

struct MapData {
    float heading;
    double lat, lon;
    float altitude;
    int rtc_hour, rtc_min, sats, bat_pct, fanet_peers;
    bool buddy_connected;
    bool gps_fix;
};

static void showMapScreen(EpdiyHighlevelState *hl, const MapData &d) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[32];

    // === STATUSBAR (Akku bei x700, nicht x866 — Ticket §3 Karte) ===
    snprintf(buf,32,"%02d:%02d",d.rtc_hour,d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);
    for(int i=0;i<11;i++){
        int cx=150+i*15;
        if(i<d.sats) fillCircle(cx,29,5,fb);
        else drawCircle(cx,29,5,fb);
    }
    snprintf(buf,32,"FANET %d",d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);

    // Akku bei x700 (Ticket: Karte hat Tasten rechts)
    uiBox(700,16,58,26,fb);
    uiFill(758,22,7,14,fb);
    uiFill(704,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);
    snprintf(buf,32,"%d%%",d.bat_pct);
    drawText(&ArialBold16, buf, 770, 38, fb);

    uiHLine(14, 54, 932, fb);

    // === KARTEN-CLIP-RAHMEN (x14 y56 w778 h470, stroke 2) ===
    uiHLine(MAP_CLIP_X, MAP_CLIP_Y, MAP_CLIP_W, fb);
    uiHLine(MAP_CLIP_X, MAP_CLIP_Y+MAP_CLIP_H-2, MAP_CLIP_W, fb);
    uiVLine(MAP_CLIP_X, MAP_CLIP_Y, MAP_CLIP_H, fb);
    uiVLine(MAP_CLIP_X+MAP_CLIP_W-2, MAP_CLIP_Y, MAP_CLIP_H, fb);

    // === OSM TILE HINTERGRUND (wenn auf SD vorhanden) ===
    bool tiles_drawn = false;
    if (d.lat != 0 && d.lon != 0) {
        drawTiles(d.lat, d.lon, 11, fb);
        // Prüfen ob mindestens 1 Tile existierte (grob: Center-Tile)
        char tp[48];
        snprintf(tp, 48, "/tiles/11_%d_%d.png", lon2tile(d.lon,11), lat2tile(d.lat,11));
        tiles_drawn = SD.exists(tp);
    }

    // === KOORDINATEN-GITTER (wenn keine Tiles) ===
    if (!tiles_drawn && d.lat != 0 && d.lon != 0) {
        float m_per_px = ZOOM_M[mapZoomIdx] / (float)MAP_CLIP_W;
        // Horizontale + vertikale Linien alle 500m / 1km je nach Zoom
        float grid_m = (ZOOM_M[mapZoomIdx] <= 2000) ? 500.0f : 1000.0f;
        float grid_px = grid_m / m_per_px;

        // Gitter zentriert auf Pilot
        if (grid_px > 30) {  // Nur zeichnen wenn Abstand > 30px
            for (float gx = MAP_PILOT_X - 5*grid_px; gx < MAP_PILOT_X + 5*grid_px; gx += grid_px) {
                int ix = (int)gx;
                if (ix > MAP_CLIP_X+2 && ix < MAP_CLIP_X+MAP_CLIP_W-4) {
                    // Gestrichelte Linie
                    for (int y = MAP_CLIP_Y+4; y < MAP_CLIP_Y+MAP_CLIP_H-4; y += 8)
                        uiFill(ix, y, 1, 4, fb);
                }
            }
            for (float gy = MAP_PILOT_Y - 5*grid_px; gy < MAP_PILOT_Y + 5*grid_px; gy += grid_px) {
                int iy = (int)gy;
                if (iy > MAP_CLIP_Y+2 && iy < MAP_CLIP_Y+MAP_CLIP_H-4) {
                    for (int x = MAP_CLIP_X+4; x < MAP_CLIP_X+MAP_CLIP_W-4; x += 8)
                        uiFill(x, iy, 4, 1, fb);
                }
            }
        }
    }

    // === TRACK-SPUR (dicke Linie, stroke 3.5) ===
    if (d.lat != 0 && d.lon != 0) {
        drawTrack(d.lat, d.lon, mapZoomIdx, fb);
    }

    // === PILOT-DREIECK (400,288) mit live Heading, tri()-Formel ===
    mapTri(MAP_PILOT_X, MAP_PILOT_Y, d.heading, 46, 34, fb);

    // === NORDPFEIL (Linie 56,118→56,78 + Dreieck) ===
    uiVLine(56, 78, 40, fb, 3);  // Linie stroke 3
    mapTri(56, 74, 0, 24, 18, fb);  // Pfeil nach Norden
    drawText(&ArialBold16, "N", 46, 140, fb);

    // === POSITIONS-INFO (im Kartenbereich, unten rechts) ===
    if (d.gps_fix && d.lat != 0) {
        snprintf(buf,32,"%.4f N", d.lat);
        drawText(&ArialBold16, buf, 550, 490, fb);
        snprintf(buf,32,"%.4f E", d.lon);
        drawText(&ArialBold16, buf, 550, 510, fb);
    }
    snprintf(buf,32,"%.0f m", d.altitude);
    drawText(&ArialBold16, buf, 700, 500, fb);

    // === MASSSTAB (Linie 40,506→160,506 + Endmarken) ===
    uiHLine(40, 506, 120, fb, 3);  // Hauptlinie stroke 3
    uiVLine(40, 500, 12, fb, 2);   // Endmarke links
    uiVLine(158, 500, 12, fb, 2);  // Endmarke rechts
    drawText(&ArialBold16, ZOOM_LABEL[mapZoomIdx], 66, 494, fb);

    // === STEUER-SPALTE (3 Touch-Buttons) ===

    // [+] Taste (x806 y66 w128 h104 rx10)
    uiBox(MAP_BTN_X, MAP_BTN_PLUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    drawBoxCenter(&ArialBold32, "+", MAP_BTN_X, MAP_BTN_PLUS_Y, MAP_BTN_W, MAP_BTN_H, fb);

    // [-] Taste (x806 y182 w128 h104)
    uiBox(MAP_BTN_X, MAP_BTN_MINUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    drawBoxCenter(&ArialBold32, "-", MAP_BTN_X, MAP_BTN_MINUS_Y, MAP_BTN_W, MAP_BTN_H, fb);

    // [Re-Center] Taste (x806 y298 w128 h104) — Fadenkreuz
    uiBox(MAP_BTN_X, MAP_BTN_CENTER_Y, MAP_BTN_W, MAP_BTN_H, fb);
    int ccx=870, ccy=350;
    drawCircle(ccx, ccy, 26, fb);         // Kreis
    uiVLine(ccx, ccy-38, 76, fb, 3);     // Vertikale Linie
    uiHLine(ccx-38, ccy, 76, fb, 3);     // Horizontale Linie

    // Zoom-Text
    drawHCenter(&ArialBold16, "ZOOM", MAP_BTN_X, MAP_BTN_W, 448, fb);
    drawHCenter(&ArialBold32, ZOOM_LABEL[mapZoomIdx], MAP_BTN_X, MAP_BTN_W, 486, fb);

    // === RENDER ===
    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

// Touch-Handler
static MapAction checkMapTap(int tx, int ty) {
    if (tx>=MAP_BTN_X && tx<MAP_BTN_X+MAP_BTN_W) {
        if (ty>=MAP_BTN_PLUS_Y && ty<MAP_BTN_PLUS_Y+MAP_BTN_H) return MAP_ZOOM_IN;
        if (ty>=MAP_BTN_MINUS_Y && ty<MAP_BTN_MINUS_Y+MAP_BTN_H) return MAP_ZOOM_OUT;
        if (ty>=MAP_BTN_CENTER_Y && ty<MAP_BTN_CENTER_Y+MAP_BTN_H) return MAP_RECENTER;
    }
    return MAP_NONE;
}
