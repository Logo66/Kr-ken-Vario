#pragma once
// ble_manager.h — BLE NimBLE GATT Server
// Dienste: Vario-Daten live an Flight Buddy App
// Name: "Aura Vario" (konfigurierbar), kein Pairing noetig
#include <NimBLEDevice.h>

// Custom Service + Characteristics UUIDs
#define AURA_SERVICE_UUID    "4155524F-0001-0001-0001-000000000001"
#define AURA_VARIO_UUID      "4155524F-0001-0001-0001-000000000002"
#define AURA_GPS_UUID        "4155524F-0001-0001-0001-000000000003"
#define AURA_STATUS_UUID     "4155524F-0001-0001-0001-000000000004"

// Vario-Daten (20 Bytes, passt in 1 BLE Notification)
struct __attribute__((packed)) BleVarioData {
    float altitude;     // m MSL
    float vario;        // m/s
    float speed;        // km/h
    float heading;      // Grad
    uint8_t sats;
    uint8_t bat_pct;
    uint8_t flags;      // Bit0=GPS, Bit1=Flying, Bit2=FANET
    uint8_t reserved;
};

// GPS-Position (16 Bytes)
struct __attribute__((packed)) BleGpsData {
    double lat;
    double lon;
};

class BLEManager {
public:
    bool ok = false;
    bool connected = false;
    char device_name[32] = "Aura Vario";

    bool init(const char *name = "Aura Vario") {
        strncpy(device_name, name, 31);
        device_name[31] = 0;

        NimBLEDevice::init(device_name);
        NimBLEDevice::setPower(ESP_PWR_LVL_P6);  // +6 dBm
        NimBLEDevice::setMTU(64);

        // GATT Server
        NimBLEServer *server = NimBLEDevice::createServer();
        server->setCallbacks(new ServerCB(this));

        // Aura Vario Service
        NimBLEService *svc = server->createService(AURA_SERVICE_UUID);

        // Vario-Characteristic (Read + Notify)
        _varioChar = svc->createCharacteristic(
            AURA_VARIO_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
        );

        // GPS-Characteristic (Read + Notify)
        _gpsChar = svc->createCharacteristic(
            AURA_GPS_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
        );

        // Status-Characteristic (Read)
        _statusChar = svc->createCharacteristic(
            AURA_STATUS_UUID,
            NIMBLE_PROPERTY::READ
        );
        _statusChar->setValue("Aura Vario v0.3");

        svc->start();

        // Advertising
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(AURA_SERVICE_UUID);
        adv->setName(device_name);
        adv->setScanResponse(true);
        adv->start();

        ok = true;
        Serial.printf("[BLE] GATT Server '%s' gestartet\n", device_name);
        return true;
    }

    // Im Loop aufrufen — sendet Notifications wenn Client verbunden
    void update(float alt, float vario, float speed, float heading,
                double lat, double lon, int sats, int bat_pct,
                bool gps_fix, bool flying, bool fanet_ok) {
        if (!ok || !connected) return;

        BleVarioData vd;
        vd.altitude = alt;
        vd.vario = vario;
        vd.speed = speed;
        vd.heading = heading;
        vd.sats = sats;
        vd.bat_pct = bat_pct;
        vd.flags = (gps_fix?1:0) | (flying?2:0) | (fanet_ok?4:0);
        vd.reserved = 0;
        _varioChar->setValue((uint8_t*)&vd, sizeof(vd));
        _varioChar->notify();

        if (gps_fix && lat != 0) {
            BleGpsData gd;
            gd.lat = lat;
            gd.lon = lon;
            _gpsChar->setValue((uint8_t*)&gd, sizeof(gd));
            _gpsChar->notify();
        }
    }

    void stop() {
        if (!ok) return;
        NimBLEDevice::deinit(true);
        ok = false;
        connected = false;
        Serial.println("[BLE] Gestoppt");
    }

private:
    NimBLECharacteristic *_varioChar = nullptr;
    NimBLECharacteristic *_gpsChar = nullptr;
    NimBLECharacteristic *_statusChar = nullptr;

    class ServerCB : public NimBLEServerCallbacks {
    public:
        BLEManager *mgr;
        ServerCB(BLEManager *m) : mgr(m) {}
        void onConnect(NimBLEServer *s) override {
            mgr->connected = true;
            Serial.println("[BLE] Client verbunden");
            NimBLEDevice::getAdvertising()->start();
        }
        void onDisconnect(NimBLEServer *s) override {
            mgr->connected = false;
            Serial.println("[BLE] Client getrennt");
            NimBLEDevice::getAdvertising()->start();
        }
    };
};
