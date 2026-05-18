#include <Arduino.h>
#include <Wire.h>
#include "pins.h"
#include "version.h"

#include <Adafruit_BMP5xx.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_SHT4x.h>
#include "PowersBQ25896.tpp"

// --- Sensor objects ---
Adafruit_BMP5xx bmp1;
Adafruit_BMP5xx bmp2;
Adafruit_LSM6DSO32 lsm;
Adafruit_BNO08x bno;
Adafruit_SHT4x sht;
sh2_SensorValue_t bnoValue;

// --- Power management ---
PowersBQ25896 ppm;

// --- State ---
bool bmp1_ok = false;
bool bmp2_ok = false;
bool lsm_ok  = false;
bool bno_ok  = false;
bool sht_ok  = false;
bool ppm_ok  = false;

unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL_MS = 1000;

// --- I2C scan (triggered by boot button) ---
void i2cScan() {
    Serial.println("\n=== I2C Scan ===");
    struct { uint8_t addr; const char* name; } expected[] = {
        {ADDR_PCA9535,          "PCA9535"},
        {ADDR_SHT40,           "SHT40"},
        {ADDR_BMP581_SECONDARY, "BMP581#2"},
        {ADDR_BMP581_PRIMARY,   "BMP581#1"},
        {ADDR_BNO085,          "BNO085"},
        {ADDR_PCF85063,        "PCF85063"},
        {ADDR_BQ27220,         "BQ27220"},
        {ADDR_GT911,           "GT911"},
        {ADDR_TPS65185,        "TPS65185"},
        {ADDR_LSM6DSO32,       "LSM6DSO32"},
        {ADDR_BQ25896,         "BQ25896"},
    };

    int found = 0;
    for (auto& dev : expected) {
        Wire.beginTransmission(dev.addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X %-10s  [FOUND]\n", dev.addr, dev.name);
            found++;
        } else {
            Serial.printf("  0x%02X %-10s  [MISSING]\n", dev.addr, dev.name);
        }
    }
    Serial.printf("=== %d / %d devices found ===\n\n", found, (int)(sizeof(expected)/sizeof(expected[0])));
}

// --- BNO085: enable game rotation vector report ---
void bnoEnableReports() {
    if (!bno.enableReport(SH2_GAME_ROTATION_VECTOR, 50000)) {
        Serial.println("[WARN] BNO085: could not enable Game Rotation Vector");
    }
    if (!bno.enableReport(SH2_ACCELEROMETER, 50000)) {
        Serial.println("[WARN] BNO085: could not enable Accelerometer");
    }
}

// --- Setup ---
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.printf("  AURA Kruecke v%s\n", AURA_VERSION);
    Serial.println("  T5 E-Paper S3 Pro · Bring-up");
    Serial.println("========================================\n");

    // Boot button as input
    pinMode(BOARD_BOOT_BTN, INPUT_PULLUP);

    // I2C init
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, I2C_FREQ_HZ);
    Serial.printf("[I2C] SDA=%d SCL=%d @ %d Hz\n\n", BOARD_I2C_SDA, BOARD_I2C_SCL, I2C_FREQ_HZ);

    // --- Power Management: BQ25896 ---
    ppm_ok = ppm.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, ADDR_BQ25896);
    if (ppm_ok) {
        Serial.printf("[OK]   BQ25896 Charger   @ 0x%02X\n", ADDR_BQ25896);
        Serial.printf("       Battery Voltage: %.2f V\n", ppm.getBattVoltage() / 1000.0);
        Serial.printf("       Charge Status:   %s\n", ppm.getChargeStatusString());
    } else {
        Serial.printf("[FAIL] BQ25896 Charger   @ 0x%02X\n", ADDR_BQ25896);
    }

    // --- BQ27220 Fuel Gauge (simple I2C probe) ---
    Wire.beginTransmission(ADDR_BQ27220);
    if (Wire.endTransmission() == 0) {
        Serial.printf("[OK]   BQ27220 FuelGauge @ 0x%02X\n", ADDR_BQ27220);
    } else {
        Serial.printf("[FAIL] BQ27220 FuelGauge @ 0x%02X\n", ADDR_BQ27220);
    }

    // --- PCF85063 RTC (simple I2C probe) ---
    Wire.beginTransmission(ADDR_PCF85063);
    if (Wire.endTransmission() == 0) {
        Serial.printf("[OK]   PCF85063 RTC      @ 0x%02X\n", ADDR_PCF85063);
    } else {
        Serial.printf("[FAIL] PCF85063 RTC      @ 0x%02X\n", ADDR_PCF85063);
    }

    Serial.println();

    // --- SHT40 (einfachster Sensor, erste Sanity) ---
    sht_ok = sht.begin(&Wire);
    if (sht_ok) {
        sht.setPrecision(SHT4X_HIGH_PRECISION);
        Serial.printf("[OK]   SHT40             @ 0x%02X\n", ADDR_SHT40);
    } else {
        Serial.printf("[FAIL] SHT40             @ 0x%02X\n", ADDR_SHT40);
    }

    // --- BMP581 #1 (Primary, 0x47) ---
    delay(100);
    bmp1_ok = bmp1.begin(ADDR_BMP581_PRIMARY, &Wire);
    if (!bmp1_ok) {
        delay(100);
        bmp1_ok = bmp1.begin(ADDR_BMP581_PRIMARY, &Wire);
    }
    if (bmp1_ok) {
        bmp1.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp1.setPressureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp1.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.printf("[OK]   BMP581 #1         @ 0x%02X\n", ADDR_BMP581_PRIMARY);
    } else {
        Serial.printf("[FAIL] BMP581 #1         @ 0x%02X  (retry exhausted)\n", ADDR_BMP581_PRIMARY);
    }

    // --- BMP581 #2 (Secondary, 0x46 — ADR-Pin auf GND) ---
    delay(100);
    bmp2_ok = bmp2.begin(ADDR_BMP581_SECONDARY, &Wire);
    if (!bmp2_ok) {
        delay(100);
        bmp2_ok = bmp2.begin(ADDR_BMP581_SECONDARY, &Wire);
    }
    if (bmp2_ok) {
        bmp2.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp2.setPressureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp2.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.printf("[OK]   BMP581 #2         @ 0x%02X\n", ADDR_BMP581_SECONDARY);
    } else {
        Serial.printf("[FAIL] BMP581 #2         @ 0x%02X  (ADR pin on GND?)\n", ADDR_BMP581_SECONDARY);
    }

    // --- LSM6DSO32 ---
    lsm_ok = lsm.begin_I2C(ADDR_LSM6DSO32, &Wire);
    if (lsm_ok) {
        lsm.setAccelRange(LSM6DSO32_ACCEL_RANGE_16_G);
        lsm.setGyroRange(LSM6DS_GYRO_RANGE_1000_DPS);
        lsm.setAccelDataRate(LSM6DS_RATE_104_HZ);
        lsm.setGyroDataRate(LSM6DS_RATE_104_HZ);
        Serial.printf("[OK]   LSM6DSO32         @ 0x%02X\n", ADDR_LSM6DSO32);
    } else {
        Serial.printf("[FAIL] LSM6DSO32         @ 0x%02X\n", ADDR_LSM6DSO32);
    }

    // --- BNO085 (SHTP, komplexester Sensor) ---
    bno_ok = bno.begin_I2C(ADDR_BNO085, &Wire);
    if (bno_ok) {
        bnoEnableReports();
        Serial.printf("[OK]   BNO085            @ 0x%02X\n", ADDR_BNO085);
    } else {
        Serial.printf("[FAIL] BNO085            @ 0x%02X  (RST pin high?)\n", ADDR_BNO085);
    }

    // --- E-Paper: Platzhalter (volle Display-Integration = AURA-KRUECKE-4) ---
    Serial.println("\n[INFO] E-Paper: AURA · Bring-up OK  (Serial-only, Display in Ticket 4)");

    // --- Boot summary ---
    int sensorCount = (int)bmp1_ok + bmp2_ok + lsm_ok + bno_ok + sht_ok;
    Serial.println("\n========================================");
    Serial.printf("  Bring-up: %d/5 Sensoren OK\n", sensorCount);
    Serial.println("  Boot-Button (GPIO 0) = I2C Scan");
    Serial.println("========================================\n");
}

// --- Loop ---
void loop() {
    // Boot button → I2C scan
    if (digitalRead(BOARD_BOOT_BTN) == LOW) {
        i2cScan();
        delay(500);
        while (digitalRead(BOARD_BOOT_BTN) == LOW) { delay(10); }
    }

    // 1 Hz sensor readout
    if (millis() - lastPrint < PRINT_INTERVAL_MS) return;
    lastPrint = millis();

    Serial.printf("--- t=%lu s ---\n", millis() / 1000);

    // BMP581 #1
    if (bmp1_ok) {
        if (bmp1.performReading()) {
            Serial.printf("  BMP581#1  P=%.2f hPa  T=%.2f C\n",
                          bmp1.pressure / 100.0, bmp1.temperature);
        } else {
            Serial.println("  BMP581#1  [read error]");
        }
    }

    // BMP581 #2
    if (bmp2_ok) {
        if (bmp2.performReading()) {
            Serial.printf("  BMP581#2  P=%.2f hPa  T=%.2f C\n",
                          bmp2.pressure / 100.0, bmp2.temperature);
        } else {
            Serial.println("  BMP581#2  [read error]");
        }
    }

    // LSM6DSO32
    if (lsm_ok) {
        sensors_event_t accel, gyro, temp;
        lsm.getEvent(&accel, &gyro, &temp);
        Serial.printf("  LSM6DSO32 Ax=%.2f Ay=%.2f Az=%.2f m/s2  Gx=%.1f Gy=%.1f Gz=%.1f dps\n",
                      accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
                      gyro.gyro.x * RAD_TO_DEG, gyro.gyro.y * RAD_TO_DEG, gyro.gyro.z * RAD_TO_DEG);
    }

    // BNO085
    if (bno_ok) {
        if (bno.getSensorEvent(&bnoValue)) {
            if (bnoValue.sensorId == SH2_GAME_ROTATION_VECTOR) {
                Serial.printf("  BNO085    Qi=%.3f Qj=%.3f Qk=%.3f Qr=%.3f\n",
                              bnoValue.un.gameRotationVector.i,
                              bnoValue.un.gameRotationVector.j,
                              bnoValue.un.gameRotationVector.k,
                              bnoValue.un.gameRotationVector.real);
            } else if (bnoValue.sensorId == SH2_ACCELEROMETER) {
                Serial.printf("  BNO085    Ax=%.2f Ay=%.2f Az=%.2f m/s2\n",
                              bnoValue.un.accelerometer.x,
                              bnoValue.un.accelerometer.y,
                              bnoValue.un.accelerometer.z);
            }
        }
    }

    // SHT40
    if (sht_ok) {
        sensors_event_t humidity, temp;
        sht.getEvent(&humidity, &temp);
        Serial.printf("  SHT40     T=%.2f C  RH=%.1f %%\n", temp.temperature, humidity.relative_humidity);
    }

    // Power status
    if (ppm_ok) {
        Serial.printf("  POWER     Vbat=%.2f V  Charge=%s\n",
                      ppm.getBattVoltage() / 1000.0, ppm.getChargeStatusString());
    }

    Serial.println();
}
