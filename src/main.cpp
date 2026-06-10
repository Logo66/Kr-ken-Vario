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
#include "goal_screen.h"
#include "map_screen.h"
#include "xsection_screen.h"
#include "pack_reader.h"

// Forward-Declarations (definiert weiter unten nach globalen Variablen)
static void updateGoalData();

// === Wegpunkt-Navigation ===
struct Waypoint {
    const char *name;
    double lat, lon;
    float alt;  // MSL
};

// Test-Wegpunkt: Fiesch Fiescheralp (Wallis)
static Waypoint activeWP = {"FIESCH", 46.4089, 8.1339, 2212.0f};
static GoalData goalLive = {};

static float haversineDist(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371000;
    double dlat = (lat2-lat1)*M_PI/180.0;
    double dlon = (lon2-lon1)*M_PI/180.0;
    double a = sin(dlat/2)*sin(dlat/2) +
               cos(lat1*M_PI/180)*cos(lat2*M_PI/180)*sin(dlon/2)*sin(dlon/2);
    return (float)(R * 2 * atan2(sqrt(a), sqrt(1-a)));
}

static float bearingTo(double lat1, double lon1, double lat2, double lon2) {
    double dlon = (lon2-lon1)*M_PI/180.0;
    double y = sin(dlon)*cos(lat2*M_PI/180);
    double x = cos(lat1*M_PI/180)*sin(lat2*M_PI/180) -
               sin(lat1*M_PI/180)*cos(lat2*M_PI/180)*cos(dlon);
    float brg = (float)(atan2(y,x)*180.0/M_PI);
    if (brg < 0) brg += 360;
    return brg;
}

// updateGoalData() — definiert nach den globalen Variablen (braucht gps + live)

#include "thermal_manager.h"
#include "landing_screen.h"
#include "menu_screen.h"
#include "qnh_screen.h"
#include "flight_detect.h"
#include "flugbuch.h"
#include "igc_logger.h"
#include "fanet.h"
#include "device_registry.h"   // Ticket C: Self-Registration + NVS-Device-Token (Bearer)
#include "sd_manager.h"
#include "funk_screen.h"
#include "wifi_screen.h"
#include "overlay_screen.h"
#include "ble_manager.h"
#include "ble_screen.h"
#include "touch.h"

static TouchManager touch;
static FlightDetector flight;
static Flugbuch flugbuch;
static IgcLogger igc;
static ThermalManager thermal;
static FanetRadio fanet;
static SDManager sdcard;
static WiFiScreen wifiScreen;
static OverlayScreen overlayScreen;
static BLEManager ble;
static BleScreen bleScreen;
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
static float g_now = 1.0f, g_max_flight = 0;   // aktuelle G-Kraft / Spitze im Flug
static CruiseData live = {};
static unsigned long lastPrint=0, lastDisplay=0;

// Screen-Manager
enum Screen { SCR_CRUISE, SCR_THERMAL, SCR_GOAL, SCR_MAP, SCR_XSECTION, SCR_MENU, SCR_LANDING, SCR_QNH, SCR_FLUGBUCH, SCR_FUNK, SCR_WIFI, SCR_OVERLAY, SCR_BLE };
static Screen currentScreen = SCR_CRUISE;
static bool backlight_on = false;

// === updateGoalData (braucht gps + live, daher hier nach den Variablen) ===
// Default: Niederneunforn — wird beim ersten GPS-Fix ueberschrieben
static double lastGoodLat = 47.5973, lastGoodLon = 8.7848;

static void updateGoalData() {
    // GPS-Position merken wenn gueltig
    if (gps.location.isValid() && gps.location.lat() != 0) {
        lastGoodLat = gps.location.lat();
        lastGoodLon = gps.location.lng();
        static unsigned long lastPosSave = 0;           // K2: alle 30s in NVS sichern
        if (millis() - lastPosSave > 30000) { lastPosSave = millis(); deviceSaveLastPos(lastGoodLat, lastGoodLon); }
    }
    parseCenterLat = lastGoodLat; parseCenterLon = lastGoodLon;   // Tile-Fenster folgt der Position

    // Track-Punkt sammeln (alle 2s bei GPS-Fix)
    static unsigned long lastTrack = 0;
    if (lastGoodLat != 0 && millis() - lastTrack > 2000) {
        lastTrack = millis();
        trackAdd(lastGoodLat, lastGoodLon);
    }

    if (lastGoodLat == 0) {
        goalLive.wp_name = activeWP.name;
        goalLive.distance_km = 0;
        goalLive.arrival_m = live.altitude - activeWP.alt;
        goalLive.gr_needed = 0;
        goalLive.gr_current = 0;
        goalLive.bearing_abs = 0;
        goalLive.bearing_rel = 0;
    } else {
        double myLat = lastGoodLat, myLon = lastGoodLon;
        float dist = haversineDist(myLat, myLon, activeWP.lat, activeWP.lon);
        float brg = bearingTo(myLat, myLon, activeWP.lat, activeWP.lon);
        float hover = live.altitude - activeWP.alt;
        goalLive.wp_name = activeWP.name;
        goalLive.distance_km = dist / 1000.0f;
        goalLive.arrival_m = hover;
        goalLive.bearing_abs = brg;
        goalLive.bearing_rel = brg - live.heading;
        while (goalLive.bearing_rel > 180) goalLive.bearing_rel -= 360;
        while (goalLive.bearing_rel < -180) goalLive.bearing_rel += 360;
        goalLive.gr_needed = (hover > 10) ? dist / hover : 999;
        if (live.speed > 5 && live.vario < -0.3f)
            goalLive.gr_current = (live.speed/3.6f) / fabsf(live.vario);
        else goalLive.gr_current = 0;
    }
    goalLive.rtc_hour = live.rtc_hour;
    goalLive.rtc_min = live.rtc_min;
    goalLive.sats = live.sats;
    goalLive.bat_pct = live.bat_pct;
    goalLive.fanet_peers = fanet.pilot_count;
    goalLive.buddy_connected = false;
    goalLive.buddy_hint = NULL;
}
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

// SHT45 lesen (raw I2C, Measure High Precision = 0xFD)
static bool rawSHT45(float *temp, float *rh) {
    uint8_t cmd = 0xFD;
    if (i2c_master_write_to_device(I2C_NUM_0, ADDR_SHT45, &cmd, 1,
                                    pdMS_TO_TICKS(50)) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));  // Messzeit
    uint8_t d[6];
    if (i2c_master_read_from_device(I2C_NUM_0, ADDR_SHT45, d, 6,
                                     pdMS_TO_TICKS(50)) != ESP_OK) return false;
    uint16_t t_raw = (d[0]<<8)|d[1];
    uint16_t h_raw = (d[3]<<8)|d[4];
    *temp = -45.0f + 175.0f * (float)t_raw / 65535.0f;
    *rh = -6.0f + 125.0f * (float)h_raw / 65535.0f;
    if (*rh > 100) *rh = 100; if (*rh < 0) *rh = 0;
    return true;
}

// === LSM6DSO32 Beschleunigung (raw I2C, 0x6A) ===
static bool lsm6Init() {
    uint8_t who = 0;
    if (!rawI2C(ADDR_LSM6DSO32, 0x0F, &who, 1)) { Serial.println("[LSM6] keine Antwort"); return false; }
    if (who != 0x6C) { Serial.printf("[LSM6] WHO_AM_I=0x%02X (erwartet 0x6C)\n", who); return false; }
    uint8_t cfg[2] = { 0x10, 0x4C };   // CTRL1_XL: ODR 104 Hz, FS +-16 g
    if (i2c_master_write_to_device(I2C_NUM_0, ADDR_LSM6DSO32, cfg, 2, pdMS_TO_TICKS(50)) != ESP_OK) {
        Serial.println("[LSM6] config FAIL"); return false;
    }
    Serial.println("[LSM6] init OK (104 Hz, +-16 g)");
    return true;
}
// Gesamt-Beschleunigung (Betrag) in g
static float lsm6ReadG() {
    uint8_t d[6];
    if (!rawI2C(ADDR_LSM6DSO32, 0x28, d, 6)) return -1.0f;   // OUTX_L_A..OUTZ_H_A (auto-increment)
    int16_t ax=(int16_t)(d[0]|(d[1]<<8)), ay=(int16_t)(d[2]|(d[3]<<8)), az=(int16_t)(d[4]|(d[5]<<8));
    const float S = 0.488f/1000.0f;   // +-16 g: 0.488 mg/LSB -> g
    float gx=ax*S, gy=ay*S, gz=az*S;
    return sqrtf(gx*gx + gy*gy + gz*gz);
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
        float raw_spd = gps.speed.kmph();
        live.speed = (raw_spd < 3.0f) ? 0 : raw_spd;  // GPS-Rauschen filtern
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

    // GPS: 9600 Baud (Diagnose bewiesen: ok=72 bei 9600, ok=0 bei 38400)
    Serial2.begin(9600, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    Serial.println("[GPS] 9600 Baud");

    // Wire freigeben → epdiy
    Wire.end();
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(1600);
    // Kein epd_clear() — Splash ueberschreibt direkt, spart 1-2 Schwarzblitze
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    // FANET LoRa NACH epdiy (beide nutzen gpio_install_isr_service)
    fanet.init();

    // SD-Karte (geteilter SPI mit LoRa, CS=12)
    sdcard.init();

    // Ticket C: Geraete-Identitaet (MAC + NVS-Token) laden. Registrierung spaeter im Loop bei WLAN.
    deviceInit();

    // K2: letzte bekannte Position aus NVS -> Karten-Fallback ohne GPS-Fix (statt 0,0/Default).
    { double la, lo; if (deviceLoadLastPos(&la, &lo)) { lastGoodLat = la; lastGoodLon = lo;
        Serial.printf("[MAP] letzte Position aus NVS: %.5f,%.5f\n", la, lo); } }

    // Luftraeume von SD parsen (wenn vorhanden)
    if (sdcard.ok && sdcard.exists("/airspace/ch_asp.txt")) {
        parseOpenAir("/airspace/ch_asp.txt");
    }

    // Karten-Pack: zuletzt heruntergeladenes (active.txt) laden — ECHTES Server-Pack, kein Demo.
    parseCenterLat = lastGoodLat; parseCenterLon = lastGoodLon;   // Fenster um letzte/Test-Position
    if (sdcard.ok) {
        char activePath[64] = {0};
        File af = SD.open("/maps/active.txt", FILE_READ);
        if (af) { String s = af.readStringUntil('\n'); s.trim();
                  strncpy(activePath, s.c_str(), sizeof(activePath)-1); af.close(); }
        if (activePath[0] && SD.exists(activePath))  parsePack(activePath);
        else if (sdcard.exists("/peaks/peaks.txt"))   parsePeaks("/peaks/peaks.txt");
    }

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

    // LSM6DSO32 Beschleunigung in Betrieb nehmen (raw I2C, nach Wire.end)
    lsm_ok = lsm6Init();

    // Touch init (GT911 ueber raw I2C)
    touch.init();

    // Flugbuch von SD laden (persistent — keine Demo-Fluege mehr)
    flugbuch.load(&sdcard);

    // Kalman init
    kf.init(live.altitude);

    // Splash → Cruise → nach 15s Thermik-Demo
    showBootSplash(&hl, AURA_VERSION);  // Einziger GC16 beim Boot (Graustufen-Logo)
    delay(4000);
    showCruiseScreen(&hl, live, MODE_DU);  // Kein zweiter Flash
    Serial.println("READY — Cruise aktiv, Thermik-Demo in 15s");
}

// === LOOP ===
static float vario_sum=0;
static int vario_count=0;
static unsigned long vario_window=0;

void loop() {
    feedGPS();
    fanet.poll();
    updateClock();

    // BLE Notifications (1x pro Sekunde)
    static unsigned long lastBle = 0;
    if (ble.ok && millis() - lastBle > 1000) {
        lastBle = millis();
        ble.update(live.altitude, live.vario, live.speed, live.heading,
                   lastGoodLat, lastGoodLon, live.sats, live.bat_pct,
                   live.gps_fix, flight.state == FLIGHT_FLYING, fanet.ok);
    }

    // FANET TX: nur im Flug senden (alle 5s)
    // Am Boden: kein TX, nur RX (Duty-Cycle schonen)
    static unsigned long lastFanetTx = 0;
    if (fanet.ok && flight.state == FLIGHT_FLYING && millis() - lastFanetTx > 5000) {
        lastFanetTx = millis();
        if (lastGoodLat != 0) {
            fanet.sendTracking(lastGoodLat, lastGoodLon, live.altitude,
                               live.vario, live.speed, live.heading, 1);
        }
    }

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

    // LSM6 Beschleunigung (G) — aktuell + Spitze im Flug
    if (lsm_ok) {
        float g = lsm6ReadG();
        if (g >= 0) {
            g_now = g;
            if (flight.state == FLIGHT_FLYING && g > g_max_flight) g_max_flight = g;
        }
    }

    // SHT45 raw read (alle 5s)
    static unsigned long lastSHT=0;
    if (millis()-lastSHT > 5000) {
        lastSHT = millis();
        float t,rh;
        if (rawSHT45(&t,&rh)) {
            live.temp = t - 3.8f;
            live.dewpoint = live.temp - (100.0f-rh)/5.0f;
        }
    }

    delay(20);  // 50Hz Loop (war 20Hz) — Touch reaktiver

    // Contract-Cross-Read einmalig ~6s nach Boot (Serial dann stabil, nicht in der Reenum-Luecke)
    deviceLoop();      // Ticket C: bei WLAN einmalig registrieren falls kein NVS-Token

    // K5: Tile-Fenster nachladen, wenn Position > 7 km vom geladenen Zentrum (Karte folgt Bewegung)
    static unsigned long lastReloadChk = 0;
    if (!panActive && mapLoadedPack[0] && millis() - lastReloadChk > 3000) {
        lastReloadChk = millis();
        double dkmLat = (lastGoodLat - mapParsedLat)*111.0;
        double dkmLon = (lastGoodLon - mapParsedLon)*111.0*cos(lastGoodLat*M_PI/180.0);
        if (dkmLat*dkmLat + dkmLon*dkmLon > 7.0*7.0) {
            parseCenterLat = lastGoodLat; parseCenterLon = lastGoodLon;
            digitalWrite(BOARD_LORA_CS, HIGH);
            parsePack(mapLoadedPack);
            Serial.println("[MAP] Tile-Fenster nachgeladen (Bewegung)");
        }
    }

    static bool contractDumped = false;
    if (!contractDumped && millis() > 6000) {
        contractDumped = true;
        fanet.selfTestTx();   // Ticket D: TX-Encoder byte-genau (kein Funk). Live-TX bleibt gated.
        Serial.printf("[DEV] (boot) MAC=%s token=%s status=%s\n",
                      deviceMac, deviceHasToken() ? "JA(NVS)" : "KEINER",
                      deviceStatus[0] ? deviceStatus : "-");
        Serial.printf("[DIAG] ch_v1=%d hoernli_v2=%d  peaks=%d contours=%d  center=%.4f,%.4f\n",
                      (int)SD.exists("/maps/region_ch_v1.pack"),
                      (int)SD.exists("/maps/region_hoernli_v2.pack"),
                      peak_count, contour_count, packCenterLat, packCenterLon);
        if (sdcard.ok) {
            if (sdcard.exists("/maps/contract_test_v1.pack"))     dumpPackContract("/maps/contract_test_v1.pack");
            if (sdcard.exists("/maps/contract_bad_magic.pack"))   dumpPackContract("/maps/contract_bad_magic.pack");
            if (sdcard.exists("/maps/contract_bad_version.pack")) dumpPackContract("/maps/contract_bad_version.pack");
            if (sdcard.exists("/maps/contract_bad_truncated.pack")) dumpPackContract("/maps/contract_bad_truncated.pack");
        }
    }

    // Serial alle 5s
    if (millis()-lastPrint >= 5000) {
        lastPrint = millis();
        Serial.printf("[%02d:%02d] V=%+.1f Alt=%.0f GPS:%s s=%d G=%.2f ok=%lu fail=%lu bytes=%lu\n",
                       live.rtc_hour, live.rtc_min,
                       live.vario, live.altitude,
                       live.gps_fix?"FIX":"---", live.sats, g_now,
                       gps.passedChecksum(), gps.failedChecksum(),
                       gps_total_bytes);
    }

    // === FLUG-ERKENNUNG ===
    flight.update(live.speed, live.vario, live.altitude, live.gps_fix, live.sats);

    // Im Flug: IGC-Punkt loggen (intern auf ~2s gedrosselt)
    if (flight.state == FLIGHT_FLYING)
        igc.logPoint(&sdcard, gps, live.altitude, live.vario);

    // Start erkannt → IGC-Datei oeffnen + Meldung auf Display
    if (flight.justStarted()) {
        igc.start(&sdcard, gps, live.altitude, "Ivo Eichenberger", "Paraglider");
        g_max_flight = 0;   // G-Spitze fuer diesen Flug zuruecksetzen
        uint8_t *fb = epd_hl_get_framebuffer(&hl);
        epd_hl_set_all_white(&hl);
        // "START" oben gross, "ERKANNT" darunter, alles zentriert
        drawHCenter(&ArialBold72, "START",    0, 960, 220, fb);
        drawHCenter(&ArialBold72, "ERKANNT",  0, 960, 310, fb);
        drawHCenter(&ArialBold24, "KIE Engineering wuenscht Dir", 0, 960, 390, fb);
        drawHCenter(&ArialBold24, "einen schoenen Flug",          0, 960, 425, fb);
        epd_poweron();
        epd_hl_update_screen(&hl, MODE_DU, (int)epd_ambient_temperature());
        epd_poweroff();
        delay(3000);
        currentScreen = SCR_CRUISE;
        showCruiseScreen(&hl, live, MODE_DU);
        lastDisplay = millis();
    }

    // Landung erkannt → Flug speichern + Lande-Screen
    if (flight.state == FLIGHT_LANDED && currentScreen != SCR_LANDING) {
        igc.end();   // IGC-Datei abschliessen, Statistik steht bereit

        // Flug-Record aus ECHTEN Werten (GPS-Datum, IGC-Statistik)
        FlightRecord rec = {};
        if (gps.date.isValid()) { rec.year = gps.date.year(); rec.month = gps.date.month(); rec.day = gps.date.day(); }
        else { rec.year = 2026; rec.month = 1; rec.day = 1; }
        rec.hour = live.rtc_hour; rec.minute = live.rtc_min;
        rec.duration_sec   = flight.flightDurationSec();
        rec.max_alt        = (int16_t)igc.max_alt;
        rec.start_alt      = (int16_t)igc.start_alt;
        rec.max_climb      = (int16_t)(igc.max_climb * 100.0f);   // m/s ×100
        rec.max_g          = (int16_t)(g_max_flight * 100.0f);   // LSM6 ×100 (z.B. 320 = 3.2g)
        rec.track_dist_m   = (uint32_t)igc.track_dist_m;
        rec.straight_dist_m= (uint32_t)igc.straight_dist_m;
        rec.valid = true;
        flugbuch.addFlight(rec);
        flugbuch.save(&sdcard);                                   // FEST auf SD persistieren
        Serial.printf("[FLUG] Gespeichert: %dmin, max %dm, Spur %.1fkm\n",
                       rec.duration_sec/60, rec.max_alt, igc.track_dist_m/1000.0);

        currentScreen = SCR_LANDING;
        showLandingScreen(&hl, live.altitude, live.rtc_hour, live.rtc_min);
        lastDisplay = millis();
    }

    // === SCREEN-MANAGER (Swipe + Button + Long-Tap=Menu) ===
    Gesture g = touch.poll();

    if (currentScreen == SCR_OVERLAY) {
        if (g == GEST_TAP) {
            if (overlayScreen.handleTap(touch.lastX(), touch.lastY(), &hl)) {
                currentScreen = SCR_MENU;
                showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_MENU;
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
        }
    } else if (currentScreen == SCR_BLE) {
        if (g == GEST_TAP) {
            if (bleScreen.handleTap(touch.lastX(), touch.lastY(), &hl)) {
                currentScreen = SCR_FUNK;
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, ble.ok,
                               fanet.ok, fanet.pilot_count, ble.ok ? ble.pin : 0);
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_FUNK;
            showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, ble.ok,
                           fanet.ok, fanet.pilot_count, ble.ok ? ble.pin : 0);
        }
    } else if (currentScreen == SCR_FUNK) {
        if (g == GEST_TAP) {
            FunkItem fi = checkFunkTap(touch.lastX(), touch.lastY());
            if (fi == FUNK_WLAN) {
                currentScreen = SCR_WIFI;
                wifiScreen.begin(&sdcard);
                wifiScreen.doScan(&hl);  // Sofort scannen
                Serial.println("[FUNK] → WLAN Scan");
            } else if (fi == FUNK_BLE) {
                currentScreen = SCR_BLE;
                bleScreen.begin(&ble, &sdcard);
                bleScreen.draw(&hl);
                Serial.println("[FUNK] → BLE Screen");
            } else if (fi == FUNK_FANET) {
                // Ticket D: sicherer Boden-TX-Test — EIN Frame pro Druck, aktuelle Position.
                // Sicherung: nur am Boden + mit Fix; live geht nur bei FANET_TX_ENABLED==1 raus.
                char msg[56];
                if (flight.state == FLIGHT_FLYING) {
                    snprintf(msg, sizeof(msg), "TX-Test nur am Boden (im Flug: Auto-TX)");
                } else if (!live.gps_fix || lastGoodLat == 0) {
                    snprintf(msg, sizeof(msg), "FANET TX-Test: kein GPS-Fix");
                } else {
                    fanet.sendTracking(lastGoodLat, lastGoodLon, live.altitude, live.vario,
                                       live.speed, live.heading, 1);
#if FANET_TX_ENABLED
                    snprintf(msg, sizeof(msg), "FANET TX gesendet: %.4f %.4f %.0fm",
                             lastGoodLat, lastGoodLon, live.altitude);
#else
                    snprintf(msg, sizeof(msg), "FANET TX GESPERRT (Flag=0) - Frame im Log");
#endif
                }
                Serial.printf("[FUNK] %s\n", msg);
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, ble.ok,
                               fanet.ok, fanet.pilot_count, ble.ok ? ble.pin : 0, msg);
            } else if (fi == FUNK_BACK) {
                currentScreen = SCR_MENU;
                showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_MENU;
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
        }
    } else if (currentScreen == SCR_WIFI) {
        if (g == GEST_TAP) {
            if (wifiScreen.handleTap(touch.lastX(), touch.lastY(), &hl)) {
                // Zurueck zum Funk-Menu
                currentScreen = SCR_FUNK;
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, false,
                               fanet.ok, fanet.pilot_count);
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_FUNK;
            showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, false,
                           fanet.ok, fanet.pilot_count);
        }
    } else if (currentScreen == SCR_FLUGBUCH) {
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
                currentScreen = SCR_MENU;
                showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count);
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
            } else if (mi == MENU_FUNK) {
                currentScreen = SCR_FUNK;
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, false,
                               fanet.ok, fanet.pilot_count);
                Serial.println("[MENU] Funk");
            } else if (mi == MENU_KARTE) {
                currentScreen = SCR_OVERLAY;
                overlayScreen.begin(&sdcard);
                overlayScreen.draw(&hl);
                Serial.println("[MENU] Karte Overlays");
            } else if (mi == MENU_AUS) {
                Serial.println("[MENU] Ausschalten → Credits");
                showCreditsAndShutdown(&hl);  // Kommt nicht zurueck
            }
        } else if (g == GEST_SWIPE_LEFT || g == GEST_SWIPE_RIGHT) {
            currentScreen = SCR_CRUISE;
            showCruiseScreen(&hl, live, MODE_DU);
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
                showCruiseScreen(&hl, live, MODE_DU);
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
            // Karussell: Cruise → Thermal → Goal → Map → Cruise
            Screen prevScr = currentScreen;
            if (currentScreen==SCR_CRUISE) currentScreen = SCR_THERMAL;
            else if (currentScreen==SCR_THERMAL) currentScreen = SCR_GOAL;
            else if (currentScreen==SCR_GOAL) currentScreen = SCR_MAP;
            else if (currentScreen==SCR_MAP) currentScreen = SCR_XSECTION;
            else currentScreen = SCR_CRUISE;
            Serial.printf("[SWIPE] → %d\n", currentScreen);
            bool fromMap = (prevScr == SCR_MAP || prevScr == SCR_XSECTION);  // schweren Screen verlassen -> GC16
            if (currentScreen==SCR_CRUISE) {
                showCruiseScreen(&hl, live, fromMap ? MODE_GC16 : MODE_DU);
            } else if (currentScreen==SCR_THERMAL) {
                if (!thermal.active) thermal.start(live.altitude);
                showThermalScreen(&hl, thermal.data, fromMap ? MODE_GC16 : MODE_DU);
            } else if (currentScreen==SCR_GOAL) {
                updateGoalData(); showGoalScreen(&hl, goalLive, fromMap ? MODE_GC16 : MODE_DU);
            } else if (currentScreen==SCR_MAP) {
                MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,
                            live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,
                            fanet.pilot_count,false,live.gps_fix};
                showMapScreen(&hl, md);
            } else if (currentScreen==SCR_XSECTION) {
                XSectionData xd={lastGoodLat,lastGoodLon,live.altitude,live.heading,live.speed,
                                 goalLive.gr_current,live.rtc_hour,live.rtc_min,live.sats,
                                 live.bat_pct,fanet.pilot_count,live.gps_fix};
                showXSectionScreen(&hl, xd);
            }
            lastDisplay = millis();
        } else if (g == GEST_TAP) {
            // Map-Touch-Handler
            if (currentScreen==SCR_MAP) {
                MapAction ma = checkMapTap(touch.lastX(), touch.lastY());
                if (ma==MAP_ZOOM_IN && mapZoomIdx>0) {
                    mapZoomIdx--;
                    MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,
                                live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,
                                fanet.pilot_count,false,live.gps_fix};
                    showMapScreen(&hl, md);
                } else if (ma==MAP_ZOOM_OUT && mapZoomIdx<6) {
                    mapZoomIdx++;
                    MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,
                                live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,
                                fanet.pilot_count,false,live.gps_fix};
                    showMapScreen(&hl, md);
                } else if (ma==MAP_PAN) {
                    // Tap-Position -> lat/lon (Umkehrung der Projektion) -> Karte dorthin verschieben
                    double clat = panActive ? panLat : lastGoodLat;
                    double clon = panActive ? panLon : lastGoodLon;
                    float mpp = ZOOM_M[mapZoomIdx] / (float)MAP_CLIP_W;
                    float dx_m = (touch.lastX() - MAP_PILOT_X) * mpp;
                    float dy_m = (MAP_PILOT_Y - touch.lastY()) * mpp;
                    panLon = clon + dx_m / (111320.0f * cosf((float)clat * M_PI/180.0f));
                    panLat = clat + dy_m / 111320.0f;
                    panActive = true;
                    double dkLat=(panLat-mapParsedLat)*111.0, dkLon=(panLon-mapParsedLon)*111.0*cos(panLat*M_PI/180.0);
                    if (mapLoadedPack[0] && dkLat*dkLat+dkLon*dkLon > 5.0*5.0) {  // weit -> Tiles nachladen
                        parseCenterLat=panLat; parseCenterLon=panLon;
                        digitalWrite(BOARD_LORA_CS, HIGH); parsePack(mapLoadedPack);
                    }
                    MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,
                                live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,
                                fanet.pilot_count,false,live.gps_fix};
                    showMapScreen(&hl, md);
                } else if (ma==MAP_RECENTER) {
                    panActive = false;   // zurueck auf GPS/letzte Position
                    double dkLat=(lastGoodLat-mapParsedLat)*111.0, dkLon=(lastGoodLon-mapParsedLon)*111.0*cos(lastGoodLat*M_PI/180.0);
                    if (mapLoadedPack[0] && dkLat*dkLat+dkLon*dkLon > 5.0*5.0) {
                        parseCenterLat=lastGoodLat; parseCenterLon=lastGoodLon;
                        digitalWrite(BOARD_LORA_CS, HIGH); parsePack(mapLoadedPack);
                    }
                    MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,
                                live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,
                                fanet.pilot_count,false,live.gps_fix};
                    showMapScreen(&hl, md);
                }
            }
            Serial.printf("[TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
        } else if (g == GEST_LONG_TAP) {
            Serial.printf("[LONG TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
            bool fromMapM = (currentScreen == SCR_MAP);  // aus Karte -> GC16 gegen Ghosting
            currentScreen = SCR_MENU;
            Serial.println("[MENU] Geoeffnet");
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count, fromMapM ? MODE_GC16 : MODE_DU);
            lastDisplay = millis();
        }
    }

    // BOOT-Button = Flug-Screen wechseln
    static bool btn_last = true;
    bool btn_now = digitalRead(0);
    if (!btn_now && btn_last && currentScreen != SCR_MENU && currentScreen != SCR_LANDING) {
        Screen prevScrB = currentScreen;
        if (currentScreen==SCR_CRUISE) currentScreen=SCR_THERMAL;
        else if (currentScreen==SCR_THERMAL) currentScreen=SCR_GOAL;
        else if (currentScreen==SCR_GOAL) currentScreen=SCR_MAP;
        else if (currentScreen==SCR_MAP) currentScreen=SCR_XSECTION;
        else currentScreen=SCR_CRUISE;
        bool fromMapB = (prevScrB==SCR_MAP || prevScrB==SCR_XSECTION);
        if (currentScreen==SCR_CRUISE) showCruiseScreen(&hl,live,fromMapB?MODE_GC16:MODE_DU);
        else if (currentScreen==SCR_THERMAL) { if(!thermal.active)thermal.start(live.altitude); showThermalScreen(&hl,thermal.data,fromMapB?MODE_GC16:MODE_DU); }
        else if (currentScreen==SCR_GOAL) { updateGoalData(); showGoalScreen(&hl,goalLive,fromMapB?MODE_GC16:MODE_DU); }
        else if (currentScreen==SCR_MAP) { MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,fanet.pilot_count,false,live.gps_fix}; showMapScreen(&hl,md); }
        else if (currentScreen==SCR_XSECTION) { XSectionData xd={lastGoodLat,lastGoodLon,live.altitude,live.heading,live.speed,goalLive.gr_current,live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,fanet.pilot_count,live.gps_fix}; showXSectionScreen(&hl,xd); }
        lastDisplay = millis();
        delay(300);
    }
    btn_last = btn_now;

    // Thermal-Manager updaten (immer, auch am Boden)
    float gps_lat = gps.location.isValid() ? gps.location.lat() : 0;
    float gps_lon = gps.location.isValid() ? gps.location.lng() : 0;
    if (thermal.active) {
        thermal.update(gps_lat, gps_lon, live.heading, live.speed,
                       live.vario, live.vario_avg, live.altitude,
                       live.temp, live.dewpoint, live.bat_pct,
                       live.rtc_hour, live.rtc_min);
    }

    // Auto-Thermik bei Steigen
    static unsigned long climb_since = 0;
    if (live.vario_avg > 0.5f && flight.state == FLIGHT_FLYING) {
        if (!climb_since) climb_since = millis();
        if (millis()-climb_since > 10000 && currentScreen==SCR_CRUISE) {
            if (!thermal.active) thermal.start(live.altitude);
            currentScreen = SCR_THERMAL;
            showThermalScreen(&hl, thermal.data, MODE_DU);
            lastDisplay = millis();
        }
    } else { climb_since = 0; }

    // 1 Hz Display Refresh (Flug-Screens — KARTE NICHT, sonst Ghosting durch DU-Overlay)
    if ((currentScreen==SCR_CRUISE || currentScreen==SCR_THERMAL ||
         currentScreen==SCR_GOAL)
        && millis()-lastDisplay >= 1000) {
        lastDisplay = millis();
        if (currentScreen==SCR_CRUISE) showCruiseScreen(&hl, live, MODE_DU);
        else if (currentScreen==SCR_THERMAL) showThermalScreen(&hl, thermal.data, MODE_DU);
        else if (currentScreen==SCR_GOAL) { updateGoalData(); showGoalScreen(&hl, goalLive, MODE_DU); }
    }
    // === KARTE: KEIN 1-Hz-Overlay (Ticket §5: kein Partial-Geschiebe -> kein Ghosting). ===
    // Karte wird sauber per GC16 nur bei Eintritt / Zoom / Re-Center gezeichnet (Touch-Handler oben).
}
