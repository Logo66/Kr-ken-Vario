#pragma once
// touch.h — GT911 Touch + Swipe-Erkennung via raw I2C
// GT911: I2C 0x5D, 16-bit Register, INT auf GPIO 3
// Nutzt epdiy's I2C_NUM_0 Driver (kein Wire)
#include <Arduino.h>
#include "driver/i2c.h"

// GT911 Register (16-bit Adressen)
#define GT911_ADDR        0x5D
#define GT911_STATUS_REG  0x814E
#define GT911_POINT1_REG  0x8150
#define GT911_INT_PIN     3

// Gesten
enum Gesture { GEST_NONE, GEST_TAP, GEST_SWIPE_LEFT, GEST_SWIPE_RIGHT,
               GEST_SWIPE_UP, GEST_SWIPE_DOWN, GEST_LONG_TAP, GEST_HOME, GEST_HOME_LONG };

class TouchManager {
public:
    void init() {
        pinMode(GT911_INT_PIN, INPUT);
        _ready = false;
        // Test-Read: GT911 erreichbar?
        uint8_t id[4];
        if (gt911Read(0x8140, id, 4)) {
            Serial.printf("[TOUCH] GT911 ID: %c%c%c%c\n", id[0],id[1],id[2],id[3]);
            _ready = true;
        } else {
            Serial.println("[TOUCH] GT911 nicht erreichbar");
        }
    }

    // Im Loop aufrufen — erkennt Gesten
    Gesture poll() {
        if (!_ready) return GEST_NONE;
        if (_homeWas && millis() - _homeT0 > 5000) _homeWas = false;  // Home-Key Auto-Release (Sicherung)

        // Status lesen
        uint8_t status = 0;
        if (!gt911Read(GT911_STATUS_REG, &status, 1)) return GEST_NONE;

        // === HOME-KEY: autonomer kapazitiver Knopf UNTER dem Screen (nicht der x/y-Touchbereich) ===
        // GT911 meldet ihn im Status-Register 0x814E, Bit 4 (0x10) = HAVE_KEY. WICHTIG: beim Halten wird das
        // Register nach dem Clear oft "still" (0x00) -> Lang-Druck ZEITBASIERT auswerten, VOR dem 0x80-Gate.
        // KURZER Druck (<0.8s) -> GEST_HOME (Ton-Menue) · LANGER Druck (>=0.8s) -> GEST_HOME_LONG (Hauptmenue).
        bool homeNow = (status & 0x10);
        if (homeNow && !_homeWas) {                 // Druck: Timer starten (noch nicht ausloesen)
            _homeWas = true; _homeFired = false; _homeT0 = millis();
            Serial.println("[HOMEKEY] DOWN");
            gt911Clear();
            return GEST_NONE;
        }
        if (_homeWas && !_homeFired && millis() - _homeT0 >= 800) {  // gehalten >=0.8s (zeitbasiert) -> Hauptmenue
            _homeFired = true;
            Serial.println("[HOMEKEY] LANG -> GEST_HOME_LONG");
            gt911Clear();
            return GEST_HOME_LONG;
        }
        if (_homeWas && (status & 0x80) && !homeNow) {  // frisches Event ohne Key-Bit = Loslassen
            _homeWas = false;
            unsigned long hdt = millis() - _homeT0;
            gt911Clear();
            if (!_homeFired) {                       // kurzer Druck -> Ton-Menue
                Serial.printf("[HOMEKEY] KURZ %lums -> GEST_HOME\n", hdt);
                return GEST_HOME;
            }
            return GEST_NONE;                        // langer Druck schon ausgeloest
        }

        if (!(status & 0x80)) return GEST_NONE;  // ab hier nur Touch-Punkte (Home-Key oben erledigt)
        int touches = status & 0x0F;

        if (touches > 0 && !_touching) {
            // Touch-Down
            uint8_t pt[4];
            gt911Read(GT911_POINT1_REG, pt, 4);
            _x0 = pt[0] | (pt[1] << 8);
            _y0 = pt[2] | (pt[3] << 8);
            _t0 = millis();
            _touching = true;
        } else if (touches == 0 && _touching) {
            // Touch-Up → Geste auswerten
            _touching = false;
            unsigned long dt = millis() - _t0;
            _lastDt = dt;                          // Haltedauer der letzten Geste (fuer Ton-Menue-Schwelle)

            // Display-Koordinaten (transformiert)
            int dx = _ylast - _y0;         // GT911 Y-Diff → Display X-Diff
            int dy = -(_xlast - _x0);      // GT911 X-Diff → Display Y-Diff (inv.)

            Gesture g = GEST_NONE;
            if (dt > 800) {
                g = GEST_LONG_TAP;
            } else if (abs(dx) > 35 || abs(dy) > 35) {  // Schwelle gesenkt (war 60)
                // Swipe
                if (abs(dx) > abs(dy)) {
                    g = (dx > 0) ? GEST_SWIPE_RIGHT : GEST_SWIPE_LEFT;
                } else {
                    g = (dy > 0) ? GEST_SWIPE_DOWN : GEST_SWIPE_UP;
                }
            } else if (dt < 300) {
                g = GEST_TAP;
            }

            // Status clearen
            gt911Clear();
            return g;
        }

        // Position tracken waehrend Touch
        if (touches > 0) {
            uint8_t pt[4];
            gt911Read(GT911_POINT1_REG, pt, 4);
            _xlast = pt[0] | (pt[1] << 8);
            _ylast = pt[2] | (pt[3] << 8);
        }

        // Status clearen
        gt911Clear();
        return GEST_NONE;
    }

    bool ready() { return _ready; }
    unsigned long lastDt() { return _lastDt; }   // Haltedauer der zuletzt erkannten Geste (ms)

    // GT911 meldet Portrait (540x960), Display ist Landscape (960x540)
    // Transform: display_x = gt911_y, display_y = 540 - gt911_x
    int lastX() { return _ylast; }          // GT911 Y → Display X
    int lastY() { return 540 - _xlast; }    // GT911 X → Display Y (invertiert)

private:
    bool _ready = false;
    bool _touching = false;
    int _x0=0, _y0=0, _xlast=0, _ylast=0;
    unsigned long _t0=0;
    unsigned long _lastDt=0;
    bool _homeWas=false;            // Home-Key Druck-Zustand (Flankenerkennung)
    bool _homeFired=false;          // langer Druck bereits ausgeloest?
    unsigned long _homeT0=0;

    bool gt911Read(uint16_t reg, uint8_t *buf, size_t len) {
        uint8_t regbuf[2] = {(uint8_t)(reg>>8), (uint8_t)(reg&0xFF)};
        return i2c_master_write_read_device(I2C_NUM_0, GT911_ADDR,
                   regbuf, 2, buf, len, pdMS_TO_TICKS(50)) == ESP_OK;
    }

    void gt911Clear() {
        uint8_t data[3] = {0x81, 0x4E, 0x00};
        i2c_master_write_to_device(I2C_NUM_0, GT911_ADDR,
                                    data, 3, pdMS_TO_TICKS(50));
    }
};
