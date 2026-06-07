// AURA Kruecke v0.2.0 — Phase 2: Sensoren + Cruise mit Live-Daten
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"
#include "version.h"
#include <Adafruit_BMP5xx.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_SHT4x.h>
#include "PowersBQ25896.tpp"
#include "epdiy.h"
#include "epd_highlevel.h"
#include "boot_splash.h"
#include "cruise_screen.h"
#include "vario/altitude.h"

// --- Objekte ---
static EpdiyHighlevelState hl;
static Adafruit_BMP5xx bmp_a, bmp_b;
static Adafruit_LSM6DSO32 lsm;
static Adafruit_SHT4x sht;
static PowersBQ25896 ppm;
static Altitude alt_calc;

// --- Zustand ---
static bool bmpA_ok=false, bmpB_ok=false, lsm_ok=false, sht_ok=false, ppm_ok=false;
static CruiseData live = {};
static unsigned long lastPrint = 0;

// --- I2C-Scan ---
static void i2cScan() {
    Serial.println("\n=== I2C Scan ===");
    struct { uint8_t addr; const char* name; bool aura; } devs[] = {
        {0x20,"PCA9535",false}, {0x44,"SHT40",true},
        {0x46,"BMP581#B",true}, {0x47,"BMP581#A",true},
        {0x51,"PCF85063",false}, {0x55,"BQ27220",false},
        {0x5D,"GT911",false}, {0x68,"TPS65185",false},
        {0x6A,"LSM6DSO32",true}, {0x6B,"BQ25896",false},
    };
    int found=0, aura=0;
    for (auto& d : devs) {
        Wire.beginTransmission(d.addr);
        bool ok = (Wire.endTransmission() == 0);
        if (ok) found++;
        if (ok && d.aura) aura++;
        Serial.printf("  0x%02X %-10s %s %s\n", d.addr, d.name,
                       ok ? "[OK]" : "[--]", d.aura ? "*" : "");
    }
    Serial.printf("=== Aura: %d/4  Total: %d/10 ===\n\n", aura, found);
}

// --- Sensor-Init ---
static void initSensors() {
    // BQ25896
    ppm_ok = ppm.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, ADDR_BQ25896);
    if (ppm_ok) {
        live.bat_pct = 87; // TODO: echten SoC aus BQ27220
        live.bat_hours = 25;
        Serial.printf("[OK] BQ25896 Vbat=%.2fV %s\n",
                       ppm.getBattVoltage()/1000.0f, ppm.getChargeStatusString());
    }

    // SHT40
    sht_ok = sht.begin(&Wire);
    if (sht_ok) { sht.setPrecision(SHT4X_HIGH_PRECISION); Serial.println("[OK] SHT40"); }
    else Serial.println("[--] SHT40");

    // BMP581 #A (0x47)
    delay(50);
    bmpA_ok = bmp_a.begin(ADDR_BMP581_PRIMARY, &Wire);
    if (!bmpA_ok) { delay(100); bmpA_ok = bmp_a.begin(ADDR_BMP581_PRIMARY, &Wire); }
    if (bmpA_ok) {
        bmp_a.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp_a.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
        bmp_a.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.println("[OK] BMP581#A");
    } else Serial.println("[--] BMP581#A");

    // BMP581 #B (0x46)
    delay(50);
    bmpB_ok = bmp_b.begin(ADDR_BMP581_SECONDARY, &Wire);
    if (!bmpB_ok) { delay(100); bmpB_ok = bmp_b.begin(ADDR_BMP581_SECONDARY, &Wire); }
    if (bmpB_ok) {
        bmp_b.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp_b.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
        bmp_b.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.println("[OK] BMP581#B");
    } else Serial.println("[--] BMP581#B");

    // LSM6DSO32
    lsm_ok = lsm.begin_I2C(ADDR_LSM6DSO32, &Wire);
    if (lsm_ok) {
        lsm.setAccelRange(LSM6DSO32_ACCEL_RANGE_16_G);
        lsm.setGyroRange(LSM6DS_GYRO_RANGE_1000_DPS);
        lsm.setAccelDataRate(LSM6DS_RATE_104_HZ);
        lsm.setGyroDataRate(LSM6DS_RATE_104_HZ);
        Serial.println("[OK] LSM6DSO32");
    } else Serial.println("[--] LSM6DSO32");

    int n = (int)bmpA_ok + bmpB_ok + lsm_ok + sht_ok;
    Serial.printf("Sensoren: %d/4\n", n);
}

// --- Sensor-Daten lesen und in CruiseData fuellen ---
static void readSensors() {
    // SHT40
    if (sht_ok) {
        sensors_event_t h, t;
        sht.getEvent(&h, &t);
        live.temp = t.temperature;
        live.dewpoint = t.temperature - (100.0f - h.relative_humidity) / 5.0f;
        Serial.printf("  SHT40 T=%.1fC RH=%.0f%% Dew=%.1fC\n",
                       t.temperature, h.relative_humidity, live.dewpoint);
    }

    // BMP581 #A → Hoehe + Vario
    static float last_alt = 0;
    static unsigned long last_alt_time = 0;

    if (bmpA_ok && bmp_a.performReading()) {
        // BUG-KRUECKE-001 FIX: Adafruit BMP5xx gibt hPa zurueck,
        // Altitude-Formel erwartet Pa → mal 100
        float pressure_hpa = bmp_a.pressure;
        float pressure_pa = pressure_hpa * 100.0f;
        float temp = bmp_a.temperature;

        float alt = alt_calc.computeISA(pressure_pa);
        live.altitude = alt;
        live.qnh = 1013.25f; // Default, spaeter kalibrierbar

        // Einfacher Vario: Δh/Δt
        unsigned long now = millis();
        if (last_alt_time > 0 && (now - last_alt_time) > 100) {
            float dt = (now - last_alt_time) / 1000.0f;
            live.vario = (alt - last_alt) / dt;
        }
        last_alt = alt;
        last_alt_time = now;

        Serial.printf("  BMP#A P=%.2f hPa T=%.1fC Alt=%.1fm Vario=%+.2fm/s\n",
                       pressure_hpa, temp, alt, live.vario);
    }

    // BMP581 #B → Δp
    if (bmpB_ok && bmp_b.performReading()) {
        if (bmpA_ok) {
            float dp = bmp_a.pressure - bmp_b.pressure;
            Serial.printf("  BMP#B P=%.2f  Dp=%.2f Pa\n", bmp_b.pressure, dp);
        }
    }

    // LSM6DSO32
    if (lsm_ok) {
        sensors_event_t a, g, t;
        lsm.getEvent(&a, &g, &t);
        float mag = sqrtf(a.acceleration.x*a.acceleration.x +
                          a.acceleration.y*a.acceleration.y +
                          a.acceleration.z*a.acceleration.z);
        Serial.printf("  LSM6 |a|=%.2f\n", mag);
    }

    // Statische Werte (noch kein GPS/FANET)
    live.speed = 0;
    live.heading = 340;  // Test: NNW, damit Kompass-Doppelpfeil sichtbar ist
    live.glide = 0;
    live.vario_avg = live.vario; // TODO: gleitender Durchschnitt
    live.delta_gnd = 0;
    live.wind_speed = 0;
    live.wind_dir = 0;
    live.gps_fix = false;
    live.sats = 0;
    live.fanet_peers = 0;
    live.avg_seconds = 20;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("AURA %s  Build %s %s\n", AURA_VERSION, __DATE__, __TIME__);
    Serial.flush();

    // SPI-CS deselect
    pinMode(46, OUTPUT); digitalWrite(46, HIGH);
    pinMode(12, OUTPUT); digitalWrite(12, HIGH);

    // === PHASE 1: I2C + Sensoren (Wire aktiv) ===
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, I2C_FREQ_HZ);
    i2cScan();
    initSensors();

    // Erste Sensor-Lesung
    Serial.println("\n--- Erste Messung ---");
    readSensors();
    Serial.flush();

    // === PHASE 2: Wire freigeben → epdiy ===
    Wire.end();
    Serial.println("\nWire.end() → epdiy init");

    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1560);

    // Panel komplett clearen (entfernt ALLE Geister von vorherigem Boot)
    epd_poweron();
    epd_clear();
    epd_poweroff();
    delay(100);

    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    Serial.printf("epdiy %dx%d VCOM=-1.56V\n", epd_width(), epd_height());
    Serial.flush();

    // Splash
    Serial.println("Splash...");
    showBootSplash(&hl, AURA_VERSION);
    Serial.println("Splash done");
    delay(3000);

    // Cruise mit Live-Daten (erste Messung)
    Serial.println("Cruise (live)...");
    showCruiseScreen(&hl, live);
    Serial.println("Cruise done");
    Serial.println("READY — Sensoren gelesen, Display steht.");
}

void loop() {
    // Kein Sensor-Read im Loop (epdiy haelt I2C)
    // Nur Serial-Heartbeat
    delay(5000);
    if (millis() - lastPrint < 30000) return;
    lastPrint = millis();
    Serial.printf("[ok] t=%lus heap=%lu\n", millis()/1000, (unsigned long)ESP.getFreeHeap());
}
