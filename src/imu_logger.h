#pragma once
// imu_logger.h — Roh-IMU + Baro fuer den TESTFLUG mitloggen (Tuning-Daten fuer die spaetere Fusion).
//
// HEILIG (Architekt-Ticket): NUR lesen + auf SD loggen. NICHTS ins Vario einrechnen.
//   Das bewaehrte Baro-Vario bleibt voellig unangetastet. Keine Lage, kein inertiales Kalman.
//
// Quelle: LSM6DSO32 FIFO @104 Hz (gleichmaessige dt, NICHT "wann der Loop grad mag").
//   Accel bleibt +-16 g (wie lsm6Init in main.cpp). Gyro NEU +-500 dps (lag bisher brach).
// Ziel:  CSV /imu/<datum>.csv  ·  Spalten: t_ms,ax,ay,az,gx,gy,gz,baro_alt,gps_kmh,flying
//   ax..az in m/s² · gx..gz in dps · baro_alt in m · gps_kmh · flying 0/1 (Boden vs. Flug trennbar)
// Schreiben gepuffert (~3.5 KB) + flush ~1 s, damit die SD-Last den Flug-Loop / IGC nicht stoert.
#include <SD.h>
#include "driver/i2c.h"

#define IMU_ADDR          0x6A   // LSM6DSO32
#define IMU_CTRL2_G       0x11   // Gyro-Konfig
#define IMU_FIFO_CTRL3    0x09   // BDR Gyro/Accel
#define IMU_FIFO_CTRL4    0x0A   // FIFO-Mode
#define IMU_FIFO_STATUS1  0x3A   // DIFF_FIFO (ungelesene Woerter)
#define IMU_FIFO_DATA     0x78   // FIFO-Tag + X/Y/Z

static const float IMU_ACC_SCALE = 0.488e-3f * 9.80665f;  // LSB -> m/s²  (+-16 g)
static const float IMU_GYR_SCALE = 17.5e-3f;              // LSB -> dps   (+-500 dps)

// Letzte Werte — Diagnose (immer aktuell, auch ohne offenes Log -> Gate "Gyro plausibel")
static float imuAx=0, imuAy=0, imuAz=0, imuGx=0, imuGy=0, imuGz=0;

static File          imuFile;
static bool          imuOpen = false;
static char          imuBuf[4096];
static int           imuBufLen = 0;
static uint32_t      imuSampleIdx = 0;
static unsigned long imuLastFlush = 0;

static bool imuWrite(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_0, IMU_ADDR, b, 2, pdMS_TO_TICKS(20)) == ESP_OK;
}
static bool imuRead(uint8_t reg, uint8_t *buf, size_t n) {
    return i2c_master_write_read_device(I2C_NUM_0, IMU_ADDR, &reg, 1, buf, n, pdMS_TO_TICKS(20)) == ESP_OK;
}

// Gyro einschalten + FIFO (Accel+Gyro @104 Hz batchen). Accel-Datenregister (max-G) bleiben unberuehrt.
static void imuLogConfig() {
    imuWrite(IMU_CTRL2_G,    0x44);  // Gyro: ODR 104 Hz, FS +-500 dps
    imuWrite(IMU_FIFO_CTRL3, 0x44);  // BDR Gyro + Accel je 104 Hz in die FIFO
    imuWrite(IMU_FIFO_CTRL4, 0x06);  // FIFO Continuous (Stream) Mode
    Serial.println("[IMU] Gyro + FIFO @104Hz aktiv (nur Logging, NICHT im Vario)");
}

static void imuLogFlush(bool force) {
    if (!imuOpen) return;
    if (imuBufLen > 0 && (force || imuBufLen > 3500)) {
        imuFile.write((const uint8_t*)imuBuf, imuBufLen);
        imuBufLen = 0;
    }
    if (force || millis() - imuLastFlush > 1000) { imuFile.flush(); imuLastFlush = millis(); }
}

static void imuLogStart(const char *path) {
    if (imuOpen) return;
    if (!SD.exists("/imu")) SD.mkdir("/imu");
    imuFile = SD.open(path, FILE_WRITE);
    if (!imuFile) { Serial.printf("[IMU] Log-Datei %s FEHLER\n", path); return; }
    imuFile.print("t_ms,ax,ay,az,gx,gy,gz,baro_alt,gps_kmh,flying\n");
    imuOpen = true; imuBufLen = 0; imuSampleIdx = 0; imuLastFlush = millis();
    Serial.printf("[IMU] Logging -> %s\n", path);
}

// Jeden Loop aufrufen: FIFO leeren (gleichmaessige dt), Diagnose aktualisieren,
// und — wenn ein Log offen ist — CSV-Zeilen puffern. Drained IMMER (auch ohne Log) gegen Overflow.
static void imuLogTick(float baro_alt, float gps_kmh, int flying) {
    uint8_t st[2];
    if (!imuRead(IMU_FIFO_STATUS1, st, 2)) return;
    int count = ((st[1] & 0x03) << 8) | st[0];     // DIFF_FIFO[9:0]
    if (count <= 0) return;
    if (count > 64) count = 64;                    // pro Tick begrenzen (I2C-Zeit beschraenken)
    static int16_t ax=0,ay=0,az=0,gx=0,gy=0,gz=0; static bool hA=false,hG=false;
    for (int i=0;i<count;i++) {
        uint8_t w[7]; if (!imuRead(IMU_FIFO_DATA, w, 7)) break;
        uint8_t tag = w[0] >> 3;                                   // 0x02=Accel, 0x01=Gyro
        int16_t x=(int16_t)(w[1]|(w[2]<<8)), y=(int16_t)(w[3]|(w[4]<<8)), z=(int16_t)(w[5]|(w[6]<<8));
        if      (tag==0x02){ ax=x; ay=y; az=z; hA=true; }
        else if (tag==0x01){ gx=x; gy=y; gz=z; hG=true; }
        if (hA && hG) {                                            // ein Accel+Gyro-Paar -> eine Zeile
            imuAx=ax*IMU_ACC_SCALE; imuAy=ay*IMU_ACC_SCALE; imuAz=az*IMU_ACC_SCALE;
            imuGx=gx*IMU_GYR_SCALE; imuGy=gy*IMU_GYR_SCALE; imuGz=gz*IMU_GYR_SCALE;
            if (imuOpen) {
                uint32_t t = imuSampleIdx*1000UL/104UL;            // gleichmaessige dt @104 Hz
                int avail = (int)sizeof(imuBuf) - imuBufLen;
                int n = snprintf(imuBuf+imuBufLen, avail,
                    "%lu,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.1f,%.1f,%d\n",
                    (unsigned long)t, imuAx,imuAy,imuAz, imuGx,imuGy,imuGz, baro_alt, gps_kmh, flying);
                if (n>0 && n<avail) imuBufLen += n;
                imuSampleIdx++;
                if (imuBufLen > 3500) imuLogFlush(false);
            }
            hA=false; hG=false;
        }
    }
    imuLogFlush(false);
}
