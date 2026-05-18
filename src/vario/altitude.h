#pragma once

class Altitude {
public:
    void setQNH(float qnh_hpa);
    float getQNH() const { return _qnh_pa; }

    // Barometric altitude with temperature correction (SHT40 temp)
    float compute(float pressure_pa, float temp_c) const;

    // Uncorrected (ISA assumption, 15°C sea level)
    float computeISA(float pressure_pa) const;

private:
    float _qnh_pa = 101325.0f; // Pa (default = standard atmosphere)
};
