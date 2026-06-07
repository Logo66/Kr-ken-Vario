# ANALYSE: Warum aktualisiert sich das Display nicht nach Firmware-Flash?

**Prioritaet:** Blocker — ohne zuverlaessiges Display-Update ist keine UI-Entwicklung moeglich.

---

## Symptom
- Display hat sich EINMAL aktualisiert (Cruise-Screen erschien).
- Seitdem: 10+ Firmware-Aenderungen geflasht, RST gedrueckt, Display zeigt IMMER den alten Cruise-Screen.
- Serial-Log zeigt: Firmware laeuft durch, epdiy meldet "PG is up", "actual draw took 467ms", "Cruise done" — alles sieht OK aus.
- Aber das E-Paper-Bild aendert sich nicht.

## Was funktioniert (bewiesen)
- Flash-Upload: pio run -t upload = SUCCESS (verifiziert via Serial-Banner)
- epdiy init: epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K) = OK
- epd_clear(): Hat beim ersten Mal das Panel weiss geflasht (sichtbar)
- epd_hl_update_screen(): Meldet Erfolg, "actual draw took 467ms"
- RST-Button: Loest Reset aus (USB disconnects, Serial zeigt Reboot)
- Board: PSRAM 8MB, Flash 16MB, Vbat 4.10V, Heap stabil

## Was NICHT funktioniert
- Display-Bild aendert sich nach dem ersten erfolgreichen Update NICHT MEHR.
- Weder nach RST (Batterie-only), noch nach RST (USB+Serial offen).
- Weder mit FiraSans-Fonts noch mit ArialBold-Fonts.
- Weder mit "BOLD TEST" Minimal-FW noch mit vollem Cruise-Screen.

## Offene Fragen (VOR dem naechsten Versuch beantworten)

### 1. epdiy Waveform / VCOM
- Die Factory-FW ruft `epd_set_vcom(ui_setting_get_vcom())` auf. Wir nie.
- Nach pio run -t erase ist NVS leer. Welchen VCOM-Wert nutzt epdiy als Default?
- Kann ein falscher VCOM dazu fuehren, dass das Panel "aktualisiert" wird aber kein sichtbarer Kontrast entsteht?
- ABER: Das erste Update hat funktioniert (mit demselben Code, nach demselben Erase). Warum?

### 2. epdiy Highlevel-API Differenz-Tracking
- epd_hl_update_screen vergleicht front_fb und back_fb.
- Wenn both buffers identisch sind (kein Diff), was passiert? Wird trotzdem ein Refresh gemacht?
- Setzt epd_hl_set_all_white() BEIDE Buffer auf weiss?
- Pruefe den epdiy-Quellcode: src/highlevel.c — was genau macht epd_hl_update_screen?

### 3. epd_clear() vs epd_hl_update_screen
- epd_clear() ist eine Low-Level-Funktion die das Panel direkt treibt (kein Framebuffer).
- epd_hl_update_screen() ist High-Level (Framebuffer-Diff).
- Koennen die sich gegenseitig in die Quere kommen? (z.B. epd_clear aendert den Panel-Zustand, aber die HL-API denkt der back_fb ist noch der alte Zustand)

### 4. LCD-Peripherie / DMA
- epdiy warnt: "cache line size is set to 45 (< 64B)! Reducing pixel clock from 20 to 10."
- Laeuft der LCD-DMA-Transfer bei 10 MHz korrekt? Oder produziert er nur Timing aber keine Daten?
- Factory-FW setzt: epd_set_lcd_pixel_clock_MHz(17). Wir setzen nichts.

### 5. Reset-Verhalten
- RST-Button = ESP32 EN-Pin. Reset sollte IMMER funktionieren (Batterie oder USB).
- Aber: Manchmal geht das Board beim RST in Download-Mode (boot:0x1). Warum?
- Haengt das mit dem Zustand des USB-CDC-Treibers zusammen?
- Kann der DTR/RTS-Zustand des Host-PCs den Boot-Mode beeinflussen?

## Untersuchungsplan (REIHENFOLGE)

### Schritt 1: epdiy Quellcode lesen
- src/highlevel.c: epd_hl_update_screen, epd_hl_set_all_white — was passiert genau?
- src/epdiy.c: epd_clear — wie unterscheidet sich das vom HL-Update?
- src/board/epd_board_v7.c: Power-Sequenz, VCOM-Handling
- src/board/tps65185.c: VCOM-Register, Default-Wert

### Schritt 2: Factory-Firmware analysieren
- examples/factory/main/main.cpp: EXAKTE Init-Sequenz kopieren
- Besonders: epd_set_vcom, epd_set_lcd_pixel_clock_MHz, Reihenfolge der Aufrufe
- Gibt es etwas das die Factory macht das wir NICHT machen?

### Schritt 3: Minimaler Reproduktionstest
- Factory-FW flashen und RST druecken. Zeigt Factory-UI? Wenn nein → Hardware-Problem.
- Wenn ja: unsere FW Schritt fuer Schritt an Factory anpassen bis es funktioniert.

### Schritt 4: VCOM-Experiment
- epd_set_vcom(1560) vor dem ersten Update aufrufen (Factory-Wert).
- Testen ob das Display dann aktualisiert.

---

## Board-Infos (fuer Referenz)
- Board: LilyGo T5 E-Paper S3 Pro (H752-01)
- Taster: S1, S2, S3 (User), S4 (Power-Bereich), RST (Rueckseite), BOOT (GPIO 0)
- Power Switch vorhanden (Dokumentation erwaehnt ihn)
- Reset: RST-Button = ESP32 EN-Pin
- Serial: USB-CDC (HWCDC), COM5, DTR/RTS muessen OFF sein beim Port-Oeffnen
- epdiy 2.0.0 (commit 1b089e60)
- Board-Def: boards/T5-ePaper-S3.json (memory_type: qio_opi)

## Regeln fuer die naechste Session
1. ERST alle 4 Untersuchungsschritte abschliessen
2. DANN einen EINZIGEN gezielten Fix implementieren
3. KEIN Trial-and-Error ohne Verstaendnis
