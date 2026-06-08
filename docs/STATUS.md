# AURA Kruecke — Projekt-Status

Letzte Aktualisierung: 2026-06-08

## DONE (Tag 2 + 3)

- **Kalman-Vario** live auf Display (1 Hz MODE_DU)
- **GPS** Fix mit 7+ Satelliten, Speed/Heading live
- **Alle 4 Sensoren** kontinuierlich via Raw I2C
- **5 Flight-Screens** im Karussell (Swipe/Button):
  1. Cruise (Vario, Hoehe, Speed, Heading, Wind, Temp)
  2. Thermal (Kompass-Rose, Lift-Punkte, Kern-Schaetzung)
  3. Goal/Final-Glide (Ankunft, Distanz, GR, Bearing-Ring)
  4. (Map — in Entwicklung)
  5. (weitere geplant)
- **Menu** (6 Buttons): QNH, Licht, Flugbuch, Funk, (frei), Aus
- **QNH-Kalibrierung** via Touch (+/-10m)
- **Flugbuch** mit Demo-Daten (Dauer, MaxAlt, Climb, Gmax, Spur, Strecke)
- **Landing-Screen** (Gut/Ride/Hilfe)
- **Start/Lande-Erkennung** automatisch
- **Touch-Swipe** (GT911, Koordinaten-Transform Portrait→Landscape)
- **RTC-Uhrzeit** (PCF8563, Software-Clock im Loop)
- **Batterie-SoC** aus BQ25896 Spannung
- **Boot-Splash** invertiert (weiss auf schwarz)
- **Credits-Screen** bei Ausschalten
- **Deep Sleep** mit BOOT-Wake
- **Font-Tiers** T1-T6 (ArialBold 72/40/32/28/24/16)
- **ui_utils.h** mit exakter Zentrierung via epd_get_text_bounds

## NAECHSTE SCHRITTE

1. XC-Screen mit Live-Daten (Wegpunkt-Navigation)
2. Karten-Screen (KRUECKE-6: Vektor, SD, Projektion)
3. FANET TX/RX (SX1262 LoRa)
4. BLE (NimBLE GATT Server)
5. WiFi (Karten-Download)
6. IGC-Logging auf SD
7. Flight Buddy App (PWA/BLE)
