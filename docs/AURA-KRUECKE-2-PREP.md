# AURA-KRUECKE-2-PREP · Verifikation vor Ticket 2

## Status
- **Erstellt:** 2026-05-18
- **Priorität:** P0 (blockiert Ticket 2)
- **Owner:** Claude (Chef-Architekt)
- **Executor:** Claude Code CLI — gebunden an `AURA-KRUECKE-EXECUTOR-RULES.md`
- **Branch:** `aura-kruecke/2-prep`
- **Geschätzt:** 2–3 h
- **Voraussetzung:** Ticket 1 abgeschlossen, Research-Report `AURA-KRUECKE-2-RESEARCH.md` gelesen

> ⚠️ **Zweck:** der Research-Report von Phase-0 enthält drei Annahmen, die wir **verifizieren oder verwerfen** müssen, bevor wir 13-21 Stunden in Ticket 2 investieren. Falls eine Annahme falsch ist, ändert sich die Ticket-Architektur. Lieber jetzt 2h Verifikation als später 5h Refactoring.

---

## Drei Verifikations-Aufgaben

### Aufgabe 1 · BLE-Protokoll-Reality-Check

Der Research-Report behauptet:

> Die App erwartet drei BLE-Characteristics mit binärem Format. Service-UUID: `E7F5A3B1-2C8D-4E6F-9A0B-3D1C5E7F9A2B`. Pakete `0xAA / 0xBB / 0xCC`.

**Diese Behauptung muss gegen den echten Code verifiziert werden.** Ohne Beweis ist sie eine Halluzination, die uns das ganze Ticket 5 versauen würde.

**Erlaubt:** read-only Scan in `C:\Users\Ivo\flight_buddy_ki\lib\core\ble\` (laut Master-Doc explizit erlaubt für Protokoll-Verständnis).
**Verboten:** dort schreiben oder Code modifizieren.

```
🛑 GATE-PREP-1 · BLE-Protokoll-Beweis
  Aktion: nichts ausführen, nur lesen + dokumentieren
  Liefere mir konkret:
    a) Datei und Zeilennummern wo die Service-UUID definiert ist
    b) Originalen Code-Block (max 30 Zeilen) der den Service registriert
    c) Datei und Zeilen für die Paket-Parser (0xAA, 0xBB, 0xCC oder andere Magic-Bytes)
    d) Originalen Code-Block der das Binär-Format parsed
    e) Suche zusätzlich nach "LK8EX1" und "NMEA" im Code:
       - wird parallel ein NMEA-Pfad unterstützt?
       - ist NMEA der Legacy-Pfad, Binary der neue?
       - welcher Pfad ist Default im aktuellen Master-Branch?
    f) Fazit in 3 Sätzen: stimmt der Research-Report, oder gibt es Abweichungen?

  Output-Format: Markdown-Datei in docs/research/ble-protocol-verified.md
  KEIN Code schreiben, KEINE Implementierungs-Annahmen treffen.
  OK?
```

**Mögliche Ergebnisse und Konsequenzen:**

| Ergebnis | Konsequenz für Ticket 2 |
|---|---|
| Research-Report stimmt exakt | Wir nutzen das Binary-Format. VarioState-Output-Struct wird direkt auf die 12-Byte-Pakete gemappt. |
| Research-Report stimmt grob, kleine Abweichungen | Anpassen, dokumentieren. Kein Drama. |
| Research-Report ist erfunden, App nutzt nur LK8EX1-NMEA | Ticket 5 wird LK8EX1-Sender. Ticket 2 bleibt unverändert, aber das VarioState-Struct wird *nicht* auf Binary-Pakete optimiert. |
| App nutzt beides (Binary + NMEA) | Wir bauen Binary (effizienter), NMEA als Fallback in T5. |

---

### Aufgabe 2 · Lizenz-Check har-in-air

Der Research-Report empfiehlt KF4D nach `har-in-air/ESP32_IMU_BARO_GPS_VARIO`. Bevor wir Code übernehmen oder uns inspirieren lassen, muss die Lizenz klar sein.

```
🛑 GATE-PREP-2 · Lizenz-Klärung
  Aktion: GitHub-Repos prüfen (Web-Lesen, keine Code-Ausführung)
  Liefere mir konkret:
    a) Lizenz von github.com/har-in-air/ESP32_IMU_BARO_GPS_VARIO (LICENSE-Datei zitieren)
    b) Lizenz von github.com/har-in-air/ESP32C3_BLUETOOTH_AUDIO_VARIO
    c) Lizenz von github.com/iltis42/XCVario (falls als Referenz genutzt)
    d) Lizenz von pataga.net Mathe-Dokumentation (falls Code-Snippets)
    e) Empfehlung: Code direkt rüberziehen (mit Attribution) oder nach Beschreibung neu implementieren?

  Output-Format: Markdown-Datei in docs/research/license-review.md
  Liste der Attribution-Pflichten falls Code übernommen wird.
  OK?
```

**Entscheidung danach durch Ivo:**
- **MIT/BSD/Apache** → Code-Übernahme mit Attribution unproblematisch
- **GPL** → entweder unser Projekt auch GPL machen, oder neu implementieren
- **Proprietary/keine Lizenz** → nur als Inspiration nutzen, kein Code-Copy

---

### Aufgabe 3 · Vario-Simulator-Skizze für Offline-Testing

Vor der echten Sensor-Implementierung in Ticket 2 bauen wir einen Daten-Simulator, der einen realistischen Vario-Daten-Strom *ohne* Hardware erzeugt. Das erlaubt:
- KF4D-Implementation während der 2.5 Wochen Hardware-Wartezeit komplett zu validieren
- Reproducible Tests (gleicher Sample-Stream, gleicher Algorithmus → gleiche Output → Regression-Tests möglich)
- Live-Demo für Technikum-Kollegen ohne Hardware

```
🛑 GATE-PREP-3 · Simulator-Spezifikation (nur Plan, kein Code)
  Aktion: Markdown-Spec schreiben
  Liefere mir konkret:
    a) Welche Simulator-Szenarien sind sinnvoll?
       Vorschlag aus Research:
       - "ruhe": konstanter Druck + leichtes Rauschen → Vario sollte 0±0.05 m/s
       - "rampe": linear steigender/fallender Druck → konstantes Vario
       - "sinus": 0.5 Hz Sinus ±50 Pa → Vario folgt smooth
       - "thermik": realistisches Thermik-Profil (rauh, mit Klapper-Impulsen überlagert)
       - "klapper": Druck-Spike + IMU-Beschleunigungs-Spike → kein Vario-Ausschlag erwartet
       - "kurbel": konstanter Druck-Anstieg + Zentripetal-Acceleration → Yaw-Rate-Switch triggert
    b) Format der Simulator-Input-Files (CSV mit time, pressure_pa, accel_xyz, gyro_xyz, quaternion)
    c) Format der Output-Files (CSV mit allen VarioState-Feldern)
    d) Wie wird der Simulator getrieben? Vorschlag: separater Build-Target in PlatformIO oder Standalone-Host-Build mit gcc
    e) Welche Pytest/Unit-Tests sollten den Simulator nutzen?

  Output-Format: Markdown-Datei in docs/research/vario-simulator-spec.md
  KEIN Code schreiben, nur Plan.
  OK?
```

**Strategischer Wert:** wenn die Hardware in 2.5 Wochen ankommt, geht Phase A von 3-5h auf 1h zurück, weil alle Algorithmen schon simulator-validiert sind.

---

## Akzeptanzkriterien (Gesamt-Ticket)

- [ ] `docs/research/ble-protocol-verified.md` existiert, beantwortet Aufgabe 1 vollständig
- [ ] `docs/research/license-review.md` existiert, beantwortet Aufgabe 2 vollständig
- [ ] `docs/research/vario-simulator-spec.md` existiert, beantwortet Aufgabe 3 vollständig
- [ ] Bei BLE-Discrepancy: Ticket 2 vor Start angepasst, das geänderte Ticket-File committed
- [ ] Bei Lizenz-Problem: Entscheidung von Ivo dokumentiert in `docs/STATUS.md`
- [ ] Branch `aura-kruecke/2-prep` gepusht, PR mit den drei Markdown-Files

---

## Sacred / Heilig-Liste

Wie immer. Insbesondere wichtig hier: **`flight_buddy_ki/lib/core/ble/` ist read-only**. Aufgabe 1 ist explizit ein Lese-Scan, kein Modifizieren. Wenn du dort versehentlich Code änderst, ist das ein Verstoss gegen die Heilig-Liste.

---

## Definition of Done

- 3 Gates abgehakt
- Alle 3 Research-Markdown-Files committed
- Falls BLE-Protokoll abweicht: Ticket 2-File entsprechend angepasst (str_replace, kein Rewrite)
- Falls Lizenz-Probleme: Ivo entscheidet, Entscheidung dokumentiert
- Vor Start von Ticket 2 GATE-A: Ivo bestätigt dass alle drei Prep-Punkte geklärt sind

---

## Was dieses Vor-Ticket *nicht* tut

- Kein Code schreiben (ausser Markdown-Dokumentation)
- Kein PlatformIO-Setup (das ist Ticket 2 GATE-A)
- Keine Sensor-Treiber implementieren
- Keine Kalman-Math implementieren

Nur **lesen, verstehen, dokumentieren**. Das ist der ganze Sinn dieses Vor-Tickets.
