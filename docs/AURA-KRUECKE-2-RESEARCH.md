# AURA-KRUECKE-2 · Vario Core Research Report

**Datum:** 2026-05-18
**Autor:** Claude (Chef-Architekt)
**Fuer:** Ivo (CEO/Pilot)
**Methode:** Parallele Web-Recherche (4 Agenten) + Reverse-Engineering des Flight-Buddy-BLE-Protokolls

---

## Executive Summary

Der Vario Core der AURA-Kruecke kann auf dem **KF4D-Algorithmus** aufbauen — einem bewährten, adaptiven Kalman-Filter aus der Open-Source-Community (har-in-air). Er fusioniert BMP581-Druckdaten mit IMU-Beschleunigung und reduziert die Vario-Latenz von ~800 ms (Baro-only) auf **~150 ms**. Das entspricht dem Niveau des XCTracer, des derzeit besten kommerziellen Varios.

Die Sensor-Kombination (BNO085 + LSM6DSO32 + 2× BMP581 + SHT40) ist einzigartig im Markt. Kein anderes Vario hat gleichzeitig:
- Dual-Druck fuer Differenzmessung
- 9-DoF Fusion-IMU fuer Orientierung
- Separaten ±32g Sensor fuer Klapper-Erkennung
- Eigenen Temperatur-/Feuchte-Sensor fuer praezise Dichtekorrektur

---

## 1. Kalman-Filter: Warum KF4D

### Vergleich der Filter-Varianten

| Filter | Zustaende | Sensoren | Latenz | Eignung |
|--------|-----------|----------|--------|---------|
| IIR/Exponential | 1 (vspeed) | nur Baro | ~800-1500 ms | Einsteiger-Varios |
| KF2 | 2 (h, v) | nur Baro | ~500 ms | BlueFlyVario-Niveau |
| KF3 | 3 (h, v, bias) | Baro + IMU | ~200 ms | gut, aber mathematisch "geschummelt" |
| **KF4D** | **4 (h, v, a, bias)** | **Baro + IMU** | **~150 ms** | **XCTracer-Niveau, adaptiv** |

**KF4D** hat einen entscheidenden Vorteil: Er ist **adaptiv**. Wenn die IMU grosse Beschleunigung misst (Thermik-Eintritt, Klapper), wird das Prozessrauschen hochgefahren → schnelle Reaktion. In ruhiger Luft wird es runtergefahren → glatte Anzeige. Das ist genau das Verhalten, das Piloten wollen.

**Quelle:** har-in-air/ESP32_IMU_BARO_GPS_VARIO (GitHub), verifiziert in Jupyter-Notebook mit echten Flugdaten. XCTracer nutzt intern ebenfalls einen 4-State-Kalman (aus Patent/Produktbeschreibung).

### Rechenaufwand

~459 Mikrosekunden pro Iteration auf ESP32. Bei 100 Hz (IMU-Rate) sind das **4.6% CPU-Last**. Kein Problem fuer den ESP32-S3 @ 240 MHz.

---

## 2. BMP581: Optimale Konfiguration

### Rausch-Analyse (aus Bosch-Datenblatt)

| OSR | Messzeit | Rauschen (Pa RMS) | Hoehenaufloesung | Max. ODR |
|-----|----------|-------------------|-------------------|----------|
| 1× | 1.1 ms | ~3.2 Pa | ~26 cm | 240 Hz |
| 4× | 2.0 ms | ~1.6 Pa | ~13 cm | 200 Hz |
| **32×** | **5.8 ms** | **~0.16 Pa** | **~1.3 cm** | **100 Hz** |
| 64× | 10.8 ms | ~0.11 Pa | ~0.9 cm | 50 Hz |
| 128× | 20.8 ms | ~0.08 Pa | ~0.66 cm | 25 Hz |

### Empfehlung: 32× OSR bei 50 Hz

- **1.3 cm Hoehenaufloesung** — 3× besser als MS5611 (Standard in kommerziellen Varios)
- 5.8 ms Messzeit passt bequem in 20 ms Zykluszeit (50 Hz)
- Genuegend Headroom fuer den Kalman-Filter
- **IIR-Filter des Sensors: AUS** — der Kalman uebernimmt die Filterung. Doppelfilterung erzeugt unvorhersagbare Phasenverschiebung.
- **Modus: Continuous** — kein Forced-Mode-Timing-Jitter

### Vergleich mit dem Wettbewerb

| Vario | Sensor | Hoehenaufloesung |
|-------|--------|-----------------|
| XCTracer | TE4525 (Differenzdruck) | ~2 cm |
| Skytraxx 5 | MS5611 | ~4 cm |
| BlueFlyVario | MS5611 | ~4 cm |
| **AURA-Kruecke** | **BMP581 @ 32× OSR** | **~1.3 cm** |

Wir haben potentiell die **beste barometrische Aufloesung aller Paraglider-Varios auf dem Markt**.

---

## 3. IMU-Architektur: Zwei Sensoren, zwei Rollen

### Rollenverteilung

| Sensor | Primaer-Rolle | Rate | Warum dieser Sensor |
|--------|---------------|------|---------------------|
| **BNO085** | Orientierung + Linear-Acceleration | 100 Hz | On-Chip Fusion (Hillcrest SH-2) liefert fertige Quaternionen und Gravity-Vektor. Spart ~2000 Zeilen eigenen AHRS-Code. |
| **LSM6DSO32** | High-G Watchdog | 416 Hz | ±32g Range fuer Klapper-Erkennung. BNO085 clippt bei ±8g — ein asymmetrischer Klapper kann kurzzeitig 5-8g erzeugen. |

### Warum nicht nur einen Sensor?

- **BNO085 allein:** Clippt bei Klappern, keine zuverlaessige Kollaps-Erkennung
- **LSM6DSO32 allein:** Kein Magnetometer, kein on-chip Fusion → wir muessten eigenen AHRS implementieren (komplex, fehleranfaellig, ~2 Wochen Mehraufwand)
- **Beide zusammen:** BNO085 fuer die "sanfte" Physik (Orientierung, Schwerkraft-Subtraktion), LSM6DSO32 fuer die "harte" Physik (Klapper-G-Kräfte)

### BNO085 Linear Acceleration

Der BNO085 liefert direkt ein "Linear Acceleration" Report — das ist die gemessene Beschleunigung **minus Schwerkraft**, bereits in den Erd-Referenzrahmen rotiert. Genau das braucht der KF4D als Input. Kein eigener Code fuer Gravitations-Subtraktion noetig.

Pilot-Koerperbewegungen (Gewichtsverlagerung, Bremsen) erzeugen ~±0.3-0.5 m/s² Rauschen. Der KF4D handhabt das ueber seine adaptive Varianz.

---

## 4. Dual-BMP581 Differenzdruck (Δp)

### Stand der Technik

- **ParaBaro (Aviometrics):** Einziges kommerzielles System fuer Druckueberwachung am Gleitschirm. Sensoren sitzen **im Fluegel**, nicht am Gurtzeug. Meldet Druck-Aenderungen 2-3 Sekunden vor sichtbarem Klapper.
- **FLYSENS:** Sechs Differenzdruck-Sensoren auf der Kappe verteilt fuer aerodynamisches Mesh-Sensing. Forschungsprojekt.

### Unsere Situation: Gurtzeug-Level

Unsere zwei BMP581 sitzen nebeneinander auf dem Board (25 mm Abstand), also am Gurtzeug, nicht im Fluegel. Das Signal ist **deutlich schwaecher**:

- **Erwartetes Signal bei Klapper:** ~5-20 Pa am Piloten-Torso (abhaengig von Entfernung, Fluegelgroesse, Schwere)
- **Sensor-Rauschen:** ~0.16 Pa RMS bei 32× OSR → **SNR theoretisch 30-125:1** — ausreichend
- **Aber:** Thermische Drift zwischen den Sensoren, Windboeen, Koerperbewegungen erzeugen Stoersignale

### Implementierungs-Strategie

1. **DC-Offset-Tracking:** Langsamer gleitender Mittelwert (τ = 60s) zwischen den Sensoren. Kompensiert Drift.
2. **AC-Signal-Extraktion:** Bandpass 0.5-5 Hz — Klapper-relevanter Frequenzbereich
3. **Turbulenz-Score:** RMS des Δp-Signals ueber 2s-Fenster. Hoher Score = unruhige Luft.
4. **Klapper-Alarm:** Schneller Δp-Spike > Schwelle innerhalb < 200 ms

### Ehrliche Einschaetzung

**Experimentelles Feature, kein Sicherheits-Feature.** Gurtzeug-Level Δp fuer Klapper-Vorwarnung hat **keinen Praezedenfall**. Die Physik ist plausibel, aber ob das SNR in der Praxis reicht, wissen wir erst nach echten Flugtests. Das ist genau das, wofuer die Kruecke da ist — validieren.

---

## 5. Thermik-Erkennung + Adaptive Umschaltung

### Thermik-Detektor

| Parameter | Wert | Quelle |
|-----------|------|--------|
| Sliding Window Laenge | **8-10 s** (nicht 20s!) | Community-Konsens, Naviter/XCTracer |
| Thermik-Schwelle | > 0.5 m/s mittlerer Climb | Stodeus/XCTracer |
| Kreisen-Erkennung | Turn-Rate > 12°/s ueber > 5s | IMU-basiert |
| Kern-Zentrierung | Bester 90°-Sektor pro Kreis | Gaggle/Naviter Thermal Assistant |

### Modus-Umschaltung

- **Cruise-Modus** (Default): Niedrigeres Kalman-Prozessrauschen → glatte Anzeige, weniger Rauschen
- **Thermik-Modus** (Auto): Hoeheres Prozessrauschen → schnelle Reaktion auf Aenderungen
- **Trigger:** Climb > 0.5 m/s ODER Turn-Rate > 15°/s → Thermik-Modus
- **Rueckschaltung:** Kein Steigen und kein Kreisen fuer > 10s → Cruise-Modus

### Thermik-Kern-Zentrierung (spaeter, fuer Display-Ticket 10)

Pro Umdrehung im Kreis: Mittleren Climb in jedem 90°-Sektor loggen. Staerkster Sektor = Richtung zum Kern. Visualisierung als Thermal Assistant auf dem E-Paper. Braucht zuerst stabilen Heading aus BNO085.

---

## 6. Temperatur + Dichtehoehenkorrektur

### Das Problem

Die Standard-Hoehenformel `h = 44330 × (1 - (p/p₀)^(1/5.255))` nimmt ISA-Standardatmosphaere an (15°C am Boden). In der Realitaet weicht die Temperatur ab.

### Fehler ohne Korrektur

Die **4%-Regel**: ~4 ft Fehler pro 1°C ISA-Abweichung pro 1000 ft Hoehe.

| Hoehe | ISA-Abweichung | Fehler |
|-------|----------------|--------|
| 1000 m | +10°C | ~13 m |
| 2000 m | +10°C | ~27 m |
| **3000 m** | **+10°C** | **~40 m** |
| 3000 m | -10°C | -40 m (gefaehrlich — Hoehe wird ueberschaetzt!) |

### Loesung

Korrigierte Formel:
```
h = (T_actual / L) × (1 - (p/p₀)^(R×L / (g×M)))
```
mit T_actual aus dem **SHT40** (nicht BMP581!).

**Warum SHT40 statt BMP581-Temperatur?**
Der BMP581 erwaermt sich intern um 1-2°C durch den Messbetrieb. Das verfaelscht die Temperaturmessung. Der SHT40 sitzt separat und liefert praezisere Umgebungstemperatur.

### Bonus: Wolkenbasis-Schaetzung (Ticket W1)

Mit SHT40-Temperatur und -Feuchte kann die lokale Wolkenbasis geschaetzt werden:
```
Spread = T - Taupunkt
Wolkenbasis ≈ Spread / 0.8 × 100  (in Meter ueber Grund)
```
Grobe Schaetzung, aber fuer Piloten nuetzlich. Kommt in Ticket W1.

---

## 7. BLE-Protokoll: Was der Vario Core liefern muss

Aus dem Reverse-Engineering des Flight-Buddy-KI Flutter-Codes:

### Primaeres Protokoll: Binary (nicht NMEA!)

Die App erwartet **drei BLE-Characteristics** mit binaerem Format:

**VARIO-Packet (12 Bytes, 10 Hz):**
```
[0xAA] [vz:int16÷100] [intVz:int16÷100] [pressure:uint32 Pa] [temp:int16÷100] [flags:uint8]
```
- `vz` = aktueller Vario-Wert (aus KF4D)
- `intVz` = geglätteter Vario (laengeres Integral)
- `pressure` = Rohdruck in Pa (BMP581 #1)
- `temp` = Temperatur in °C (SHT40)
- `flags` = Bit 0: isFlying, Bit 1: isThermik, Bit 2: gpsOk

**GPS-Packet (16 Bytes, 1 Hz):**
```
[0xBB] [lat:int32÷1e7] [lon:int32÷1e7] [alt:int16 m] [speed:uint16÷10 km/h] [heading:uint16÷10°] [sats:uint8]
```

**STATUS-Packet (6 Bytes, 1 Hz):**
```
[0xCC] [battery:uint8 %] [mode:uint8] [fanet:uint8] [uptime:uint16 min]
```

### Service UUID
```
E7F5A3B1-2C8D-4E6F-9A0B-3D1C5E7F9A2B
```
Die App erkennt das Geraet automatisch an dieser UUID.

### Design-Konsequenz fuer Ticket 2

Der Vario Core muss folgende Werte mit folgenden Raten liefern:
- **10 Hz:** vz, intVz, pressure, temp, isFlying, isThermik
- **1 Hz:** batteryPct, mode, fanetStatus, uptime

Die Output-Structs werden direkt passend zu den BLE-Packets designed. So ist Ticket 5 (BLE) nur noch "Struct in Bytes packen und senden" — kein Protokoll-Design mehr noetig.

---

## 8. Referenz-Projekte (absteigend nach Relevanz)

| Projekt | Was wir uebernehmen | Link |
|---------|---------------------|------|
| **har-in-air/ESP32_IMU_BARO_GPS_VARIO** | KF4D-Algorithmus, adaptive Varianz, Jupyter-Notebooks | github.com/har-in-air |
| **har-in-air/ESP32C3_BLUETOOTH_AUDIO_VARIO** | BLE-Audio-Vario-Architektur | github.com/har-in-air |
| **iltis42/XCVario** | Forward-predicting Kalman, Netto-Vario | github.com/iltis42 |
| **Pataga IMU Kalman Vario** | Mathe-Dokumentation der IMU-Baro-Fusion | pataga.net |
| **BlueFlyVario Blog** | IIR-Filter Vergleich, Praxis-Erfahrung | blueflyvario.blogspot.com |
| **XCTracer** | Benchmark fuer "was ist moeglich" | xctracer.com |
| **Aviometrics ParaBaro** | Δp-Konzept am Flügel | aviometrics.com |

---

## 9. Risiken + Mitigationen

| Risiko | Wahrscheinlichkeit | Mitigation |
|--------|---------------------|------------|
| Δp-Signal am Gurtzeug zu schwach | **Mittel** | Feature degradiert graceful zu "Turbulenz-Anzeige" statt "Klapper-Warnung" |
| BNO085 SHTP-Init instabil | **Niedrig** | Bekanntes Problem, Adafruit-Lib handhabt es. Reset-Pin muss HIGH sein. |
| BMP581 Dual-Adresse funktioniert nicht (ADR-Pin) | **Niedrig** | Einfacher Hardware-Fix: Dupont-Kabel auf GND pruefen |
| Kalman-Tuning braucht echte Flugdaten | **Sicher** | Default-Parameter aus har-in-air als Startpunkt, Feintuning im Flug |
| 100 Hz IMU + 50 Hz Baro + Kalman + Δp ueberlasten ESP32-S3 | **Sehr niedrig** | KF4D braucht 0.46 ms/Iteration, ESP32-S3 hat 240 MHz + PSRAM |

---

## 10. Empfehlung

**Volle Umsetzung.** Die Recherche zeigt, dass unser Hardware-Setup jedes Feature unterstuetzt. Die Algorithmen sind erprobt (KF4D), die Libs verifiziert (Ticket 1), und der BLE-Vertrag ist klar (Binary, nicht NMEA).

**Was wir jetzt bauen (ohne Hardware):** Kompletter Algorithmus-Code, kompiliert und bereit zum Flashen. Saubere Modul-Struktur, die in spaeteren Tickets (BLE, Display, ML) einfach angebunden wird.

**Was auf Hardware wartet:** Filter-Tuning, Δp-Kalibrierung, Thermik-Schwellen — alles konfigurierbar, mit sinnvollen Defaults aus der Literatur.

---

*Naechster Schritt nach CEO-Approval: GATE-A (Dateistruktur + Types + Hoehenformel)*
