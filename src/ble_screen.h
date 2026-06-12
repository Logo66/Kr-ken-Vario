#pragma once
// ble_screen.h — BLE Einstellungen: Name, PIN, AN/AUS
#include "ui_utils.h"
#include "keyboard.h"
#include "ble_manager.h"
#include "sd_manager.h"

enum BleScreenState { BLE_MAIN, BLE_EDIT_NAME, BLE_EDIT_PIN };

class BleScreen {
public:
    BleScreenState state = BLE_MAIN;
    OnScreenKeyboard kb;
    BLEManager *ble = nullptr;
    SDManager *sd = nullptr;
    char name[32] = "Aura Vario";
    char pin_str[8] = "1234";
    bool enabled = false;   // Schalter-Zustand, persistent in /ble.cfg -> Auto-Start beim Boot/Aufwachen

    void begin(BLEManager *b, SDManager *s) {
        ble = b;
        sd = s;
        state = BLE_MAIN;
        // Aktuelle Werte laden
        strncpy(name, b->device_name, 31);
        snprintf(pin_str, 8, "%04lu", b->pin);
        loadConfig();
    }

    void draw(EpdiyHighlevelState *hl) {
        uint8_t *fb = epd_hl_get_framebuffer(hl);
        epd_hl_set_all_white(hl);
        char buf[64];

        drawHCenter(&ArialBold28, "BLE", 0, 960, 44, fb);
        uiHLine(14, 58, 932, fb);

        switch (state) {
        case BLE_MAIN: {
            int bx = 100, bw = 760, bh = 65, gap = 18;
            int y0 = 80;

            // Status AN/AUS
            uiBox(bx, y0, bw, bh, fb);
            snprintf(buf, 64, "BLE  %s", ble->ok ? "AN" : "AUS");
            drawBoxCenter(&ArialBold24, buf, bx, y0, bw, bh, fb);

            // Name
            uiBox(bx, y0+bh+gap, bw, bh, fb);
            snprintf(buf, 64, "Name: %s", name);
            drawBoxCenter(&ArialBold16, buf, bx, y0+bh+gap, bw, bh, fb);

            // PIN
            uiBox(bx, y0+2*(bh+gap), bw, bh, fb);
            snprintf(buf, 64, "PIN: %s", pin_str);
            drawBoxCenter(&ArialBold16, buf, bx, y0+2*(bh+gap), bw, bh, fb);

            // Zurueck
            uiBox(350, 460, 260, 52, fb);
            drawBoxCenter(&ArialBold24, "ZURUECK", 350, 460, 260, 52, fb);
            break;
        }

        case BLE_EDIT_NAME:
            drawHCenter(&ArialBold16, "BLE Geraetename:", 0, 960, 80, fb);
            kb.draw(fb, "NAME");
            break;

        case BLE_EDIT_PIN:
            drawHCenter(&ArialBold16, "BLE PIN (4 Ziffern):", 0, 960, 80, fb);
            kb.draw(fb, "PIN");
            break;
        }

        epd_poweron();
        epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
        epd_poweroff();
    }

    // true = Screen verlassen
    bool handleTap(int tx, int ty, EpdiyHighlevelState *hl) {
        int bx = 100, bw = 760, bh = 65, gap = 18;
        int y0 = 80;

        switch (state) {
        case BLE_MAIN:
            if (tx >= bx && tx < bx+bw) {
                if (ty >= y0 && ty < y0+bh) {
                    // Toggle AN/AUS — Zustand merken (persistent -> Auto-Start beim naechsten Boot)
                    if (ble->ok) {
                        ble->stop();
                    } else {
                        ble->pin = atoi(pin_str);   // PIN VOR init setzen
                        ble->init(name);
                    }
                    enabled = ble->ok;
                    saveConfig();
                    draw(hl);
                } else if (ty >= y0+bh+gap && ty < y0+2*bh+gap) {
                    // Name aendern
                    kb.reset(name);
                    state = BLE_EDIT_NAME;
                    draw(hl);
                } else if (ty >= y0+2*(bh+gap) && ty < y0+3*bh+2*gap) {
                    // PIN aendern
                    kb.reset(pin_str);
                    state = BLE_EDIT_PIN;
                    draw(hl);
                }
            }
            // Zurueck
            if (ty >= 440) return true;
            break;

        case BLE_EDIT_NAME:
            if (kb.handleTap(tx, ty)) {
                if (kb.done) {
                    strncpy(name, kb.text, 31);
                    name[31] = 0;
                    saveConfig();
                    // Wenn BLE aktiv, neu starten mit neuem Namen
                    if (ble->ok) {
                        ble->stop();
                        ble->init(name);
                        ble->pin = atoi(pin_str);
                    }
                    state = BLE_MAIN;
                } else if (kb.cancelled) {
                    state = BLE_MAIN;
                }
                draw(hl);
            }
            break;

        case BLE_EDIT_PIN:
            if (kb.handleTap(tx, ty)) {
                if (kb.done) {
                    strncpy(pin_str, kb.text, 7);
                    pin_str[7] = 0;
                    if (ble->ok) {
                        ble->pin = atoi(pin_str);
                    }
                    saveConfig();
                    state = BLE_MAIN;
                } else if (kb.cancelled) {
                    state = BLE_MAIN;
                }
                draw(hl);
            }
            break;
        }
        return false;
    }

private:
    void saveConfig() {
        if (!sd || !sd->ok) return;
        File f = sd->openWrite("/ble.cfg");
        if (!f) return;
        f.println(name);
        f.println(pin_str);
        f.println(enabled ? "1" : "0");
        f.close();
        Serial.printf("[BLE] Config gespeichert: %s / %s / %s\n", name, pin_str, enabled?"AN":"AUS");
    }

    void loadConfig() {
        if (!sd || !sd->ok) return;
        File f = sd->openRead("/ble.cfg");
        if (!f) return;
        String n = f.readStringUntil('\n');
        String p = f.readStringUntil('\n');
        String e = f.readStringUntil('\n');
        f.close();
        n.trim(); p.trim(); e.trim();
        if (n.length() > 0) strncpy(name, n.c_str(), 31);
        if (p.length() > 0) strncpy(pin_str, p.c_str(), 7);
        enabled = (e == "1");
        Serial.printf("[BLE] Config geladen: %s / %s / %s\n", name, pin_str, enabled?"AN":"AUS");
    }
};
