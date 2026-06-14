#pragma once
// chat_screen.h — zeigt einen Chat-Kanal (Buddy oder FANET): Nachrichtenliste + ZURUECK.
#include "ui_utils.h"
#include "chat.h"

static void showChatScreen(EpdiyHighlevelState* hl, const char* title,
                           const ChatChannel& ch, const char* emptyHint) {
    uint8_t* fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    drawHCenter(&ArialBold28, title, 0, 960, 44, fb);
    uiHLine(14, 58, 932, fb);

    if (ch.count == 0) {
        drawHCenter(&ArialBold24, "Noch keine Nachrichten", 0, 960, 250, fb);
        if (emptyHint) drawHCenter(&ArialBold16, emptyHint, 0, 960, 300, fb);
    } else {
        // Die letzten bis zu 5 Nachrichten, aelteste oben -> neueste unten.
        int show = ch.count > 5 ? 5 : ch.count;
        int from = ch.count - show;
        int y = 92, maxY = 466;
        for (int i = from; i < ch.count && y < maxY; i++) {
            const ChatMsg& m = ch.at(i);
            char hdr[44];
            snprintf(hdr, 44, "%02d:%02d  %s%s", m.hh, m.mm, m.mine ? "Du" : m.from, m.mine ? " >>" : ":");
            drawText(&ArialBold16, hdr, 30, y, fb); y += 24;
            y = chatDrawWrapped(&ArialBold24, m.text, 46, y + 4, 36, 34, maxY, fb) + 16;
        }
    }

    uiBox(350, 478, 260, 52, fb);
    drawBoxCenter(&ArialBold24, "ZURUECK", 350, 478, 260, 52, fb);

    epd_poweron();
    epd_hl_update_screen(hl, MODE_DU, (int)epd_ambient_temperature());
    epd_poweroff();
}
