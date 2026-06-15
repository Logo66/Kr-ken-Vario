#pragma once
// imu.h — IMU-Abstraktion: die EINZIGE Quelle fuer welt-bezogene Vertikalbeschleunigung
// + Heading (und Gesamt-G fuers G-Meter). Vario, Heading und G-Meter haengen NUR an diesem
// Interface, nicht an einem konkreten Chip. Sensor-Wechsel (LSM6 -> BNO055 -> BNO085) ist
// damit nur ein neuer Treiber; die Fusions-Logik + das Daempfungs-Tuning bleiben unberuehrt.
//
//   Heute aktiv: ImuNull (kein brauchbarer Sensor — LSM6 ist tot) -> alle *_valid = false
//     -> Vario bleibt reines Baro, Heading bleibt GPS-Kurs. EXAKT das heutige Verhalten.
//   Naechste Woche: ImuBno055 (imu_bno055.h) liefert accel_up + heading + accel_g echt.
//   Spaeter: ImuBno085 — einfach ein weiterer Treiber hinter diesem Interface.
#include <Arduino.h>

struct ImuSample {
    float accel_up      = 0;      // welt-bezogene Vertikalbeschleunigung [m/s^2], schwerkraftbereinigt, + = aufwaerts
    bool  accel_valid   = false;  // accel_up brauchbar? (braucht Lage-Fusion -> BNO055; LSM6/Stub: false)
    float heading       = 0;      // fused/magnetisches Heading [0..360 Grad) — auch im Stand gueltig
    bool  heading_valid = false;  // heading brauchbar? (Magnetometer -> BNO055; LSM6/Stub: false)
    float accel_g       = 0;      // Gesamtbeschleunigung [g] (Betrag, inkl. g) fuers G-Meter
    bool  g_valid       = false;  // accel_g brauchbar?
};

class IMU {
public:
    bool ok = false;                          // echter Sensor vorhanden + initialisiert
    virtual ~IMU() {}
    virtual bool begin() = 0;                 // Sensor hochfahren; true = da
    virtual void update() = 0;                // im Loop aufrufen; aktualisiert das letzte Sample
    virtual ImuSample sample() = 0;           // letztes Sample (Kopie)
    virtual const char* name() const = 0;     // Treibername (Log)
};

// Null-Treiber = heutiger Stand (kein brauchbarer IMU). Liefert nichts Gueltiges,
// damit faellt alles sauber auf die bestehenden Quellen zurueck (Baro / GPS-Kurs).
class ImuNull : public IMU {
public:
    bool begin() override { ok = false; return false; }
    void update() override {}
    ImuSample sample() override { return ImuSample{}; }
    const char* name() const override { return "none"; }
};
