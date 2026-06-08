#pragma once
// funk_screen.h — FUNK Sub-Menü: WLAN / BLE / FANET
#include "ui_utils.h"

enum FunkItem { FUNK_NONE, FUNK_WLAN, FUNK_BLE, FUNK_FANET, FUNK_BACK };

static void showFunkScreen(EpdiyHighlevelState *hl, bool wifi_on, bool ble_on,
                           bool fanet_on, int fanet_peers) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    char buf[48];

    drawHCenter(&ArialBold28, "FUNK", 0, 960, 44, fb);
    uiHLine(14, 58, 932, fb);

    // 3 grosse Buttons vertikal
    int bx = 200, bw = 560, bh = 90, gap = 20;

    // WLAN
    int y0 = 80;
    uiBox(bx, y0, bw, bh, fb);
    drawText(&ArialBold28, "WLAN", bx + 30, y0 + 55, fb);
    drawHCenter(&ArialBold16, wifi_on ? "VERBUNDEN" : "AUS", bx + bw/2, bw/2, y0 + 55, fb);

    // BLE
    int y1 = y0 + bh + gap;
    uiBox(bx, y1, bw, bh, fb);
    drawText(&ArialBold28, "BLE", bx + 30, y1 + 55, fb);
    drawHCenter(&ArialBold16, ble_on ? "AN" : "AUS", bx + bw/2, bw/2, y1 + 55, fb);

    // FANET
    int y2 = y1 + bh + gap;
    uiBox(bx, y2, bw, bh, fb);
    drawText(&ArialBold28, "FANET", bx + 30, y2 + 55, fb);
    if (fanet_on && fanet_peers > 0) {
        snprintf(buf, 48, "AN  (%d)", fanet_peers);
    } else {
        snprintf(buf, 48, "%s", fanet_on ? "AN" : "AUS");
    }
    drawHCenter(&ArialBold16, buf, bx + bw/2, bw/2, y2 + 55, fb);

    // ZURUECK
    int y3 = y2 + bh + gap + 20;
    uiBox(350, y3, 260, 60, fb);
    drawBoxCenter(&ArialBold24, "ZURUECK", 350, y3, 260, 60, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}

static FunkItem checkFunkTap(int tx, int ty) {
    int bx = 200, bw = 560, bh = 90, gap = 20;
    int y0 = 80, y1 = y0+bh+gap, y2 = y1+bh+gap, y3 = y2+bh+gap+20;

    if (tx >= bx && tx < bx+bw) {
        if (ty >= y0 && ty < y0+bh) return FUNK_WLAN;
        if (ty >= y1 && ty < y1+bh) return FUNK_BLE;
        if (ty >= y2 && ty < y2+bh) return FUNK_FANET;
    }
    if (tx >= 350 && tx < 610 && ty >= y3 && ty < y3+60) return FUNK_BACK;
    return FUNK_NONE;
}
