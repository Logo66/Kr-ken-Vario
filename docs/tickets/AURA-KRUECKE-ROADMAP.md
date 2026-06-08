# AURA Kruecke — Feature-Roadmap

## Kruecke Mini-Menu (on-device, rudimentaer)
- Ausschalten (Deep Sleep + epd_clear)
- QNH einstellen (Referenzhoehe oder manuell)
- Lautstaerke Vario-Ton (wenn Buzzer kommt)
- Flugbuch: letzte Fluege anzeigen

## Flugbuch + Automatik
- Start-Erkennung (GPS Speed > Schwelle + Steigen)
- Lande-Erkennung (GPS Speed < Schwelle + Sinken aufhoert)
- Nach Landung: Abfrage-Screen mit grossen Buttons:
  - "Gut gelandet" ✅
  - "Brauche Hike" 🥾 (FANET Ground-Tracking starten)
  - "Brauche Hilfe" 🆘 (FANET Type 7 Code 13/14)
- Flugdaten: Startzeit, Landezeit, Max-Hoehe, Strecke, IGC-Log auf SD

## Handy-App (Flight Buddy)
- HTML/Web-basiert (PWA) → laeuft auf Android + iOS ohne App-Store
- ODER bestehende Flight Buddy V5 erweitern
- BLE-Verbindung zum Kruecke-Vario
- Einstellungen: Vario-Daempfung, Ton-Profil, Datenfelder, FANET-Config
- FANET-Textnachrichten (Spracheingabe auf Handy → Text via BLE → FANET TX)
- Flugbuch-Sync (Fluege vom Geraet aufs Handy)

## XC-Screens (aus Architekt-Studie)
1. Goal/Final-Glide (Ankunftshoehe, erforderliche GR)
2. Waypoint-Navigation (Bearing, Distanz)
3. Luftraum-Warnung (Overlay)
4. Karten-Screen (Vektor, Zoom-Stufen)
5. FAI-Dreieck-Assistent (spaeter)
