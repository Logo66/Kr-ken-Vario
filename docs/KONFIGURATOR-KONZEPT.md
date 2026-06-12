# KONZEPT — Online-Konfigurator der Krücke (Handy + WLAN-Web-App)
## Entscheidungsvorlage · Code-KRÜCKE · 2026-06-11

> **Frage (Ivo):** Was muss alles in den Online-Konfigurator, und wie machen wir das —
> es muss **am Handy** (im Feld) gehen **und zu Hause übers WLAN als Web-App**.
> **Art:** Konzept + Empfehlung. Kein Code gebaut. Betrifft Krücke-Firmware + Server + App (cross-team).

---

## 0. KURZFASSUNG / EMPFEHLUNG

1. **EINE Konfig-Wahrheit** statt heute verstreuter Dateien (NVS „sound", `/ble.cfg`, `/wifi.cfg`, QNH zur Laufzeit). → **ein versioniertes Settings-Modell** (JSON, ArduinoJson) im Geräte-NVS.
2. **Drei Editoren, ein Modell:** (a) Touch am Gerät (minimal, schon da: Lautstärke/Stumm), (b) **Handy-App über BLE** (Feld, kein WLAN), (c) **Web-App über WLAN** (zu Hause).
3. **Der „Wie"-Fork** ist die Web-App: **Gerät-direkt** (Krücke hostet die Seite selbst) **oder Cloud-Hub** (Buddy-Server speichert, Gerät synct). 
4. **Empfehlung = phasenweise:**
   - **Phase 1 (firmware-only, schnell, kein Server-Aufwand):** gemeinsames Settings-Modell + **BLE-Konfig-Dienst** (Handy/Feld) + **Geräte-eigene Web-Konfig-Seite** (WLAN/zu Hause, baut auf dem schon vorhandenen IGC-Webserver auf). **Damit gehen BEIDE Wege sofort.**
   - **Phase 2 (Kür, cross-team):** Konfig zusätzlich im **Buddy-Server** (Cloud) — Web-App + Handy-App editieren dort, Gerät zieht sie beim Heartbeat. „Von überall konfigurierbar", auch wenn das Gerät grad aus ist.
5. **Heilig:** sicherheitsrelevante Sachen (FANET-TX-Freigabe) bleiben hinter ihren Gates; Konfig ändert Schwellen/Anzeige/Ton, nicht die Sicherheits-Logik.

---

## 1. WAS rein muss — Konfig-Inventar

Gruppiert; „heute" = wo es aktuell (verstreut) liegt.

### A. Ton / Vario-Akustik  *(das Herzstück der Boden-Konfig)*
| Parameter | heute |
|---|---|
| Lautstärke (0–5) | NVS „sound" (Touch) |
| Stumm an/aus | NVS „sound" (Touch) |
| Steig-Schwelle (ab wann Steigton, m/s) | NVS-Platzhalter |
| Sink-Alarm-Schwelle (m/s) | NVS-Platzhalter |
| Totzone/Deadband um 0 (m/s) | NVS-Platzhalter |
| Tonkurve (Steigen → Tonhöhe/Frequenz) | NVS-Platzhalter |
| Sink-Ton an/aus | neu |

### B. Einheiten & Anzeige
Höhe m/ft · Speed km/h/mph/kt · Steigen m/s / ft·min⁻¹ · Temp °C/°F · aktive Screens + Reihenfolge · Backlight/Helligkeit/Auto-off · QNH-Default beim Boot. *(heute: fix im Code)*

### C. Höhe / Luftdruck
QNH (hPa) · Referenzhöhe-Kalibrierung (bekannte Platzhöhe → QNH). *(heute: QNH-Screen zur Laufzeit)*

### D. Funk
WLAN SSID+Passwort (`/wifi.cfg`) · BLE Name/PIN/an-aus (`/ble.cfg`, schon persistent) · **FANET**: an/aus, Flugzeugtyp, **Pilotenname + FANET-ID**, Online-Tracking-Flag, **TX-Freigabe** (erst wenn Gate D offen).

### E. Pilot / Polare  *(für Gleitzahl + Endanflug)*
Pilotenname · Schirm/Modell · Startgewicht · **Polare** (Sinken/Speed-Punkte) — füttert GR-Berechnung + Ziel-Endanflug. *(heute: nicht vorhanden)*

### F. Flug-Logik *(advanced)*
Start-/Lande-Schwellen + Dwell-Zeiten · Auto-Thermik-Umschalt-Schwelle · (später Fusion/ZUPT-Schwellen). *(heute: fix in `flight_detect.h`)*

### G. Karte
Region-Pack (welcher) · Layer an/aus: Höhenlinien · Wasser · Luftraum · Hindernisse · Track.

### H. Wind / Sensor-Fusion *(teils später)*
Wind-Schätzer an/aus · (Fusion an/aus + Tuning — nach dem Fusion-Bau-Ticket).

### I. Logging
IGC-Logging an/aus · IMU-Roh-Log an/aus (Testflug).

### J. Buddy
Pairing-Code (schon da) · Buddy-Liste (welche Piloten).

> **Kernpunkt:** das ist viel und liegt heute an ~4 verschiedenen Orten. Schritt 1 ist **Konsolidierung in ein Modell**, sonst wird jeder Editor ein Sonderfall.

---

## 2. WIE — Architektur

### 2.1 Ein Settings-Modell = die Wahrheit
- **Format:** JSON (ArduinoJson ist schon Dependency) — gut für Web (Formular ↔ JSON) **und** App (JSON über BLE). Mit **`config_version`** (Hochzähler) für Sync/Konflikt.
- **Speicherort:** Geräte-**NVS** (autoritativ, überlebt alles). Default-Werte beim ersten Boot.
- **Anwenden:** ein `applyConfig()` setzt aus dem Modell die Laufzeit (Ton-Schwellen, Einheiten, BLE-Name…). Jeder Editor schreibt nur das Modell + ruft `applyConfig()` → **alle Wege identisch**.

### 2.2 Drei Editoren, ein Modell
1. **Touch am Gerät** — bewusst minimal (Lautstärke/Stumm im Flug). Schon gebaut.
2. **Handy-App über BLE** — Feld, **kein WLAN nötig**. Ein BLE-**Konfig-Dienst**: App liest aktuelles Modell, schreibt Änderungen (geblockt/chunked, da kleine MTU). Gerät wendet an + speichert NVS.
3. **Web-App über WLAN** — zu Hause. Siehe Fork 2.3.

### 2.3 Der Fork (Web-App): Gerät-direkt vs. Cloud-Hub

**Variante A — Gerät hostet die Web-Seite selbst** *(WLAN, direkt)*
- Die Krücke serviert eine Konfig-Webseite (HTML-Formular), **baut auf dem schon vorhandenen IGC-Webserver auf** (Port 80, läuft bei WLAN). Du gehst zu Hause ins gleiche WLAN, öffnest die Seite, editierst, speicherst → schreibt direkt das NVS-Modell.
- ➕ offline, einfach, **kein Server-Aufwand**, schon halb da · ➖ Gerät muss an + im WLAN sein; kein „konfigurieren wenn Gerät aus"; Web-Seite ist schlicht (vom Gerät generiert).

**Variante B — Cloud-Hub (Buddy-Server)** *(WLAN, über die Cloud)*
- Konfig liegt im **Buddy-Server** pro `device_id`. **Web-App + Handy-App editieren dieselbe Konfig dort** (ein UI-Modell, zwei Frontends). Gerät **zieht** die Konfig beim Heartbeat über WLAN (neuer Endpoint, z. B. `GET /devices/config` + `config_version`), wendet an, speichert NVS.
- ➕ **von überall** konfigurierbar (Couch-Browser, Handy), auch wenn das Gerät grad aus ist (synct beim nächsten WLAN); passt ins Buddy-Ökosystem (die bauen eh Web-App + Server) · ➖ mehr Aufwand, braucht Sync-Protokoll + Konfliktauflösung, **cross-team** (Server/App-Baumeister).

### 2.4 Empfehlung — phasenweise
- **Phase 1 (jetzt, firmware-only):** Settings-Modell konsolidieren + **BLE-Konfig-Dienst** (2) + **Geräte-Web-Konfig** (Variante A). → **beide von dir genannten Wege funktionieren sofort**, ohne auf den Server zu warten. Risiko niedrig, alles in der Krücke.
- **Phase 2 (Kür):** **Cloud-Hub (Variante B)** ergänzen — dann editieren Web-App + Handy-App über den Server, Gerät synct. BLE bleibt als **Feld-Weg ohne WLAN**. So hat man am Ende: überall konfigurierbar **und** feldtauglich offline.

> Die Empfehlung verbaut nichts: Phase 1 ist die Grundlage (ein Modell + `applyConfig`), Phase 2 hängt nur einen Cloud-Sync dran. Das Gerät bleibt immer mit lokaler Kopie offline-fähig.

---

## 3. Firmware-Bausteine (Phase 1, Krücke-Seite)
1. **`config.h` — ein Settings-Struct/JSON** (alle Gruppen aus §1) in NVS, versioniert, mit Defaults + `applyConfig()`.
2. **Migration:** bestehende verstreute Settings (sound/NVS, `/ble.cfg`, `/wifi.cfg`, QNH) einmalig ins Modell ziehen.
3. **BLE-Konfig-Dienst:** Characteristic(s) zum Lesen/Schreiben des Modells (geblockt). App-seitig dokumentieren (wie der BLE-Datenvertrag).
4. **Web-Konfig-Seite:** IGC-Webserver erweitern — `GET /config` (Formular aus Modell) + `POST /config` (Modell schreiben + `applyConfig`).

## 4. Bedarf an Server/App-Baumeister (Phase 2)
- Server: `GET/PUT /devices/config` (Bearer, pro Gerät) + `config_version` im Heartbeat.
- Web-App + Flutter-App: **ein** Konfig-UI gegen denselben Server-Endpoint.
- Gemeinsamer **Konfig-Vertrag** (wie der BLE-Datenvertrag): das JSON-Schema des Modells — eine Quelle für Firmware + Server + App.

## 5. OFFENE ENTSCHEIDUNGEN (für Ivo + Architekt)
1. **Phase-1-Web = Variante A** (Gerät hostet) — OK als Sofort-Lösung? *(meine Empfehlung: ja)*
2. **Phase 2 Cloud-Hub** — wollen wir das als Ziel festschreiben? *(ich empfehle: ja, aber später)*
3. **Konfig-Umfang Phase 1:** Fangen wir mit **Ton + Einheiten + Funk + Pilot/Polare** an (der Kern), Rest folgt? 
4. **Polare/Pilot:** brauchen wir ein Glider-Modell jetzt schon (für Endanflug-Genauigkeit) oder später?

---
*Kein Code geändert. Nächster Schritt: Architekt-/Ivo-Entscheidung zu §5 → dann Phase-1-Bau-Ticket (Settings-Modell + BLE-Konfig + Web-Konfig).*
