#pragma once
// imu_bno085.h — BNO085 (CEVA/Hillcrest SH-2) Treiber fuer die IMU-Abstraktion (imu.h),
// RAW ESP-IDF I2C (driver/i2c.h, I2C_NUM_0) — wie BNO055/BMP581/SHT4x, BEWUSST KEIN Wire
// (Wire.begin() kollidiert mit dem schon installierten Raw-Treiber, siehe imu_bno055.h).
//
// Anders als der BNO055 (simple Register) spricht der BNO085 das SHTP/SH-2-Protokoll:
//   - jede I2C-Transaktion beginnt mit einem 4-Byte-SHTP-Header [len_lsb, len_msb, channel, seq]
//     (len inkl. Header; Bit15 von len_msb = "continuation" und wird maskiert)
//   - Sensor-Reports werden per "Set Feature Command" (Control-Channel 2) scharfgeschaltet
//   - Daten kommen als Input-Reports (Channel 3): Timebase-Referenz + ein/mehrere Sensor-Reports
//   - jede neue I2C-Lese-Transaktion liefert das Paket AB DEM HEADER (wie SparkFun-Lib):
//     darum Header lesen -> Laenge -> ganzes Paket lesen -> Payload ab Offset 4.
//
// Wir aktivieren drei Reports und bauen daraus exakt dieselben Groessen wie der BNO055:
//   - Rotation Vector (0x05): Quaternion (Lage, absolutes Heading via Mag) -> Heading + Erd-Rotation
//   - Linear Acceleration (0x04): Beschl. OHNE g (Koerperframe) -> per Quaternion ins Erdframe -> accel_up
//   - Accelerometer (0x01): Beschl. MIT g -> Betrag fuers G-Meter
//
// HW (sensor_board, generate_schematic.py): Adresse 0x4A (HSA0=GND), PS0=PS1=0 (I2C-Mode),
//   HINTN/RSTN NICHT auf ESP-GPIO gefuehrt -> wir POLLEN ueber I2C und resetten per
//   Soft-Reset (Executable-Channel 1). Kein INT noetig.
//
// AKTIVIERUNG: build_flags += -DUSE_IMU_BNO085   (keine externe Lib noetig).
//
// VOR-ORT BEIM EINBAU PRUEFEN (wie BNO055): accel_up im Stand ~0, beim Anheben positiv;
//   sonst kBno085UpSign = -1. Heading-Nordpunkt setzt der User per "NORDEN SETZEN" (NVS).
#include "imu.h"

#if defined(USE_IMU_BNO085)
#include "driver/i2c.h"
#include <math.h>

// Einbaulage: Vorzeichen accel_up (im Stand ~0, beim Anheben positiv -> sonst -1). Das Heading
// ist mount-unabhaengig (Drehung um die Welt-Hochachse relativ zur NORDEN-Lage, siehe imu.h),
// braucht also KEINE "Vorne"-Achse mehr; nur die Drehrichtung ggf. umkehren.
static const float kBno085UpSign  = 1.0f;
static const float kBno085HdgSign = 1.0f;   // +1 = im Uhrzeigersinn steigend; bei Bedarf -1 (vor Ort)

class ImuBno085 : public IMU {
public:
    bool begin() override {
        _addr = 0x4A;

        // I2C-CLOCK-STRETCHING: der BNO085 haelt SCL gedrueckt, waehrend er Pakete bereitstellt.
        // Default-Bus-Timeout (von epdiy gesetzt) kann zu kurz sein -> Lese-Fehler/Muelldaten.
        // Wir VERLAENGERN das Timeout auf I2C_NUM_0, aber NIE verkuerzen (geteilter Bus: ein zu
        // kurzer Wert wuerde BMP581/SHT4x/Touch/PMIC mitreissen). Skala ist core-abhaengig:
        // ESP32-S3 = Exponent (2^val APB-Takte), klassischer ESP32 = direkte Takte. Aktuellen
        // Wert lesen, Skala daran ablesen, nur erhoehen. Rueckgaben tolerant (no-op bei Range).
        int tout = 0;
        if (i2c_get_timeout(I2C_NUM_0, &tout) == ESP_OK) {
            int want = (tout <= 31) ? 22 : 0xFFFFF;   // S3: 2^22 ~ 52 ms; klassisch: Maximum
            if (want > tout && i2c_set_timeout(I2C_NUM_0, want) == ESP_OK)
                Serial.printf("[BNO085] I2C-Timeout %d -> %d (Clock-Stretch-Toleranz)\n", tout, want);
        }

        // Chip ueberhaupt da? Nach Power-On ACKt der BNO085 seine Adresse sofort; bis ~1.5s
        // proben (robust gegen Boot-Race / spaet hochkommende Sensor-Schiene).
        bool present = false;
        for (int t = 0; t < 15 && !present; t++) {
            uint8_t hdr[4];
            if (i2c_master_read_from_device(I2C_NUM_0, _addr, hdr, 4, pdMS_TO_TICKS(50)) == ESP_OK)
                present = true;
            else delay(100);
        }
        if (!present) { ok = false; Serial.println("[BNO085] nicht gefunden @0x4A (kein I2C-ACK)"); return false; }

        softReset();
        delay(200);            // Reboot
        drainPackets(60);      // Advertisement + Reset-Complete + Restpakete weglesen

        // Reports scharfschalten (Intervall in us): 50 Hz = 20000 us.
        setFeature(0x05, 20000);  delay(20);   // Rotation Vector (Lage/Heading)
        setFeature(0x04, 20000);  delay(20);   // Linear Acceleration (ohne g, Vario)
        setFeature(0x01, 20000);  delay(20);   // Accelerometer (mit g, G-Meter)
        drainPackets(10);      // erste Antworten/Reports abholen

        ok = true;
        Serial.println("[BNO085] gefunden @0x4A, SH-2: RV+LinAccel+Accel @50Hz aktiv");
        return true;
    }

    void update() override {
        if (!ok) return;
        // Mehrere Pakete pro Loop draenen (Reports kommen gebatcht auf Channel 3).
        for (int i = 0; i < 8; i++) if (!readPacket()) break;

        // accel_up: Linearbeschl. (Koerperframe) per Quaternion ins Erdframe drehen, Z-Komponente
        // = 3. Zeile der Rotationsmatrix * Vektor (identisch zur BNO055-Fusion).
        float w=_qw, x=_qx, y=_qy, z=_qz;
        float up = 2.0f*(x*z - w*y)*_lax + 2.0f*(y*z + w*x)*_lay + (1.0f - 2.0f*(x*x + y*y))*_laz;
        _s.accel_up      = kBno085UpSign * up;
        _s.accel_valid   = _haveQuat && _haveLin;

        // Tilt-stabiles Heading: Drehung um die Welt-Hochachse relativ zur NORDEN-Referenz
        // (_qcal). Mount-unabhaengig, pitch/roll-invariant -> KEIN Sprung beim Kippen.
        _s.heading       = imuHeadingDeg(w, x, y, z, _qcal, kBno085HdgSign);
        _s.heading_valid = _haveQuat;

        _s.accel_g       = sqrtf(_axg*_axg + _ayg*_ayg + _azg*_azg) / 9.80665f;   // g
        _s.g_valid       = _haveAcc;
    }

    ImuSample sample() override { return _s; }
    const char* name() const override { return "BNO085"; }

    void captureNorth() override {                 // "NORDEN SETZEN": aktuelle Lage = Referenz
        if (!_haveQuat) return;
        _qcal[0]=_qw; _qcal[1]=_qx; _qcal[2]=_qy; _qcal[3]=_qz;
        _s.heading = imuHeadingDeg(_qw,_qx,_qy,_qz,_qcal,kBno085HdgSign);   // sofort 0
        _s.heading_valid = true;
    }

    // BNO085 kalibriert dynamisch selbst; wir spiegeln die Rotation-Vector-Genauigkeit (0-3)
    // ins sys-Feld fuers Kalibrier-Menue. (DCD-Save/Restore koennen wir spaeter ergaenzen.)
    ImuCal cal() override { ImuCal c; c.sys = _rvAccuracy; c.mag = _rvAccuracy; return c; }

private:
    uint8_t   _addr = 0x4A;
    ImuSample _s;
    uint8_t   _seq[6] = {0};      // Sequenznummer je Channel (0..5), nur fuer ausgehende Pakete

    // letzte Rohwerte
    float _qw=1, _qx=0, _qy=0, _qz=0;            // Quaternion (Rotation Vector)
    float _lax=0, _lay=0, _laz=0;                // Linearbeschl. (ohne g) [m/s^2]
    float _axg=0, _ayg=0, _azg=0;                // Beschl. mit g [m/s^2]
    bool  _haveQuat=false, _haveLin=false, _haveAcc=false;
    uint8_t _rvAccuracy=0;

    static int16_t s16(const uint8_t* b, int i) { return (int16_t)(b[i] | (b[i+1] << 8)); }

    // --- SHTP-Helfer (raw I2C) ---
    bool writePacket(uint8_t channel, const uint8_t* data, uint8_t len) {
        uint8_t buf[4 + 32];
        uint16_t total = (uint16_t)len + 4;
        buf[0] = total & 0xFF;
        buf[1] = (total >> 8) & 0xFF;     // kein continuation-Bit bei unseren kurzen Paketen
        buf[2] = channel;
        buf[3] = _seq[channel & 7]++;
        for (int i = 0; i < len && i < 32; i++) buf[4+i] = data[i];
        return i2c_master_write_to_device(I2C_NUM_0, _addr, buf, total, pdMS_TO_TICKS(50)) == ESP_OK;
    }

    void softReset() {
        uint8_t r = 1;                   // Executable-Channel (1): 1 = Reset
        writePacket(1, &r, 1);
    }

    void setFeature(uint8_t reportId, uint32_t interval_us) {
        uint8_t cmd[17] = {0};
        cmd[0] = 0xFD;                   // Set Feature Command
        cmd[1] = reportId;
        // [2]=flags [3..4]=change-sensitivity [5..8]=report interval (us) [9..12]=batch [13..16]=sensor-spec
        cmd[5] = interval_us & 0xFF;
        cmd[6] = (interval_us >> 8) & 0xFF;
        cmd[7] = (interval_us >> 16) & 0xFF;
        cmd[8] = (interval_us >> 24) & 0xFF;
        writePacket(2, cmd, 17);         // Control-Channel (2)
    }

    void drainPackets(int maxN) { for (int i = 0; i < maxN; i++) if (!readPacket()) break; }

    // Liest EIN SHTP-Paket. true = es kam ein Paket (ggf. relevant geparst), false = nichts da.
    bool readPacket() {
        uint8_t hdr[4];
        if (i2c_master_read_from_device(I2C_NUM_0, _addr, hdr, 4, pdMS_TO_TICKS(30)) != ESP_OK) return false;
        uint16_t len = (uint16_t)hdr[0] | ((uint16_t)(hdr[1] & 0x7F) << 8);   // Bit15 = continuation
        if (len == 0) return false;      // kein Paket in der Queue
        if (len <= 4) return true;       // nur Header, kein Inhalt
        // Ganzes Paket lesen — neue Transaktion liefert den Header erneut, Payload ab Offset 4.
        static uint8_t pkt[256];
        uint16_t toRead = (len > sizeof(pkt)) ? sizeof(pkt) : len;
        if (i2c_master_read_from_device(I2C_NUM_0, _addr, pkt, toRead, pdMS_TO_TICKS(30)) != ESP_OK) return false;
        if (pkt[2] == 3) parseInputReports(pkt + 4, (int)toRead - 4);   // Channel 3 = Sensor-Daten
        return true;
    }

    // Channel-3-Payload: optional Timebase-Referenz (0xFB, 5 B), dann Sensor-Reports hintereinander.
    void parseInputReports(const uint8_t* p, int n) {
        if (n < 1) return;
        int i = (n >= 5 && p[0] == 0xFB) ? 5 : 0;   // Timebase-Referenz ueberspringen
        while (i < n) {
            uint8_t rid = p[i];
            if (rid == 0x05) {                 // Rotation Vector: 4-B-Kopf + i,j,k,real (Q14) + acc
                if (i + 14 > n) break;
                _rvAccuracy = p[i+2] & 0x03;
                _qx = s16(p, i+4)  / 16384.0f;
                _qy = s16(p, i+6)  / 16384.0f;
                _qz = s16(p, i+8)  / 16384.0f;
                _qw = s16(p, i+10) / 16384.0f;
                _haveQuat = true;
                i += 14;
            } else if (rid == 0x04) {          // Linear Acceleration: 4-B-Kopf + x,y,z (Q8)
                if (i + 10 > n) break;
                _lax = s16(p, i+4) / 256.0f;
                _lay = s16(p, i+6) / 256.0f;
                _laz = s16(p, i+8) / 256.0f;
                _haveLin = true;
                i += 10;
            } else if (rid == 0x01) {          // Accelerometer: 4-B-Kopf + x,y,z (Q8)
                if (i + 10 > n) break;
                _axg = s16(p, i+4) / 256.0f;
                _ayg = s16(p, i+6) / 256.0f;
                _azg = s16(p, i+8) / 256.0f;
                _haveAcc = true;
                i += 10;
            } else {
                break;   // unbekannte Report-ID -> Laenge unsicher -> Rest verwerfen (Recovery naechstes Paket)
            }
        }
    }
};
#endif // USE_IMU_BNO085
