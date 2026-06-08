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

    // TX Test: einfacher Type-1 Frame
    bool sendTracking(float lat, float lon, float alt, float climb,
                      float speed, float heading, uint8_t aircraft) {
        if (!ok) return false;

        uint8_t buf[14];
        buf[0] = 0x01;   // Type 1
        buf[1] = 0xFC;   // Manufacturer: experimental
        buf[2] = 0x01;   // ID low
        buf[3] = 0x00;   // ID high

        int32_t lat_enc = (int32_t)(lat * 93206.0f);
        int32_t lon_enc = (int32_t)(lon * 46603.0f);
        buf[4] = lat_enc & 0xFF;
        buf[5] = (lat_enc >> 8) & 0xFF;
        buf[6] = (lat_enc >> 16) & 0xFF;
        buf[7] = lon_enc & 0xFF;
        buf[8] = (lon_enc >> 8) & 0xFF;
        buf[9] = (lon_enc >> 16) & 0xFF;

        uint16_t alt_enc = (uint16_t)constrain(alt, 0, 4095);
        buf[10] = alt_enc & 0xFF;
        buf[11] = ((alt_enc >> 8) & 0x0F) | ((aircraft & 0x07) << 4) | (1 << 7);
        buf[12] = (uint8_t)constrain(speed * 2.0f, 0, 255);
        buf[13] = (uint8_t)constrain(climb * 10.0f + 128.0f, 0, 255);

        int state = radio.transmit(buf, 14);
        Serial.printf("[FANET] TX %s (state=%d)\n", state == RADIOLIB_ERR_NONE ? "OK" : "FAIL", state);
        radio.startReceive();
        return (state == RADIOLIB_ERR_NONE);
    }

private:
    SX1262 radio = new Module(BOARD_LORA_CS, BOARD_LORA_IRQ, BOARD_LORA_RST, BOARD_LORA_BUSY);
    static volatile bool _received;
    static void onReceive() { _received = true; }

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
