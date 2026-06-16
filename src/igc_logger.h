#pragma once
// igc_logger.h — echtes IGC-Flug-Logging auf SD nach /igc/<datum>_<zeit>.igc
// B-Records ~alle 2s, jeder sofort geschrieben (power-loss-sicher).
// Sammelt zugleich die Statistik fuer den Flugbuch-Eintrag (Max-Hoehe/Climb, Spur/Luftlinie).
#include <Arduino.h>
#include <math.h>
#include "sd_manager.h"
#include <TinyGPSPlus.h>

class IgcLogger {
public:
    bool active = false;
    char path[48] = {0};

    // Statistik (fuer FlightRecord)
    float  max_alt = 0, start_alt = 0, max_climb = 0;
    double track_dist_m = 0, straight_dist_m = 0;
    double start_lat = 0, start_lon = 0, last_lat = 0, last_lon = 0;
    bool   have_last = false;
    uint32_t pointCount = 0;

    void start(SDManager *sd, TinyGPSPlus &gps, float baroAlt,
               const char *pilot, const char *glider) {
        if (!sd || !sd->ok) return;
        int yy=26, mo=1, dd=1, hh=0, mi=0;
        if (gps.date.isValid()) { yy=gps.date.year()%100; mo=gps.date.month(); dd=gps.date.day(); }
        if (gps.time.isValid()) { hh=gps.time.hour(); mi=gps.time.minute(); }
        snprintf(path, sizeof(path), "/igc/20%02d-%02d-%02d_%02d%02d.igc", yy, mo, dd, hh, mi);

        File f = sd->openWrite(path);   // FILE_WRITE -> Datei neu anlegen
        if (!f) { Serial.printf("[IGC] open FAIL %s\n", path); active=false; return; }
        f.println("AXXXAUR Aura Kruecke");                        // A-Record (experimentell)
        f.printf("HFDTEDATE:%02d%02d%02d,01\r\n", dd, mo, yy);    // Flugdatum
        f.printf("HFPLTPILOTINCHARGE:%s\r\n", pilot);
        f.printf("HFGTYGLIDERTYPE:%s\r\n", glider);
        f.println("HFDTMGPSDATUM:WGS84");
        f.printf("HFRFWFIRMWAREVERSION:%s\r\n", AURA_VERSION);
        f.println("HFFTYFRTYPE:KIE Engineering,Aura Kruecke");
        f.println("I013638GFO");   // B-Record-Extension: Bytes 36-38 = G-Kraft (0.1g), Code GFO
        f.close();

        active = true; max_alt = baroAlt; start_alt = baroAlt; max_climb = 0;
        track_dist_m = 0; straight_dist_m = 0; have_last = false; pointCount = 0; _lastLog = 0;
        start_lat = start_lon = 0;
        if (gps.location.isValid()) { start_lat = gps.location.lat(); start_lon = gps.location.lng(); }
        Serial.printf("[IGC] START %s\n", path);
    }

    // jeden Loop aufrufen waehrend FLIGHT_FLYING (intern auf ~2s gedrosselt)
    void logPoint(SDManager *sd, TinyGPSPlus &gps, float baroAlt, float vario, float &gSegMax) {
        if (!active || !sd || !sd->ok) return;
        if (_lastLog != 0 && millis() - _lastLog < 2000) return;
        _lastLog = millis();

        if (baroAlt > max_alt) max_alt = baroAlt;
        if (vario   > max_climb) max_climb = vario;
        if (gps.location.isValid()) {
            double la = gps.location.lat(), lo = gps.location.lng();
            if (have_last) {
                double d = TinyGPSPlus::distanceBetween(last_lat, last_lon, la, lo);
                if (d < 2000) track_dist_m += d;          // Ausreisser/Sprung ignorieren
            }
            if (start_lat != 0 || start_lon != 0)
                straight_dist_m = TinyGPSPlus::distanceBetween(start_lat, start_lon, la, lo);
            last_lat = la; last_lon = lo; have_last = true;
        }

        File f = sd->openAppend(path);
        if (!f) return;
        char b[64]; formatB(b, sizeof(b), gps, baroAlt, gSegMax);
        f.println(b);
        f.close();
        pointCount++;
        gSegMax = 0;   // G-Fenster fuer den naechsten Punkt zuruecksetzen
    }

    void end() {
        active = false;
        Serial.printf("[IGC] ENDE %s — %lu Punkte, Spur %.1f km, Luftlinie %.1f km\n",
                       path, pointCount, track_dist_m/1000.0, straight_dist_m/1000.0);
    }

private:
    unsigned long _lastLog = 0;

    void formatB(char *out, int n, TinyGPSPlus &gps, float baroAlt, float gforce) {
        int hh=0, mi=0, ss=0;
        if (gps.time.isValid()) { hh=gps.time.hour(); mi=gps.time.minute(); ss=gps.time.second(); }
        double lat=0, lon=0; char fix='V';
        if (gps.location.isValid()) { lat=gps.location.lat(); lon=gps.location.lng(); fix='A'; }
        char ns = (lat>=0)?'N':'S'; char ew = (lon>=0)?'E':'W';
        lat=fabs(lat); lon=fabs(lon);
        int latd=(int)lat, latm=(int)((lat-latd)*60000.0);     // MMmmm = Minuten*1000
        int lond=(int)lon, lonm=(int)((lon-lond)*60000.0);
        int palt=(int)baroAlt; if(palt<0)palt=0; if(palt>99999)palt=99999;
        int galt = gps.altitude.isValid() ? (int)gps.altitude.meters() : 0;
        if(galt<0)galt=0; if(galt>99999)galt=99999;
        int gff=(int)(gforce*10.0f); if(gff<0)gff=0; if(gff>999)gff=999;   // G in 0.1g -> Bytes 36-38 (I-Record GFO)
        // B HHMMSS DDMMmmmN DDDMMmmmE A PPPPP GGGGG GGG(=G*10)
        snprintf(out, n, "B%02d%02d%02d%02d%05d%c%03d%05d%c%c%05d%05d%03d",
                 hh, mi, ss, latd, latm, ns, lond, lonm, ew, fix, palt, galt, gff);
    }
};
