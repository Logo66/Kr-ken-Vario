#pragma once
// imu_bno055.h — BNO055-Treiber fuer die IMU-Abstraktion (imu.h), RAW ESP-IDF I2C.
//
// Bewusst NICHT Adafruit/Wire: dieses Board schaltet nach dem Boot von Wire auf das rohe
// ESP-IDF-I2C um (driver/i2c.h, I2C_NUM_0, sda=39/scl=40) — genau wie BMP581 und LSM6.
// Adafruits internes Wire.begin() kollidiert dann mit dem schon installierten Raw-Treiber
// ("i2c_driver_install error" / "NULL TX buffer pointer"). Mit rohem I2C teilen wir uns
// denselben Bus konfliktfrei.
//
// Der BNO055 macht die 9-Achs-Fusion ONBOARD: Quaternion (Lage) + Linearbeschleunigung
// (schwerkraftbereinigt) + Euler-Heading. Wir drehen die Linearbeschleunigung per Quaternion
// ins Erdframe und nehmen die Hoch-Achse als Vertikalbeschleunigung fuers Vario.
//
// AKTIVIERUNG: build_flags += -DUSE_IMU_BNO055   (keine externe Lib noetig).
#include "imu.h"

#if defined(USE_IMU_BNO055)
#include "driver/i2c.h"

// VOR-ORT BEIM EINBAU PRUEFEN:
//   accel_up muss im Stand ~0 sein und beim kurzen Anheben positiv ausschlagen.
//   Ist das Vorzeichen invertiert (Einbaulage), kUpSign auf -1 setzen.
//   Heading ggf. spaeter per Achsen-Remap (Reg 0x41/0x42) an die Einbaulage anpassen.
static const float kBnoUpSign = 1.0f;

class ImuBno055 : public IMU {
public:
    bool begin() override {
        if      (probe(0x28)) _addr = 0x28;
        else if (probe(0x29)) _addr = 0x29;
        else { ok = false; Serial.println("[BNO055] nicht gefunden (CHIP_ID != 0xA0 @0x28/0x29)"); return false; }

        wr(0x3D, 0x00); delay(25);     // OPR_MODE = CONFIG
        wr(0x3F, 0x20); delay(700);    // SYS_TRIGGER = RST_SYS (Reset) -> ~650ms Reboot
        wr(0x3E, 0x00); delay(10);     // PWR_MODE = NORMAL
        wr(0x07, 0x00);                // PAGE_ID = 0
        wr(0x3F, 0x80); delay(10);     // SYS_TRIGGER = CLK_SEL -> externer Quarz (Modul hat einen)
        wr(0x3D, 0x0C); delay(25);     // OPR_MODE = NDOF (9-Achs-Fusion mit absolutem Heading)

        ok = true;
        Serial.printf("[BNO055] gefunden @0x%02X, NDOF + ext. Quarz aktiv\n", _addr);
        return true;
    }

    void update() override {
        if (!ok) return;
        uint8_t q[8], la[6], eu[2], ac[6];
        if (!rd(0x20, q, 8)) return;   // QUA_DATA w,x,y,z (LSB 1/16384)
        rd(0x28, la, 6);               // LIA_DATA x,y,z  (Linearbeschl., ohne g, LSB 1/100 m/s^2)
        rd(0x1A, eu, 2);               // EUL_Heading     (LSB 1/16 Grad)
        rd(0x08, ac, 6);               // ACC_DATA x,y,z  (mit g, LSB 1/100 m/s^2) fuers G-Meter

        float w=s16(q,0)/16384.0f, x=s16(q,2)/16384.0f, y=s16(q,4)/16384.0f, z=s16(q,6)/16384.0f;
        float ax=s16(la,0)/100.0f, ay=s16(la,2)/100.0f, az=s16(la,4)/100.0f;   // m/s^2, Koerperframe
        // Koerperframe -> Erdframe (Rotation per Quaternion), Hoch-(Z-)Komponente
        // = 3. Zeile der Rotationsmatrix * Vektor.
        float up = 2.0f*(x*z - w*y)*ax + 2.0f*(y*z + w*x)*ay + (1.0f - 2.0f*(x*x + y*y))*az;

        _s.accel_up      = kBnoUpSign * up;
        _s.accel_valid   = true;
        _s.heading       = s16(eu,0)/16.0f;    // Grad [0..360)
        _s.heading_valid = true;
        float gx=s16(ac,0)/100.0f, gy=s16(ac,2)/100.0f, gz=s16(ac,4)/100.0f;
        _s.accel_g       = sqrtf(gx*gx + gy*gy + gz*gz) / 9.80665f;             // g
        _s.g_valid       = true;
    }

    ImuSample sample() override { return _s; }
    const char* name() const override { return "BNO055"; }

private:
    uint8_t   _addr = 0x28;
    ImuSample _s;

    static int16_t s16(const uint8_t* b, int i) { return (int16_t)(b[i] | (b[i+1] << 8)); }
    bool wr(uint8_t reg, uint8_t val) {
        uint8_t b[2] = { reg, val };
        return i2c_master_write_to_device(I2C_NUM_0, _addr, b, 2, pdMS_TO_TICKS(50)) == ESP_OK;
    }
    bool rd(uint8_t reg, uint8_t* buf, size_t n) {
        return i2c_master_write_read_device(I2C_NUM_0, _addr, &reg, 1, buf, n, pdMS_TO_TICKS(50)) == ESP_OK;
    }
    bool probe(uint8_t a) {
        uint8_t id = 0, reg = 0x00;   // CHIP_ID muss 0xA0 sein
        return i2c_master_write_read_device(I2C_NUM_0, a, &reg, 1, &id, 1, pdMS_TO_TICKS(50)) == ESP_OK && id == 0xA0;
    }
};
#endif // USE_IMU_BNO055
