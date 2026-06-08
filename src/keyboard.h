#pragma once
// keyboard.h — On-Screen Touch-Tastatur fuer E-Paper
// 10 Tasten pro Reihe, 4 Reihen + Sondertasten
// Layout QWERTZ (Schweizer Standard)
#include "ui_utils.h"

static const int KB_ROWS = 4;
static const int KB_COLS = 10;
static const int KB_KEY_W = 80;
static const int KB_KEY_H = 52;
static const int KB_GAP = 5;
static const int KB_X0 = 50;    // Startposition X
static const int KB_Y0 = 200;   // Startposition Y (hoeher fuer Platz)

// QWERTZ Layout
static const char* KB_LOWER[KB_ROWS][KB_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"q","w","e","r","t","z","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","@"},
    {" ","y","x","c","v","b","n","m",".","<"}  // < = Backspace
};

static const char* KB_UPPER[KB_ROWS][KB_COLS] = {
    {"!","\"","#","$","%","&","/","(",")","="},
    {"Q","W","E","R","T","Z","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L","@"},
    {" ","Y","X","C","V","B","N","M","-","<"}
};

class OnScreenKeyboard {
public:
    char text[64] = {0};
    int cursor = 0;
    bool shift = false;
    bool done = false;       // Enter gedrueckt
    bool cancelled = false;  // Abbruch

    void reset(const char *initial = "") {
        strncpy(text, initial, 63);
        text[63] = 0;
        cursor = strlen(text);
        shift = false;
        done = false;
        cancelled = false;
    }

    // Tastatur zeichnen + Textfeld oben
    void draw(uint8_t *fb, const char *title) {
        // Titel
        drawHCenter(&ArialBold24, title, 0, 960, 120, fb);

        // Textfeld (Rahmen + Inhalt)
        uiBox(KB_X0, 135, 860, 40, fb);
        // Text mit Cursor
        char display[68];
        snprintf(display, 68, "%s_", text);
        drawText(&ArialBold24, display, KB_X0 + 10, 165, fb);

        // Tasten zeichnen
        const char* (*layout)[KB_COLS] = shift ? KB_UPPER : KB_LOWER;
        for (int r = 0; r < KB_ROWS; r++) {
            for (int c = 0; c < KB_COLS; c++) {
                int x = KB_X0 + c * (KB_KEY_W + KB_GAP);
                int y = KB_Y0 + r * (KB_KEY_H + KB_GAP);
                uiBox(x, y, KB_KEY_W, KB_KEY_H, fb);

                const char *key = layout[r][c];
                if (key[0] == '<') {
                    // Backspace-Symbol
                    drawBoxCenter(&ArialBold24, "DEL", x, y, KB_KEY_W, KB_KEY_H, fb);
                } else if (key[0] == ' ') {
                    // Leertaste — breiter darstellen
                    drawBoxCenter(&ArialBold16, "___", x, y, KB_KEY_W, KB_KEY_H, fb);
                } else {
                    drawBoxCenter(&ArialBold28, key, x, y, KB_KEY_W, KB_KEY_H, fb);
                }
            }
        }

        // Sondertasten unterhalb
        int sy = KB_Y0 + KB_ROWS * (KB_KEY_H + KB_GAP) + 4;
        // SHIFT
        uiBox(KB_X0, sy, 160, 48, fb);
        drawBoxCenter(&ArialBold16, shift ? "SHIFT *" : "SHIFT", KB_X0, sy, 160, 48, fb);
        // OK
        uiBox(KB_X0 + 360, sy, 160, 48, fb);
        if (shift) uiFill(KB_X0 + 360, sy, 160, 48, fb);
        drawBoxCenter(&ArialBold24, "OK", KB_X0 + 360, sy, 160, 48, fb,
                       shift ? 0xFF : 0);
        // ABBRUCH
        uiBox(KB_X0 + 700, sy, 160, 48, fb);
        drawBoxCenter(&ArialBold16, "ABBRUCH", KB_X0 + 700, sy, 160, 48, fb);
    }

    // Touch verarbeiten — gibt true zurueck wenn Display-Update noetig
    bool handleTap(int tx, int ty) {
        // Sondertasten pruefen
        int sy = KB_Y0 + KB_ROWS * (KB_KEY_H + KB_GAP) + 4;
        if (ty >= sy && ty < sy + 48) {
            if (tx >= KB_X0 && tx < KB_X0 + 160) {
                shift = !shift;  // SHIFT toggle
                return true;
            }
            if (tx >= KB_X0 + 360 && tx < KB_X0 + 520) {
                done = true;     // OK
                return true;
            }
            if (tx >= KB_X0 + 700 && tx < KB_X0 + 860) {
                cancelled = true; // ABBRUCH
                return true;
            }
        }

        // Normale Tasten
        const char* (*layout)[KB_COLS] = shift ? KB_UPPER : KB_LOWER;
        for (int r = 0; r < KB_ROWS; r++) {
            for (int c = 0; c < KB_COLS; c++) {
                int x = KB_X0 + c * (KB_KEY_W + KB_GAP);
                int y = KB_Y0 + r * (KB_KEY_H + KB_GAP);
                if (tx >= x && tx < x + KB_KEY_W && ty >= y && ty < y + KB_KEY_H) {
                    const char *key = layout[r][c];
                    if (key[0] == '<') {
                        // Backspace
                        if (cursor > 0) {
                            text[--cursor] = 0;
                        }
                    } else {
                        // Zeichen einfuegen
                        if (cursor < 62) {
                            text[cursor++] = key[0];
                            text[cursor] = 0;
                        }
                    }
                    return true;
                }
            }
        }
        return false;
    }
};
