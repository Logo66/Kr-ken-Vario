#pragma once
// sd_manager.h — SD-Karte via SPI (CS=12, geteilter Bus mit LoRa CS=46)
// FAT32, Verzeichnis /airspace/ fuer OpenAir, /obstacles/ fuer Hindernisse
#include <SPI.h>
#include <SD.h>
#include "pins.h"

class SDManager {
public:
    bool ok = false;

    bool init() {
        // LoRa CS HIGH halten waehrend SD-Zugriff
        pinMode(BOARD_LORA_CS, OUTPUT);
        digitalWrite(BOARD_LORA_CS, HIGH);
        pinMode(BOARD_SD_CS, OUTPUT);
        digitalWrite(BOARD_SD_CS, HIGH);

        // SD.begin nutzt den bereits initialisierten SPI-Bus
        if (!SD.begin(BOARD_SD_CS)) {
            Serial.println("[SD] Init FAIL");
            return false;
        }

        uint64_t total = SD.totalBytes() / (1024*1024);
        uint64_t used = SD.usedBytes() / (1024*1024);
        Serial.printf("[SD] OK — %llu MB total, %llu MB belegt\n", total, used);

        // Verzeichnisse anlegen
        if (!SD.exists("/airspace")) SD.mkdir("/airspace");
        if (!SD.exists("/obstacles")) SD.mkdir("/obstacles");
        if (!SD.exists("/igc")) SD.mkdir("/igc");
        if (!SD.exists("/tasks")) SD.mkdir("/tasks");   // M3: BLE-empfangene Flugplaene/Tasks

        ok = true;
        return true;
    }

    // Datei schreiben (fuer Downloads)
    bool writeFile(const char *path, const uint8_t *data, size_t len) {
        if (!ok) return false;
        digitalWrite(BOARD_LORA_CS, HIGH);  // LoRa deselektieren
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            Serial.printf("[SD] writeFile FAIL: %s\n", path);
            return false;
        }
        f.write(data, len);
        f.close();
        Serial.printf("[SD] geschrieben: %s (%d bytes)\n", path, len);
        return true;
    }

    // Datei streamen (fuer grosse Downloads) — FILE_WRITE = neu/ueberschreiben
    File openWrite(const char *path) {
        if (!ok) return File();
        digitalWrite(BOARD_LORA_CS, HIGH);
        return SD.open(path, FILE_WRITE);
    }

    // Anhaengen (fuer IGC-B-Records) — FILE_APPEND, kuerzt NICHT
    File openAppend(const char *path) {
        if (!ok) return File();
        digitalWrite(BOARD_LORA_CS, HIGH);
        return SD.open(path, FILE_APPEND);
    }

    // Datei lesen
    File openRead(const char *path) {
        if (!ok) return File();
        digitalWrite(BOARD_LORA_CS, HIGH);
        return SD.open(path, FILE_READ);
    }

    bool exists(const char *path) {
        if (!ok) return false;
        return SD.exists(path);
    }

    // Dateien in Verzeichnis auflisten
    int listDir(const char *dir, char names[][64], int maxFiles) {
        if (!ok) return 0;
        File root = SD.open(dir);
        if (!root || !root.isDirectory()) return 0;
        int count = 0;
        File entry = root.openNextFile();
        while (entry && count < maxFiles) {
            if (!entry.isDirectory()) {
                strncpy(names[count], entry.name(), 63);
                names[count][63] = 0;
                count++;
            }
            entry = root.openNextFile();
        }
        return count;
    }
};
