# TICKET — P3 Cross-Read fertigstellen (Server-Pack ← Krücken-Reader)

**Stand:** P1 ✅ (beidseitig) · P2 ✅ (Server) · P4 ✅ (Krücke) · **P3 ❌ offen.**
**Was P3 beweist:** Krücken-Reader liest das **echte Server-Pack** (nicht ein selbst erzeugtes) und
liefert Feld für Feld dieselben Werte wie die Server-`expected.json`. Schließt **geteilte Fehlannahme**
zwischen den zwei Implementierungen aus. Driftet das Format in EINEM Byte, laden später ALLE Geräte Müll.
**Arbeitsregel:** ein Ziel, ein Beweis, STOP.

## 0. AUSGANGSLAGE
- Server: `tests/pack_contract/contract_test_v1.pack` (**102 Bytes, SHA-256 `ad76a667…3c9f704`**) + `expected.json`.
- Krücke: Reader `src/pack_reader.h` + Auto-Dump `dumpPackContract()` (~6 s nach Boot, wenn `/maps/contract_test_v1.pack` auf SD).
- **Krücke darf das Test-Pack NICHT selbst erzeugen** — nur die physische Server-Datei mit dem SHA zählt.

## 1. Datei vom Server zum Krücken-Rechner (Ivo) — einer der Wege:
- **Branch-Sync:** Server pusht `feature/maps-packs-srv-lm-07` (mit `tests/pack_contract/`) auf gemeinsames `origin`
  → Krücke `git fetch` + Datei extrahieren. **(Stand jetzt: Branch noch NICHT auf origin.)**
- **USB/Kopieren:** `contract_test_v1.pack` + `expected.json` vom Server-PC auf den Krücken-PC.

## 2. Identität prüfen BEVOR auf SD (Code-Krücke)
`Get-FileHash contract_test_v1.pack -Algorithm SHA256` → MUSS `ad76a667…3c9f704` sein.
**Hash falsch → STOP** (falsche/eigene Datei).

## 3. Auf SD, booten, Auto-Dump lesen (Code-Krücke)
Verifizierte Datei → SD `/maps/contract_test_v1.pack` (überschreibt Selbsttest), booten, ~6 s, Serial erfassen.

## 4. Feld für Feld gegen SERVER-`expected.json` (Code-Krücke)
NICHT gegen krücken-eigene Erwartung, NICHT aus dem Gedächtnis. Inkl. UTF-8-Bytes korrekt gelesen +
lat/lon korrekt rekonstruiert (`lat = tile_lat0/1e7 + dlat/1e5`).

## 5. GATE P3
SHA der SD-Datei == `ad76a667…` **UND** Serial-Dump == Server-`expected.json` Feld für Feld.
Abweichung = **kein „kaputt", sondern der gesuchte Drift** → Feld melden → Architekt entscheidet
(Default: Server-`pack_format.py` ist Referenz, Krücke zieht nach).

## 6. DANACH (erst wenn P3 grün)
Heilig-Sperre „kein echter Karten-Pack-Transfer" fällt → echte Region-Packs (Hörnli/CH) dürfen übertragen werden.

## 7. STOP / HEILIG
- STOP, wenn SHA ≠ `ad76a667…`. Krücke erzeugt das Test-Pack NICHT selbst.
- Vergleich nur gegen Server-`expected.json`. Reader/Format eingefroren bis P3 entschieden.
