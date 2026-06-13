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
#include "sound_settings.h"
#include "sound_screen.h"
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

// === M4: aktiver Task (Flugplan) — Wegpunkt-Navigation ===
struct TaskWP { char name[24]; double lat, lon; float alt; int radius; };
static TaskWP g_task[24];
static int    g_taskCount = 0, g_taskIdx = 0;

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
#include "igc_server.h"
#include "imu_logger.h"   // Roh-IMU mitloggen fuer den Testflug (HEILIG: nur Logging, nicht im Vario)
#include "wind_estimator.h"   // Windschaetzung aus GPS-Kreisdrift (nur Anzeige/BLE, nicht im Vario)
#include "buzzer.h"           // Arduino Modulino Buzzer (I2C 0x1E)
#include <ArduinoJson.h>      // M2/M3: BLE-Konfig/Task-JSON
#include "config_model.h"     // M1: ein versioniertes NVS-JSON-Settings-Modell
#include "vario_sound.h"      // Steigton ueber den Buzzer (vom Vario gesteuert)
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
static int g_avgWindowSec = 20;   // AVG-Fenster (s) — gemeinsam Cruise+Thermik, per App konfigurierbar (vario.avg_window_s)
static uint8_t g_fanetAircraft = 1;   // FANET-Flugzeugtyp (1=Gleitschirm) — per App (fanet.aircraft)
static char    g_fanetPilotName[32] = "";   // FANET-Name-Beacon (fanet.pilot_name)
static int g_windTest = -1;           // Windpfeil-Bench-Test: -1=aus, sonst Test-Richtung in Grad (0/45/.../315)
static bool  g_warnAirspace = true;   // #3: Luftraum-Warnung an/aus (warn.airspace)
static float g_warnBufV = 150.0f;     // #3: vertikaler Puffer in m (warn.buffer_v)
static int   g_aspWarnIdx = -1, g_aspWarnPrev = -1;   // aktueller/voriger Luftraum (Eintritts-Erkennung)
static WindEstimator windEst;   // Wind aus Kreisdrift -> live.wind_speed/wind_dir + BLE
static VarioSound    varioSound; // Steigton-Zustand fuer den Buzzer
static unsigned long lastPrint=0, lastDisplay=0;

// Screen-Manager
enum Screen { SCR_CRUISE, SCR_THERMAL, SCR_GOAL, SCR_MAP, SCR_XSECTION, SCR_MENU, SCR_LANDING, SCR_QNH, SCR_FLUGBUCH, SCR_FUNK, SCR_WIFI, SCR_OVERLAY, SCR_BLE, SCR_SOUND };
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
    // M4: aktiver Task -> Wegpunkt-Index an den Namen haengen
    static char g_wpLabel[40];
    if (g_taskCount > 0) { snprintf(g_wpLabel, 40, "%s  %d/%d", activeWP.name, g_taskIdx+1, g_taskCount); goalLive.wp_name = g_wpLabel; }
    goalLive.rtc_hour = live.rtc_hour;
    goalLive.rtc_min = live.rtc_min;
    goalLive.sats = live.sats;
    goalLive.bat_pct = live.bat_pct;
    goalLive.fanet_peers = fanet.pilot_count;
    goalLive.buddy_connected = false;
    goalLive.buddy_hint = NULL;
}

// === M4: Task laden (SD /tasks/) + Wegpunkt-Navigation ===
static bool taskLoad(const char *name) {
    char path[80]; snprintf(path, sizeof(path), "/tasks/%s.json", name);
    File f = sdcard.openRead(path);
    if (!f) { Serial.printf("[TASK] %s nicht gefunden\n", path); return false; }
    JsonDocument doc; DeserializationError e = deserializeJson(doc, f); f.close();
    if (e) { Serial.println("[TASK] JSON-Fehler"); return false; }
    g_taskCount = 0;
    for (JsonObject w : doc["waypoints"].as<JsonArray>()) {
        if (g_taskCount >= 24) break;
        TaskWP &t = g_task[g_taskCount];
        const char* wn = w["name"].as<const char*>(); strncpy(t.name, (wn&&*wn)?wn:"WP", 23); t.name[23]=0;
        t.lat = w["lat"].as<double>(); t.lon = w["lon"].as<double>();
        t.alt = w["alt"].as<float>(); int r = w["radius"].as<int>(); t.radius = (r>0)?r:400;
        g_taskCount++;
    }
    g_taskIdx = 0;
    Serial.printf("[TASK] aktiv: %s (%d WP)\n", name, g_taskCount);
    return g_taskCount > 0;
}
// aktiven Wegpunkt in activeWP spiegeln + bei Erreichen (im Flug, im Radius) weiterschalten
static void taskTick() {
    if (g_taskCount == 0) return;
    TaskWP &t = g_task[g_taskIdx];
    activeWP.name = t.name; activeWP.lat = t.lat; activeWP.lon = t.lon; activeWP.alt = t.alt;
    if (flight.state == FLIGHT_FLYING && live.gps_fix && lastGoodLat != 0 && g_taskIdx < g_taskCount-1) {
        if (haversineDist(lastGoodLat, lastGoodLon, t.lat, t.lon) <= t.radius) {
            g_taskIdx++; Serial.printf("[TASK] WP erreicht -> %d/%d\n", g_taskIdx+1, g_taskCount);
        }
    }
}

// === #3: Luftraum-Warnung — warn-Config laden + Innen-Check (nur im Flug) ===
static void warnLoad() {
    JsonVariant wa = g_model["warn"]["airspace"]; g_warnAirspace = wa.isNull() ? true : wa.as<bool>();
    int bv = g_model["warn"]["buffer_v"].as<int>(); if (bv > 0) g_warnBufV = (float)bv;
}
static void aspWarnTick() {
    g_aspWarnIdx = -1;
    if (!g_warnAirspace || airspace_count == 0 || flight.state != FLIGHT_FLYING || !live.gps_fix || lastGoodLat == 0) return;
    for (int a = 0; a < airspace_count; a++) {
        Airspace& asp = airspaces[a];
        if (!asp.active || asp.num_pts < 3) continue;
        if (!aspContains(asp, lastGoodLat, lastGoodLon)) continue;
        if (live.altitude >= aspAltM(asp.lower) - g_warnBufV && live.altitude <= aspAltM(asp.upper) + g_warnBufV) { g_aspWarnIdx = a; break; }
    }
}

// === Ton-Menue: Zustand + Helfer zum Neuzeichnen des Flug-Screens =============
static Screen        soundReturnScreen = SCR_CRUISE;   // wohin nach dem Schliessen
static unsigned long soundLastActivity = 0;            // fuer Auto-Close (5 s)
static void drawFlightScreen(Screen s, enum EpdDrawMode mode) {
    if (s==SCR_CRUISE) showCruiseScreen(&hl, live, mode);
    else if (s==SCR_THERMAL) { if(!thermal.active) thermal.start(live.altitude); showThermalScreen(&hl, thermal.data, mode); }
    else if (s==SCR_GOAL) { updateGoalData(); showGoalScreen(&hl, goalLive, mode); }
    else if (s==SCR_MAP) { MapData md={live.heading,lastGoodLat,lastGoodLon,live.altitude,live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,fanet.pilot_count,false,live.gps_fix}; showMapScreen(&hl, md); }
    else if (s==SCR_XSECTION) { XSectionData xd={lastGoodLat,lastGoodLon,live.altitude,live.heading,live.speed,goalLive.gr_current,live.rtc_hour,live.rtc_min,live.sats,live.bat_pct,fanet.pilot_count,live.gps_fix}; showXSectionScreen(&hl, xd); }
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
        // Wind aus der Kreisdrift schaetzen (NUR Anzeige/BLE, fliesst NICHT ins Vario)
        windEst.update(gps.course.deg(), raw_spd, live.gps_fix);
        live.wind_speed = windEst.wind_speed;
        live.wind_dir   = windEst.wind_dir;
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
        live.humidity = h.relative_humidity;
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
    if (lsm_ok) imuLogConfig();   // Gyro + FIFO @104Hz fuer Roh-IMU-Logging (NICHT im Vario)

    // Touch init (GT911 ueber raw I2C)
    touch.init();
    soundSettingsLoad();   // Ton-Einstellungen (Lautstaerke/Stumm) aus NVS

    // BLE-Schalter mit Gedaechtnis: Zustand aus /ble.cfg laden, bei "AN" automatisch starten
    // (gilt fuer Kaltstart UND Aufwachen aus dem Tiefschlaf — setup() laeuft dabei neu).
    bleScreen.begin(&ble, &sdcard);
    if (bleScreen.enabled) {
        ble.pin = atoi(bleScreen.pin_str);   // PIN VOR init
        ble.init(bleScreen.name);
        Serial.println("[BLE] Auto-Start beim Boot (Schalter stand auf AN)");
    } else {
        Serial.println("[BLE] aus (Schalter stand auf AUS)");
    }

    // M1: EIN Settings-Modell (NVS-JSON) laden/erzeugen — Migration der Altpfade (Ton/BLE/QNH)
    modelInit(g_sound.volume, bleScreen.name, (uint32_t)atoi(bleScreen.pin_str), bleScreen.enabled, alt_calc.getQNH()/100.0f);
    int _aw = g_model["vario"]["avg_window_s"].as<int>();                    // AVG-Fenster aus dem Modell
    g_avgWindowSec = (_aw >= 1 && _aw <= 120) ? _aw : 20;                    // fehlt/ungueltig -> Default 20
    g_fanetTxEnabled = g_model["fanet"]["tx_enabled"].as<bool>();            // FANET-TX-Arm aus dem Modell (Doppel-Sicherung)
    { int ac = g_model["fanet"]["aircraft"].as<int>(); if (ac>=1 && ac<=7) g_fanetAircraft=(uint8_t)ac; }
    { JsonVariant ot = g_model["fanet"]["online_tracking"]; g_fanetOnline = ot.isNull() ? true : ot.as<bool>(); }
    { const char* pn = g_model["fanet"]["pilot_name"].as<const char*>(); if (pn) { strncpy(g_fanetPilotName, pn, 31); g_fanetPilotName[31]=0; } }
    unitsLoad();                                                            // #1: Anzeige-Einheiten aus dem Modell
    backlight_on = g_model["display"]["backlight"].as<bool>(); digitalWrite(11, backlight_on?HIGH:LOW);  // Backlight-Zustand aus dem Modell
    { File af = sdcard.openRead("/tasks/active.txt"); if (af) { String tn=af.readStringUntil('\n'); af.close(); tn.trim(); if (tn.length()) taskLoad(tn.c_str()); } }  // M4: aktiven Task laden
    warnLoad();   // #3: Luftraum-Warn-Config aus dem Modell

    // Flugbuch von SD laden (persistent — keine Demo-Fluege mehr)
    flugbuch.load(&sdcard);

    // Kalman init
    kf.init(live.altitude);

    // Splash → Cruise → nach 15s Thermik-Demo
    showBootSplash(&hl, AURA_VERSION);  // Einziger GC16 beim Boot (Graustufen-Logo)
    buzzerStartup();                    // Start-Jingle spielt, WAEHREND das Logo steht -> beide gleich lang
    showCruiseScreen(&hl, live, MODE_DU);  // Kein zweiter Flash
    Serial.println("READY — Cruise aktiv, Thermik-Demo in 15s");
}

// === LOOP ===
static float vario_sum=0;
static int vario_count=0;
static unsigned long vario_window=0;

// === M2: ein Settings-Key/Value anwenden (dotted-path wie KONFIG-VERTRAG Teil 2). Fuellt ack. ===
static bool applySettingKV(const char *k, JsonVariant v, char *ack, size_t alen) {
    bool ok = true; const char *err = "";
    if      (!strcmp(k,"sound.volume"))          { int x=v.as<int>(); if(x<0||x>SND_VOL_MAX){ok=false;err="range";} else {g_sound.volume=(uint8_t)x; soundSettingsSave();} }
    else if (!strcmp(k,"sound.muted"))           { bool mu=v.as<bool>(); g_sound.volume = mu?0:(g_sound.volume?g_sound.volume:3); soundSettingsSave(); }
    else if (!strcmp(k,"vario.climb_threshold")) { g_sound.climb_threshold=v.as<float>(); soundSettingsSave(); }
    else if (!strcmp(k,"vario.sink_alarm"))      { g_sound.sink_alarm=v.as<float>(); soundSettingsSave(); }
    else if (!strcmp(k,"vario.deadband"))        { g_sound.deadband=v.as<float>(); soundSettingsSave(); }
    else if (!strcmp(k,"vario.tone_curve"))      { g_sound.tone_curve=(uint8_t)v.as<int>(); soundSettingsSave(); }
    else if (!strcmp(k,"vario.avg_window_s"))    { int w=v.as<int>(); if(w<1||w>120){ok=false;err="range";} else g_avgWindowSec=w; }
    else if (!strcmp(k,"alt.qnh"))               { float q=v.as<float>(); if(q<800.0f||q>1100.0f){ok=false;err="range";} else alt_calc.setQNH(q); }
    // ble.* werden persistiert und greifen beim naechsten Neustart (kein Live-Reinit -> aktive Verbindung bleibt)
    else if (!strcmp(k,"ble.name"))              { const char* s=v.as<const char*>(); if(!s){ok=false;err="type";} else {strncpy(bleScreen.name,s,31);bleScreen.name[31]=0;bleScreen.saveConfig();} }
    else if (!strcmp(k,"ble.pin"))               { int p=v.as<int>(); if(p<0||p>9999){ok=false;err="range";} else {snprintf(bleScreen.pin_str,sizeof(bleScreen.pin_str),"%04d",p); bleScreen.saveConfig();} }
    else if (!strcmp(k,"ble.enabled"))           { bleScreen.enabled=v.as<bool>(); bleScreen.saveConfig(); }
    else if (!strcmp(k,"fanet.tx_enabled"))      { g_fanetTxEnabled = v.as<bool>(); }                  // Arm-Flag (Gate D bleibt zusaetzlich noetig + nur im Flug)
    else if (!strcmp(k,"fanet.aircraft"))        { int ac=v.as<int>(); if(ac<1||ac>7){ok=false;err="range";} else g_fanetAircraft=(uint8_t)ac; }
    else if (!strcmp(k,"fanet.online_tracking")) { g_fanetOnline = v.as<bool>(); }
    else if (!strcmp(k,"fanet.pilot_name"))      { const char* s=v.as<const char*>(); if(s){strncpy(g_fanetPilotName,s,31);g_fanetPilotName[31]=0;} }
    else if (!strcmp(k,"display.backlight"))     { backlight_on = v.as<bool>(); digitalWrite(11, backlight_on?HIGH:LOW); }
    else if (!modelKeyKnown(k))                  { ok=false; err="unknown_key"; }   // nicht im Vertrag -> ablehnen
    // andere Vertrags-Keys (units/display/pilot/fanet/map/log/warn/buddy): nur ins Modell (Live-Wirkung folgt)
    if (ok) modelSet(k, v);                       // EINE Wahrheit: ins NVS-JSON-Modell (+ updated_at)
    if (ok && !strncmp(k, "units.", 6)) unitsLoad();   // Einheiten sofort live uebernehmen
    if (ok && !strncmp(k, "warn.", 5))  warnLoad();    // Luftraum-Warn-Config sofort uebernehmen
    if (ok) snprintf(ack, alen, "{\"ack\":\"settings\",\"k\":\"%s\",\"ok\":true}", k);
    else    snprintf(ack, alen, "{\"ack\":\"settings\",\"k\":\"%s\",\"ok\":false,\"err\":\"%s\"}", k, err);
    Serial.printf("[CFG] settings %s -> %s\n", k, ok?"ok":err);
    return ok;
}

// === M3: Task (Flugplan) per Chunks empfangen, CRC32 pruefen, auf SD /tasks/ ablegen ===
static uint32_t cfgCrc32(const uint8_t *p, size_t n) {            // Standard IEEE/zlib CRC32
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i=0;i<n;i++) { c ^= p[i]; for (int k=0;k<8;k++) c = (c & 1) ? (c>>1) ^ 0xEDB88820u : (c>>1); }
    return ~c;
}
static void cfgSanitize(const char *in, char *out, size_t olen) { // sicherer Dateiname (kein Pfad-Trick)
    size_t j=0;
    for (size_t i=0; in[i] && j+1<olen; i++) {
        char ch=in[i];
        if ((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_') out[j++]=ch;
        else if (ch==' ') out[j++]='_';
    }
    if (j==0) { strncpy(out,"task",olen); out[olen-1]=0; return; }
    out[j]=0;
}
static char     _taskBuf[4096];     // Reassembly-Puffer (Vertrag: max 4 KB)
static size_t   _taskLen = 0;
static uint32_t _taskCrc = 0;
static bool     _taskActive = false;

static void handleTaskChunk(JsonDocument &doc) {
    int i = doc["i"] | -1;
    int n = doc["n"] | 0;
    const char *d = doc["d"] | "";
    if (i < 0 || n <= 0) { ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"proto\"}"); return; }
    if (i == 0) { _taskLen = 0; _taskCrc = doc["crc"].as<uint32_t>(); _taskActive = true; }  // crc nur in i:0
    if (!_taskActive) { ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"no_start\"}"); return; }
    size_t dl = strlen(d);
    if (_taskLen + dl >= sizeof(_taskBuf)) { _taskActive=false; ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"too_big\"}"); return; }
    memcpy(_taskBuf + _taskLen, d, dl); _taskLen += dl;
    if (i != n - 1) return;                                       // noch nicht der letzte Chunk

    _taskActive = false;
    _taskBuf[_taskLen] = 0;
    uint32_t crc = cfgCrc32((const uint8_t*)_taskBuf, _taskLen);
    if (crc != _taskCrc) { Serial.printf("[CFG] task CRC %08X != %08X\n",(unsigned)crc,(unsigned)_taskCrc); ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"crc\"}"); return; }
    JsonDocument td;
    if (deserializeJson(td, _taskBuf, _taskLen)) { ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"json\"}"); return; }
    const char *name = td["name"] | "task";
    int wp = td["waypoints"].size();                             // 0 falls kein Array
    char safe[48]; cfgSanitize(name, safe, sizeof(safe));
    char path[80]; snprintf(path, sizeof(path), "/tasks/%s.json", safe);
    if (!sdcard.ok) { ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"no_sd\"}"); return; }
    File f = sdcard.openWrite(path);
    if (!f) { ble.notifyCfg("{\"ack\":\"task\",\"ok\":false,\"err\":\"sd_write\"}"); return; }
    f.write((const uint8_t*)_taskBuf, _taskLen); f.close();
    { File af = sdcard.openWrite("/tasks/active.txt"); if(af){ af.println(safe); af.close(); } } taskLoad(safe);  // M4: hochgeladenen Task aktiv setzen
    char ack[120]; snprintf(ack, sizeof(ack), "{\"ack\":\"task\",\"name\":\"%s\",\"wp\":%d,\"ok\":true}", safe, wp);
    Serial.printf("[CFG] task gespeichert: %s (%d WP, %u Byte)\n", path, wp, (unsigned)_taskLen);
    ble.notifyCfg(ack);
}

// Eingehende BLE-Schreibnachricht verarbeiten (kind:settings = M2, kind:task = M3).
static void processConfigWrite(const uint8_t *data, size_t len) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, data, len);
    if (e) { ble.notifyCfg("{\"ack\":\"error\",\"ok\":false,\"err\":\"json\"}"); Serial.println("[CFG] JSON-Fehler"); return; }
    const char *kind = doc["kind"] | "";
    if (!strcmp(kind, "settings")) {
        char ack[160];
        applySettingKV(doc["k"] | "", doc["v"], ack, sizeof(ack));
        ble.notifyCfg(ack);
    } else if (!strcmp(kind, "task")) {
        handleTaskChunk(doc);
    } else {
        ble.notifyCfg("{\"ack\":\"error\",\"ok\":false,\"err\":\"unknown_kind\"}");
    }
}

void loop() {
    feedGPS();
    if (g_windTest >= 0 && flight.state != FLIGHT_FLYING) { live.wind_speed = 12.0f; live.wind_dir = (float)g_windTest; }  // Windpfeil-Bench-Test haelt die Test-Richtung
    fanet.poll();
    updateClock();

    // BLE Notifications — Vario ~10 Hz, GPS 1 Hz, Umwelt 1x/Minute
    static unsigned long lastBleVario=0, lastBleGps=0, lastBleEnv=0;
    static bool blePrevConn = false;
    if (ble.ok) {
        if (ble.connected && !blePrevConn) { lastBleVario=0; lastBleGps=0; lastBleEnv=0; }  // frisch verbunden -> sofort senden
        blePrevConn = ble.connected;
        unsigned long now = millis();
        if (now - lastBleVario >= 100) {     // Vario/Hoehe ~10 Hz (fluessig fuers App-Vario)
            lastBleVario = now;
            ble.update(live.altitude, live.vario, live.speed, live.heading,
                       live.sats, live.bat_pct, live.gps_fix,
                       flight.state == FLIGHT_FLYING, fanet.ok);
        }
        if (now - lastBleGps >= 1000) {      // GPS 1 Hz (GPS-Takt)
            lastBleGps = now;
            ble.updateGps(lastGoodLat, lastGoodLon, live.gps_fix);
        }
        if (now - lastBleEnv >= 60000) {     // Umwelt + Wind 1x/Minute
            lastBleEnv = now;
            ble.updateEnv(live.temp, live.humidity, live.dewpoint, thermal.data.base_est,
                          live.wind_speed, live.wind_dir);
        }
    }

    // FANET TX: nur im Flug senden (alle 5s)
    // Am Boden: kein TX, nur RX (Duty-Cycle schonen)
    static unsigned long lastFanetTx = 0;
    if (fanet.ok && flight.state == FLIGHT_FLYING && millis() - lastFanetTx > 5000) {
        lastFanetTx = millis();
        if (lastGoodLat != 0) {
            fanet.sendTracking(lastGoodLat, lastGoodLon, live.altitude,
                               live.vario, live.speed, live.heading, g_fanetAircraft);
        }
    }
    static unsigned long lastFanetName = 0;   // #4: Namens-Beacon ~alle 60s im Flug
    if (fanet.ok && flight.state == FLIGHT_FLYING && g_fanetPilotName[0] && millis()-lastFanetName > 60000) {
        lastFanetName = millis(); fanet.sendName(g_fanetPilotName);
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
        live.avg_seconds = g_avgWindowSec;                                  // Cruise zeigt das echte Fenster
        if (millis()-vario_window > (unsigned long)g_avgWindowSec*1000UL) {
            live.vario_avg = vario_sum / fmaxf(1,vario_count);
            vario_sum=0; vario_count=0; vario_window=millis();
        }
    }

    // === VARIO-STEIGTON (Modulino-Buzzer) — NUR im Flug (erkannter Start), sonst still ===
    bool flyingNow = (flight.state == FLIGHT_FLYING);
    varioSound.update(live.vario, soundMuted() || !flyingNow, g_sound.climb_threshold, g_sound.sink_alarm);

    // LSM6 Beschleunigung (G) — aktuell + Spitze im Flug
    if (lsm_ok) {
        float g = lsm6ReadG();
        if (g >= 0) {
            g_now = g;
            if (flight.state == FLIGHT_FLYING && g > g_max_flight) g_max_flight = g;
        }
    }

    // === Roh-IMU MITLOGGEN (Testflug-Daten; HEILIG: nur loggen, NICHTS ins Vario) ===
    if (lsm_ok) {
        if (!imuOpen && live.gps_fix && gps.date.isValid()) {   // Log startet bei GPS-Fix (am Startplatz)
            char p[48];
            snprintf(p, sizeof(p), "/imu/%04d%02d%02d_%02d%02d.csv",
                     gps.date.year(), gps.date.month(), gps.date.day(),
                     gps.time.hour(), gps.time.minute());
            imuLogStart(p);
        }
        imuLogTick(live.altitude, live.speed, (flight.state == FLIGHT_FLYING) ? 1 : 0);
    }

    // SHT45 raw read (alle 5s)
    static unsigned long lastSHT=0;
    if (millis()-lastSHT > 5000) {
        lastSHT = millis();
        float t,rh;
        if (rawSHT45(&t,&rh)) {
            live.temp = t - 3.8f;
            live.humidity = rh;
            live.dewpoint = live.temp - (100.0f-rh)/5.0f;
        }
    }

    // M2/M3: eingehende BLE-Konfig/Task-Writes abarbeiten (Queue leeren, alle pro Loop)
    { uint8_t cbuf[256]; int cn;
      while ((cn = ble.takeCfgIn(cbuf, sizeof(cbuf))) >= 0) processConfigWrite(cbuf, (size_t)cn);
    }

    delay(20);  // 50Hz Loop (war 20Hz) — Touch reaktiver

    // Contract-Cross-Read einmalig ~6s nach Boot (Serial dann stabil, nicht in der Reenum-Luecke)
    igcServerLoop();   // WLAN-Webserver fuer IGC-Download (laeuft nur wenn WLAN verbunden)
    if (!ble.connected) deviceLoop();  // Buddy-Heartbeat (blockierendes TLS) NICHT waehrend aktiver BLE-Session
                                       // -> keine Radio-Koexistenz-Stoerung -> kein BLE-Disconnect/Bond-Abbruch

    // Einheitliche Statusleiste 1x pro Loop fuellen (alle Screens lesen denselben Zustand):
    // Uhr | Sat | FANET | Buddy | Batterie  (Buddy-Kreis = Verbindung zum Buddy-Server)
    statusBarSet(live.rtc_hour, live.rtc_min, live.sats, fanet.pilot_count,
                 /*Buddy-Server-Verbindung*/ deviceServerOk, live.bat_pct,
                 /*BLE-Client verbunden*/ ble.connected,
                 /*WLAN verbunden*/ WiFi.status() == WL_CONNECTED);

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

        // === SENSOR-CHECK (Loetbruecke -> BMP-Adresse 0x46/0x47? Accel/Gyro ok?) ===
        Serial.println("[SCAN] === I2C-Bus ===");
        for (uint8_t a=0x08; a<0x78; a++) {
            uint8_t d=0;
            if (i2c_master_write_to_device(I2C_NUM_0, a, &d, 1, pdMS_TO_TICKS(10)) == ESP_OK)
                Serial.printf("[SCAN]   gefunden 0x%02X\n", a);
        }
        { uint8_t id;
          if (rawI2C(0x47, 0x01, &id, 1)) Serial.printf("[BMP581] 0x47 CHIP_ID=0x%02X (soll 0x50)\n", id);
          else                            Serial.println("[BMP581] 0x47 KEINE Antwort");
          if (rawI2C(0x46, 0x01, &id, 1)) Serial.printf("[BMP581] 0x46 CHIP_ID=0x%02X (soll 0x50)  <- Loetbruecke-Adresse!\n", id);
          else                            Serial.println("[BMP581] 0x46 keine Antwort");
          if (rawI2C(0x6A, 0x0F, &id, 1)) Serial.printf("[LSM6]   0x6A WHO_AM_I=0x%02X (soll 0x6C)\n", id);
          else                            Serial.println("[LSM6]   0x6A KEINE Antwort (Accel/Gyro defekt?)");
          if (rawI2C(0x14, 0x00, &id, 1)) Serial.printf("[BMM350] 0x14 ID=0x%02X (Kompass)\n", id);
          else                            Serial.println("[BMM350] 0x14 keine Antwort");
        }
        // Buzzer-Beweis kommt jetzt als Start-Jingle beim Logo (setup) — nicht mehr hier.

        fanet.selfTestTx();   // Ticket D: TX-Encoder byte-genau (kein Funk). Live-TX bleibt gated.
        Serial.printf("[DEV] (boot) MAC=%s token=%s status=%s\n",
                      deviceMac, deviceHasToken() ? "JA(NVS)" : "KEINER",
                      deviceStatus[0] ? deviceStatus : "-");
        Serial.printf("[DIAG] ch_v1=%d hoernli_v2=%d  peaks=%d contours=%d  center=%.4f,%.4f\n",
                      (int)SD.exists("/maps/region_ch_v1.pack"),
                      (int)SD.exists("/maps/region_hoernli_v2.pack"),
                      peak_count, contour_count, packCenterLat, packCenterLon);
        // T1-Selbsttest (Ticket Terrain-Schnitt): Geländehöhe an Zentrum + 2/5 km Nord plausibel?
        {
            float te0 = terrainElevAt(parseCenterLat, parseCenterLon);
            float te2 = terrainElevAt(parseCenterLat + 2.0/111.0, parseCenterLon);
            float te5 = terrainElevAt(parseCenterLat + 5.0/111.0, parseCenterLon);
            Serial.printf("[XSEC] Terrain @0/+2/+5km = %.0f / %.0f / %.0f m (Niederneunforn-Gegend ~400-700)\n",
                          te0, te2, te5);
        }
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
        // Gyro-Plausibilitaet (Gate): im Stand a_z~9.8, g~0; dreht bei Drehung
        Serial.printf("[IMU] a=%.2f,%.2f,%.2f m/s2  g=%.1f,%.1f,%.1f dps  log=%s\n",
                       imuAx, imuAy, imuAz, imuGx, imuGy, imuGz, imuOpen ? "AN" : "aus");
    }

    // === FLUG-ERKENNUNG ===
    flight.update(live.speed, live.vario, live.altitude, live.gps_fix, live.sats);
    taskTick();   // M4: aktiven Wegpunkt spiegeln + bei Erreichen weiterschalten
    aspWarnTick();   // #3: Luftraum-Innen-Check (nur im Flug)
    if (g_aspWarnIdx >= 0 && g_aspWarnIdx != g_aspWarnPrev) {   // Eintritt -> Alarm (Buzzer + 2.5s Warn-Screen)
        Airspace& a = airspaces[g_aspWarnIdx];
        Serial.printf("[WARN] LUFTRAUM: %s %s (%s-%s)\n", airspaceClassStr(a.cls), a.name, a.lower, a.upper);
        for (int b=0;b<3;b++){ buzzerTone(2600,160); delay(220); }
        uint8_t* wfb = epd_hl_get_framebuffer(&hl); epd_hl_set_all_white(&hl); char wb[64];
        drawHCenter(&ArialBold40, "!! LUFTRAUM !!", 0, 960, 175, wfb);
        drawHCenter(&ArialBold28, a.name, 0, 960, 255, wfb);
        snprintf(wb,64,"%s    %s - %s", airspaceClassStr(a.cls), a.lower, a.upper);
        drawHCenter(&ArialBold28, wb, 0, 960, 320, wfb);
        epd_poweron(); epd_hl_update_screen(&hl, MODE_DU, (int)epd_ambient_temperature()); epd_poweroff();
        delay(2500);
        drawFlightScreen(currentScreen, MODE_DU);
    }
    g_aspWarnPrev = g_aspWarnIdx;

    // Im Flug: IGC-Punkt loggen (intern auf ~2s gedrosselt)
    if (flight.state == FLIGHT_FLYING)
        igc.logPoint(&sdcard, gps, live.altitude, live.vario);

    // Start erkannt → IGC-Datei oeffnen + Meldung auf Display
    if (flight.justStarted()) {
        { const char* pn = g_model["pilot"]["name"].as<const char*>();
          const char* pg = g_model["pilot"]["glider"].as<const char*>();
          igc.start(&sdcard, gps, live.altitude, (pn&&*pn)?pn:"Pilot", (pg&&*pg)?pg:"Paraglider"); }
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
                                       live.speed, live.heading, g_fanetAircraft);
#if FANET_TX_ENABLED
                    if (g_fanetTxEnabled) snprintf(msg, sizeof(msg), "FANET TX gesendet: %.4f %.4f", lastGoodLat, lastGoodLon);
                    else                  snprintf(msg, sizeof(msg), "FANET bereit - tx_enabled=AUS (in App scharf schalten)");
#else
                    snprintf(msg, sizeof(msg), "FANET TX GESPERRT (Gate D) - Frame im Log");
#endif
                }
                Serial.printf("[FUNK] %s\n", msg);
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, ble.ok,
                               fanet.ok, fanet.pilot_count, ble.ok ? ble.pin : 0, msg);
            } else if (fi == FUNK_FANET_TX) {
                g_fanetTxEnabled = !g_fanetTxEnabled;                 // TX scharf/aus (Gate D ist offen)
                { JsonDocument t; t["v"] = g_fanetTxEnabled; modelSet("fanet.tx_enabled", t["v"]); }   // persistieren
                char msg2[56];
                snprintf(msg2, sizeof(msg2), g_fanetTxEnabled ? "FANET TX SCHARF - sendet IM FLUG!" : "FANET TX aus");
                Serial.printf("[FUNK] %s\n", msg2);
                showFunkScreen(&hl, WiFi.status()==WL_CONNECTED, ble.ok,
                               fanet.ok, fanet.pilot_count, ble.ok ? ble.pin : 0, msg2);
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
                { JsonDocument t; t["v"]=backlight_on; modelSet("display.backlight", t["v"]); }   // persistieren
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
                char m[56]; snprintf(m, sizeof(m), "RIDE bitte %s %.4f,%.4f", g_fanetPilotName[0]?g_fanetPilotName:"Pilot", lastGoodLat, lastGoodLon);
                bool sent = fanet.sendMessage(m); buzzerTone(2400,150); delay(180); buzzerTone(2400,150);
                Serial.printf("[LAND] Ride -> FANET %s: %s\n", sent?"gesendet":"(TX aus - Funk scharf schalten)", m);
            } else if (lc == LAND_HELP) {
                g_fanetTxEnabled = true;   // #4: SOS armt FANET im Notfall (Gate D muss offen sein)
                char m[56]; snprintf(m, sizeof(m), "SOS HILFE %s %.4f,%.4f", g_fanetPilotName[0]?g_fanetPilotName:"Pilot", lastGoodLat, lastGoodLon);
                bool sent = fanet.sendMessage(m); for(int b=0;b<3;b++){buzzerTone(2800,180);delay(220);}
                Serial.printf("[LAND] SOS -> FANET %s: %s\n", sent?"gesendet":"(Gate D zu)", m);
            }
        }
    } else if (currentScreen == SCR_SOUND) {
        // Flug-Ton-Menue: Tipp = Lautstaerke weiter, Lang = OK/schliessen, Auto-Close nach 5 s
        if (g == GEST_TAP) {
            uint16_t note = soundPianoFreqAt(touch.lastX(), touch.lastY());
            if (note) {
                buzzerTone(note, 300);                       // Klaviertaste spielen — kein Redraw (responsiv)
                soundLastActivity = millis();
            } else {
                g_sound.volume = (uint8_t)((g_sound.volume + 1) % (SND_VOL_MAX + 1));  // ausserhalb = Lautstaerke (0..5 Umlauf)
                if (g_sound.volume > 0) buzzerTone(1200, 120);
                soundLastActivity = millis();
                showSoundScreen(&hl, MODE_DU);               // nur bei Lautstaerke-Aenderung neu zeichnen
            }
        } else if (g == GEST_LONG_TAP || g == GEST_HOME || g == GEST_HOME_LONG) {  // Home-Knopf schliesst auch
            soundSettingsSave();
            currentScreen = soundReturnScreen;
            drawFlightScreen(currentScreen, MODE_GC16);
            lastDisplay = millis();
            Serial.println("[SOUND] geschlossen (OK)");
        } else if (millis() - soundLastActivity > 5000) {  // Auto-Close
            soundSettingsSave();
            currentScreen = soundReturnScreen;
            drawFlightScreen(currentScreen, MODE_GC16);
            lastDisplay = millis();
            Serial.println("[SOUND] Auto-Close");
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
            else if (currentScreen==SCR_CRUISE && flight.state != FLIGHT_FLYING
                     && touch.lastX()>=310 && touch.lastX()<=628 && touch.lastY()>=288 && touch.lastY()<=405) {
                // Windpfeil-Bench-Test: aufs Wind-Feld tippen -> Test-Richtung 0/45/.../315/aus durchsteppen
                g_windTest = (g_windTest < 0) ? 0 : g_windTest + 45;
                if (g_windTest >= 360) g_windTest = -1;
                live.wind_speed = (g_windTest>=0) ? 12.0f : 0.0f;
                live.wind_dir   = (g_windTest>=0) ? (float)g_windTest : 0.0f;
                showCruiseScreen(&hl, live, MODE_DU);
                Serial.printf("[WINDTEST] dir=%d\n", g_windTest);
            }
            Serial.printf("[TAP] x=%d y=%d\n", touch.lastX(), touch.lastY());
        // Screen-Langdruck oeffnet das Menue NICHT mehr — Menue NUR ueber den Kapazitiv-Knopf (Ivo).
        } else if (g == GEST_HOME) {
            // Autonomer kapazitiver Knopf UNTER dem Screen: KURZ -> Ton-Menue
            soundReturnScreen = currentScreen;
            currentScreen = SCR_SOUND;
            soundLastActivity = millis();
            showSoundScreen(&hl, MODE_GC16);
            Serial.println("[SOUND] Ton-Menue geoeffnet (Home-Knopf kurz)");
        } else if (g == GEST_HOME_LONG) {
            // ... und LANG -> Hauptmenue (alle Menues an einem Ort, Ivo)
            bool fromMapL = (currentScreen == SCR_MAP);
            currentScreen = SCR_MENU;
            Serial.println("[MENU] Geoeffnet (Home-Knopf lang)");
            showMenuScreen(&hl, alt_calc.getQNH()/100.0f, backlight_on, flugbuch.count, fromMapL ? MODE_GC16 : MODE_DU);
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
        thermal.data.avg_seconds = g_avgWindowSec;                          // Thermik zeigt dasselbe Fenster
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
