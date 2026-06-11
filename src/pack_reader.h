#pragma once
// pack_reader.h — liest region_<name>_v1.pack nach AURA-KRUECKE-8 §3 (Little-Endian).
// Stufe 1: nur Gipfel (Konturen werden uebersprungen). Fuellt peaks_arr (PSRAM) aus peaks.h.
// Du erzeugst KEINE Kartendaten — du liest den Server-Pack. Format §3 ist Vertrag.
#include <Arduino.h>
#include <SD.h>
#include "peaks.h"
#include "contours.h"

// Little-Endian Leser auf einem Speicherpuffer
static inline uint16_t lePackU16(const uint8_t *b, size_t &p){ uint16_t v=(uint16_t)b[p]|((uint16_t)b[p+1]<<8); p+=2; return v; }
static inline int16_t  lePackI16(const uint8_t *b, size_t &p){ return (int16_t)lePackU16(b,p); }
static inline uint32_t lePackU32(const uint8_t *b, size_t &p){ uint32_t v=(uint32_t)b[p]|((uint32_t)b[p+1]<<8)|((uint32_t)b[p+2]<<16)|((uint32_t)b[p+3]<<24); p+=4; return v; }
static inline int32_t  lePackI32(const uint8_t *b, size_t &p){ return (int32_t)lePackU32(b,p); }

// Standort-Fenster fuer den Loader (main.cpp setzt = lastGood). Nur Kacheln im Umkreis laden.
static double parseCenterLat = 47.5973, parseCenterLon = 8.7848;
static double parseRadiusKm  = 15.0;
static char   mapLoadedPack[80] = {0};              // zuletzt geladenes Pack (Reload bei Bewegung)
static double mapParsedLat = 0, mapParsedLon = 0;   // Zentrum, um das geladen wurde
struct TileRef { uint32_t off, len; double lat0, lon0; float distSq; };

// parsePack — seek-per-Tile (KEIN Full-Load -> auch 14.7 MB CH-Pack passt). Laedt nur die
// Kacheln im Umkreis von (parseCenterLat,parseCenterLon), distanz-sortiert, gecappt auf
// CONT_MAX/CONT_POOL/PEAK_MAX. Byte-Format identisch zum Vertrag (dumpPackContract unveraendert).
static int parsePack(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[PACK] nicht gefunden: %s\n", path); return 0; }
    size_t sz = f.size();
    if (sz < 25) { Serial.println("[PACK] Datei zu klein"); f.close(); return 0; }

    // HEADER (25 Byte)
    uint8_t hdr[25];
    if (f.read(hdr, 25) != 25) { f.close(); return 0; }
    if (memcmp(hdr, "AURA", 4) != 0) { Serial.println("[PACK] kein AURA-Header!"); f.close(); return 0; }
    size_t hp = 4;
    uint8_t version = hdr[hp++];
    if (version != 1) { Serial.printf("[PACK] REJECT: Version %d (erwartet 1)\n", version); f.close(); return 0; }
    char region[17] = {0}; memcpy(region, hdr+hp, 16); hp += 16;
    uint16_t tile_size  = lePackU16(hdr, hp);   (void)tile_size;
    uint16_t tile_count = lePackU16(hdr, hp);
    Serial.printf("[PACK] AURA v%d region=%s tiles=%d (%d KB) @ %.4f,%.4f r=%.0fkm\n",
                  version, region, tile_count, (int)(sz/1024), parseCenterLat, parseCenterLon, parseRadiusKm);

    if (!peaks_arr) {
        peaks_arr = (Peak*)heap_caps_malloc((size_t)PEAK_MAX*sizeof(Peak), MALLOC_CAP_SPIRAM);
        if (!peaks_arr) { Serial.println("[PACK] peaks PSRAM FAIL"); f.close(); return 0; }
    }
    peak_count = 0;
    if (!contInit()) { f.close(); return 0; }

    // INDEX lesen (tile_count*16) -> PSRAM
    size_t idxBytes = (size_t)tile_count * 16;
    if (25 + idxBytes > sz) { Serial.println("[PACK] Index truncated"); f.close(); return 0; }
    uint8_t *idx = (uint8_t*)heap_caps_malloc(idxBytes, MALLOC_CAP_SPIRAM);
    if (!idx) { Serial.println("[PACK] Index PSRAM FAIL"); f.close(); return 0; }
    f.seek(25);
    if (f.read(idx, idxBytes) != idxBytes) { heap_caps_free(idx); f.close(); return 0; }

    // Kacheln im Umkreis sammeln + nach Distanz sortieren (naechste zuerst)
    static const int MAXWIN = 400;
    TileRef *win = (TileRef*)heap_caps_malloc(sizeof(TileRef)*MAXWIN, MALLOC_CAP_SPIRAM);
    if (!win) { heap_caps_free(idx); f.close(); return 0; }
    int nwin = 0;
    double coslat = cos(parseCenterLat * M_PI/180.0);
    double rSq = parseRadiusKm * parseRadiusKm;
    for (int t = 0; t < tile_count && nwin < MAXWIN; t++) {
        size_t pp = (size_t)t*16;
        int32_t lat0 = lePackI32(idx, pp), lon0 = lePackI32(idx, pp);
        uint32_t off = lePackU32(idx, pp), len = lePackU32(idx, pp);
        double dlat0 = lat0/1e7, dlon0 = lon0/1e7;
        double dkmLat = (dlat0 - parseCenterLat)*111.0;
        double dkmLon = (dlon0 - parseCenterLon)*111.0*coslat;
        float dsq = (float)(dkmLat*dkmLat + dkmLon*dkmLon);
        if (dsq > rSq) continue;
        if (off >= sz || len == 0 || (size_t)off+len > sz) continue;
        win[nwin].off=off; win[nwin].len=len; win[nwin].lat0=dlat0; win[nwin].lon0=dlon0; win[nwin].distSq=dsq;
        nwin++;
    }
    for (int i = 1; i < nwin; i++) { TileRef k = win[i]; int j = i-1;
        while (j >= 0 && win[j].distSq > k.distSq) { win[j+1] = win[j]; j--; } win[j+1] = k; }
    heap_caps_free(idx);

    // Tile-Puffer (ein Block; CH ~7 KB avg)
    const size_t TILEBUF = 128*1024;
    uint8_t *tbuf = (uint8_t*)heap_caps_malloc(TILEBUF, MALLOC_CAP_SPIRAM);
    if (!tbuf) { heap_caps_free(win); f.close(); return 0; }

    int loaded = 0;
    for (int w = 0; w < nwin; w++) {
        if (contour_count >= CONT_MAX || contPoolUsed >= CONT_POOL - 200) break;   // voll
        TileRef &tr = win[w];
        if (tr.len > TILEBUF) continue;
        f.seek(tr.off);
        if (f.read(tbuf, tr.len) != tr.len) continue;
        size_t bp = 0;
        uint16_t n_contours = lePackU16(tbuf, bp);
        for (int c = 0; c < n_contours && bp < tr.len; c++) {
            if (bp + 5 > tr.len) break;
            int16_t ch = lePackI16(tbuf, bp);
            uint8_t cf = tbuf[bp++];
            uint16_t np = lePackU16(tbuf, bp);
            if (bp + (size_t)np * 4 > tr.len) break;
            if (contour_count >= CONT_MAX || contPoolUsed + (int)np > CONT_POOL) { bp += (size_t)np*4; continue; }
            contBegin(ch, cf);
            for (int i = 0; i < np; i++) {
                int16_t cdlat = lePackI16(tbuf, bp);
                int16_t cdlon = lePackI16(tbuf, bp);
                contAddPt((float)(tr.lat0 + (double)cdlat/1e5), (float)(tr.lon0 + (double)cdlon/1e5));
            }
            contEnd();
        }
        if (bp + 2 > tr.len) { loaded++; continue; }
        uint16_t n_peaks = lePackU16(tbuf, bp);
        for (int k = 0; k < n_peaks && peak_count < PEAK_MAX; k++) {
            if (bp + 8 > tr.len) break;
            int16_t pdlat = lePackI16(tbuf, bp);
            int16_t pdlon = lePackI16(tbuf, bp);
            int16_t ele   = lePackI16(tbuf, bp);
            uint8_t rank  = tbuf[bp++];  (void)rank;
            uint8_t nlen  = tbuf[bp++];
            if (bp + nlen > tr.len) break;
            Peak &pk = peaks_arr[peak_count];
            pk.lat = (float)(tr.lat0 + (double)pdlat/1e5);
            pk.lon = (float)(tr.lon0 + (double)pdlon/1e5);
            pk.ele = ele;
            int rd = (nlen > 25) ? 25 : nlen;
            memcpy(pk.name, tbuf+bp, rd); pk.name[rd] = 0;
            bp += nlen;
            peak_count++;
        }
        // n_airspace + n_obstacles folgen (v1 = 0) — kein Skip noetig (naechste Kachel per seek)
        loaded++;
        yield();
    }
    heap_caps_free(tbuf);
    heap_caps_free(win);
    f.close();

    if (contour_count > 0 || peak_count > 0) {
        packCenterLat = parseCenterLat; packCenterLon = parseCenterLon;
        mapParsedLat  = parseCenterLat; mapParsedLon  = parseCenterLon;
        strncpy(mapLoadedPack, path, sizeof(mapLoadedPack)-1); mapLoadedPack[sizeof(mapLoadedPack)-1] = 0;
    }
    int nWater = 0; for (int c = 0; c < contour_count; c++) if (contours[c].flag == 2) nWater++;
    Serial.printf("[PACK] %d/%d Kacheln im Umkreis -> %d Gipfel + %d Konturen (%d Wasser)\n",
                  loaded, nwin, peak_count, contour_count, nWater);
    return peak_count;
}

// === Contract-Cross-Read (KRUECKE-8 Vertrag, Schritt C/P3/P4) ===
// Liest ein Pack und gibt ALLE dekodierten Felder ueber Serial aus (Abgleich gegen Server-expected.json).
// Reject-Pfad: kaputtes magic/version/Truncation -> klare Meldung, KEIN Crash.
static void dumpPackContract(const char *path) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[CONTRACT] nicht gefunden: %s\n", path); return; }
    size_t sz = f.size();
    if (sz < 25) { Serial.println("[CONTRACT] REJECT: zu klein"); f.close(); return; }
    uint8_t *buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!buf) { Serial.println("[CONTRACT] PSRAM FAIL"); f.close(); return; }
    size_t got = f.read(buf, sz); f.close();
    if (got != sz) { Serial.println("[CONTRACT] Lesefehler"); heap_caps_free(buf); return; }

    Serial.println("===== PACK-CONTRACT CROSS-READ =====");
    if (memcmp(buf,"AURA",4)!=0) { Serial.println("[CONTRACT] REJECT: magic != AURA"); heap_caps_free(buf); return; }
    size_t p=4;
    uint8_t version = buf[p++];
    if (version != 1) { Serial.printf("[CONTRACT] REJECT: version=%d (erwartet 1)\n", version); heap_caps_free(buf); return; }
    char region[17]={0}; memcpy(region,buf+p,16); p+=16;
    uint16_t tsize = lePackU16(buf,p);
    uint16_t tcount = lePackU16(buf,p);
    Serial.printf("HEADER magic=AURA version=%d region=%s tile_size=%d tile_count=%d\n", version, region, tsize, tcount);

    bool ok = true;
    for (int t=0; t<tcount && ok; t++){
        size_t ip = 25 + (size_t)t*16;
        if (ip+16 > sz) { Serial.println("[CONTRACT] REJECT: Index truncated"); ok=false; break; }
        size_t pp=ip;
        int32_t lat0=lePackI32(buf,pp), lon0=lePackI32(buf,pp);
        uint32_t off=lePackU32(buf,pp), len=lePackU32(buf,pp); (void)len;
        double dlat0=lat0/1e7, dlon0=lon0/1e7;
        Serial.printf("TILE %d origin=%.7f,%.7f off=%u\n", t, dlat0, dlon0, (unsigned)off);
        if (off+2 > sz) { Serial.println("[CONTRACT] REJECT: Tile-Offset truncated"); ok=false; break; }
        size_t bp=off;
        uint16_t nc=lePackU16(buf,bp);
        Serial.printf("  n_contours=%d\n", nc);
        for (int c=0;c<nc && ok;c++){
            if (bp+5 > sz) { Serial.println("[CONTRACT] REJECT: Kontur truncated"); ok=false; break; }
            int16_t h=lePackI16(buf,bp); uint8_t flag=buf[bp++]; uint16_t np=lePackU16(buf,bp);
            if (bp + (size_t)np*4 > sz) { Serial.println("[CONTRACT] REJECT: Kontur-Punkte truncated"); ok=false; break; }
            double flat=0,flon=0,llat=0,llon=0;
            for (int i=0;i<np;i++){ int16_t da=lePackI16(buf,bp), dn=lePackI16(buf,bp);
                double la=dlat0+da/1e5, lo=dlon0+dn/1e5; if(i==0){flat=la;flon=lo;} llat=la;llon=lo; }
            Serial.printf("  CONTOUR h=%d flag=%d pts=%d first=%.5f,%.5f last=%.5f,%.5f\n", h, flag, np, flat, flon, llat, llon);
        }
        if (!ok) break;
        if (bp+2 > sz) { Serial.println("[CONTRACT] REJECT: n_peaks truncated"); ok=false; break; }
        uint16_t npk=lePackU16(buf,bp);
        Serial.printf("  n_peaks=%d\n", npk);
        for (int k=0;k<npk && ok;k++){
            if (bp+8 > sz) { Serial.println("[CONTRACT] REJECT: Peak truncated"); ok=false; break; }
            int16_t da=lePackI16(buf,bp), dn=lePackI16(buf,bp), ele=lePackI16(buf,bp);
            uint8_t rank=buf[bp++]; uint8_t nlen=buf[bp++];
            if (bp+nlen > sz) { Serial.println("[CONTRACT] REJECT: Peak-Name truncated"); ok=false; break; }
            char nm[40]={0}; int rd=(nlen>39)?39:nlen; memcpy(nm,buf+bp,rd); bp+=nlen;
            Serial.printf("  PEAK %.5f,%.5f ele=%d rank=%d name=%s\n", dlat0+da/1e5, dlon0+dn/1e5, ele, rank, nm);
        }
        if (ok) {
            if (bp + 4 > sz) { Serial.println("[CONTRACT] REJECT: n_airspace/n_obstacles truncated"); ok=false; }
            else {
                uint16_t n_air = lePackU16(buf, bp);
                uint16_t n_obs = lePackU16(buf, bp);
                Serial.printf("  n_airspace=%d  n_obstacles=%d\n", n_air, n_obs);
                // Stufe 4: bei n_air/n_obs > 0 hier die Bloecke dekodieren (Block-Format TBD).
            }
        }
    }
    if (ok) Serial.println("===== CONTRACT OK (Cross-Read komplett) =====");
    heap_caps_free(buf);
}
