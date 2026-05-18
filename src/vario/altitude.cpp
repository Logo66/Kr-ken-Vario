#include "altitude.h"
#include <math.h>

// ISA constants
static constexpr float LAPSE_RATE = 0.0065f;   // K/m
static constexpr float R_GAS = 8.31447f;        // J/(mol·K)
static constexpr float G = 9.80665f;            // m/s²
static constexpr float M_AIR = 0.0289644f;      // kg/mol
static constexpr float EXPONENT = (R_GAS * LAPSE_RATE) / (G * M_AIR); // ≈ 0.190284

void Altitude::setQNH(float qnh_hpa) {
    _qnh_pa = qnh_hpa * 100.0f;
}

float Altitude::compute(float pressure_pa, float temp_c) const {
    // Temperature-corrected hypsometric formula
    // Uses actual temperature from SHT40 instead of ISA assumption
    // Error without correction: ~13m per 1000m per 10°C ISA deviation
    float t_kelvin = temp_c + 273.15f;
    float p_ratio = pressure_pa / _qnh_pa;
    return (t_kelvin / LAPSE_RATE) * (1.0f - powf(p_ratio, EXPONENT));
}

float Altitude::computeISA(float pressure_pa) const {
    // Standard ISA formula (assumes 15°C at sea level = 288.15 K)
    float p_ratio = pressure_pa / _qnh_pa;
    return 44330.0f * (1.0f - powf(p_ratio, 1.0f / 5.255f));
}
