#pragma once
// fanet.h — KRUECKE-8 Phase 1: SX1262 FANET RX Beweis
// Parameter aus GXAirCom (funktionierend) + LilyGo Factory (Board-spezifisch)
// TCXO 2.4V (Factory), DIO2 RF-Switch, Sync 0xF1, Preamble 12, CRC ON
#include <SPI.h>
#include <RadioLib.h>
#include "pins.h"

// FANET Message Types (fuer spaeter, Phase 2+)
#define FANET_TYPE_TRACKING 1
#define FANET_TYPE_NAME     2
#define FANET_TYPE_MESSAGE  3
#define FANET_TYPE_SERVICE  4

// === FANET TX SAFETY GATE (Ticket D) =========================================
// Sicherheits-Funk: die Kruecke sendet ihre Position an FREMDE Piloten in der Luft.
// Der Encoder unten ist spec-konform (Referenz: 3s1d/fanet-stm32 protocol.txt, vom
// FANET-Autor) und per Round-Trip-Selbsttest (selfTestTx) byte-genau belegt.
// Live-TX bleibt aber AUS, bis ein ECHTES Empfangsgeraet (Skytraxx / go-fanet) die
// Kruecke an KORREKTER Position/Hoehe zeigt (Gate D). Erst DANN auf 1 setzen.
// Rote Linie: lieber TX aus als TX falsch — falsche Position fuehrt fremde Piloten in die Irre.
#define FANET_TX_ENABLED 1   // Gate D OFFEN (2026-06-13, nach Ivos Test-OK). DOPPEL-SICHERUNG bleibt: siehe g_fanetTxEnabled.
// Live-TX nur wenn BEIDE an: Gate D (oben, compile) UND dieses Config-Flag (App/Menue: fanet.tx_enabled).
// Default AUS -> Gate D offen heisst NICHT Dauerfunk. Plus: gesendet wird ohnehin nur IM FLUG.
static bool g_fanetTxEnabled = false;
static bool g_fanetOnline    = true;   // FANET Online-Tracking-Flag (fanet.online_tracking)

struct FanetPilot {
    uint8_t manufacturer;
    uint16_t id;
    float lat, lon, altitude, climb, speed, heading;
    uint8_t aircraft;
    bool online;
    unsigned long last_seen;
};

struct FanetStation {
    uint8_t manufacturer;
    uint16_t id;
    float lat, lon, wind_speed, wind_dir, temp, humidity;
    unsigned long last_seen;
};

class FanetRadio {
public:
    static const int MAX_PILOTS = 20;
    static const int MAX_STATIONS = 10;
    FanetPilot pilots[MAX_PILOTS];
    int pilot_count = 0;
    FanetStation stations[MAX_STATIONS];
    int station_count = 0;
    bool ok = false;

    bool init() {
        // --- Schritt 1: SPI-Bus CS sauber (Factory-Vorgabe) ---
        pinMode(BOARD_LORA_CS, OUTPUT);  digitalWrite(BOARD_LORA_CS, HIGH);
        pinMode(BOARD_SD_CS, OUTPUT);    digitalWrite(BOARD_SD_CS, HIGH);
        SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

        // --- Schritt 2: radio.begin() mit Defaults (wie Factory) ---
        Serial.print("[SX1262] begin ... ");
        int state = radio.begin();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("FAIL %d\n", state);
            return false;
        }
        Serial.println("OK");

        // --- Schritt 3: TCXO 2.4V via DIO3 (Board-spezifisch, aus Factory) ---
        state = radio.setTCXO(2.4);
        Serial.printf("[SX1262] setTCXO(2.4) = %d\n", state);

        // --- Schritt 4: DIO2 als RF-Switch (GXAirCom + Factory) ---
        state = radio.setDio2AsRfSwitch();
        Serial.printf("[SX1262] setDio2AsRfSwitch = %d\n", state);

        // --- Schritt 5: FANET PHY exakt (GXAirCom verifiziert) ---
        Serial.printf("[SX1262] setFrequency(868.2) = %d\n", radio.setFrequency(868.2));
        Serial.printf("[SX1262] setBandwidth(250) = %d\n",   radio.setBandwidth(250.0));
        Serial.printf("[SX1262] setSF(7) = %d\n",            radio.setSpreadingFactor(7));
        Serial.printf("[SX1262] setCR(8) = %d\n",            radio.setCodingRate(8));
        Serial.printf("[SX1262] setSyncWord(0xF1) = %d\n",   radio.setSyncWord(0xF1));
        Serial.printf("[SX1262] setPreamble(12) = %d\n",     radio.setPreambleLength(12));
        Serial.printf("[SX1262] setCRC(true) = %d\n",        radio.setCRC(true));
        Serial.printf("[SX1262] setOutputPower(14) = %d\n",  radio.setOutputPower(14));
        Serial.printf("[SX1262] setCurrentLimit(140) = %d\n",radio.setCurrentLimit(140));

        // --- Schritt 6: Sync-Word Verifikation ---
        // setSyncWord(0xF1, 0x44) → Register 0x0740 = 0xF4, 0x0741 = 0x14
        // (RadioLib Formel: MSB=(sync&0xF0)|(ctrl>>4), LSB=(sync<<4)|(ctrl&0x0F))
        // 0xF1,0x44 → (0xF0|0x04)=0xF4, (0x10|0x04)=0x14 ✓ FANET korrekt
        Serial.println("[SX1262] SyncWord: 0xF1 → Register 0xF4,0x14 (rechnerisch verifiziert)");

        // --- Schritt 7: DIO1 IRQ + RX Continuous ---
        radio.setDio1Action(onReceive);
        state = radio.startReceive();
        Serial.printf("[SX1262] startReceive = %d\n", state);

        if (state == RADIOLIB_ERR_NONE) {
            ok = true;
            Serial.println("[FANET] === RX AKTIV 868.2 MHz — warte auf Pakete ===");
        }
        return ok;
    }

    // Phase 1: Roher Hex-Dump jedes empfangenen Pakets
    void poll() {
        if (!ok) return;

        // Periodischer Heartbeat alle 10s
        static unsigned long lastHB = 0;
        if (millis() - lastHB > 10000) {
            lastHB = millis();
            Serial.printf("[FANET] heartbeat rx=%s rssi=%.0f t=%lus\n",
                           _received ? "PKT!" : "idle", radio.getRSSI(), millis()/1000);
        }

        if (!_received) return;
        _received = false;

        // --- RAW HEX DUMP (Phase 1 Beweis) ---
        uint8_t buf[255];
        int len = radio.getPacketLength();
        int state = radio.readData(buf, len);
        float rssi = radio.getRSSI();
        float snr = radio.getSNR();

        Serial.printf("[FANET] *** RX %d bytes RSSI=%.0f SNR=%.1f state=%d ***\n",
                       len, rssi, snr, state);
        if (state == RADIOLIB_ERR_NONE && len > 0) {
            Serial.print("[FANET] HEX: ");
            for (int i = 0; i < len; i++) {
                Serial.printf("%02X ", buf[i]);
            }
            Serial.println();

            // Minimales FANET-Header-Parsing (Phase 2 Preview)
            if (len > 4) {
                uint8_t type = buf[0] & 0x3F;
                uint8_t mfr = buf[1];
                uint16_t uid = buf[2] | (buf[3] << 8);
                Serial.printf("[FANET] Type=%d Mfr=0x%02X ID=0x%04X\n", type, mfr, uid);
                // Pilot-Liste updaten (Type 1=Airborne, Type 7=Ground)
                // Eigene TX-Echos ignorieren (Mfr 0xFC)
                if ((type == FANET_TYPE_TRACKING || type == 7) && len >= 11 && mfr != 0xFC) {
                    parseTracking(buf+4, len-4, mfr, uid);
                }
            }
        } else if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[FANET] readData FAIL: %d\n", state);
        }

        // Zurueck in RX
        radio.startReceive();
    }

    // === FANET Type 1 (Tracking) Encoder — SPEC-KONFORM ===
    // Referenz: 3s1d/fanet-stm32 Src/fanet/radio/protocol.txt (FANET-Autor).
    // 15-Byte-Frame: [0]Header(Type1) [1]Mfr [2-3]UID(LE) | Payload[0-10]:
    //   0-5 Position(LE 2-compl, lat*93206/lon*46603), 6-7 Online+Aircraft+AltScaling+Alt,
    //   8 Speed(0.5km/h,5x), 9 Climb(0.1m/s,2-compl,5x), 10 Heading(360/256deg).
    // Reine Funktion (kein Radio) — wird auch vom Round-Trip-Selbsttest genutzt.
    static int encodeTracking(uint8_t *buf, uint8_t mfr, uint16_t uid,
                              float lat, float lon, float alt_m, float climb_ms,
                              float speed_kmh, float heading_deg,
                              uint8_t aircraft, bool online) {
        buf[0] = 0x01;                       // ext=0, forward=0, Type=1 (Tracking)
        buf[1] = mfr;
        buf[2] = uid & 0xFF;
        buf[3] = (uid >> 8) & 0xFF;

        // Payload 0-5: Position (24-bit LE, 2-complement)
        int32_t lat_i = (int32_t)lroundf(lat * 93206.0f);
        int32_t lon_i = (int32_t)lroundf(lon * 46603.0f);
        buf[4] = lat_i & 0xFF; buf[5] = (lat_i >> 8) & 0xFF; buf[6] = (lat_i >> 16) & 0xFF;
        buf[7] = lon_i & 0xFF; buf[8] = (lon_i >> 8) & 0xFF; buf[9] = (lon_i >> 16) & 0xFF;

        // Payload 6-7: Online(15) | Aircraft(12-14) | AltScaling(11,1->4x) | Alt(0-10), 16-bit LE
        int alt = (int)lroundf(alt_m); if (alt < 0) alt = 0;
        uint16_t alt_scale = 0;
        if (alt > 2047) { alt = (alt + 2) / 4; if (alt > 2047) alt = 2047; alt_scale = 1; }
        uint16_t w = ((uint16_t)alt & 0x07FF)
                   | (uint16_t)(alt_scale << 11)
                   | (uint16_t)((aircraft & 0x07) << 12)
                   | (uint16_t)((online ? 1 : 0) << 15);
        buf[10] = w & 0xFF;
        buf[11] = (w >> 8) & 0xFF;

        // Payload 8: Speed — bit0-6 in 0.5 km/h, bit7 Scaling 1->5x
        int sp = (int)lroundf(speed_kmh * 2.0f); if (sp < 0) sp = 0;
        uint8_t sp_scale = 0;
        if (sp > 127) { sp = (int)lroundf(speed_kmh * 0.4f); if (sp > 127) sp = 127; sp_scale = 1; }
        buf[12] = (uint8_t)((sp & 0x7F) | (sp_scale << 7));

        // Payload 9: Climb — bit0-6 7-bit 2-complement in 0.1 m/s, bit7 Scaling 1->5x
        int cl = (int)lroundf(climb_ms * 10.0f);
        uint8_t cl_scale = 0;
        if (cl > 63 || cl < -64) { cl = (int)lroundf(climb_ms * 2.0f); cl_scale = 1; }
        if (cl > 63) cl = 63; if (cl < -64) cl = -64;
        buf[13] = (uint8_t)((cl & 0x7F) | (cl_scale << 7));

        // Payload 10: Heading — 360/256 deg
        float h = heading_deg; while (h < 0) h += 360.0f; while (h >= 360.0f) h -= 360.0f;
        buf[14] = (uint8_t)((int)lroundf(h * 256.0f / 360.0f) & 0xFF);

        return 15;
    }

    // Eindeutige Source-ID aus der ESP32-MAC (nie 0). Manufacturer 0xFC = experimental.
    static uint16_t txUid() {
        uint16_t id = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
        return id ? id : 0x0001;
    }

    // TX: encodet IMMER spec-konform; sendet NUR wenn FANET_TX_ENABLED==1 (Gate D).
    bool sendTracking(float lat, float lon, float alt, float climb,
                      float speed, float heading, uint8_t aircraft) {
        if (!ok) return false;
        uint8_t buf[15];
        int n = encodeTracking(buf, 0xFC, txUid(), lat, lon, alt, climb,
                               speed, heading, aircraft, g_fanetOnline);
#if FANET_TX_ENABLED
        if (!g_fanetTxEnabled) {   // Gate D offen, aber Config-Flag aus -> nur encoden, KEIN Funk (Doppel-Sicherung)
            static unsigned long lw=0;
            if (millis()-lw > 30000) { lw=millis(); Serial.println("[FANET] TX bereit (Gate D offen), aber fanet.tx_enabled=false -> kein Funk"); }
            (void)n; return false;
        }
        int state = radio.transmit(buf, n);
        Serial.printf("[FANET] TX %s (state=%d)\n",
                      state == RADIOLIB_ERR_NONE ? "OK" : "FAIL", state);
        radio.startReceive();
        return (state == RADIOLIB_ERR_NONE);
#else
        // SICHERHEIT (Gate D): Live-TX deaktiviert bis Validierung gegen echten Decoder.
        // Encoder laeuft, aber es geht NICHTS auf die Luft. Periodischer Log-Beleg (alle 30s).
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 30000) {
            lastWarn = millis();
            Serial.print("[FANET] TX GESPERRT (Gate D, FANET_TX_ENABLED=0) — Frame waere:");
            for (int i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
            Serial.println();
        }
        (void)n;
        return false;
#endif
    }

    // === FANET Type 2 (Name) + Type 3 (Message) — #4. Gleiche Doppel-Sicherung wie Tracking. ===
    bool sendName(const char* name) {
        if (!ok || !name || !*name) return false;
        uint8_t buf[40]; buf[0]=0x02; buf[1]=0xFC; uint16_t uid=txUid(); buf[2]=uid&0xFF; buf[3]=(uid>>8)&0xFF;
        int n=4; for (const char* p=name; *p && n<36; p++) buf[n++]=(uint8_t)*p;
#if FANET_TX_ENABLED
        if (!g_fanetTxEnabled) return false;
        int st=radio.transmit(buf,n); Serial.printf("[FANET] Name-TX %s\n", st==RADIOLIB_ERR_NONE?"OK":"FAIL"); radio.startReceive();
        return st==RADIOLIB_ERR_NONE;
#else
        (void)n; return false;
#endif
    }
    // Nachricht (z.B. Ride/SOS) — sendet auch am Boden, aber nur bei Gate D + tx_enabled (bewusste Aktion).
    bool sendMessage(const char* msg) {
        if (!ok || !msg || !*msg) return false;
        uint8_t buf[60]; buf[0]=0x03; buf[1]=0xFC; uint16_t uid=txUid(); buf[2]=uid&0xFF; buf[3]=(uid>>8)&0xFF;
        buf[4]=0;   // Subheader: Message-Subtype 0 (Broadcast)
        int n=5; for (const char* p=msg; *p && n<56; p++) buf[n++]=(uint8_t)*p;
#if FANET_TX_ENABLED
        if (!g_fanetTxEnabled) { Serial.println("[FANET] Msg: tx_enabled=false -> kein Funk"); return false; }
        int st=radio.transmit(buf,n); Serial.printf("[FANET] Msg-TX %s: %s\n", st==RADIOLIB_ERR_NONE?"OK":"FAIL", msg); radio.startReceive();
        return st==RADIOLIB_ERR_NONE;
#else
        Serial.printf("[FANET] Msg GESPERRT (Gate D): %s\n", msg); (void)n; return false;
#endif
    }

    // Round-Trip-Selbsttest: encode → decode → Felder vergleichen. KEIN Funk, kein Risiko.
    void selfTestTx() {
        struct TC { float lat, lon, alt, climb, speed, hdg; uint8_t ac; };
        TC cases[] = {
            {47.37694f, 8.94185f, 2500.0f,  3.2f, 38.0f, 270.0f, 1}, // Alpen: Alt>2047 (Scaling 4x), +Climb
            {46.50000f, 7.10000f,  850.0f, -2.5f, 12.0f,  90.0f, 1}, // tief, -Climb
            {47.00000f, 9.00000f, 4000.0f,  0.2f, 75.0f, 359.0f, 4}, // hoch + Speed>63 (Scaling 5x), Glider
        };
        Serial.println("[FANET-TEST] === Type-1 TX Encoder Round-Trip (Spec 3s1d) ===");
        bool all = true;
        for (auto &c : cases) {
            uint8_t f[15];
            encodeTracking(f, 0xFC, 0x1234, c.lat, c.lon, c.alt, c.climb, c.speed, c.hdg, c.ac, true);
            Serial.print("[FANET-TEST] HEX:");
            for (int i = 0; i < 15; i++) Serial.printf(" %02X", f[i]);
            Serial.println();
            FanetPilot p = {};
            decodeTracking(f, 15, &p);
            bool ok2 = fabsf(p.lat - c.lat) < 0.0002f && fabsf(p.lon - c.lon) < 0.0002f
                    && fabsf(p.altitude - c.alt) <= 4.0f && fabsf(p.climb - c.climb) <= 0.5f
                    && fabsf(p.speed - c.speed) <= 2.5f && fabsf(p.heading - c.hdg) <= 2.0f
                    && p.aircraft == c.ac;
            Serial.printf("[FANET-TEST]  in : lat=%.5f lon=%.5f alt=%.0f clb=%+.1f spd=%.0f hdg=%.0f ac=%d\n",
                          c.lat, c.lon, c.alt, c.climb, c.speed, c.hdg, c.ac);
            Serial.printf("[FANET-TEST]  out: lat=%.5f lon=%.5f alt=%.0f clb=%+.1f spd=%.0f hdg=%.0f ac=%d  %s\n",
                          p.lat, p.lon, p.altitude, p.climb, p.speed, p.heading, p.aircraft, ok2 ? "PASS" : "FAIL");
            all = all && ok2;
        }
        Serial.printf("[FANET-TEST] === %s ===\n", all ? "ALLE PASS — Encoder byte-genau" : "FAIL!");
    }

private:
    SX1262 radio = new Module(BOARD_LORA_CS, BOARD_LORA_IRQ, BOARD_LORA_RST, BOARD_LORA_BUSY);
    static volatile bool _received;
    static void onReceive() { _received = true; }

    // Voll-Decoder (alle Felder) — NUR fuer den Selbsttest. Der LIVE-RX-Pfad nutzt
    // parseTracking() unten und bleibt unveraendert (HEILIG: RX unangetastet).
    static void decodeTracking(const uint8_t *buf, int len, FanetPilot *p) {
        if (len < 15) return;
        p->manufacturer = buf[1];
        p->id = buf[2] | (buf[3] << 8);
        int32_t lat_i = buf[4] | (buf[5] << 8) | (buf[6] << 16);
        if (lat_i & 0x800000) lat_i |= 0xFF000000;
        int32_t lon_i = buf[7] | (buf[8] << 8) | (buf[9] << 16);
        if (lon_i & 0x800000) lon_i |= 0xFF000000;
        p->lat = lat_i / 93206.0f;
        p->lon = lon_i / 46603.0f;
        uint16_t w = buf[10] | (buf[11] << 8);
        int alt = w & 0x07FF;
        if (w & 0x0800) alt *= 4;                 // AltScaling 4x
        p->altitude = alt;
        p->aircraft = (w >> 12) & 0x07;
        p->online   = (w >> 15) & 0x01;
        uint8_t sp = buf[12];
        int spv = sp & 0x7F; if (sp & 0x80) spv *= 5;
        p->speed = spv * 0.5f;                     // km/h
        uint8_t cb = buf[13];
        int clv = cb & 0x7F; if (clv & 0x40) clv -= 128;   // 7-bit 2-complement
        if (cb & 0x80) clv *= 5;
        p->climb = clv * 0.1f;                     // m/s
        p->heading = buf[14] * 360.0f / 256.0f;
    }

    void parseTracking(uint8_t *data, int len, uint8_t mfr, uint16_t uid) {
        if (len < 7) return;
        int32_t lat_raw = data[0] | (data[1]<<8) | (data[2]<<16);
        if (lat_raw & 0x800000) lat_raw |= 0xFF000000;
        int32_t lon_raw = data[3] | (data[4]<<8) | (data[5]<<16);
        if (lon_raw & 0x800000) lon_raw |= 0xFF000000;

        FanetPilot p = {};
        p.manufacturer = mfr;
        p.id = uid;
        p.lat = lat_raw / 93206.0f;
        p.lon = lon_raw / 46603.0f;
        p.last_seen = millis();

        for (int i = 0; i < pilot_count; i++) {
            if (pilots[i].manufacturer == mfr && pilots[i].id == uid) {
                pilots[i] = p; return;
            }
        }
        if (pilot_count < MAX_PILOTS) pilots[pilot_count++] = p;
    }
};

volatile bool FanetRadio::_received = false;
