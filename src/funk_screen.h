#pragma once
// funk_screen.h — FUNK Sub-Menue: WLAN / BLE / FANET / FANET TX / BUDDY / F-CHAT
// Gleicher Kachel-Stil wie das Hauptmenue (menu_screen.h): 2 Spalten x 3 Reihen, ALLE Kaesten gleich gross.
// Zurueck per Wischen (wie im Hauptmenue) — kein eigener ZURUECK-Knopf mehr.
#include "ui_utils.h"
#include "fanet.h"   // g_fanetTxEnabled (TX-Arm-Anzeige)

enum FunkItem { FUNK_NONE, FUNK_WLAN, FUNK_BLE, FUNK_FANET, FUNK_FANET_TX,
                FUNK_BUDDY, FUNK_FANETCHAT, FUNK_BACK };

// Identische Kachel-Geometrie wie das Hauptmenue (FBW==MBW usw.).
static const int FBW=410, FBH=125, FGX=40, FGY=20, FX0=50, FY0=90;
static int fbX(int c){return FX0 + c*(FBW+FGX);}
static int fbY(int r){return FY0 + r*(FBH+FGY);}

static void drawFBtn(int c, int r, const char *label, const char *info, uint8_t *fb) {
    int x = fbX(c), y = fbY(r);
    uiBox(x, y, FBW, FBH, fb);
    if (info) {                                          // Label + kleine Info, mittig als Block
        drawHCenter(&ArialBold24, label, x, FBW, y + FBH/2 - 6, fb);
        drawHCenter(&ArialBold16, info,  x, FBW, y + FBH/2 + 28, fb);
    } else {                                             // nur Label, voll zentriert
        drawBoxCenter(&ArialBold24, label, x, y, FBW, FBH, fb);
    }
}

static void showFunkScreen(EpdiyHighlevelState *hl, bool wifi_on, bool ble_on,
                           bool fanet_on, int fanet_peers, uint32_t ble_pin = 0,
                           const char *msg = nullptr) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    drawHCenter(&ArialBold28, "FUNK", 0, 960, 48, fb);
    uiHLine(40, 66, 880, fb);

    drawFBtn(0, 0, "WLAN", wifi_on ? "VERBUNDEN" : "AUS", fb);

    if (ble_on && ble_pin > 0) { snprintf(buf, 48, "AN  PIN %04lu", ble_pin); drawFBtn(1, 0, "BLE", buf, fb); }
    else                         drawFBtn(1, 0, "BLE", ble_on ? "AN" : "AUS", fb);

    if (fanet_on && fanet_peers > 0) { snprintf(buf, 48, "AN  (%d)", fanet_peers); drawFBtn(0, 1, "FANET", buf, fb); }
    else                              drawFBtn(0, 1, "FANET", fanet_on ? "AN" : "AUS", fb);

    drawFBtn(1, 1, "FANET TX", g_fanetTxEnabled ? "SCHARF" : "AUS", fb);
    drawFBtn(0, 2, "BUDDY", "Chat", fb);
    drawFBtn(1, 2, "F-CHAT", "Piloten", fb);

    if (msg) drawHCenter(&ArialBold16, msg, 0, 960, 525, fb);
    else     drawHCenter(&ArialBold16, "Antippen  |  Wischen = zurueck", 0, 960, 525, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static FunkItem checkFunkTap(int tx, int ty) {
    FunkItem items[] = {FUNK_WLAN, FUNK_BLE, FUNK_FANET, FUNK_FANET_TX, FUNK_BUDDY, FUNK_FANETCHAT};
    int cols[] = {0,1,0,1,0,1}, rows[] = {0,0,1,1,2,2};
    for (int i = 0; i < 6; i++) {
        int x = fbX(cols[i]), y = fbY(rows[i]);
        if (tx >= x && tx < x+FBW && ty >= y && ty < y+FBH) return items[i];
    }
    return FUNK_NONE;
}
