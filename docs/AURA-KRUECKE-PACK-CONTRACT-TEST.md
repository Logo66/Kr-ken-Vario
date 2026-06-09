# TICKET — Pack-Format-Vertrag abgleichen (BEIDE Rechner, vor dem ersten echten Pack-Transfer)

**Für:** Code-SERVER **und** Code-KRÜCKE · gemeinsame Pflicht.
**Anlass:** Zwei Format-Beschreibungen sind entstanden:
- Server: `pack_format.py` (single-source-of-truth, 15/15 Round-Trip grün)
- Krücke: §3 in `docs/AURA-KRUECKE-8-MAP-PACK.md` + C++-Reader `src/pack_reader.h`
**Risiko:** Driften die in EINEM Feld, liest die Krücke beim ersten echten Server-Pack Müll.
**Ziel:** Beweisen, dass beide Seiten **byte-identisch** dasselbe Format meinen — bevor je ein
echtes Pack übertragen wird.
**Arbeitsregel:** ein Ziel, ein Beweis, STOP. Reine Schnittstellen-Absicherung.

---
## 0. GRUNDSATZ
> Es gibt genau EINEN Vertrag. `pack_format.py` (Server) ist die Referenz.
> Geräte-Doku (KRUECKE-8 §3) und C++-Reader müssen ihr exakt entsprechen.
> Abweichung = Bug. Änderungen nur im Doppel (beide Seiten zugleich).

## 1. ABZUGLEICHENDE FELDER (Referenz `pack_format.py`)
| Feld | Soll |
|---|---|
| Byte-Order | Little-Endian |
| HEADER magic | 4 Bytes "AURA" |
| HEADER version | uint8 = 1 |
| HEADER region_id | char[16], null-terminiert |
| HEADER tile_size | uint16, 1/100 Grad |
| HEADER tile_count | uint16 |
| INDEX tile_lat0 / tile_lon0 | int32, 1e-7 Grad (SW-Ecke) |
| INDEX offset / length | uint32 / uint32 |
| TILE n_contours | uint16 |
| Kontur height_m / flag / n_points | int16 / uint8 (0=normal,1=Index) / uint16 |
| Kontur-Punkte dlat / dlon | int16 / int16, 1e-5 Grad, rel. tile_lat0/lon0 |
| TILE n_peaks | uint16 |
| Peak dlat/dlon/height_m/rank/name_len/name | int16/int16/int16/uint8/uint8/char[name_len] UTF-8 |
| Luftraum/Hindernis | analog, in v1 leer (n=0) zulässig |
| Rekonstruktion | `lat = tile_lat0/1e7 + dlat/1e5` (lon analog) |

> **KRÜCKE-STATUS (geprüft):** `src/pack_reader.h` entspricht dieser Tabelle Feld für Feld → **P1 von der Geräteseite erfüllt.**

## 2. DER BEWEIS — Cross-Read mit EINEM gemeinsamen Test-Pack
- **A (Server):** `pack_format.py` erzeugt `contract_test_v1.pack` mit gemischtem Inhalt
  (≥1 Tile, 2 Konturen [1 normal, 1 Index], 1 Peak mit Umlaut-Namen, 0 Lufträume) +
  `tests/pack_contract/expected.json` (erwartete lat/lon/Höhe/Name).
- **B (Server-Selbsttest):** Round-Trip `pack_format.py` → reproduziert `expected.json` exakt.
- **C (Krücke-Cross-Read):** Geräte-Reader liest **dieselbe** Datei und gibt alle dekodierten
  Werte über Serial aus (Header, je Kontur height/flag/Punktzahl + erster/letzter lat/lon, Peak lat/lon/Höhe/Name).
- **D (Vergleich):** Serial-Ausgabe == `expected.json`, Feld für Feld.

## 3. GATEs
| Gate | Bedingung |
|---|---|
| P1 | §3 + `pack_format.py` == Tabelle §1. **(Krücke-Seite erfüllt.)** |
| P2 | Server-Round-Trip exakt. |
| P3 | Cross-Read: Krücke dekodiert dieselbe Datei identisch (inkl. Umlaut-Peak + korrekte lat/lon). |
| P4 | Kaputtes Feld (magic "XURA" / version=2 / Truncation) → Krücke lehnt sauber ab, kein Crash. |

## 4. AUFTEILUNG
- **Server:** A + B, `expected.json`, Test-Pack liefern.
- **Krücke:** C (Reader gegen dieselbe Datei) + P4 (Reject-Pfad).
- **Architekt:** entscheidet bei §1-Abweichung, welche Seite nachzieht.

## 5. STOP / HEILIG
- **Kein echter Karten-Pack-Transfer, bevor P1–P4 grün sind.**
- `pack_format.py` ist die Referenz; bei Konflikt zieht die Krücke nach (außer Architekt entscheidet).
- Format-Änderungen ab jetzt nur im Doppel (beide Dateien + dieser Cross-Read-Test im selben Schritt).

---
## KRÜCKE-UMSETZUNG (Geräteseite, in diesem Repo)
- **P1:** `src/pack_reader.h` deckt §1 ab (Little-Endian, Header, Tile-Index, Kontur-Skip, Peaks, Rekonstruktion).
- **P4 Reject:** falsches magic ODER version!=1 ODER Truncation → klare `[CONTRACT] REJECT: …`-Meldung, kein Crash.
- **Schritt C:** `dumpPackContract()` gibt alle Felder über Serial aus. Trigger: liegt
  `/maps/contract_test_v1.pack` auf der SD, wird ~6 s nach Boot automatisch ein Cross-Read-Dump
  ausgegeben (Serial dann stabil). **Wartet auf das Server-`contract_test_v1.pack`.**
