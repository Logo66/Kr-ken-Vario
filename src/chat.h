#pragma once
// chat.h — Buddy-Chat (KI ueber Server) + FANET-Chat (Funk, Pilot-zu-Pilot).
// Je Kanal ein Ringpuffer. Eine eingehende BUDDY-Nachricht setzt ein Popup-Flag
// -> die Hauptschleife blendet sie ein (wie eine Wetter-/Luftraum-Warnung).
// Quellen rufen chatAddBuddy()/chatAddFanet() (Server-Poll, BLE, FANET-RX, Test).
#include <Arduino.h>
#include "ui_utils.h"

struct ChatMsg {
    char    from[18];   // "Buddy" / Pilot-Name oder -ID
    char    text[140];
    uint8_t hh, mm;     // Empfangs-/Sendezeit
    bool    mine;       // selbst gesendet?
};

static const int CHAT_MAX = 12;     // Nachrichten je Kanal (Ring)
struct ChatChannel {
    ChatMsg msg[CHAT_MAX];
    int count = 0;      // 0..CHAT_MAX
    int head  = 0;      // naechste Schreibposition
    void add(const char* from, const char* text, uint8_t hh, uint8_t mm, bool mine) {
        ChatMsg& m = msg[head];
        strncpy(m.from, from, 17); m.from[17] = 0;
        strncpy(m.text, text, 139); m.text[139] = 0;
        m.hh = hh; m.mm = mm; m.mine = mine;
        head = (head + 1) % CHAT_MAX;
        if (count < CHAT_MAX) count++;
    }
    const ChatMsg& at(int i) const {                 // 0 = aeltester sichtbarer, count-1 = neuester
        int start = (count < CHAT_MAX) ? 0 : head;
        return msg[(start + i) % CHAT_MAX];
    }
};

static ChatChannel  g_buddyChat;
static ChatChannel  g_fanetChat;
// Eingehende Nachricht (Buddy ODER FANET) -> kurzes Einblenden. Dauer = g_chatPopupSec (main.cpp).
static volatile bool g_popupPending = false;
static char          g_popupText[140] = "";
static char          g_popupFrom[18]  = "";

static void chatPopup(const char* from, const char* text) {
    strncpy(g_popupFrom, from, 17); g_popupFrom[17] = 0;
    strncpy(g_popupText, text, 139); g_popupText[139] = 0;
    g_popupPending = true;
}
// popup=false (Severity "info") -> landet nur im Chat, kein Einblenden.
static void chatAddBuddy(const char* text, uint8_t hh, uint8_t mm, bool popup = true) {
    g_buddyChat.add("Buddy", text, hh, mm, false);
    if (popup) chatPopup("Buddy", text);
}
static void chatAddFanet(const char* from, const char* text, uint8_t hh, uint8_t mm) {
    g_fanetChat.add(from, text, hh, mm, false);
    chatPopup(from, text);
}

// Greedy Wort-Umbruch: zeichnet text in Zeilen <= maxChars, gibt die naechste y zurueck.
static int chatDrawWrapped(const EpdFont* font, const char* text, int x, int y,
                           int maxChars, int lineH, int maxY, uint8_t* fb) {
    char line[80]; int lp = 0;
    const char* p = text;
    while (*p && y <= maxY) {
        // ein Wort (bis Space/Ende) einlesen
        const char* ws = p; int wl = 0;
        while (*p && *p != ' ' && wl < 78) { p++; wl++; }
        bool fits = (lp == 0) ? (wl <= maxChars) : (lp + 1 + wl <= maxChars);
        if (!fits && lp > 0) {                       // Zeile voll -> ausgeben
            line[lp] = 0; drawText(font, line, x, y, fb); y += lineH; lp = 0;
        }
        if (lp > 0) line[lp++] = ' ';
        for (int i = 0; i < wl && lp < 78; i++) line[lp++] = ws[i];
        while (*p == ' ') p++;                        // Spaces schlucken
    }
    if (lp > 0 && y <= maxY) { line[lp] = 0; drawText(font, line, x, y, fb); y += lineH; }
    return y;
}
