#pragma once
// ble_manager.h — BLE NimBLE GATT Server Vorbereitung
// Dienste: Vario-Daten, GPS-Position, Flight-Status fuer Flight Buddy App
// TODO: NimBLE Library einbinden und GATT Services implementieren
#include <Arduino.h>

// BLE Service UUIDs (Custom fuer Aura Vario)
#define AURA_BLE_SERVICE_UUID     "4155524F-5641-5249-4F00-000000000001"
#define AURA_BLE_VARIO_CHAR_UUID  "4155524F-5641-5249-4F00-000000000002"
#define AURA_BLE_GPS_CHAR_UUID    "4155524F-5641-5249-4F00-000000000003"
#define AURA_BLE_STATUS_CHAR_UUID "4155524F-5641-5249-4F00-000000000004"

// Vario-Daten Paket (20 Bytes, passt in eine BLE Notification)
struct __attribute__((packed)) AuraVarioPacket {
    float altitude;     // m MSL
    float vario;        // m/s
    float speed;        // km/h
    float heading;      // Grad
    uint16_t sats;      // GPS Sats
    uint8_t bat_pct;    // Batterie %
    uint8_t flags;      // Bit 0=GPS fix, Bit 1=Flying, Bit 2=FANET
};

class BLEManager {
public:
    bool ok = false;
    bool connected = false;

    bool init() {
        // TODO: NimBLE init
        // NimBLEDevice::init("Aura Vario");
        // GATT Server + Service + Characteristics erstellen
        // Advertising starten
        Serial.println("[BLE] Vorbereitet (noch nicht aktiv)");
        return false;  // Noch nicht implementiert
    }

    void update(float alt, float vario, float speed, float heading,
                int sats, int bat_pct, bool gps_fix, bool flying, bool fanet) {
        if (!ok) return;
        AuraVarioPacket pkt;
        pkt.altitude = alt;
        pkt.vario = vario;
        pkt.speed = speed;
        pkt.heading = heading;
        pkt.sats = sats;
        pkt.bat_pct = bat_pct;
        pkt.flags = (gps_fix ? 1 : 0) | (flying ? 2 : 0) | (fanet ? 4 : 0);
        // TODO: Notification senden
        // varioCharacteristic->setValue((uint8_t*)&pkt, sizeof(pkt));
        // varioCharacteristic->notify();
    }

    void stop() {
        if (!ok) return;
        // TODO: NimBLEDevice::deinit();
        ok = false;
    }
};
