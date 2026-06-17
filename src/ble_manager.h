#pragma once
// ble_manager.h — BLE NimBLE GATT Server
// Dienste: Vario-Daten live an Flight Buddy App
// Name: "Aura Vario" (konfigurierbar), kein Pairing noetig
#include <NimBLEDevice.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Custom Service + Characteristics UUIDs
#define AURA_SERVICE_UUID    "4155524F-0001-0001-0001-000000000001"
#define AURA_VARIO_UUID      "4155524F-0001-0001-0001-000000000002"
#define AURA_GPS_UUID        "4155524F-0001-0001-0001-000000000003"
#define AURA_STATUS_UUID     "4155524F-0001-0001-0001-000000000004"
#define AURA_ENV_UUID        "4155524F-0001-0001-0001-000000000005"
#define AURA_CFG_UUID        "4155524F-0001-0001-0001-000000000006"  // M2/M3: Write(verschluesselt)+Notify

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

// Umwelt + Wind (24 Bytes) — alle float32 LE
struct __attribute__((packed)) BleEnvData {
    float temp;        // Grad C
    float humidity;    // % rel. Feuchte
    float dewpoint;    // Grad C Taupunkt
    float base_est;    // m Wolkenbasis-Schaetzung (beim Kurbeln)
    float wind_speed;  // km/h
    float wind_dir;    // Grad, woher der Wind kommt (0 bis erster Kreis geschaetzt)
};

// Eine eingehende BLE-Schreibnachricht (ein Write = ein Settings-KV oder ein Task-Chunk).
struct BleCfgMsg { uint16_t len; uint8_t data[244]; };

class BLEManager {
public:
    bool ok = false;
    bool connected = false;
    char device_name[32] = "Aura Vario";

    uint32_t pin = 1234;  // Default PIN, aenderbar

    bool init(const char *name = "Aura Vario") {
        // BLE-Controller + NimBLE-Host brauchen einen grossen ZUSAMMENHAENGENDEN internen Heap-Block.
        // Bei zu wenig (z.B. WLAN frisst internen RAM) paniced NimBLEDevice::init() -> Reboot statt Fehler.
        // Darum vorher pruefen: lieber sauber "AUS" lassen als das Geraet neu starten.
        size_t freeHeap = ESP.getFreeHeap();
        size_t largest  = ESP.getMaxAllocHeap();
        Serial.printf("[BLE] init-Check: heap frei=%u  groesster Block=%u\n", (unsigned)freeHeap, (unsigned)largest);
        if (largest < 42000) {
            Serial.println("[BLE] ABBRUCH: zu wenig zusammenhaengender Heap fuer BLE (KEIN Crash). Tipp: WLAN aus, dann BLE.");
            ok = false;
            return false;
        }
        strncpy(device_name, name, 31);
        device_name[31] = 0;
        if (!_cfgQ) _cfgQ = xQueueCreate(16, sizeof(BleCfgMsg)); else xQueueReset(_cfgQ);

        NimBLEDevice::init(device_name);
        NimBLEDevice::setPower(ESP_PWR_LVL_P6);
        NimBLEDevice::setMTU(247);   // groesseres MTU fuer Task-Chunks (M3); Lese-Chars unberuehrt

        // Sicherheit: PIN-Pairing erforderlich
        NimBLEDevice::setSecurityAuth(true, true, true);  // Bond, MITM, SC
        NimBLEDevice::setSecurityPasskey(pin);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
        Serial.printf("[BLE] PIN: %06lu\n", pin);

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

        // Umwelt + Wind Characteristic (Read + Notify)
        _envChar = svc->createCharacteristic(
            AURA_ENV_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
        );

        // Konfig/Task-Write-Characteristic (M2/M3): OFFEN (WRITE + NOTIFY) — kein Encryption-Gate.
        // Konsequent mit den Lese-Chars (die auch offen sind). Verschluesselung+Bond = Phase-2-Haertung,
        // dann konsequent fuer ALLE Chars. Vorher: WRITE_ENC wies den Write ohne Pairing ab ("ausstehend").
        _cfgChar = svc->createCharacteristic(
            AURA_CFG_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
        );
        _cfgChar->setCallbacks(new CfgCB(this));

        svc->start();

        // Advertising
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(AURA_SERVICE_UUID);
        adv->setName(device_name);
        adv->setScanResponse(true);
        adv->start();

        ok = true;
        Serial.printf("[BLE] GATT Server '%s' gestartet (internHeap nachher=%u)\n",
                      device_name, (unsigned)ESP.getFreeHeap());
        return true;
    }

    // Vario-Char — schnell (~10 Hz): Hoehe/Vario reagieren fluessig in der App.
    void update(float alt, float vario, float speed, float heading,
                int sats, int bat_pct, bool gps_fix, bool flying, bool fanet_ok) {
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
    }

    // GPS-Char — 1 Hz (GPS-Modul liefert nur 1 Hz, schneller waere reine Wiederholung).
    void updateGps(double lat, double lon, bool gps_fix) {
        if (!ok || !connected) return;
        if (!gps_fix || lat == 0) return;
        BleGpsData gd;
        gd.lat = lat;
        gd.lon = lon;
        _gpsChar->setValue((uint8_t*)&gd, sizeof(gd));
        _gpsChar->notify();
    }

    // Umwelt + Wind senden (1x pro Sekunde, parallel zu update())
    void updateEnv(float temp, float humidity, float dewpoint, float base_est,
                   float wind_speed, float wind_dir) {
        if (!ok || !connected) return;
        BleEnvData ed;
        ed.temp = temp; ed.humidity = humidity; ed.dewpoint = dewpoint;
        ed.base_est = base_est; ed.wind_speed = wind_speed; ed.wind_dir = wind_dir;
        _envChar->setValue((uint8_t*)&ed, sizeof(ed));
        _envChar->notify();
    }

    // === M2/M3: BLE-Schreibweg (Konfig + Task) ===
    // Eingehende Writes liegen in der Queue; main.cpp leert sie im Loop (kein SD/JSON im BLE-Callback).
    bool cfgPending() { return _cfgQ && uxQueueMessagesWaiting(_cfgQ) > 0; }
    int  takeCfgIn(uint8_t *out, size_t max) {        // -1 = nichts da, sonst Laenge
        if (!_cfgQ) return -1;
        BleCfgMsg m;
        if (xQueueReceive(_cfgQ, &m, 0) != pdTRUE) return -1;
        size_t n = (m.len < max) ? m.len : max;
        memcpy(out, m.data, n);
        return (int)n;
    }
    void notifyCfg(const char *json) {                // Echo/Ack auf demselben Char …0006
        if (!_cfgChar) return;
        _cfgChar->setValue((const uint8_t*)json, strlen(json));
        _cfgChar->notify();
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
    NimBLECharacteristic *_envChar = nullptr;
    NimBLECharacteristic *_cfgChar = nullptr;
    QueueHandle_t _cfgQ = nullptr;

    class ServerCB : public NimBLEServerCallbacks {
    public:
        BLEManager *mgr;
        ServerCB(BLEManager *m) : mgr(m) {}
        void onConnect(NimBLEServer *s) override {
            mgr->connected = true;
            Serial.println("[BLE] Client verbunden");
            // KEIN Advertising-Neustart hier: stoerte das Pairing-/Bonding-Fenster
            // ("Advertising already active"). Nach Disconnect laeuft es eh wieder an.
        }
        void onDisconnect(NimBLEServer *s) override {
            mgr->connected = false;
            Serial.println("[BLE] Client getrennt");
            NimBLEDevice::getAdvertising()->start();
        }
        void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
            Serial.printf("[BLE] Auth fertig: bonded=%d enc=%d auth=%d\n",
                          desc->sec_state.bonded, desc->sec_state.encrypted, desc->sec_state.authenticated);
        }
    };

    // Write-Callback fuer …0006: kopiert jeden Write in die Queue (leichtgewichtig, kein SD/JSON hier).
    class CfgCB : public NimBLECharacteristicCallbacks {
    public:
        BLEManager *mgr;
        CfgCB(BLEManager *m) : mgr(m) {}
        void onWrite(NimBLECharacteristic *c) override {
            NimBLEAttValue v = c->getValue();
            BleCfgMsg m;
            m.len = v.length() > 244 ? 244 : v.length();
            memcpy(m.data, v.data(), m.len);
            if (mgr->_cfgQ) xQueueSend(mgr->_cfgQ, &m, 0);
            Serial.printf("[CFG] BLE-Write %u Byte -> Queue\n", (unsigned)m.len);
        }
    };
};
