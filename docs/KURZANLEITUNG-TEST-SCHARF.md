# Kurzanleitung — Testen & Scharfschalten (Krücke)
## Stand 2026-06-13 · für Ivo

---

## A) FANET-Senden scharf schalten + testen

**Sicherheits-Logik (3 Ebenen):** Live gesendet wird NUR, wenn **alle** zutreffen:
1. **Gate D** (im Code, jetzt **offen** ✅),
2. **TX SCHARF** (der neue Toggle / `fanet.tx_enabled`, Default **AUS**),
3. **im Flug** (Auto-TX) **oder** Boden-TX-Test-Knopf.

### 1. Scharf schalten (am Gerät)
1. **Hauptmenü** (Knopf lang) → **FUNK**.
2. Im **FANET-Feld rechts** antippen („TX SENDEN") → wechselt auf **SCHARF**. (Bleibt gespeichert, übersteht Neustart.)
3. Wieder antippen = zurück auf **AUS**.

> Alternativ über die App: `fanet.tx_enabled = true` (gleicher Effekt).

### 2. Boden-Test (ein einzelner Frame, kontrolliert)
1. **SCHARF** muss an sein (Schritt 1) **und** GPS-**Fix** vorhanden (≥4 Sat).
2. Im FANET-Feld **links** antippen („Test") → Krücke sendet **EIN** Frame mit der aktuellen Position. Anzeige unten: „FANET TX gesendet: …".
3. **Verifizieren** (wichtig!): auf einer **zweiten FANET-Gegenstelle** prüfen, dass deine Position/Höhe korrekt ankommt — z. B.:
   - ein anderes FANET-Vario (Skytraxx, XCTrack-Box, anderes Gerät), **oder**
   - **OGN / FANET-Tracking-Karte** (Open Glider Network), wo dein Gerät auftauchen sollte.
4. Stimmen **Position, Höhe, Steigen, Flugzeugtyp** (Gleitschirm)? → ✅ passt.

### 3. Im Flug
- Sobald **SCHARF**, sendet die Krücke **automatisch alle 5 s** die eigene Position — **nur während des Flugs** (erkannter Start). Am Boden: kein Auto-TX (nur der Test-Knopf).
- **Ausschalten:** FANET-Feld rechts auf **AUS** tippen.

### 4. Wenn etwas nicht stimmt
- Kommt nichts an / falsche Position → **sofort auf AUS** und melden. Leitsatz bleibt: **lieber TX aus als TX falsch.**

---

## B) Schnell-Check der restlichen Funktionen

| Test | So geht's | Erwartet |
|---|---|---|
| **Start-Jingle** | Gerät neu starten | Blues-Jingle **während** das Logo steht, gleich lang |
| **Cruise-Screen** | Cruise ansehen | „m/s" und „avg N s" sauber unter den Vario-Zahlen, keine Kollision |
| **Thermik-Screen** | zu Thermik wischen | AVG/HOEHE/BASE EST mit Abstand zu beiden Linien |
| **BLE-Konfig** | App verbinden, Werte ändern | Änderung greift live, kein PIN, „ausstehend" → ok |
| **AVG-Fenster** | in App `vario.avg_window_s` ändern | Cruise + Thermik zeigen dasselbe neue „avg N s" |
| **Aus-Screen** | Menü → AUS | KIE-Seite **weiss** mit schwarzer Schrift, kein Ghosting |

---

## C) Was die App noch braucht (an den Baumeister)
- **FANET-Reiter:** Toggle `fanet.tx_enabled` (Arm) + Auswahl `fanet.aircraft` (1=Gleitschirm) — beide jetzt **live wirksam**.
- **Vario-Reiter:** `vario.avg_window_s` (Sekunden, 1–30, Default 20) — siehe separates AVG-Ticket.
