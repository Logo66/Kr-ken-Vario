#pragma once
// imu_bno055.h — BNO055-Treiber fuer die IMU-Abstraktion (imu.h).
// Der BNO055 (Bosch 9-Achser) macht die Sensor-Fusion ONBOARD und liefert fertig:
//   - Quaternion (Lage) + Linearbeschleunigung (schwerkraftbereinigt, Koerperframe)
//   - Euler-Heading (auch im Stand) aus dem Magnetometer (NDOF-Modus)
// Wir drehen die Linearbeschleunigung per Quaternion ins Erdframe und nehmen die Hoch-Achse
// als Vertikalbeschleunigung fuers Vario.
//
// AKTIVIERUNG (wenn der Sensor verbaut ist) — sonst NICHTS hier noetig:
//   1) platformio.ini:  lib_deps   += adafruit/Adafruit BNO055
//   2) platformio.ini:  build_flags += -DUSE_IMU_BNO055
//   3) Sensor an I2C (0x28 default, 0x29 falls ADR high) — der Rest haengt schon an imu.h.
// Ohne -DUSE_IMU_BNO055 ist diese Datei ein No-op (kein Build-Einfluss, keine Lib noetig).
#include "imu.h"

#if defined(USE_IMU_BNO055)
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

// VOR-ORT BEIM EINBAU PRUEFEN:
//   accel_up muss im Stand ~0 sein und beim kurzen Anheben positiv ausschlagen.
//   Ist das Vorzeichen invertiert (Einbaulage/Achsen-Remap), kUpSign auf -1 setzen.
static const float kBnoUpSign = 1.0f;

class ImuBno055 : public IMU {
public:
    bool begin() override {
        if (!bno.begin(OPERATION_MODE_NDOF)) { ok = false; return false; }
        bno.setExtCrystalUse(true);   // BNO055-Breakout hat externen Quarz -> genauere Fusion
        ok = true;
        return true;
    }

    void update() override {
        if (!ok) return;
        imu::Quaternion q   = bno.getQuat();
        imu::Vector<3>  lin = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);  // m/s^2, Koerperframe, ohne g
        imu::Vector<3>  eul = bno.getVector(Adafruit_BNO055::VECTOR_EULER);        // x = Heading [Grad]
        imu::Vector<3>  acc = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);// m/s^2, inkl. g (fuer G-Meter)

        // Koerperframe-Linearbeschleunigung -> Erdframe (Rotation per Quaternion),
        // Hoch-(Z-)Komponente = 3. Zeile der Rotationsmatrix * Vektor.
        float w=q.w(), x=q.x(), y=q.y(), z=q.z();
        float ax=lin.x(), ay=lin.y(), az=lin.z();
        float up = 2.0f*(x*z - w*y)*ax + 2.0f*(y*z + w*x)*ay + (1.0f - 2.0f*(x*x + y*y))*az;

        _s.accel_up      = kBnoUpSign * up;
        _s.accel_valid   = true;
        _s.heading       = eul.x();
        _s.heading_valid = true;
        _s.accel_g       = sqrtf(acc.x()*acc.x() + acc.y()*acc.y() + acc.z()*acc.z()) / 9.80665f;
        _s.g_valid       = true;
    }

    ImuSample sample() override { return _s; }
    const char* name() const override { return "BNO055"; }

private:
    Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire);  // (sensorID, I2C-Adresse, Wire)
    ImuSample _s;
};
#endif // USE_IMU_BNO055
