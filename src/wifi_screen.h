#pragma once
// wifi_screen.h — WiFi: Scan → Liste → Passwort → Verbinden
// NUR Verbindung — Downloads sind in overlay_screen.h
#include <WiFi.h>
#include "ui_utils.h"
#include "keyboard.h"
#include "sd_manager.h"
#include "device_registry.h"   // Ticket C: bei Verbindung registrieren + Pairing-Code zeigen

enum WiFiState {
    WIFI_SCANNING,
    WIFI_LIST,
    WIFI_ENTER_PASS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_ERROR
};

class WiFiScreen {
public:
    WiFiState state = WIFI_SCANNING;
    OnScreenKeyboard kb;
    char ssid[33] = {0};
    char pass[65] = {0};
    char status_msg[64] = {0};
    SDManager *sd = nullptr;

    static const int MAX_NETS = 8;
    char nets[MAX_NETS][33];
    int rssis[MAX_NETS];
    int net_count = 0;

    void begin(SDManager *sdm) {
        sd = sdm;
        state = WIFI_SCANNING;
        net_count = 0;
        ssid[0] = 0;
        pass[0] = 0;
    }

    void doScan(EpdiyHighlevelState *hl) {
        // Gespeichertes WiFi? Direkt verbinden
        if (ssid[0] == 0 && loadSaved()) {
            state = WIFI_CONNECTING;
            draw(hl);
            doConnect(hl);
            return;
        }
        state = WIFI_SCANNING;
        draw(hl);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        Serial.println("[WIFI] Scanning...");
        int n = WiFi.scanNetworks();
        net_count = min(n, MAX_NETS);
        for (int i = 0; i < net_count; i++) {
            strncpy(nets[i], WiFi.SSID(i).c_str(), 32);
            nets[i][32] = 0;
            rssis[i] = WiFi.RSSI(i);
            Serial.printf("[WIFI] %d: %s (%d dBm)\n", i, nets[i], rssis[i]);
        }
        WiFi.scanDelete();
        state = WIFI_LIST;
        draw(hl);
    }

    void draw(EpdiyHighlevelState *hl) {
        uint8_t *fb = epd_hl_get_framebuffer(hl);
        epd_hl_set_all_white(hl);
        char buf[64];
        drawHCenter(&ArialBold28, "WLAN", 0, 960, 40, fb);
        uiHLine(14, 54, 932, fb);

        switch (state) {
        case WIFI_SCANNING:
            drawHCenter(&ArialBold40, "SUCHE...", 0, 960, 280, fb);
            break;

        case WIFI_LIST:
            drawHCenter(&ArialBold16, "Netzwerk antippen", 0, 960, 74, fb);
            for (int i = 0; i < net_count; i++) {
                int y = 100 + i * 52;
                uiBox(60, y, 840, 46, fb);
                drawText(&ArialBold24, nets[i], 76, y + 33, fb);
                int bars = (rssis[i] > -50) ? 4 : (rssis[i] > -65) ? 3 : (rssis[i] > -75) ? 2 : 1;
                for (int b = 0; b < bars; b++) {
                    int bh = 8 + b * 6;
                    uiFill(830 - (3-b)*14, y + 42 - bh, 10, bh, fb);
                }
            }
            if (net_count == 0)
                drawHCenter(&ArialBold24, "Keine Netzwerke", 0, 960, 280, fb);
            uiBox(60, 468, 200, 46, fb);
            drawBoxCenter(&ArialBold16, "NEU SUCHEN", 60, 468, 200, 46, fb);
            uiBox(700, 468, 200, 46, fb);
            drawBoxCenter(&ArialBold16, "ZURUECK", 700, 468, 200, 46, fb);
            break;

        case WIFI_ENTER_PASS:
            snprintf(buf, 64, "Passwort: %s", ssid);
            drawHCenter(&ArialBold16, buf, 0, 960, 80, fb);
            kb.draw(fb, "PASSWORT");
            break;

        case WIFI_CONNECTING:
            drawHCenter(&ArialBold40, "VERBINDE...", 0, 960, 260, fb);
            drawHCenter(&ArialBold24, ssid, 0, 960, 320, fb);
            break;

        case WIFI_CONNECTED:
            drawHCenter(&ArialBold40, "VERBUNDEN", 0, 960, 200, fb);
            drawHCenter(&ArialBold24, ssid, 0, 960, 250, fb);
            drawHCenter(&ArialBold16, WiFi.localIP().toString().c_str(), 0, 960, 280, fb);
            {   // Ticket C: Geraete-Status / Pairing-Code (zum Koppeln in der Buddy-App)
                const char *dev = deviceFunkStatus();
                if (dev[0]) drawHCenter(&ArialBold16, dev, 0, 960, 312, fb);
            }
            uiBox(100, 340, 340, 60, fb);
            drawBoxCenter(&ArialBold16, "TRENNEN", 100, 340, 340, 60, fb);
            uiBox(520, 340, 340, 60, fb);
            drawBoxCenter(&ArialBold16, "ZURUECK", 520, 340, 340, 60, fb);
            break;

        case WIFI_ERROR:
            drawHCenter(&ArialBold40, "FEHLER", 0, 960, 240, fb);
            drawHCenter(&ArialBold16, status_msg, 0, 960, 300, fb);
            uiBox(350, 380, 260, 60, fb);
            drawBoxCenter(&ArialBold24, "OK", 350, 380, 260, 60, fb);
            break;
        }
        epd_poweron();
        epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
        epd_poweroff();
    }

    // true = Screen verlassen
    bool handleTap(int tx, int ty, EpdiyHighlevelState *hl) {
        switch (state) {
        case WIFI_LIST:
            for (int i = 0; i < net_count; i++) {
                int y = 100 + i * 52;
                if (ty >= y && ty < y + 46 && tx >= 60 && tx < 900) {
                    strncpy(ssid, nets[i], 32);
                    kb.reset("");
                    state = WIFI_ENTER_PASS;
                    draw(hl);
                    return false;
                }
            }
            if (ty >= 468 && ty < 514 && tx >= 60 && tx < 260) {
                ssid[0] = 0;  // Reset damit nicht auto-connect
                doScan(hl);
                return false;
            }
            if (ty >= 468 && ty < 514 && tx >= 700 && tx < 900) {
                WiFi.mode(WIFI_OFF);
                return true;
            }
            break;

        case WIFI_ENTER_PASS:
            if (kb.handleTap(tx, ty)) {
                if (kb.done) {
                    strncpy(pass, kb.text, 64);
                    state = WIFI_CONNECTING;
                    draw(hl);
                    doConnect(hl);
                } else if (kb.cancelled) {
                    state = WIFI_LIST;
                    draw(hl);
                } else {
                    draw(hl);
                }
            }
            break;

        case WIFI_CONNECTED:
            if (ty >= 340 && ty < 400) {
                if (tx >= 100 && tx < 440) {
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                    ssid[0] = 0;
                    doScan(hl);
                } else if (tx >= 520 && tx < 860) {
                    return true;
                }
            }
            break;

        case WIFI_ERROR:
            if (ty >= 380 && ty < 440 && tx >= 350 && tx < 610) {
                if (WiFi.status() == WL_CONNECTED) {
                    state = WIFI_CONNECTED; draw(hl);
                } else { return true; }
            }
            break;
        default: break;
        }
        return false;
    }

    bool loadSaved() {
        if (!sd || !sd->ok) return false;
        File f = sd->openRead("/wifi.cfg");
        if (!f) return false;
        String line1 = f.readStringUntil('\n');
        String line2 = f.readStringUntil('\n');
        f.close();
        line1.trim(); line2.trim();
        if (line1.length() == 0) return false;
        strncpy(ssid, line1.c_str(), 32);
        strncpy(pass, line2.c_str(), 64);
        Serial.printf("[WIFI] Geladen: %s\n", ssid);
        return true;
    }

private:
    void saveCredentials() {
        if (!sd || !sd->ok) return;
        File f = sd->openWrite("/wifi.cfg");
        if (!f) return;
        f.println(ssid);
        f.println(pass);
        f.close();
        Serial.printf("[WIFI] Gespeichert: %s\n", ssid);
    }

    void doConnect(EpdiyHighlevelState *hl) {
        Serial.printf("[WIFI] Connecting '%s'...\n", ssid);
        WiFi.begin(ssid, pass);
        int timeout = 30;   // 15 s
        while (WiFi.status() != WL_CONNECTED && timeout > 0) { delay(500); timeout--; }
        int st = WiFi.status();
        if (st == WL_CONNECTED) {
            Serial.printf("[WIFI] OK IP=%s\n", WiFi.localIP().toString().c_str());
            saveCredentials();
            deviceLoop();   // Ticket C: jetzt registrieren (falls kein NVS-Token) -> Pairing-Code bereit
            state = WIFI_CONNECTED;
        } else {
            const char* reason = (st==WL_NO_SSID_AVAIL) ? "Netz nicht gefunden" :
                                 (st==WL_CONNECT_FAILED) ? "Passwort falsch?" :
                                 (st==WL_CONNECTION_LOST) ? "Verbindung verloren" :
                                 (st==WL_DISCONNECTED)   ? "Getrennt / kein DHCP" : "Timeout";
            snprintf(status_msg, 64, "%s (Code %d)", reason, st);
            Serial.printf("[WIFI] FAIL status=%d\n", st);
            state = WIFI_ERROR;
        }
        draw(hl);
    }
};
