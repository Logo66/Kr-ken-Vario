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
#include "openair_parser.h"
#include "peaks.h"
#include "contours.h"

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
// Karte bis zum rechten Rand (960px)
static const int MAP_CLIP_X=0, MAP_CLIP_Y=56, MAP_CLIP_W=960, MAP_CLIP_H=484;
static const int MAP_PILOT_X=480, MAP_PILOT_Y=296;
// Zoom-Buttons: rechts, ueber der Karte, Handschuh-gross
static const int MAP_BTN_X=850, MAP_BTN_W=100, MAP_BTN_H=140;
static const int MAP_BTN_PLUS_Y=66, MAP_BTN_MINUS_Y=220, MAP_BTN_CENTER_Y=374;

// Zoom-Stufen (Index 0..4 → Kartenbreite in Metern)
static const float ZOOM_M[] = {500, 1000, 2000, 5000, 10000, 20000, 50000};
static const char* ZOOM_LABEL[] = {"0.5 km", "1 km", "2 km", "5 km", "10 km", "20 km", "50 km"};
static int mapZoomIdx = 2;  // Start: 2 km
static bool   panActive = false;       // Karte verschoben (Pan) statt GPS-zentriert
static double panLat = 0, panLon = 0;  // Pan-Blickpunkt

enum MapAction { MAP_NONE, MAP_ZOOM_IN, MAP_ZOOM_OUT, MAP_RECENTER, MAP_PAN };

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
    int pixels_drawn;
    int dark_pixels;   // Nicht-weisse Pixel (Strassen, Text etc.)
};

static void tileDrawCb(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const uint8_t rgba[4]) {
    TileCtx *ctx = (TileCtx *)pngle_get_user_data(pngle);
    int px = ctx->draw_x + (int)x;
    int py = ctx->draw_y + (int)y;
    // Clip-Check
    if (px < ctx->clip_x || px >= ctx->clip_x + ctx->clip_w) return;
    if (py < ctx->clip_y || py >= ctx->clip_y + ctx->clip_h) return;
    // RGB → S/W fuer E-Paper: nur dunkle Features (Strassen, Text, Grenzen)
    uint8_t grey = (uint8_t)(0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2]);
    // OSM Hintergrund ~240, Wald ~207, Strassen ~165, Text ~0
    // Threshold 195: Strassen, Fluesse, Text, Grenzen sichtbar
    uint8_t epd_val = (grey < 195) ? 0x00 : 0xFF;
    epd_draw_pixel(px, py, epd_val, ctx->fb);
    ctx->pixels_drawn++;
    if (epd_val == 0) ctx->dark_pixels++;
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

    // 4x3 Tiles um den Center-Tile (960px breit braucht 4 Tiles)
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 2; dx++) {
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
            TileCtx ctx = {fb, ox, oy, MAP_CLIP_X+2, MAP_CLIP_Y+2, MAP_CLIP_W-4, MAP_CLIP_H-4, 0, 0};
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
                yield();  // Watchdog fuettern waehrend Decode
            }
            f.close();
            Serial.printf("[MAP] Tile %d_%d: fed=%d pixels=%d dark=%d\n",
                          tx, ty, total_fed, ctx.pixels_drawn, ctx.dark_pixels);
            delay(1);  // Watchdog fuettern zwischen Tiles
            // FB sofort nach diesem Tile pruefen
            if (tx == 1073 && ty == 715) {
                uint8_t *bp300 = &fb[300 * 480 + 300/2];
                Serial.printf("[MAP] FB(300,300) SOFORT nach Tile: 0x%02X\n", *bp300);
            }
            // Erste 8 Bytes der PNG roh loggen (PNG magic = 89 50 4E 47)
            if (ctx.pixels_drawn == 0) {
                f = SD.open(path, FILE_READ);
                if (f) {
                    uint8_t hdr[8];
                    f.read(hdr, 8);
                    f.close();
                    Serial.printf("[MAP] Header: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                                   hdr[0],hdr[1],hdr[2],hdr[3],hdr[4],hdr[5],hdr[6],hdr[7]);
                }
            }
            pngle_destroy(pngle);
        }
    }
}

// === Tile-Cache: Framebuffer-Kopie nach Tile-Rendering ===
static uint8_t *tileCacheFb = nullptr;  // PSRAM, 259200 Bytes

static void cacheTiles(uint8_t *fb) {
    int fb_size = epd_width() / 2 * epd_height();
    if (!tileCacheFb) {
        tileCacheFb = (uint8_t*)heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
        if (!tileCacheFb) { Serial.println("[MAP] Cache alloc FAIL"); return; }
    }
    memcpy(tileCacheFb, fb, fb_size);
}

static void restoreTiles(uint8_t *fb) {
    if (tileCacheFb) {
        int fb_size = epd_width() / 2 * epd_height();
        memcpy(fb, tileCacheFb, fb_size);
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

// === LUFTRAUM-ZEICHNUNG (Vektor, dicke Linien) ===
// Dicke Linie zwischen zwei Punkten (Bresenham mit Strichstaerke)
static void drawThickLine(int x0, int y0, int x1, int y1, int thick, uint8_t *fb) {
    int dx = abs(x1-x0), dy = abs(y1-y0);
    int steps = max(dx, dy);
    if (steps == 0 || steps > 2000) return;
    int ht = thick / 2;
    for (int s = 0; s <= steps; s++) {
        int x = x0 + (x1-x0) * s / steps;
        int y = y0 + (y1-y0) * s / steps;
        if (x-ht >= MAP_CLIP_X && x+ht < MAP_CLIP_X+MAP_CLIP_W &&
            y-ht >= MAP_CLIP_Y && y+ht < MAP_CLIP_Y+MAP_CLIP_H) {
            uiFill(x-ht, y-ht, thick, thick, fb);
        }
    }
}

static void drawAirspaces(double myLat, double myLon, int zoomIdx, uint8_t *fb) {
    for (int a = 0; a < airspace_count; a++) {
        Airspace &asp = airspaces[a];
        if (!asp.active || asp.num_pts < 3) continue;

        // Linienstärke nach Klasse
        int thick = 4;
        if (asp.cls == ASP_CTR || asp.cls == ASP_R || asp.cls == ASP_Q || asp.cls == ASP_P)
            thick = 5;  // Restricted/Prohibited extra dick

        // Polygon zeichnen (geschlossen)
        int first_sx = 0, first_sy = 0;
        int prev_sx = 0, prev_sy = 0;
        bool any_visible = false;

        for (int i = 0; i <= asp.num_pts; i++) {
            int idx = (i < asp.num_pts) ? i : 0;  // Letzter Punkt → zurueck zum ersten
            int sx, sy;
            projectToScreen(myLat, myLon, asp.pts[idx].lat, asp.pts[idx].lon, zoomIdx, &sx, &sy);

            if (i == 0) {
                first_sx = sx; first_sy = sy;
                prev_sx = sx; prev_sy = sy;
                continue;
            }

            // Linie nur wenn mindestens ein Endpunkt sichtbar
            bool p_vis = inClip(prev_sx, prev_sy);
            bool c_vis = inClip(sx, sy);
            if (p_vis || c_vis) {
                drawThickLine(prev_sx, prev_sy, sx, sy, thick, fb);
                any_visible = true;
            }
            prev_sx = sx;
            prev_sy = sy;
        }

        // Beschriftung: Klasse + Untergrenze am ersten sichtbaren Punkt
        if (any_visible) {
            int lx, ly;
            // Mitte des Polygons (Schwerpunkt der sichtbaren Punkte)
            float cx_sum = 0, cy_sum = 0;
            int vis_count = 0;
            for (int i = 0; i < asp.num_pts; i++) {
                int sx, sy;
                projectToScreen(myLat, myLon, asp.pts[i].lat, asp.pts[i].lon, zoomIdx, &sx, &sy);
                if (inClip(sx, sy)) {
                    cx_sum += sx; cy_sum += sy; vis_count++;
                }
            }
            if (vis_count > 0 && zoomIdx <= 4) {  // Labels nur bis 10 km (sonst Clutter)
                lx = (int)(cx_sum / vis_count);
                ly = (int)(cy_sum / vis_count);
                // Label: "CTR D · GND-2500"
                char label[48];
                snprintf(label, 48, "%s %s-%s", airspaceClassStr(asp.cls), asp.lower, asp.upper);
                // Weisser Hintergrund fuer Lesbarkeit
                int tw, th;
                measureText(&ArialBold16, label, &tw, &th);
                // Label nicht in Status-/Infoleiste schieben
                if (ly < MAP_CLIP_Y + th + 4) ly = MAP_CLIP_Y + th + 4;
                if (ly > MAP_CLIP_Y + MAP_CLIP_H - 28) ly = MAP_CLIP_Y + MAP_CLIP_H - 28;
                uiFill(lx - tw/2 - 2, ly - th - 2, tw + 4, th + 4, fb, 0xFF);
                drawHCenter(&ArialBold16, label, lx - tw/2, tw, ly, fb);
            }
        }
    }
}

// Luftraeume auch in der Naehe pruefen (Warnung)
static float nearestAirspaceDist(double myLat, double myLon) {
    float min_dist = 999999.0f;
    for (int a = 0; a < airspace_count; a++) {
        if (!airspaces[a].active || airspaces[a].num_pts < 3) continue;
        for (int i = 0; i < airspaces[a].num_pts; i++) {
            float dlat = (airspaces[a].pts[i].lat - myLat) * 111320.0f;
            float dlon = (airspaces[a].pts[i].lon - myLon) * 111320.0f * cosf(myLat * M_PI / 180.0f);
            float dist = sqrtf(dlat*dlat + dlon*dlon);
            if (dist < min_dist) min_dist = dist;
        }
    }
    return min_dist;
}

struct MapData {
    float heading;
    double lat, lon;
    float altitude;
    int rtc_hour, rtc_min, sats, bat_pct, fanet_peers;
    bool buddy_connected;
    bool gps_fix;
};

// === HOEHENLINIEN (Konturen als Polylinien) — Daten aus contours.h ===
static void drawContours(double myLat, double myLon, int zoomIdx, uint8_t *fb) {
    if (!contPool) return;
    for (int c = 0; c < contour_count; c++) {
        const Contour &ct = contours[c];
        if (ct.num_pts < 2) continue;
        // Lesbarkeit (Ticket §5): bei hohem Zoom nur Index-Konturen
        if (ZOOM_M[zoomIdx] >= 5000 && ct.flag == 0) continue;
        int thick = (ct.flag == 1) ? 2 : 1;          // Index dicker, normal duenn
        const ContourPt *pts = contPoints(ct);
        int prev_sx = 0, prev_sy = 0;
        for (int i = 0; i < ct.num_pts; i++) {
            int sx, sy;
            projectToScreen(myLat, myLon, pts[i].lat, pts[i].lon, zoomIdx, &sx, &sy);
            if (i > 0 && (inClip(prev_sx, prev_sy) || inClip(sx, sy)))
                drawThickLine(prev_sx, prev_sy, sx, sy, thick, fb);
            prev_sx = sx; prev_sy = sy;
        }
    }
}

// === GIPFEL-LAYER (Marker + Name + Hoehe) — Daten aus peaks.h ===
static void peakTriUp(int cx, int cy, int s, uint8_t *fb) {
    // gefuelltes Dreieck nach oben: Spitze (cx, cy-s), Basis bei cy
    for (int dy = 0; dy <= s; dy++) {
        int half = (dy * (s / 2)) / s;
        uiFill(cx - half, cy - s + dy, 2 * half + 1, 1, fb);
    }
}

static void drawPeaks(double myLat, double myLon, int zoomIdx, uint8_t *fb) {
    for (int i = 0; i < peak_count; i++) {
        int sx, sy;
        projectToScreen(myLat, myLon, peaks_arr[i].lat, peaks_arr[i].lon, zoomIdx, &sx, &sy);
        if (!inClip(sx, sy)) continue;
        peakTriUp(sx, sy, 9, fb);   // Mountain-Symbol
        char lbl[40];
        snprintf(lbl, sizeof(lbl), "%s %d", peaks_arr[i].name, peaks_arr[i].ele);
        int tw, th; measureText(&ArialBold16, lbl, &tw, &th);
        int lx = sx + 9;
        int ly = sy + 4;
        if (lx + tw > MAP_CLIP_X + MAP_CLIP_W - 4) lx = sx - 9 - tw;  // sonst links vom Gipfel
        uiFill(lx - 2, ly - th - 1, tw + 4, th + 4, fb, 0xFF);        // weisser Kasten dahinter
        drawText(&ArialBold16, lbl, lx, ly, fb);
    }
}

static void showMapScreen(EpdiyHighlevelState *hl, const MapData &d) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    // KEIN back_fb-memset: Vektor-Karte ist duenn; weisse Flaechen muessen den
    // vorherigen Screen ueberschreiben, sonst Ghosting. (memset war nur fuer Raster-Tiles.)
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

    // Akku rechts (volle Breite jetzt verfuegbar)
    uiBox(866,16,58,26,fb);
    uiFill(924,22,7,14,fb);
    uiFill(870,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);
    snprintf(buf,32,"%d%%",d.bat_pct);
    drawText(&ArialBold16, buf, 770, 38, fb);

    uiHLine(14, 54, 932, fb);

    // Karte edge-to-edge, kein Rahmen noetig

    // === ZENTRUM: Pan-Blickpunkt (falls verschoben), sonst GPS/letzte Position (nie 0,0) ===
    double cLat = panActive ? panLat : d.lat;
    double cLon = panActive ? panLon : d.lon;
    bool haveCenter = (cLat != 0 && cLon != 0);

    // === Raster-Tiles: Default AUS (KRUECKE-6B: Vektor statt Raster) ===
    // Tile-Code bleibt erhalten, aber nur aktiv wenn showRasterTiles=true
    static bool showRasterTiles = false;  // Default AUS
    if (showRasterTiles && d.lat != 0 && d.lon != 0) {
        static const int OSM_ZOOM[] = {15, 14, 13, 12, 11};
        int tileZoom = OSM_ZOOM[mapZoomIdx];
        char tp[48];
        snprintf(tp, 48, "/tiles/%d_%d_%d.png", tileZoom, lon2tile(d.lon,tileZoom), lat2tile(d.lat,tileZoom));
        if (!SD.exists(tp)) tileZoom = 11;
        drawTiles(d.lat, d.lon, tileZoom, fb);
    }

    // === KOORDINATEN-GITTER (dezent, als Orientierung) ===
    if (haveCenter) {
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

    // === HOEHENLINIEN (Konturen — unter Luftraum/Gipfel) ===
    if (haveCenter && contour_count > 0) {
        drawContours(cLat, cLon, mapZoomIdx, fb);
    }

    // === LUFTRAEUME (Vektor-Polygone, dicke Linien) ===
    if (haveCenter && airspace_count > 0) {
        drawAirspaces(cLat, cLon, mapZoomIdx, fb);
    }

    // === GIPFEL (Marker + Name + Hoehe, KRUECKE-6C Stufe 1) ===
    if (haveCenter && peak_count > 0) {
        drawPeaks(cLat, cLon, mapZoomIdx, fb);
    }

    // Ohne GPS-Fix: klarer Status (Karte zeigt letzte/Test-Position, nicht die Live-Position)
    if (panActive) {
        drawText(&ArialBold16, "VERSCHOBEN - Fadenkreuz = zurueck zu mir", 230, 80, fb);
    } else if (!d.gps_fix) {
        drawText(&ArialBold16, "kein GPS-Fix - letzte/Test-Position", 280, 80, fb);
    }

    // === TRACK-SPUR (dicke Linie, stroke 3.5) ===
    if (d.lat != 0 && d.lon != 0) {
        drawTrack(d.lat, d.lon, mapZoomIdx, fb);
    }

    // === PILOT-MARKER: weisser Kreis + grosses schwarzes Dreieck ===
    if (panActive) {
        // Verschoben: Pilot an seiner ECHTEN projizierten Position; Kreuz in der Mitte = Blickpunkt
        int psx, psy;
        projectToScreen(cLat, cLon, d.lat, d.lon, mapZoomIdx, &psx, &psy);
        if (inClip(psx, psy)) {
            for (int r = 24; r >= 18; r--) drawCircle(psx, psy, r, fb);
            uiFill(psx-18, psy-18, 36, 36, fb, 0xFF);
            drawCircle(psx, psy, 18, fb);
            mapTri(psx, psy, d.heading, 32, 24, fb);
        }
        uiHLine(MAP_PILOT_X-12, MAP_PILOT_Y, 24, fb, 2);   // Fadenkreuz = Pan-Blickpunkt
        uiVLine(MAP_PILOT_X, MAP_PILOT_Y-12, 24, fb, 2);
    } else {
        for (int r = 38; r >= 30; r--)
            drawCircle(MAP_PILOT_X, MAP_PILOT_Y, r, fb);  // Dicke weisse Umrandung
        uiFill(MAP_PILOT_X-30, MAP_PILOT_Y-30, 60, 60, fb, 0xFF);  // Weiss fuellen
        drawCircle(MAP_PILOT_X, MAP_PILOT_Y, 30, fb);  // Schwarzer Kreis-Rand
        mapTri(MAP_PILOT_X, MAP_PILOT_Y, d.heading, 52, 38, fb);  // Grosses Dreieck
    }

    // === NORDPFEIL (Linie 56,118→56,78 + Dreieck) ===
    uiVLine(56, 78, 40, fb, 3);  // Linie stroke 3
    mapTri(56, 74, 0, 24, 18, fb);  // Pfeil nach Norden
    drawText(&ArialBold16, "N", 46, 140, fb);

    // === INFO-LEISTE unten (eine Zeile, sauber aufgeteilt) ===
    // Weisser Balken als Hintergrund
    uiFill(MAP_CLIP_X+2, MAP_CLIP_Y+MAP_CLIP_H-26, MAP_CLIP_W-4, 24, fb, 0xFF);
    // Massstab links
    int bar_y = MAP_CLIP_Y+MAP_CLIP_H-14;
    uiHLine(20, bar_y, 80, fb, 3);
    uiVLine(20, bar_y-5, 10, fb, 2);
    uiVLine(98, bar_y-5, 10, fb, 2);
    drawText(&ArialBold16, ZOOM_LABEL[mapZoomIdx], 104, bar_y+5, fb);
    // Diagnose (temporaer): geladene Gipfel (G) / Luftraeume (L)
    snprintf(buf, 32, "G%d L%d", peak_count, airspace_count);
    drawText(&ArialBold16, buf, 690, bar_y+5, fb);
    // Koordinaten + Hoehe rechts
    if (d.lat != 0) {
        snprintf(buf,32,"%.3fN %.3fE  %.0fm", d.lat, d.lon, d.altitude);
        drawText(&ArialBold16, buf, 300, bar_y+5, fb);
    }

    // === ZOOM-BUTTONS schwebend, fett, Handschuh-tauglich ===
    int pcx = MAP_BTN_X + MAP_BTN_W/2;
    // [+]
    uiBox(MAP_BTN_X, MAP_BTN_PLUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    uiBox(MAP_BTN_X+3, MAP_BTN_PLUS_Y+3, MAP_BTN_W-6, MAP_BTN_H-6, fb);
    int pcy = MAP_BTN_PLUS_Y + MAP_BTN_H/2;
    uiFill(pcx-28, pcy-5, 56, 10, fb);   // Horizontal fett
    uiFill(pcx-5, pcy-28, 10, 56, fb);   // Vertikal fett
    // [-]
    uiBox(MAP_BTN_X, MAP_BTN_MINUS_Y, MAP_BTN_W, MAP_BTN_H, fb);
    uiBox(MAP_BTN_X+3, MAP_BTN_MINUS_Y+3, MAP_BTN_W-6, MAP_BTN_H-6, fb);
    int mcy = MAP_BTN_MINUS_Y + MAP_BTN_H/2;
    uiFill(pcx-28, mcy-5, 56, 10, fb);   // Horizontal fett
    // [Fadenkreuz]
    uiBox(MAP_BTN_X, MAP_BTN_CENTER_Y, MAP_BTN_W, MAP_BTN_H, fb);
    uiBox(MAP_BTN_X+3, MAP_BTN_CENTER_Y+3, MAP_BTN_W-6, MAP_BTN_H-6, fb);
    int ccy2 = MAP_BTN_CENTER_Y + MAP_BTN_H/2;
    drawCircle(pcx, ccy2, 24, fb);
    drawCircle(pcx, ccy2, 23, fb);
    uiFill(pcx-36, ccy2-3, 72, 6, fb);   // Horizontal fett
    uiFill(pcx-3, ccy2-36, 6, 72, fb);   // Vertikal fett

    // Tile-Framebuffer cachen fuer 1Hz Overlay-Updates
    cacheTiles(fb);

    // === RENDER ===
    epd_poweron();
    epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
}

// === 1Hz Overlay-Update (nur Pilot + Track + Statusbar, KEINE Tiles) ===
static void updateMapOverlay(EpdiyHighlevelState *hl, const MapData &d) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    // Gecachte Tiles wiederherstellen (statt alles loeschen)
    if (tileCacheFb) {
        restoreTiles(fb);
    } else {
        epd_hl_set_all_white(hl);
    }
    char buf[32];

    // Statusbar
    snprintf(buf,32,"%02d:%02d",d.rtc_hour,d.rtc_min);
    drawText(&ArialBold16, buf, 22, 38, fb);
    for(int i=0;i<11;i++){
        int cx=150+i*15;
        if(i<d.sats) fillCircle(cx,29,5,fb);
        else drawCircle(cx,29,5,fb);
    }
    snprintf(buf,32,"FANET %d",d.fanet_peers);
    drawText(&ArialBold16, buf, 332, 38, fb);
    uiBox(866,16,58,26,fb); uiFill(924,22,7,14,fb);
    uiFill(870,20,(int)(42.0f*d.bat_pct/100.0f),18,fb);
    uiHLine(14, 54, 932, fb);

    // Kein Rahmen — Karte edge-to-edge

    // Luftraeume
    if (d.lat != 0 && d.lon != 0 && airspace_count > 0)
        drawAirspaces(d.lat, d.lon, mapZoomIdx, fb);

    // Track
    if (d.lat != 0 && d.lon != 0)
        drawTrack(d.lat, d.lon, mapZoomIdx, fb);

    // Pilot-Marker mit weissem Halo
    uiFill(MAP_PILOT_X-30, MAP_PILOT_Y-30, 60, 60, fb, 0xFF);
    drawCircle(MAP_PILOT_X, MAP_PILOT_Y, 30, fb);
    mapTri(MAP_PILOT_X, MAP_PILOT_Y, d.heading, 52, 38, fb);

    // Info-Leiste unten (identisch wie showMapScreen)
    uiFill(MAP_CLIP_X+2, MAP_CLIP_Y+MAP_CLIP_H-26, MAP_CLIP_W-4, 24, fb, 0xFF);
    int bar_y = MAP_CLIP_Y+MAP_CLIP_H-14;
    uiHLine(20, bar_y, 80, fb, 3);
    uiVLine(20, bar_y-5, 10, fb, 2);
    uiVLine(98, bar_y-5, 10, fb, 2);
    drawText(&ArialBold16, ZOOM_LABEL[mapZoomIdx], 104, bar_y+5, fb);
    if (d.lat != 0) {
        snprintf(buf,32,"%.3fN %.3fE  %.0fm", d.lat, d.lon, d.altitude);
        drawText(&ArialBold16, buf, 300, bar_y+5, fb);
    }

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
        return MAP_NONE;
    }
    // Karten-Flaeche (links der Buttons) antippen -> dorthin verschieben (Pan)
    if (tx >= MAP_CLIP_X && tx < MAP_BTN_X && ty >= MAP_CLIP_Y+30 && ty < MAP_CLIP_Y+MAP_CLIP_H-30)
        return MAP_PAN;
    return MAP_NONE;
}
