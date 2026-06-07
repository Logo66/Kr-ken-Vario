# Architektur-Studie: Aura Kruecke — E-Paper-Paragliding-Vario

Gespeichert: 2026-06-07
Quelle: Architekt (Ivo) — verbindliches Referenzdokument

Inhalt siehe Original-Prompt vom 2026-06-07 (zu gross fuer Inline-Kopie).
Kernpunkte fuer die Implementierung:

1. Flight-UI: reines 1-Bit S/W, FETTE Fonts, MODE_DU/A2 Partial-Refresh
2. Boden-UI: Touch-Menu, GC16, grosse Buttons (min 80x80 px)
3. ScreenManager: MODE_FLIGHT (Swipe zwischen 4 Screens) vs MODE_MENU
4. Anti-Ghosting: Zaehler, alle N=30-60 Partials ein GC16 Full-Refresh
5. Fonts: FreeSansBold-Aequivalent in >=3 Groessen, KEINE duennen Fonts
6. Touch: GT911 Software-Gesten (Tap/Swipe), INT-Pin-getrieben
7. FANET: Type 3 Messages max 80 Zeichen, Duty-Cycle <1% (36s/h)
8. BLE: NimBLE GATT Server, Text von App empfangen
9. Karte: Vektor (nicht Raster), dicke Linien, OpenAir + Hoehenlinien
10. NVS: settings_t Struct, laden/speichern bei Boot/Aenderung
