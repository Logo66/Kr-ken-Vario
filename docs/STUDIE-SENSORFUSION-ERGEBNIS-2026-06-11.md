# STUDIE — Sensor-Fusion / Kalman: IMU-gestütztes Vario + Lage + Startplatz-Ruhe
## Ergebnis-Bericht · Code-KRÜCKE · 2026-06-11

> **Art:** Recherche + Konzept. **KEIN Code gebaut.** Dieser Bericht ist das Ergebnis = Entscheidungsvorlage.
> **Leitsatz (Ivo):** Grenzen ausloten, aber STABIL. Lieber konservativ-richtig als spektakulär-zappelig.
> **Methodik:** Code-Ist-Analyse (unsere Firmware) + Literatur/Open-Source-Recherche (4 parallele Rechercheure,
> Quellen unten mit URLs). Jede Aussage ist markiert: **[BELEGT]** = Lehrbuch/Quellcode/Datenblatt belegt ·
> **[FLUGTEST]** = Mechanismus belegt, exakte Zahl muss am Gerät/in der Luft bestätigt werden.

---

## 0. MANAGEMENT SUMMARY — die Entscheidung in 7 Sätzen

1. **Fusion lohnt sich und ist bewährt, kein Forschungswagnis.** Baro-Vario hat prinzipbedingt Latenz (Druck muss sich erst ändern); die IMU spürt die Vertikalbeschleunigung sofort. Realistischer Gewinn: **~1 s weniger Verzug beim Thermikeinstieg** [FLUGTEST – Mechanismus BELEGT, Zahl ist Hersteller-/Felderfahrung].
2. **Wir bauen nicht neu, wir erweitern.** Unsere Firmware hat **schon** ein 2-State-Baro-Kalman (`kalman_vario.h`). Die Fusion = dieselben zwei Zustände, aber die **Beschleunigung kommt als Steuergröße in den Vorhersageschritt**. Minimaler, evolutionärer Eingriff.
3. **Der eigentliche Schwierige ist die LAGE, nicht der Filter.** In der Dauerkurbel misst der Sensor 1.15–1.41 g plus gekippte Gravitation. Ohne Lagekorrektur „sieht" das Vario in **jeder** Thermikkurve ein Steigen, das keins ist. Lösung: Lage aus **Accel + Gyro** (der **Gyro liegt heute brach!**) via Mahony-Komplementärfilter, plus Accel-Gating.
4. **Das Vario braucht den BMM350-Kompass NICHT.** „Unten" (Pitch/Roll) kommt aus Accel+Gyro (6DOF). Der Magnetometer liefert nur Heading/Yaw = Kompass-**Anzeige**. **Buzzer-Störung trifft die Anzeige, niemals das Steigsignal.** → Vario auf 6DOF bauen, Kompass separat und „best effort".
5. **Startplatz-Ruhe über IMU:** Stillstandserkennung (ZUPT: Accel-Varianz + Gyro-Energie) wird das **dominante Gate**. Steht die IMU still, wird GPS-Speed-Jitter (im Stand real bis ~10 km/h möglich) hart unterdrückt. Flugstatus nur bei **Mehr-Signal-Bestätigung** (IMU-Bewegung UND GPS-Speed, ODER Höhentrend) — nie GPS allein.
6. **Empfohlene Filterwahl:** 2-State-Inertial-Filter. Variante A = **Komplementärfilter (feste Gains)** für maximale Stabilität/Wartbarkeit; Variante B = **2-State-Kalman (zwei Tuning-Knöpfe)**. Beide sind mathematisch dasselbe (Higgins 1975). Empfehlung: **mit B starten** (passt auf unser bestehendes Kalman), Gains einfrieren sobald getunt = automatisch A.
7. **Fallback ist HEILIG:** Das reine Baro-Vario bleibt jederzeit der Rückfall. Fusion ist rein additiv und schaltet bei Sensorausfall/Instabilität sauber auf Baro zurück.

**Bau-Empfehlung:** Stufe 1 (sicher, jetzt machbar) = Startplatz-Ruhe + 6DOF-Lage + Accel ins bestehende Kalman mit Gating + harter Baro-Fallback. Stufe 2 (Kür, später) = GPS-Zentripetal-Korrektur, adaptives Messrauschen, 2. BMP581 als Redundanz, BMM350-Kompassanzeige. **Erst nach Architekt-OK wird ein Bau-Ticket geschrieben.**

---

## 1. AUSGANGSLAGE — was die Hardware HEUTE wirklich tut

Aus der Code-Analyse (`src/main.cpp`, `src/kalman_vario.h`, `src/flight_detect.h`):

| Baustein | Stand heute | Detail |
|---|---|---|
| **Vario** | ✓ aktiv | **2-State-Kalman**, Zustand `[altitude, vario]`, `Q_alt=0.02`, `Q_var=0.5`, `R=0.5`, ~20 Hz. `vario_avg` = 20-s-Mittel. **Reines Baro, keine IMU-Fusion.** |
| **BMP581 #1** | ✓ aktiv | ODR 50 Hz, Druck 16× Oversampling, roh per I²C jeden Loop gelesen. |
| **BMP581 #2** | ✗ ungenutzt | Adresse 0x46 deklariert, nie initialisiert. **Reserve für Redundanz vorhanden.** |
| **LSM6DSO32 Accel** | ✓ teil-aktiv | 104 Hz, ±16 g — aber es wird **nur der Betrag** `\|a\|` gelesen (für G-Spitze im Flug). Keine Achsen-/Vertikalkomponente. |
| **LSM6DSO32 Gyro** | ✗ **brach** | Sensor konfiguriert, **Gyro wird nie ausgelesen.** Das ist der Schlüssel für die Lage — schon da, kostet nur Auslesen. |
| **BMM350 Kompass** | ✗ nicht implementiert | Kein Code. Heading kommt heute aus **GPS-Kurs** (`gps.course.deg()`), <2 km/h auf 0 gesetzt. |
| **GPS** | ✓ aktiv | TinyGPSPlus 9600 Bd. Speed roh, **<3 km/h → 0** geklemmt. ~1 Hz. |
| **Starterkennung** | ✓ aktiv | GROUND→FLYING: Speed >20 km/h, Fix, ≥4 Sat, **10 s gehalten**. FLYING→LANDED: Speed <5 km/h UND Vario <0.3 m/s, **30 s**. |
| **Buzzer** | (kein Code gefunden) | Im Firmware-Stand kein Beeper-Code; Architekt-Hinweis zur Magnetfeld-Störung betrifft die Hardware-Platzierung. |
| **Loop / RAM** | 50 Hz (`delay(20)`) | Kalman effektiv ~20 Hz. PSRAM 8 MB, davon ~480 KB Kontur-Pool. CPU-Reserve groß. |

**Wichtige Diskrepanz zum Aufräumen (für den Architekten):** In der Repo-Doku tauchen ein **BNO085** (9-DOF mit *eingebauter* Fusion) und ein **„KF4D"** (4-State-Filter) als Spec auf — im echten Code aber **nicht**. Gebaut/benutzt wird **LSM6DSO32** (rohe 6 Achsen, Fusion müssen wir selbst rechnen). Falls real doch ein BNO085 bestückt wäre, würde er Lage + linearbeschleunigung fertig liefern und das halbe Lage-Problem (Kap. 3) verschwinden. **→ Bitte Hardware-Bestückung bestätigen, bevor das Bau-Ticket geschrieben wird.** Dieser Bericht plant konservativ auf LSM6DSO32 + BMM350 (= Ticket-Hardware).

**Fazit:** Wir stehen besser da als gedacht — 2-State-Kalman existiert, Gyro und 2. Baro sind vorhanden aber ungenutzt. Die Fusion ist eine **Erweiterung**, kein Neubau.

---

## 2. RECHERCHE-ZUSAMMENFASSUNG — bewährte Verfahren & Referenzen

### 2.1 Es gibt ZWEI Dinge, die „Kalman-Vario" heißen — nicht verwechseln
- **(I) Baro-only 2-State-Kalman** `[Höhe, Steigen]` — „Beschleunigung" ist nur ein **Tuning-Skalar**, kein echter Sensor. Das fahren **BlueFlyVario** und **XCSoar**. Das ist exakt **unser heutiger Stand**. Glättet, aber schlägt die Baro-Latenz prinzipiell nicht.
- **(II) Echtes Inertial-Vario** — ein **gemessener** Vertikal-Accel treibt die Vorhersage, das Baro **korrigiert**. Das ist der latenzarme Entwurf, den wir wollen. Referenzen: **Robin Lilja AltitudeKF** (2-State), **prunkdump/GNUVario** (2-State), **har-in-air KF4d** (4-State mit Bias). [BELEGT]

### 2.2 Open-Source-Referenztabelle (mit Filtertyp, Zustand, Konstanten)
| Projekt | Filter | Zustand | Raten | Konstanten (Start) |
|---|---|---|---|---|
| **BlueFlyVario** (A. Dickie) | Baro-only 2-State | `[x, v]` | Baro 50 Hz | R=0.2, Q=1.0, altDamp=0.05 |
| **XCSoar** `KalmanFilter1d` | Baro-only 2-State | `[x, v]` | 50 Hz | var_z=0.25, var_accel=1.0 |
| **Robin Lilja AltitudeKF** | **Inertial** 2-State (Accel=Steuergröße) | `[h, v]` | 100 Hz | Q_accel≈0.1, R_alt≈0.1 |
| **prunkdump / GNUVario** | **Inertial** 2-State | `[p, v, a]` | je Sample | σ_p=0.1, σ_a=0.3 |
| **har-in-air ESP32 Vario** | **Inertial** 4-State + Bias + adaptiv | `[z, v, a, b]` | IMU 500 Hz, Baro 50 Hz | ⚠ in **cm/cm²**! nicht 1:1 kopieren |

### 2.3 Komplementärfilter vs. Kalman — die theoretische Pointe
**Ein Komplementärfilter IST ein stationäres (festverstärktes) Kalman-Filter** für genau dieses Problem — bewiesen in **Higgins (1975), IEEE T-AES**. Man wählt also nicht zwischen „genau" und „ungenau", sondern zwischen **zeitvariabler Verstärkung** (volles KF) und **fester Verstärkung** (Komplementär). Fürs Vario:
```
v = Hochpass(∫ a_vertikal dt)  +  Tiefpass(d/dt Baro-Höhe)
```
Der Accel-Pfad gibt die schnelle Führung, das Baro die driftfreie Langzeit-Wahrheit. Eine Übergangs-Zeitkonstante τ (typ. **2–6 s**) ist das Pendant zur festen KF-Verstärkung. [BELEGT]

### 2.4 Ehrliche Recherche-Befunde
- **„Badura-Algorithmus":** **nicht auffindbar.** Keine Primärquelle, kein Repo, kein Forum (auch DE/Segelflug) belegt einen so benannten, eigenständigen Algorithmus. Die real verbreiteten Arduino-Linien sind **har-in-air** und **prunkdump/GNUVario**. **Empfehlung: den Begriff fallen lassen oder vom Zitierenden eine Primärquelle nachfordern.** [BELEGT: nicht verifizierbar]
- **Latenzgewinn „~1 s":** Hersteller-/Felderfahrung (STODEUS, XC Tracer), kein kontrollierter Benchmark. Der **Mechanismus** ist solide. Härteste öffentliche Vergleichsbasis: har-in-airs `compare_kf2_kf3_kf4.ipynb` auf echten Fluglogs. [FLUGTEST für die exakte Zahl]

---

## 3. FALLSTRICK 1 — SCHWERKRAFT-TRENNUNG (die eigentliche Schwierigkeit)

### 3.1 Das Grundproblem
Ein Accelerometer misst **spezifische Kraft** `f = a_eigen − g` im **Körperframe**. Fürs Vario brauchen wir die **erd-vertikale** Komponente der Eigenbeschleunigung. Also: Körper-Accel mit der **Lage** ins Erdframe drehen, 1 g abziehen, Z-Komponente nehmen:
```
a_vertikal = [ R(Lage) · f_körper ]_z  + g
```
har-in-air macht das in einer Zeile (Quaternion-Rotationszeile · Körper-Accel − 1 g). [BELEGT]

### 3.2 Warum die Lage SO genau sein muss
Ist die Lage um einen Winkel ε falsch, leckt ein Teil der vollen Gravitation ins „Vertikal"-Signal:
```
falsche Vertikalbeschleunigung ≈ g · sin(ε) ≈ 0,17 m/s² PRO GRAD Lagefehler
```
1° → 0,17 m/s²; 3° → 0,51 m/s²; 5° → 0,85 m/s². Ein **stationärer** Lage-Bias von 2–3° wird zu einem **stationären Falsch-Steigen/Sinken**. **Lage-Genauigkeit = Vario-Genauigkeit.** [BELEGT, reine Trigonometrie]

### 3.3 Der Dauerkurbel-Fall (genau unser Paragleiter-Problem)
- In der sauberen Kurve gilt **Lastfaktor n = 1/cos(Schräglage)**: 30° → 1,15 g; 45° → 1,41 g. Körper-Z liest also **>1 g in stationärer, ebener Kreiserei** → naive Integration erfindet Steigen in jeder Thermik. [BELEGT, Aerodynamik]
- **Zweiter, tieferer Effekt:** AHRS-Filter (Mahony/Madgwick) nutzen den Accel als Langzeit-Referenz für „unten", um Gyro-Drift zu stoppen. In der Dauerkurve ist die gemessene Kraft aber **Gravitation + Zentripetal** → die Referenz „unten" ist verkippt, die Lageschätzung **driftet in die Kurve hinein** (~**10° Lagefehler je 0,2 g Zentripetal**). Selbstverstärkend: Kurve verfälscht Lage, falsche Lage verfälscht Vertikal-Accel. [BELEGT – VectorNav, bot-thoughts]

### 3.4 Wie echte Varios das entschärfen (3 Stufen)
- **(a) Accel-Betrags-Gating** — Accel nur dann ins AHRS, wenn `|a|` nahe 1 g (= plausibel „nur Gravitation"). har-in-air real: **0,75–1,25 g** (enger als die im Ticket genannten 0,6–1,4 g!), sonst läuft die Lage kurz **nur auf dem Gyro** weiter. Verhindert das *Einspeisen* des Kurvenfehlers, **entfernt** den Zentripetal-Anteil aber nicht. [BELEGT aus Quellcode]
- **(b) x-io „Fusion" (moderner Madgwick-Nachfolger)** — verwirft Accel über **Winkel-Schwelle (Default 10°)** statt hartem Betragsfenster, mit **Auto-Recovery** bei Dauerbeschleunigung. Sauberer dokumentiert, starker Kandidat. [BELEGT]
- **(c) GPS-gestützter Zentripetal-Abzug** — der einzige Weg, der wirklich **korrigiert** statt nur verwirft: `a_zentripetal = ω × v` (v aus GPS-Bodengeschwindigkeit, ω aus Gyro/Kursänderung) **vor** der Accel-Nutzung abziehen. Etablierte Patente (US 8,442,703 / US 6,456,905). **har-in-air macht das NICHT.** [BELEGT als Methode]
- **Glücksfall Paragleiter:** Bei langsamem Flug (~10 m/s) und Thermikradius 100–150 m ist die Zentripetalbeschleunigung nur **~0,07–0,1 g** — oft schon **innerhalb** des 0,75–1,25-g-Gates. Deshalb reicht **simples Gating** für viele PG-Varios praktisch aus. Echte GPS-Korrektur ist die **Kür (Stufe 2)**. [BELEGT-Mechanismus; Gewinn am PG = FLUGTEST]

---

## 4. FALLSTRICK 2 — FILTERSTRUKTUR (für DIESE Hardware)

### 4.1 Der minimal-invasive Vorschlag: unser 2-State-Kalman wird inertial
Wir behalten Zustand `[Höhe, Steigen]`. Wir ergänzen die **Vorhersage** um die gemessene, schwerkraftbereinigte Vertikalbeschleunigung `a` als Steuergröße:
```
Vorhersage (vom Accel getrieben):
    Höhe   ← Höhe + Steigen·dt + ½·a·dt²
    Steigen ← Steigen + a·dt
    Q      = σ_a² · [[dt⁴/4, dt³/2],[dt³/2, dt²]]
Korrektur (vom Baro):
    y = Baro_Höhe − Höhe ; K aus Kovarianz ; Höhe += K0·y ; Steigen += K1·y
```
Nur **zwei Tuning-Knöpfe**: `σ_a` (Vertrauen in den Accel → höher = schneller) und `R` (Baro-Rauschen → höher = ruhiger). Startwerte aus der Literatur (metrisch!): **σ_a ≈ 0,1–0,3 m/s²** (prunkdump nutzt 0,3), **R ≈ 0,1–0,5** (wir haben heute 0,5). [BELEGT]

### 4.2 Volles KF vs. Komplementär — Empfehlung
- **CPU ist kein Argument:** ein 2-State-Update kostet **~5–10 µs** auf dem ESP32-S3 (FPU). Bei 50–100 Hz bleibt riesig Reserve. [BELEGT]
- **Robustheit/Wartbarkeit spricht für feste Verstärkung (Komplementär):** keine Kovarianz, die „explodieren" kann, keine `dt`-Stolperfallen (XCSoar warnt explizit vor `var_accel·dt⁴` und resettet bei zu großem `dt`). Feste Gains sind log- und unit-test-bar.
- **Volles KF gewinnt** nur bei Reset-Einschwingen und optionaler Adaptivität (har-in-airs „bei hoher G mehr aufs Baro vertrauen") — das ist zugleich die Hauptquelle für Tuning-Zickigkeit.
- **Empfehlung:** Als **2-State-Kalman starten** (passt auf unseren Bestand, zwei Knöpfe). Sind die Gains getunt und stabil → **einfrieren = Komplementärfilter** = bestmögliche Dauerstabilität. Das **4-State (Accel-Bias)** nur, falls sich der Accel-Bias als nicht kalibrierbar erweist. [BELEGT]

### 4.3 Abtastraten
Accel/Gyro **100–200 Hz** (Lead kommt vom Accel — schneller als Baro abtasten; LSM6 kann 104/208 Hz). Baro **50 Hz** reicht (BMP581 kann bis 240 Hz, <0,1 Pa ≈ 0,8 cm Rauschen — Baro ist **nicht** der Engpass). [BELEGT, Datenblatt BMP581]

---

## 5. LAGE über LSM6DSO32 (+ BMM350?) — braucht das Vario den Kompass?

### 5.1 Klares Verdikt: NEIN, das Vario braucht den BMM350 nicht
- **Roll & Pitch** (= Richtung „unten", alles was die Schwerkraft-Trennung braucht) sind aus **Accel + Gyro allein** beobachtbar (6DOF):
  `Roll = atan2(a_y, a_z)`, `Pitch = atan2(−a_x, √(a_y²+a_z²))`, gestützt durchs Gyro.
- **Heading/Yaw** ist aus dem Accel **prinzipiell nicht** bestimmbar (Drehung um die Lotrechte ändert den Gravitationsvektor nicht) → dafür braucht es den Magnetometer. **Aber Yaw geht nie in die erd-vertikale Projektion ein.** [BELEGT – AHRS-Theorie]

**Folgerung (zentral für die Entscheidung):** **Buzzer-Magnetstörung verfälscht die Kompass-ANZEIGE (Heading), niemals das Steigsignal.** Wir können das **Vario mit voller Sicherheit auf 6DOF (Accel+Gyro)** bauen und den Kompass als **separates, best-effort-Anzeige-Feature** behandeln. [BELEGT]

### 5.2 AHRS-Wahl für den ESP32-S3
**Empfehlung: Mahony-Komplementärfilter.** PI-Regler, **schätzt den Gyro-Bias online mit** (genau das, was die „Gyro trägt durch den langen Steigflug"-Sorge adressiert), feste Gains, billig, stabil — und genau das, was har-in-airs Vario fährt. Alternative: **x-io Fusion** (eingebaute 10°-Accel/Mag-Verwerfung + Recovery). Madgwick ist gleichwertig billig; EKF ist Overkill. Kanonische Quellen: Mahony 2008 (IEEE TAC), Madgwick 2010 (x-io Report). **Gyro auslesen ist Pflicht** — er liefert die Kurzzeit-Lage, auf die das Gating baut. [BELEGT]

### 5.3 Kompass-Realität (wenn BMM350 als Anzeige doch kommt — Stufe 2)
- **Statische Kalibrierung Pflicht:** Hard-Iron (additiver Offset) + Soft-Iron (3×3-Matrix). [BELEGT]
- **Dynamische Buzzer-Störung** ist NICHT statisch kalibrierbar (skaliert linear mit Strom, wie ArduPilot „CompassMot"). Schon **<1 µT** verschiebt das Heading um Grad. Gegenmittel: **räumlich trennen** (10–15 cm, Feld ~1/r³), Layout (Rückstrompfad weg vom Sensor), **Mag-Updates während des Piepsens aussetzen** (Gyro trägt kurzfristig). [BELEGT]
- **Toleranz:** Accel+Gyro-Lage ist gegen kurzen Mag-Aussetzer sehr robust — der Gyro trägt, der Mag korrigiert nur langsam. Ein Piepser verursacht höchstens kurzes Heading-Zittern in der **Anzeige**, **null** Effekt auf Pitch/Roll/Vertikal-Accel. [BELEGT-Begründung; exakte µT-Störung am Gerät = BENCH-TEST]

---

## 6. STARTPLATZ-RUHE — Speed stillstellen + sichere Starterkennung

### 6.1 Wie sehr zappelt GPS im Stand?
- **Doppler-Geschwindigkeit** (gute Module, u-blox-Spec ~**0,05 m/s**): cm/s-Bereich. [BELEGT]
- **Worst Case Startplatz** (schlechte Himmelssicht, Körperabschattung, Multipath am Hang/Baum): stationär **bis ~10 km/h** gemessen (Felderfahrung). **Unsere 3-km/h-Klemme ist vernünftig, aber allein nicht sicher.** [BELEGT]

### 6.2 ZUPT — Stillstand über die IMU erkennen (das tragende Gate)
Standard aus der Inertialnavigation (Skog 2010). Über ein kurzes Fenster `W` Teststatistik bilden, unter Schwelle = steht:
| Detektor | Statistik | Kanon. Schwelle (Fuß-INS, 100 Hz, W=100) |
|---|---|---|
| MV (Accel-Varianz) | var(‖a‖) | τ ≈ 0,1 m²/s⁴ |
| MAG (Accel-Betrag) | mean(‖‖a‖−g‖) | τ ≈ 0,3 m/s² |
| **ARE (Gyro-Energie)** | (1/W)·Σ‖ω‖² | τ ≈ 1,0 (rad/s)² |
| **SHOE (Accel+Gyro)** | kombiniert | best |
**Gyro-basiert (ARE) bzw. kombiniert (SHOE) gewinnen** — der Gyro ist im Stand am verlässlichsten ruhig. [BELEGT]

**Praktischer Detektor für uns** (billig, keine Matrix), Schwellen **lockerer** als Fuß-INS, weil Körperschwanken + Windbuffeting am Start *normal* ist und NICHT „Bewegung" heißen darf:
```
still := var(|a|) < τ_a  UND  mittel(|ω|²) < τ_ω        über 0,5–1,0 s Fenster
Startwerte [FLUGTEST]:  τ_a ≈ 0,2–0,5 m²/s⁴ ,  τ_ω ≈ (0,05–0,15 rad/s)² (≈ 3–9 °/s)
```
**Anti-Zappel (Schmitt-Trigger):** zwei verschiedene Schwellen für „still→bewegt" und „bewegt→still" + Mindest-Verweildauer. [BELEGT-Praxis]

### 6.3 Was echte Flugrechner tun (Quellcode)
- **XCSoar** (`FlyingComputer.cpp`): Start = Speed ≥ Schwelle **10 s gehalten** ODER Höhe AGL ≥ 300 m; Landung = Speed < **halbe** Startschwelle **30 s** UND **nicht steigend** („climbing"-Guard). [BELEGT verbatim]
- **LK8000** (`TakeoffLanding.cpp`): 10-s-Start-Verweildauer; **Paragleiter sind von der Höhen-Gate ausgenommen** (`!ISPARAGLIDER`) — weil PG-Starts tief & langsam sind und die AGL-Heuristik sonst **Fehl-Negative** erzeugt. **Wichtigste Praxis-Lehre für uns.** [BELEGT verbatim]

### 6.4 Empfohlene Verbund-Regel „ruhig am Boden" (zwei getrennte Automaten)
**A) Anzeige-Beruhigung (schnell, klemmt das Speed-Feld auf 0):**
```
Speed_Anzeige = 0   WENN   (IMU still, Kap. 6.2)  UND  (GPS-Speed < V_quiet)
V_quiet ≈ 5 km/h  [FLUGTEST]   (über dem Jitterband, unter „Loslaufen"; heute 3 km/h)
```
Weil **IMU-still** das dominante Gate ist, werden selbst 10-km/h-Multipath-Spikes unterdrückt, solange die IMU ruhig ist. **Das ist der Kern-Fix.** Hysterese: erst bei `V_quiet + Marge` (7–8 km/h) oder „IMU nicht mehr still" wieder anzeigen.

**B) Flugstatus-Latch (langsam, gated FANET-TX / IGC / Screen):**
```
START := (GPS-Speed > V_takeoff für ≥ T)  UND  (IMU NICHT still in dem Fenster)
         ODER (anhaltender Höhentrend |Δh| > 0,5 m/s für ≥ 10 s)   // Hang-/Soaring-Start
Startwerte [FLUGTEST]: V_takeoff ≈ 12–18 km/h ,  T ≈ 8–10 s [BELEGT 10 s]
LANDUNG := GPS-Speed < V_takeoff/2  UND  IMU still  UND  nicht steigend, ≥ 30–60 s
```
GPS **allein** kann den Flugstatus nie auslösen (IMU-Bestätigung Pflicht) → Multipath-Spike im Stand latcht nie. Der **Höhentrend-Zweig** fängt den langsamen Soaring-Start vom Hang (Fehl-Negativ-Schutz, LK8000-Lehre).

### 6.5 Fehlerfälle (ehrlich)
- **Wind schüttelt den Piloten (IMU bewegt, GPS ~0):** B verlangt zusätzlich GPS-Speed → kein Fehlstart. ✓ Behandelt.
- **Groundhandling / Kiten (bewegt, nicht in der Luft):** schwerster Fall. Mildern über Höhentrend-Zweig (flaches Kiten ⇒ kein Höhengewinn ⇒ bleibt „Boden") und V_takeoff über zügiger Kite-Geschwindigkeit. Schnelles Vorwärts-Kiten mit >12–18 km/h über 10 s ist vom Tracking kaum von echtem Start zu trennen → **bewusst konservativ Richtung „Boden" über den Höhen-Zweig.** [Rest-Risiko, FLUGTEST].
- **Langsamer Klippenstart (niedrige Bodengeschwindigkeit, Fehl-Negativ-Risiko):** durch Höhentrend-Zweig gefangen. ✓ Behandelt, Zahlen FLUGTEST.

### 6.6 Sicherheits-Rahmen (warum „lieber Boden")
Ein falsches „FLYING" ist teuer: es sendet einen **FANET-Typ-1-AIRBORNE-Tracking-Paket** (statt korrekt still oder Typ-7-Ground), füttert FLARM/OGN-Konspikuität mit einem **Geist-Luftfahrzeug**, startet IGC-Log zu früh und wirft die UI um. Die FANET-Spec **selbst** trennt Typ-1-Airborne vs. Typ-7-Ground. **Kostenasymmetrie:** Fehl-„FLYING" = Funkmüll + Geisterkontakt + kaputtes Log; Fehl-„Boden" = Tracking/Log startet ein paar Sekunden später. → **Bei Zweifel Boden/still. FANET-TX/IGC immer am langsamen Latch (B), nie an rohem GPS-Speed.** [BELEGT – FANET protocol.txt; XCSoar-Bug #937 „Start beim App-Start erkannt"]

> Hinweis: Unsere bestehende 20-km/h-/10-s-Regel ist gegen Fehlstart bereits recht robust. Der Gewinn liegt in **(i)** der IMU-Bestätigung (macht GPS-Jitter irrelevant), **(ii)** der ruhigen **Speed-Anzeige** am Boden, und **(iii)** dem Höhentrend-Zweig für langsame Starts.

---

## 7. STABILITÄT & RESSOURCEN (ESP32-S3)

- **CPU:** AHRS (Mahony, 100–200 Hz) + 2-State-KF (50–100 Hz) zusammen **< 100 µs/Zyklus** — Bruchteil unseres 20-ms-Loops. Kein Engpass. [BELEGT]
- **RAM:** Filterzustände sind ein paar Floats. ZUPT-Fenster (0,5–1 s @ 100 Hz) = ~50–100 Floats Ringpuffer. **Kein PSRAM nötig** (anders als der Karten-Puffer). [BELEGT]
- **Numerik:** feste Gains (Komplementär) umgehen die `dt`-Instabilität des vollen KF. Bei vollem KF: `dt`-Begrenzung + Reset wie XCSoar. [BELEGT]
- **Abtast-Disziplin:** Accel/Gyro mit stabiler Rate lesen (FIFO der LSM6 nutzen, nicht „wann der Loop grad mag") — ungleiche `dt` ist der Hauptfeind. [BELEGT]
- **Temperatur:** MEMS-Accel-Bias driftet mit Temperatur → spricht für Online-Bias (Mahony-I-Term fürs Gyro; optional 4-State fürs Accel). Baro-Absoluthöhe driftet mit Temperatur, das **Steigen** (Ableitung) ist davon weitgehend immun. [BELEGT]

---

## 8. ENTSCHEIDUNGSVORLAGE — Stufenplan (was bauen, was noch nicht)

### STUFE 1 — sicher, jetzt machbar, hoher Nutzen-Risiko-Quotient
1. **Gyro auslesen** (LSM6DSO32, 104/208 Hz, am besten via FIFO) — Voraussetzung für alles.
2. **6DOF-Lage (Mahony)** aus Accel+Gyro. **Ohne Magnetometer.** Liefert Pitch/Roll → „unten".
3. **Schwerkraft-Trennung** → erd-vertikale Eigenbeschleunigung `a_vertikal`, mit **Accel-Gating 0,75–1,25 g** (sonst Gyro-Coast).
4. **Inertial-Vario:** `a_vertikal` als Steuergröße in das **bestehende** 2-State-Kalman. Zwei Knöpfe (σ_a, R). Gains nach Tuning einfrieren = Komplementär.
5. **Startplatz-Ruhe:** ZUPT-Stillstand (Accel-Varianz + Gyro-Energie) → Anzeige-Gate (A) + IMU-Bestätigung im Flug-Latch (B) + Höhentrend-Zweig.
6. **Harter Fallback:** bei IMU-Ausfall/`a`-Implausibilität/Instabilität automatisch auf reines Baro-Kalman (heutiger Stand). Sichtbar geloggt.

### STUFE 2 — Kür, später, nach bewährter Stufe 1
7. **GPS-Zentripetal-Korrektur** (`a_c = ω×v` abziehen) — echte statt nur verworfene Kurvenkorrektur. Gewinn am langsamen PG eher klein (Kap. 3.4) → bewusst nachgelagert.
8. **Adaptives Messrauschen** (bei hoher G mehr aufs Baro) — har-in-air-Trick; mehr Tuning-Risiko.
9. **2. BMP581** als Redundanz/Mittelung (liegt brach).
10. **BMM350-Kompassanzeige** (Hard/Soft-Iron-Kalibrierung + Mag-Gate während Piepsen). **Getrennt vom Vario**, weil Vario sie nicht braucht.

### Explizit (noch) NICHT bauen
- Kein volles adaptives/4-State-KF in Stufe 1 (Tuning-Zickigkeit gegen Ivos „stabil").
- Kein BMM350 als Vario-Voraussetzung (unnötig + Buzzer-Risiko).
- Keine Änderung am bestehenden Baro-Vario, bis die Fusion im Flug bewiesen ist.

---

## 9. RISIKEN EHRLICH — und was nur der Flugtest klärt

| Risiko | Schwere | Abfederung | Klärung |
|---|---|---|---|
| **Lage-Drift in langer Dauerkurbel** (Gyro-Bias trägt minutenlang) | hoch | Mahony schätzt Gyro-Bias online; Gating; Stufe-2-GPS-Korrektur | **[FLUGTEST]** echte Kurbel mehrere Minuten loggen |
| **Falsch-Steigen durch Lage-Bias** (0,17 m/s²/°) | hoch | sauberes 6DOF, Accel-Gating, Baro-Anker zieht zurück | **[FLUGTEST]** Steigen ruhend vs. kurbelnd |
| **Accel-Bias (Temperatur)** | mittel | Ruhe-Kalibrierung beim Boot; optional 4-State | Bench + **[FLUGTEST]** |
| **ZUPT-Schwellen am Brust-/Cockpit-Mount** (Fuß-INS-Zahlen passen nicht 1:1) | mittel | Struktur belegt, Zahlen lockerer angesetzt | **[FLUGTEST]** Stillstand + Start aufzeichnen |
| **Kiten vs. echter Start** | mittel | Höhentrend-Zweig, V_takeoff hoch, „bei Zweifel Boden" | **[FLUGTEST]** — das gezielt testen |
| **Einheiten-Falle** (har-in-air in cm) beim Konstanten-Übernehmen | niedrig | nur metrische Referenzen (Lilja/prunkdump) übernehmen | Code-Review |
| **Hardware-Unklarheit** (BNO085/KF4D in Doku, LSM6 im Code) | — | auf LSM6 geplant | **Bestückung bestätigen** vor Ticket |

**Grundsatz:** Jede Stufe-1-Komponente ist einzeln scharf-/abschaltbar und fällt sauber aufs Baro-Vario zurück. Wir nähern uns dem Limit additiv, nie durch Abriss des Bewährten.

---

## 10. QUELLEN (Auswahl, mit URL)

**Filter / Vario-Fusion**
- Higgins (1975), *A Comparison of Complementary and Kalman Filtering*, IEEE T-AES 11(3):321–325 — https://ieeexplore.ieee.org/document/4101411/
- Robin Lilja, AltitudeKF (2-State inertial) — https://github.com/rblilja/AltitudeKF
- prunkdump / GNUVario `kalmanvert` — https://github.com/prunkdump/arduino-variometer
- har-in-air ESP32 IMU/BARO/GPS Vario (KF4d, Gating, Mahony) — https://github.com/har-in-air/ESP32_IMU_BARO_GPS_VARIO
- XCSoar `KalmanFilter1d` — https://github.com/XCSoar/XCSoar/blob/master/src/Math/KalmanFilter1d.cpp
- BlueFlyVario — https://github.com/alistairdickie/BlueFlyVario_Android
- BMP581 Datenblatt (ODR/Rauschen) — https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp581-ds004.pdf

**Lage / AHRS / Kurvenfehler**
- Mahony, Hamel, Pflimlin (2008), IEEE TAC, doi:10.1109/TAC.2008.923738 — https://hal.science/hal-00488376/en/
- Madgwick (2010), x-io Report — https://x-io.co.uk/downloads/madgwick_internal_report.pdf
- x-io „Fusion" (Rejection + Recovery) — https://github.com/xioTechnologies/Fusion
- VectorNav, AHRS-Theorie (Kurvenfehler) — https://www.vectornav.com/resources/inertial-navigation-primer/theory-of-operation/theory-ahrs
- bot-thoughts, „AHRS, IMU and Acceleration" (10°/0,2 g) — https://www.bot-thoughts.com/2012/04/ahrs-imu-and-acceleration.html
- Magnetstörung: NXP AN4247 — https://www.nxp.com/docs/en/application-note/AN4247.pdf · ArduPilot CompassMot — https://ardupilot.org/copter/docs/common-magfit.html

**Startplatz-Ruhe / Flugerkennung**
- Skog et al. (2010), *Zero-Velocity Detection*, IEEE TBME — Übersicht: https://arxiv.org/abs/2008.09208
- ZUPT-Schwellen (Skog-Baselines) — https://bpb-us-e2.wpmucdn.com/faculty.sites.uci.edu/dist/e/700/files/2019/11/Adaptive-Threshold-for-Zero-Velocity-Detector-in-ZUPT-Aided-Pedestrian.pdf
- XCSoar `FlyingComputer.cpp` — https://github.com/XCSoar/XCSoar/blob/master/src/Computer/FlyingComputer.cpp
- LK8000 `TakeoffLanding.cpp` (Paragleiter-Ausnahme) — https://github.com/LK8000/LK8000/blob/master/Common/Source/Calc/TakeoffLanding.cpp
- GNSS-Geschwindigkeitsrauschen — https://insidegnss.com/how-does-a-gnss-receiver-estimate-velocity/
- FANET-Protokoll (Typ-1 Airborne / Typ-7 Ground) — https://github.com/3s1d/fanet-stm32/blob/master/Src/fanet/radio/protocol.txt

---
*Ende des Berichts. Kein Code geändert. Nächster Schritt: Architekt-Review dieser Vorlage → bei OK separates BAU-Ticket für Stufe 1.*
