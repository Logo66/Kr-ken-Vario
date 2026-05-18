#pragma once
#include <stdint.h>

// --- Vario State (output of KF4D, 10 Hz → BLE VARIO packet) ---
struct VarioState {
    float altitude;         // m (barometric, temp-corrected)
    float vario;            // m/s (vertical speed, from Kalman)
    float vario_integrated; // m/s (smoothed, longer time constant)
    float accel_vertical;   // m/s² (vertical acceleration estimate)
    float accel_bias;       // m/s² (estimated accelerometer bias)
    uint32_t pressure_pa;   // Pa (raw BMP581 #1)
    float temperature;      // °C (from SHT40, NOT BMP581)
    bool is_flying;
    bool is_thermal;
    bool gps_ok;
    uint32_t timestamp_ms;
};

// --- Thermal detection state ---
struct ThermalInfo {
    bool detected;          // true if currently in thermal
    float avg_climb;        // m/s (8s sliding window average)
    float turn_rate;        // °/s (absolute, from BNO085 gyro Z)
    uint8_t mode;           // 0=ground, 1=cruise, 2=thermal
    uint32_t mode_since_ms; // timestamp of last mode change
};

// --- Environment data (SHT40, 1 Hz) ---
struct EnvironmentData {
    float temperature;      // °C (SHT40)
    float humidity;         // %RH
    float dewpoint;         // °C
    float cloud_base;       // m AGL (estimated from spread)
    uint32_t timestamp_ms;
};

// --- Delta-P raw recording (BMP581 #2) ---
struct DeltaPState {
    float dp_raw;           // Pa (baro1 - baro2 - dc_offset)
    float dc_offset;        // Pa (slow moving average, tau=60s)
    float dp_rms_2s;        // Pa (RMS over last 2s, turbulence indicator)
    uint32_t pressure2_pa;  // Pa (raw BMP581 #2)
};

// --- GPS data (1 Hz → BLE GPS packet) ---
struct GpsData {
    double latitude;        // decimal degrees
    double longitude;       // decimal degrees
    int16_t altitude_msl;   // m
    float speed_kmh;        // km/h (ground speed)
    float heading_deg;      // degrees
    uint8_t satellites;
    bool fix_valid;
    uint32_t timestamp_ms;
};

// --- Power / Status (1 Hz → BLE STATUS packet) ---
struct PowerStatus {
    uint8_t battery_pct;    // 0-100%
    float battery_voltage;  // V
    uint8_t mode;           // 0=ground, 1=cruise, 2=thermal
    uint8_t fanet_status;   // 0=off, 1=rx, 2=tx
    uint16_t uptime_min;    // minutes since boot
};

// --- High-G event (LSM6DSO32 watchdog) ---
struct HighGEvent {
    float accel_magnitude;  // m/s² (total)
    float accel_x, accel_y, accel_z; // m/s² (body frame)
    uint32_t timestamp_ms;
};
