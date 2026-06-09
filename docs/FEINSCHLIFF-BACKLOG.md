# AURA-KRUECKE — Feinschliff-Backlog

Notizen aus dem Test am 2026-06-09 (Auto-Testfahrt). „Zum Merken" für die Polish-Phase —
NICHT sofort umsetzen, sondern gesammelt nach Bestätigung der Karte/Gipfel.

## Kopfzeile / Statusbar (ALLE Screens)
- [ ] Kopfzeile mit Batterie-Anzeige auf **jedem** Screen **identisch** darstellen, allgemein einheitlich.
- [ ] Auf **Cruise** zusätzlich anzeigen: ob **Buddy-Verbindung** steht.

## Thermik-Screen
- [ ] **Höhe**, **Base EST**, **AVG Climb** alle **gleich groß**.
- [ ] Alle Daten gleich groß, und alles **ausgemittet** (geglättet).

## Karten-Screen
- [ ] **Fadenkreuz-Button (Re-Center) löschen** — brauchen wir nicht, da wir die Karte nicht verschieben.
- [ ] Frei werdenden Platz auf **Plus / Minus** aufteilen (größere, handschuhtaugliche Tasten).
- [ ] Temporären Diagnose-Zähler **`G<n> L<n>`** wieder entfernen, sobald Gipfel bestätigt sind.

## Funktion / Bug
- [ ] **Gleitzahl (GR)** wurde im Test **nie angezeigt** — prüfen.
      Verdacht: Im Auto gibt es kein echtes Sinken → GR = Speed / Sinkrate ist undefiniert
      und wird evtl. als „---" ausgeblendet. Logik + Anzeigebedingung kontrollieren.

## Test-Feedback (läuft gut, NICHT anfassen)
- Speed ✓ · Höhe ✓ · Temp ✓ · Start-Erkennung ✓ · Lande-Erkennung ✓
- Karte sauber: Vektor-Lufträume, kein Clutter, kein Ghosting ✓
