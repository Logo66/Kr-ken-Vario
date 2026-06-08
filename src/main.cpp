// AURA Kruecke v0.3 — Kalman-Vario + Raw I2C + schneller Refresh
#include <Arduino.h>
#include <Wire.h>
#include "driver/i2c.h"
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
#include "thermal_screen.h"
#include "landing_screen.h"
#include "menu_screen.h"
#include "qnh_screen.h"
#include "flight_detect.h"
#include "flugbuch.h"
#include "touch.h"

static TouchManager touch;
static FlightDetector flight;
static Flugbuch flugbuch;
#include "vario/altitude.h"
#include "kalman_vario.h"
#include "esp_sleep.h"

// --- Objekte ---
static EpdiyHighlevelState hl;
static Adafruit_BMP5xx bmp_a, bmp_b;
static Adafruit_LSM6DSO32 lsm;
static Adafruit_SHT4x sht;
static PowersBQ25896 ppm;
static Altitude alt_calc;
static TinyGPSPlus gps;
static KalmanVario kf;

// --- Zustand ---
static bool bmpA_ok=false, bmpB_ok=false, lsm_ok=false, sht_ok=false, ppm_ok=false;
static CruiseData live = {};
static unsigned long lastPrint=0, lastDisplay=0;

// Screen-Manager
enum Screen { SCR_CRUISE, SCR_THERMAL, SCR_MENU, SCR_LANDING, SCR_QNH, SCR_FLUGBUCH };
static Screen currentScreen = SCR_CRUISE;
static bool backlight_on = false;
static float qnh_ref_alt = 489.0f;  // Referenzhoehe fuer QNH (kalibrierbar)
static unsigned long rtc_boot_millis=0;
static int rtc_boot_seconds=0;

// === Raw I2C (nutzt epdiy's I2C_NUM_0 — NUR wenn Display idle) ===
static bool rawI2C(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0, addr, &reg, 1,
                                         buf, len, pdMS_TO_TICKS(50)) == ESP_OK;
}

// BMP581 Pressure lesen (Continuous Mode, Register 0x20-0x22)
static float rawBMP581Pressure() {
    uint8_t d[3];
    if (!rawI2C(ADDR_BMP581_PRIMARY, 0x20, d, 3)) return -1;
    int32_t raw = (int32_t)d[0] | ((int32_t)d[1]<<8) | ((int32_t)d[2]<<16);
    if (raw & 0x800000) raw |= 0xFF000000;
    return (float)raw / 64.0f;  // Pa
}

// SHT40 lesen (raw I2C, Measure High Precision = 0xFD)
static bool rawSHT40(float *temp, float *rh) {
    uint8_t cmd = 0xFD;
    if (i2c_master_write_to_device(I2C_NUM_0, ADDR_SHT40, &cmd, 1,
                                    pdMS_TO_TICKS(50)) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));  // Messzeit
    uint8_t d[6];
    if (i2c_master_read_from_device(I2C_NUM_0, ADDR_SHT40, d, 6,
                                     pdMS_TO_TICKS(50)) != ESP_OK) return false;
    uint16_t t_raw = (d[0]<<8)|d[1];
    uint16_t h_raw = (d[3]<<8)|d[4];
    *temp = -45.0f + 175.0f * (float)t_raw / 65535.0f;
    *rh = -6.0f + 125.0f * (float)h_raw / 65535.0f;
    if (*rh > 100) *rh = 100; if (*rh < 0) *rh = 0;
    return true;
}

// === RTC (vor Wire.end) ===
static uint8_t toBCD(int v) { return ((v/10)<<4)|(v%10); }
static void setupRTC() {
    // Detect
    Wire.beginTransmission(0x51); Wire.write(0x02);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x51,(uint8_t)3);
    if (Wire.available()<3) return;
    uint8_t r0=Wire.read()&0x7F, r1=Wire.read()&0x7F, r2=Wire.read()&0x3F;
    int h=(r2>>4)*10+(r2&0xF), m=(r1>>4)*10+(r1&0xF), s=(r0>>4)*10+(r0&0xF);
    Serial.printf("[RTC] gelesen %02d:%02d:%02d\n", h, m, s);

    // Setzen aus __TIME__
    int ch=0,cm=0,cs=0;
    sscanf(__TIME__,"%d:%d:%d",&ch,&cm,&cs);
    cm+=1; if(cm>=60){cm-=60;ch=(ch+1)%24;}
    Wire.beginTransmission(0x51); Wire.write(0x02);
    Wire.write(toBCD(cs)); Wire.write(toBCD(cm)); Wire.write(toBCD(ch));
    Wire.endTransmission();
    Serial.printf("[RTC] gesetzt %02d:%02d:%02d\n", ch, cm, cs);
    rtc_boot_seconds = ch*3600+cm*60+cs;
    rtc_boot_millis = millis();
    live.rtc_hour=ch; live.rtc_min=cm;
}

// BQ25896 Batterie-SoC aus Spannung
static void readBattery() {
    if (!ppm_ok) return;
    float v = ppm.getBattVoltage()/1000.0f;
    int soc = (int)((v-3.3f)/(4.2f-3.3f)*100.0f);
    if(soc>100)soc=100; if(soc<0)soc=0;
    live.bat_pct=soc;
    live.bat_hours=(1500.0f*soc/100.0f)/40.0f;
    Serial.printf("[BAT] %.2fV → %d%%\n", v, soc);
}

// GPS Power via PCA9535
static void enableGPS() {
    Wire.beginTransmission(0x20); Wire.write(0x06);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x20,(uint8_t)1);
    uint8_t cfg = Wire.available()?Wire.read():0xFF;
    cfg &= ~0x01;
    Wire.beginTransmission(0x20); Wire.write(0x06); Wire.write(cfg); Wire.endTransmission();
    Wire.beginTransmission(0x20); Wire.write(0x02);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x20,(uint8_t)1);
    uint8_t out = Wire.available()?Wire.read():0;
    out |= 0x01;
    Wire.beginTransmission(0x20); Wire.write(0x02); Wire.write(out); Wire.endTransmission();
    Serial.println("[OK] GPS power on");
}

// Uhr-Update
static void updateClock() {
    unsigned long e = (millis()-rtc_boot_millis)/1000;
    int t = (rtc_boot_seconds+(int)e)%86400;
    live.rtc_hour=t/3600; live.rtc_min=(t%3600)/60;
}

// GPS feed
static unsigned long gps_total_bytes = 0;
static void feedGPS() {
    while(Serial2.available()) {
        gps.encode(Serial2.read());
        gps_total_bytes++;
    }
    live.sats = gps.satellites.value();
    live.gps_fix = gps.location.isValid();
    if(gps.location.isUpdated()) {
        live.speed = gps.speed.kmph();
        live.heading = gps.course.deg();
    }
    if(live.speed < 2.0f) live.heading = 0;
}

// === SETUP ===
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.printf("AURA %s Build %s %s\n", AURA_VERSION, __DATE__, __TIME__);

    pinMode(46,OUTPUT); digitalWrite(46,HIGH);  // LoRa CS
    pinMode(12,OUTPUT); digitalWrite(12,HIGH);  // SD CS
    pinMode(11,OUTPUT); digitalWrite(11,LOW);   // Backlight aus
    pinMode(0, INPUT_PULLUP);                   // BOOT button

    // I2C + Sensoren (Wire Phase)
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, I2C_FREQ_HZ);

    ppm_ok = ppm.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, ADDR_BQ25896);
    sht_ok = sht.begin(&Wire);
    delay(50);
    bmpA_ok = bmp_a.begin(ADDR_BMP581_PRIMARY, &Wire);
    if(bmpA_ok) {
        bmp_a.setTemperatureOversampling(BMP5XX_OVERSAMPLING_4X);
        bmp_a.setPressureOversampling(BMP5XX_OVERSAMPLING_16X);
        bmp_a.setOutputDataRate(BMP5XX_ODR_50_HZ);
    }
    Serial.printf("Sensoren: BMP=%d SHT=%d\n", bmpA_ok, sht_ok);

    setupRTC();
    readBattery();
    enableGPS();

    // QNH kalibrieren
    // QNH kalibrieren — wird nach epdiy init nochmal mit raw I2C gemacht
    // (Adafruit .pressure gibt hier unzuverlaessige Werte)
    live.altitude = 489.0f;  // Platzhalter bis raw-Kalibrierung

    // Erste SHT40 Lesung
    if(sht_ok) {
        sensors_event_t h,t;
        sht.getEvent(&h,&t);
        live.temp = t.temperature - 3.8f;
        live.dewpoint = live.temp - (100.0f-h.relative_humidity)/5.0f;
    }

    // GPS: erst 9600 (L76K), dann 38400 (u-blox) probieren
    Serial2.begin(9600, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    delay(500);
    int gps_bytes = 0;
    unsigned long gps_test = millis();
    while (millis() - gps_test < 2000) {
        if (Serial2.available()) { Serial2.read(); gps_bytes++; }
    }
    if (gps_bytes > 10) {
        Serial.printf("[GPS] L76K erkannt (9600 Baud, %d Bytes)\n", gps_bytes);
    } else {
        Serial2.updateBaudRate(38400);
        delay(500);
        gps_bytes = 0;
        gps_test = millis();
        while (millis() - gps_test < 2000) {
            if (Serial2.available()) { Serial2.read(); gps_bytes++; }
        }
        if (gps_bytes > 10) {
            Serial.printf("[GPS] u-blox M10Q erkannt (38400 Baud, %d Bytes)\n", gps_bytes);
        } else {
            Serial.printf("[GPS] KEIN GPS-Signal (%d Bytes bei 9600+38400)\n", gps_bytes);
        }
    }

    // Wire freigeben → epdiy
    Wire.end();
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1600);
    epd_poweron(); epd_clear(); epd_poweroff();
    delay(100);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    // QNH kalibrieren mit raw I2C (zuverlaessig, 96719 Pa bewiesen)
    delay(200);
    float p_cal = rawBMP581Pressure();
    if (p_cal > 80000 && p_cal < 120000) {
        float ref = 489.0f;
        float qnh_pa = p_cal / powf(1.0f - ref/44330.0f, 5.255f);
        alt_calc.setQNH(qnh_pa / 100.0f);
        live.altitude = alt_calc.computeISA(p_cal);
        Serial.printf("[QNH] %.1f hPa (raw P=%.0f Pa) → Alt=%.0fm\n",
                       qnh_pa/100.0f, p_cal, live.altitude);
    } else {
        Serial.printf("[QNH] raw P=%.0f — unplausibel, skip\n", p_cal);
    }

    // Touch init (GT911 ueber raw I2C)
    touch.init();

    // Flugbuch Demo-Daten
    flugbuch.addDemoFlights();
    Serial.printf("[FLUGBUCH] %d Demo-Fluege geladen\n", flugbuch.count);

    // Kalman init
    kf.init(live.altitude);

    // Splash → Cruise → nach 15s Thermik-Demo
    showBootSplash(&hl, AURA_VERSION);
    delay(4000);
    showCruiseScreen(&hl, live);
    Serial.println("READY — Cruise aktiv, Thermik-Demo in 15s");
}

// === LOOP ===
static float vario_sum=0;
static int vario_count=0;
static unsigned long vario_window=0;

void loop() {
    feedGPS();
    updateClock();

    // BMP581 raw read + Kalman (~20 Hz)
    float p = rawBMP581Pressure();  // Pa
    if (p > 10000 && p < 120000) {  // Plausibilitaet: 100-1200 hPa
        float alt = alt_calc.computeISA(p);
        kf.update(alt);
        live.altitude = kf.altitude;
        live.vario = kf.vario;
        vario_sum += kf.vario;
        vario_count++;
        if (millis()-vario_window > 20000) {
            live.vario_avg = vario_sum / fmaxf(1,vario_count);
            vario_sum=0; vario_count=0; vario_window=millis();
        }
    }

    // SHT40 raw read (alle 5s)
    static unsigned long lastSHT=0;
    if (millis()-lastSHT > 5000) {
        lastSHT = millis();
        float t,rh;
        if (rawSHT40(&t,&rh)) {
            live.temp = t - 3.8f;
            live.dewpoint = live.temp - (100.0f-rh)/5.0f;
        }
    }

    delay(50);

    // Serial alle 5s
    if (millis()-lastPrint >= 5000) {
        lastPrint = millis();
        Serial.printf("[%02d:%02d] V=%+.1f avg=%+.1f Alt=%.0f T=%.1f GPS:%s s=%d bytes=%lu\n",
                       live.rtc_hour, live.rtc_min,
                       live.vario, live.vario_avg, live.altitude,
                       live.temp, live.gps_fix?"FIX":"---", live.sats, gps_total_bytes);
    }

    // === FLUG-ERKENNUNG ===
    flight.update(live.speed, live.vario, live.altitude);

    // Start erkannt → kurze Meldung auf Display
    if (flight.justStarted()) {
        uint8_t *fb = epd_hl_get_framebuffer(&hl);
        epd_hl_set_all_white(&hl);
        EpdFontProperties p = epd_font_properties_default(); p.fg_color = 0;
        int cx=250, cy=280;
        epd_write_string(&ArialBold40, "START ERKANNT", &cx, &cy, fb, &p);
        cx=300; cy=330;
        epd_write_string(&ArialBold16, "Aufzeichnung gestartet", &cx, &cy, fb, &p);
        epd_poweron();
        epd_hl_update_screen(&hl, MODE_GC16, (int)epd_ambient_temperature());
        epd_poweroff();
        delay(3000);
        currentScreen = SCR_CRUISE;
        showCruiseScreen(&hl, live, MODE_GC16);
        lastDisplay = millis();
    }

    // Landung erkannt → Flug speichern + Lande-Screen
    if (flight.state == FLIGHT_LANDED && currentScreen != SCR_LANDING) {
        // Flug-Record erstellen
        FlightRecord rec = {};
        rec.year = 2026; rec.month = 6; rec.day = 8; // TODO: aus RTC
        rec.hour = live.rtc_hour; rec.minute = live.rtc_min;
        rec.duration_sec = flight.flightDurationSec();
        rec.max_alt = (int16_t)flight.max_altitude;
        rec.start_alt = (int16_t)flight.start_altitude;
        rec.max_climb = 0; // TODO: max_climb tracken
        rec.track_dist_m = 0; // TODO: GPS-Spur berechnen
        rec.straight_dist_m = 0; // TODO: Luftlinie berechnen
        rec.valid = true;
        flugbuch.addFlight(rec);
        Serial.printf("[FLUG] Gespeichert: %dmin, max %dm\n",
                       rec.duration_sec/60, rec.max_alt);

        currentScreen = SCR_LANDING;
        showLandingScreen(&hl, live.altitude, live.rtc_hour, live.rtc_min);
        lastDisplay = millis();
    }

    // === SCREEN-MANAGER (Swipe + Button + Long-Tap=Menu) ===
    Gesture g = touch.poll();

    if (currentScreen == SCR_FLUGBUCH) {
        if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_MENU;
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
        }
    } else if (currentScreen == SCR_QNH) {
        if (g == GEST_TAP) {
            QnhAction qa = checkQnhTap(touch.lastX(), touch.lastY());
            float p = rawBMP581Pressure();
            if (qa == QNH_PLUS) {
                qnh_ref_alt += 10;
                float qnh = calcQnhFromAlt(qnh_ref_alt, p);
                alt_calc.setQNH(qnh);
                showQnhScreen(&hl, qnh_ref_alt, qnh, p);
                Serial.printf("[QNH] +10 → Alt=%.0f QNH=%.1f\n", qnh_ref_alt, qnh);
            } else if (qa == QNH_MINUS) {
                qnh_ref_alt -= 10;
                float qnh = calcQnhFromAlt(qnh_ref_alt, p);
                alt_calc.setQNH(qnh);
                showQnhScreen(&hl, qnh_ref_alt, qnh, p);
                Serial.printf("[QNH] -10 → Alt=%.0f QNH=%.1f\n", qnh_ref_alt, qnh);
            } else if (qa == QNH_OK) {
                float qnh = calcQnhFromAlt(qnh_ref_alt, p);
                alt_calc.setQNH(qnh);
                kf.init(alt_calc.computeISA(p));
                currentScreen = SCR_CRUISE;
                showCruiseScreen(&hl, live, MODE_GC16);
                Serial.printf("[QNH] OK → Alt=%.0f QNH=%.1f\n", qnh_ref_alt, qnh);
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_MENU;
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
        }
    } else if (currentScreen == SCR_MENU) {
        // Im Menu: Tap = waehlen, Swipe = schliessen
        if (g == GEST_TAP) {
            Serial.printf("[MENU TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
            MenuItem mi = checkMenuTap(touch.lastX(), touch.lastY());
            if (mi == MENU_QNH) {
                currentScreen = SCR_QNH;
                float p = rawBMP581Pressure();
                float qnh = calcQnhFromAlt(qnh_ref_alt, p);
                showQnhScreen(&hl, qnh_ref_alt, qnh, p);
                Serial.printf("[QNH] Screen: Alt=%.0f QNH=%.1f\n", qnh_ref_alt, qnh);
            } else if (mi == MENU_BACKLIGHT) {
                backlight_on = !backlight_on;
                digitalWrite(11, backlight_on ? HIGH : LOW);
                Serial.printf("[MENU] Backlight %s\n", backlight_on?"AN":"AUS");
                showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
            } else if (mi == MENU_FLUGBUCH) {
                currentScreen = SCR_FLUGBUCH;
                showFlugbuchScreen(&hl, flugbuch);
                Serial.println("[MENU] Flugbuch");
            } else if (mi == MENU_AUS) {
                Serial.println("[MENU] Ausschalten → Credits");
                showCreditsAndShutdown(&hl);  // Kommt nicht zurueck
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_CRUISE;
            showCruiseScreen(&hl, live, MODE_GC16);
            lastDisplay = millis();
        }
    } else if (currentScreen == SCR_LANDING) {
        // Lande-Screen: Tap = Option waehlen
        if (g == GEST_TAP) {
            LandingChoice lc = checkLandingTap(touch.lastX(), touch.lastY());
            if (lc == LAND_OK) {
                Serial.println("[LAND] Gut gelandet");
                flight.reset();
                currentScreen = SCR_CRUISE;
                showCruiseScreen(&hl, live, MODE_GC16);
                lastDisplay = millis();
            } else if (lc == LAND_RIDE) {
                Serial.println("[LAND] Brauche Ride → FANET (TODO)");
            } else if (lc == LAND_HELP) {
                Serial.println("[LAND] HILFE → FANET Notruf (TODO)");
            }
        }
    } else {
        // Cruise/Thermik: Swipe = wechseln, Long-Tap = Menu
        if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = (currentScreen==SCR_CRUISE) ? SCR_THERMAL : SCR_CRUISE;
            Serial.printf("[SWIPE] → %s\n", currentScreen==SCR_CRUISE?"Cruise":"Thermik");
            if (currentScreen==SCR_CRUISE) showCruiseScreen(&hl, live, MODE_GC16);
            else showDemoThermalScreen(&hl);
            lastDisplay = millis();
        } else if (g == GEST_TAP) {
            Serial.printf("[TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
        } else if (g == GEST_LONG_TAP) {
            Serial.printf("[LONG TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
            currentScreen = SCR_MENU;
            Serial.println("[MENU] Geoeffnet");
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
            lastDisplay = millis();
        }
    }

    // BOOT-Button = Flug-Screen wechseln
    static bool btn_last = true;
    bool btn_now = digitalRead(0);
    if (!btn_now && btn_last && currentScreen != SCR_MENU && currentScreen != SCR_LANDING) {
        currentScreen = (currentScreen==SCR_CRUISE) ? SCR_THERMAL : SCR_CRUISE;
        if (currentScreen==SCR_CRUISE) showCruiseScreen(&hl, live, MODE_GC16);
        else showDemoThermalScreen(&hl);
        lastDisplay = millis();
        delay(300);
    }
    btn_last = btn_now;

    // Auto-Thermik bei Steigen
    static unsigned long climb_since = 0;
    if (live.vario_avg > 0.5f && flight.state == FLIGHT_FLYING) {
        if (!climb_since) climb_since = millis();
        if (millis()-climb_since > 10000 && currentScreen==SCR_CRUISE) {
            currentScreen = SCR_THERMAL;
            showDemoThermalScreen(&hl);
            lastDisplay = millis();
        }
    } else { climb_since = 0; }

    // 1 Hz Display Refresh — NUR Flug-Screens (Menu/Landing/QNH/Flugbuch sind statisch)
    if ((currentScreen == SCR_CRUISE || currentScreen == SCR_THERMAL)
        && millis()-lastDisplay >= 1000) {
        lastDisplay = millis();
        if (currentScreen == SCR_CRUISE)
            showCruiseScreen(&hl, live, MODE_DU);
        else
            showDemoThermalScreen(&hl);
    }
}
