// AURA Kruecke v0.3.0 — Live-Daten: RTC + Batterie + GPS + Sensoren
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"
#include "version.h"
#include <Adafruit_BMP5xx.h>
#include <Adafruit_LSM6DSO32.h>
#include <Adafruit_SHT4x.h>
#include "PowersBQ25896.tpp"
#include <TinyGPSPlus.h>
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
static TinyGPSPlus gps;

// --- Zustand ---
static bool bmpA_ok=false, bmpB_ok=false, lsm_ok=false, sht_ok=false, ppm_ok=false;
static bool gps_ok=false;
static CruiseData live = {};
static unsigned long lastPrint = 0;
static unsigned long rtc_boot_millis = 0;  // millis() beim RTC-Lesen
static int rtc_boot_seconds = 0;           // Sekunden seit Mitternacht beim Boot

// === I2C-Hilfen (vor Wire.end()) ===

// Dezimal → BCD
static uint8_t toBCD(int val) { return ((val/10)<<4) | (val%10); }

// RTC (0x51) — Chip kann PCF8563 ODER PCF85063 sein!
// PCF8563:  Sec=0x02, Min=0x03, Hrs=0x04
// PCF85063: Sec=0x04, Min=0x05, Hrs=0x06
// Wir probieren beide und nehmen den der plausibel ist.
static uint8_t RTC_SEC_REG = 0x02;  // Default: PCF8563

static void detectRTC() {
    // Lese Register 0x02 (PCF8563 Seconds)
    Wire.beginTransmission(0x51);
    Wire.write(0x02);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x51, (uint8_t)3);
    if (Wire.available() < 3) return;
    uint8_t r02 = Wire.read() & 0x7F;
    uint8_t r03 = Wire.read() & 0x7F;
    uint8_t r04 = Wire.read() & 0x3F;
    int s02 = (r02>>4)*10 + (r02&0x0F);
    int m03 = (r03>>4)*10 + (r03&0x0F);
    int h04 = (r04>>4)*10 + (r04&0x0F);

    // Lese Register 0x04 (PCF85063 Seconds)
    Wire.beginTransmission(0x51);
    Wire.write(0x04);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x51, (uint8_t)3);
    if (Wire.available() < 3) return;
    uint8_t r04b = Wire.read() & 0x7F;
    uint8_t r05 = Wire.read() & 0x7F;
    uint8_t r06 = Wire.read() & 0x3F;
    int s04 = (r04b>>4)*10 + (r04b&0x0F);
    int m05 = (r05>>4)*10 + (r05&0x0F);
    int h06 = (r06>>4)*10 + (r06&0x0F);

    Serial.printf("[RTC] 0x02: %02d:%02d:%02d  0x04: %02d:%02d:%02d\n",
                   h04, m03, s02, h06, m05, s04);

    // Plausibilitaet: Stunden 0-23, Minuten 0-59
    if (h04 < 24 && m03 < 60 && s02 < 60) {
        RTC_SEC_REG = 0x02;
        Serial.println("[OK] RTC = PCF8563 (Reg 0x02)");
    } else if (h06 < 24 && m05 < 60 && s04 < 60) {
        RTC_SEC_REG = 0x04;
        Serial.println("[OK] RTC = PCF85063 (Reg 0x04)");
    } else {
        RTC_SEC_REG = 0x02;  // Fallback
        Serial.println("[??] RTC Typ unklar, nehme PCF8563");
    }
}

static void setRTC() {
    int h=0, m=0, s=0;
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    m += 1;  // +1 Min Kompensation fuer Flash
    if (m >= 60) { m -= 60; h = (h+1) % 24; }

    Wire.beginTransmission(0x51);
    Wire.write(RTC_SEC_REG);
    Wire.write(toBCD(s));
    Wire.write(toBCD(m));
    Wire.write(toBCD(h));
    Wire.endTransmission();
    Serial.printf("[OK] RTC gesetzt: %02d:%02d:%02d (CEST)\n", h, m, s);
}

static void readRTC() {
    Wire.beginTransmission(0x51);
    Wire.write(RTC_SEC_REG);
    if (Wire.endTransmission() != 0) { Serial.println("[--] RTC read fail"); return; }
    Wire.requestFrom((uint8_t)0x51, (uint8_t)3);
    if (Wire.available() < 3) return;
    uint8_t sec = Wire.read() & 0x7F;
    uint8_t min = Wire.read() & 0x7F;
    uint8_t hrs = Wire.read() & 0x3F;
    int s = (sec>>4)*10 + (sec&0x0F);
    int m = (min>>4)*10 + (min&0x0F);
    int h = (hrs>>4)*10 + (hrs&0x0F);
    Serial.printf("[OK] RTC %02d:%02d:%02d\n", h, m, s);
    live.rtc_hour = h;
    live.rtc_min = m;
    rtc_boot_seconds = h * 3600 + m * 60 + s;
    rtc_boot_millis = millis();
}

// BQ27220 Fuel Gauge (0x55) — SoC + Spannung lesen
static uint16_t bq27220Read(uint8_t cmd) {
    Wire.beginTransmission(0x55);
    Wire.write(cmd);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x55, (uint8_t)2);
    if (Wire.available() < 2) return 0xFFFF;
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    return (hi << 8) | lo;
}

static void readBattery() {
    // Versuche BQ27220 Fuel Gauge
    uint16_t soc = bq27220Read(0x1C);
    if (soc != 0xFFFF && soc <= 100) {
        live.bat_pct = soc;
        Serial.printf("[OK] BQ27220 SoC=%d%%\n", soc);
    } else {
        // Fallback: BQ25896 Spannung → SoC schaetzen
        // LiPo: 4.2V=100%, 3.7V=50%, 3.3V=0%
        if (ppm_ok) {
            float vbat = ppm.getBattVoltage() / 1000.0f;
            int soc_est = (int)((vbat - 3.3f) / (4.2f - 3.3f) * 100.0f);
            if (soc_est > 100) soc_est = 100;
            if (soc_est < 0) soc_est = 0;
            live.bat_pct = soc_est;
            // Restlaufzeit: 1500mAh Akku, ~40mA Verbrauch
            live.bat_hours = (1500.0f * soc_est / 100.0f) / 40.0f;
            Serial.printf("[OK] BAT Vbat=%.2fV → SoC~%d%% (~%.0fh)\n",
                           vbat, soc_est, live.bat_hours);
        }
    }
}

// PCA9535 (0x20) — GPS/LoRa 3.3V Rail einschalten
static void enableGPSPower() {
    // PCA9535 Port 0, Bit 0 = LORA_EN (shared GPS/LoRa 3.3V rail)
    // Config register 0x06: bit0=0 (output)
    // Output register 0x02: bit0=1 (enable)

    // Read current config
    Wire.beginTransmission(0x20);
    Wire.write(0x06);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x20, (uint8_t)1);
    uint8_t cfg = Wire.available() ? Wire.read() : 0xFF;

    // Set bit 0 as output (0)
    cfg &= ~0x01;
    Wire.beginTransmission(0x20);
    Wire.write(0x06);
    Wire.write(cfg);
    Wire.endTransmission();

    // Read current output
    Wire.beginTransmission(0x20);
    Wire.write(0x02);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x20, (uint8_t)1);
    uint8_t out = Wire.available() ? Wire.read() : 0x00;

    // Set bit 0 high (enable)
    out |= 0x01;
    Wire.beginTransmission(0x20);
    Wire.write(0x02);
    Wire.write(out);
    Wire.endTransmission();

    Serial.println("[OK] GPS/LoRa 3.3V rail enabled (PCA9535 IO0_0)");
}

// === I2C-Scan ===
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

// === Sensor-Init ===
static void initSensors() {
    ppm_ok = ppm.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, ADDR_BQ25896);
    if (ppm_ok) Serial.printf("[OK] BQ25896 Vbat=%.2fV %s\n",
                               ppm.getBattVoltage()/1000.0f, ppm.getChargeStatusString());

    sht_ok = sht.begin(&Wire);
    if (sht_ok) { sht.setPrecision(SHT4X_HIGH_PRECISION); Serial.println("[OK] SHT40"); }
    else Serial.println("[--] SHT40");

    delay(50);
    bmpA_ok = bmp_a.begin(ADDR_BMP581_PRIMARY, &Wire);
    if (!bmpA_ok) { delay(100); bmpA_ok = bmp_a.begin(ADDR_BMP581_PRIMARY, &Wire); }
    if (bmpA_ok) {
        bmp_a.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp_a.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
        bmp_a.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.println("[OK] BMP581#A");
    } else Serial.println("[--] BMP581#A");

    delay(50);
    bmpB_ok = bmp_b.begin(ADDR_BMP581_SECONDARY, &Wire);
    if (!bmpB_ok) { delay(100); bmpB_ok = bmp_b.begin(ADDR_BMP581_SECONDARY, &Wire); }
    if (bmpB_ok) {
        bmp_b.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp_b.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
        bmp_b.setOutputDataRate(BMP5XX_ODR_50_HZ);
        Serial.println("[OK] BMP581#B");
    } else Serial.println("[--] BMP581#B");

    lsm_ok = lsm.begin_I2C(ADDR_LSM6DSO32, &Wire);
    if (lsm_ok) {
        lsm.setAccelRange(LSM6DSO32_ACCEL_RANGE_16_G);
        lsm.setGyroRange(LSM6DS_GYRO_RANGE_1000_DPS);
        lsm.setAccelDataRate(LSM6DS_RATE_104_HZ);
        lsm.setGyroDataRate(LSM6DS_RATE_104_HZ);
        Serial.println("[OK] LSM6DSO32");
    } else Serial.println("[--] LSM6DSO32");

    Serial.printf("Sensoren: %d/4\n", (int)bmpA_ok+bmpB_ok+lsm_ok+sht_ok);
}

// === Sensor-Daten lesen ===
static void readSensors() {
    if (sht_ok) {
        sensors_event_t h, t;
        sht.getEvent(&h, &t);
        // SHT40 Temp-Offset: Sensor wird vom ESP32 aufgeheizt (~5.5C)
        // Kalibriert gegen Zimmer-Hygrometer: SHT40=31C, Real=25.8C → Offset=5.2C
        // TODO: in NVS speichern, ueber Menu kalibrierbar
        const float TEMP_OFFSET = -3.8f;  // SHT40 Eigenwaerme, TODO: NVS-kalibrierbar
        live.temp = t.temperature + TEMP_OFFSET;
        live.dewpoint = live.temp - (100.0f - h.relative_humidity) / 5.0f;
    }

    static float last_alt = 0;
    static unsigned long last_alt_time = 0;

    if (bmpA_ok && bmp_a.performReading()) {
        float pressure_hpa = bmp_a.pressure;
        float pressure_pa = pressure_hpa * 100.0f;
        // QNH-Hoehe = ISA-Formel (konsistent mit QNH-Definition)
        // Temp-Korrektur wuerde "True Altitude" geben, nicht QNH-Hoehe
        float alt = alt_calc.computeISA(pressure_pa);
        live.altitude = alt;
        live.qnh = alt_calc.getQNH() / 100.0f;  // QNH in hPa fuer Anzeige

        unsigned long now = millis();
        if (last_alt_time > 0 && (now - last_alt_time) > 100) {
            float dt = (now - last_alt_time) / 1000.0f;
            live.vario = (alt - last_alt) / dt;
        }
        last_alt = alt;
        last_alt_time = now;
    }

    if (bmpB_ok && bmp_b.performReading() && bmpA_ok) {
        float dp = bmp_a.pressure - bmp_b.pressure;
        Serial.printf("  Dp=%.2f Pa\n", dp);
    }

    if (lsm_ok) {
        sensors_event_t a, g, t;
        lsm.getEvent(&a, &g, &t);
    }

    live.vario_avg = live.vario;
    live.delta_gnd = 0;
    live.wind_speed = 0;
    live.wind_dir = 0;
    live.avg_seconds = 20;
}

// === Software-Uhr (RTC-Startzeit + millis) ===
static void updateClock() {
    unsigned long elapsed = (millis() - rtc_boot_millis) / 1000;
    int total_sec = (rtc_boot_seconds + (int)elapsed) % 86400;
    live.rtc_hour = total_sec / 3600;
    live.rtc_min = (total_sec % 3600) / 60;
}

// === GPS Feed (UART, im Loop) ===
static void feedGPS() {
    while (Serial2.available()) {
        char c = Serial2.read();
        gps.encode(c);
    }
    if (gps.location.isUpdated()) {
        live.speed = gps.speed.kmph();
        live.heading = gps.course.deg();
        live.sats = gps.satellites.value();
        live.gps_fix = gps.location.isValid();
        if (!gps_ok) {
            gps_ok = true;
            Serial.printf("[GPS] Fix! Sats=%d Spd=%.1f Hdg=%.0f\n",
                           live.sats, live.speed, live.heading);
        }
    } else {
        live.sats = gps.satellites.value();
        live.gps_fix = gps.location.isValid();
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("AURA %s  Build %s %s\n", AURA_VERSION, __DATE__, __TIME__);
    Serial.flush();

    // SPI-CS deselect
    pinMode(46, OUTPUT); digitalWrite(46, HIGH);
    pinMode(12, OUTPUT); digitalWrite(12, HIGH);

    // === I2C Phase (Wire aktiv) ===
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, I2C_FREQ_HZ);
    i2cScan();
    initSensors();
    detectRTC(); // PCF8563 oder PCF85063?
    setRTC();    // RTC auf Compile-Zeit (CEST) setzen
    readRTC();   // Zuruecklesen zur Verifikation
    readBattery();
    enableGPSPower();

    // QNH kalibrieren: Referenzhoehe → QNH berechnen
    // TODO: aus NVS laden oder ueber Menu setzen
    // Oberneunforn ≈ 470m MSL, Garmin-Abgleich 489m
    // QNH setzen: Referenzhoehe + aktueller Druck → QNH
    // computeISA: alt = 44330*(1-(P/QNH)^0.190284)
    // Umgestellt: QNH = P / (1 - alt/44330)^5.255
    if (bmpA_ok) {
        // Frische Messung fuer QNH-Kalibrierung
        delay(100);
        bmp_a.performReading();
        float p_pa = bmp_a.pressure * 100.0f;
        float ref_alt = 489.0f;  // Garmin-Referenz, TODO: Menu/NVS
        float base = 1.0f - ref_alt / 44330.0f;
        float qnh_pa = p_pa / powf(base, 5.255f);
        alt_calc.setQNH(qnh_pa / 100.0f);
        // Sofort-Check: compute mit gleichem Druck muss ref_alt ergeben
        float check = alt_calc.computeISA(p_pa);
        Serial.printf("[OK] QNH=%.1f hPa (Ref=%.0fm P=%.1f hPa Check=%.1fm)\n",
                       qnh_pa/100.0f, ref_alt, bmp_a.pressure, check);
    }

    // Erste Sensor-Lesung
    Serial.println("--- Erste Messung ---");
    readSensors();
    Serial.printf("  Alt=%.1fm T=%.1fC Dew=%.1fC Bat=%d%%\n",
                   live.altitude, live.temp, live.dewpoint, live.bat_pct);
    Serial.flush();

    // === GPS UART starten (laeuft parallel zu epdiy, kein I2C) ===
    Serial2.begin(38400, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    Serial.println("[OK] GPS UART2 started (38400 baud)");

    // === Wire freigeben → epdiy ===
    Wire.end();
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1600);  // Panel-spezifisch: -1.6V (vom FPC-Kabel abgelesen)
    epd_poweron(); epd_clear(); epd_poweroff();
    delay(100);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    Serial.printf("epdiy %dx%d\n", epd_width(), epd_height());

    // === KINO-SEQUENZ ===
    // 1. Invertierter Splash (schwarz, weisses Logo) — 5s
    showBootSplash(&hl, AURA_VERSION);
    delay(5000);

    // 2. Weiss-Blende (dramatischer Uebergang)
    uint8_t *fb_clear = epd_hl_get_framebuffer(&hl);
    memset(fb_clear, 0xFF, 960/2*540);
    epd_poweron();
    epd_hl_update_screen(&hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
    delay(800);

    // 3. Cruise-Screen erscheint
    Serial.println("Cruise...");
    showCruiseScreen(&hl, live);
    Serial.println("READY");
}

// Display-Refresh Intervall (Sekunden)
static const unsigned long DISPLAY_INTERVAL_MS = 30000;  // 30s
static unsigned long lastDisplay = 0;
static bool first_gps_fix = false;

void loop() {
    // GPS + Uhr updaten (laeuft immer)
    feedGPS();
    updateClock();

    delay(100);

    // Serial-Log alle 10s
    if (millis() - lastPrint >= 10000) {
        lastPrint = millis();
        Serial.printf("[%02d:%02d] GPS:%s sat=%d spd=%.0f hdg=%.0f | Alt=%.0fm T=%.1f Bat=%d%%\n",
                       live.rtc_hour, live.rtc_min,
                       live.gps_fix ? "FIX" : "---",
                       live.sats, live.speed, live.heading,
                       live.altitude, live.temp, live.bat_pct);
    }

    // Display-Update alle 30s ODER bei erstem GPS-Fix
    bool force_update = (live.gps_fix && !first_gps_fix);
    if (force_update) {
        first_gps_fix = true;
        Serial.println("[EPD] Erster GPS-Fix → Display-Update!");
    }

    if (force_update || (millis() - lastDisplay >= DISPLAY_INTERVAL_MS)) {
        lastDisplay = millis();

        // Heading nur anzeigen wenn Speed > 2 km/h (GPS-COG braucht Bewegung)
        if (live.speed < 2.0f) live.heading = 0;

        Serial.printf("[EPD] Refresh %02d:%02d Alt=%.0f Sat=%d\n",
                       live.rtc_hour, live.rtc_min, live.altitude, live.sats);

        showCruiseScreen(&hl, live);

        Serial.println("[EPD] done");
    }
}
