# AURA-KRÜCKE — Ticket D: FANET-TX spec-konform

**Stand:** Encoder fertig + byte-genau belegt · **Live-TX bewusst DEAKTIVIERT** (Gate-D-Option B)
**Datei:** `src/fanet.h` · **Gate-Schalter:** `#define FANET_TX_ENABLED 0`

---

## Status gegen Gate D

> Gate D: Krücke sendet Type-1 und ein echtes Empfangsgerät zeigt sie an korrekter
> Position/Höhe — **ODER** TX für V1 bewusst deaktiviert (RX bleibt), klar markiert.

**Aktuell erfüllt: Option B (TX deaktiviert).** Der Encoder ist spec-konform und per
Selbsttest belegt, aber **es geht nichts auf die Luft**, bis ein echtes Empfangsgerät
die korrekte Position bestätigt. Rote Linie eingehalten: *lieber TX aus als TX falsch.*

- `FANET_TX_ENABLED 0` → `sendTracking()` encodet, sendet aber **nicht** (kein `radio.transmit`).
- RX-Pfad (`init/poll/parseTracking`) unverändert — empfängt weiter echte Pakete.
- Quelle der Spec: `3s1d/fanet-stm32` → `Src/fanet/radio/protocol.txt` (vom FANET-Autor).

---

## Was am alten Encoder falsch war (jetzt behoben)

| Feld | Alt (falsch) | Spec / neu |
|---|---|---|
| **Höhe** | 4 Bits (0–3) für Alt-MSB → überschrieb das **Scaling-Bit 11**; kein 4×-Scaling. Ab ≥2048 m grob falsche Höhe beim Empfänger. | bit0–10 Alt, **bit11 Scaling 1→4×**. ≥2048 m → Wert/4 + Scaling-Bit. |
| **Speed** | `speed*2` über alle 8 Bits → ab >63 km/h kippt bit7 → Empfänger liest 5×. | bit0–6 in 0.5 km/h, **bit7 Scaling 1→5×**. |
| **Climb** | Offset-128 (`climb*10+128`) → komplett falsch (z. B. +2 → Decoder +10). | **7-bit 2's-complement** in 0.1 m/s, bit7 Scaling 1→5×. |
| **Heading** | fehlte ganz (Frame nur 14 statt 15 Byte). | byte10 = 360/256°. |
| **Source-ID** | fix `0x0001`. | **aus ESP32-MAC** abgeleitet (`txUid()`), nie 0. Mfr 0xFC (experimental). |

---

## Frame-Layout (Type 1, 15 Byte)

```
[0]   Header     0x01  (ext=0, forward=0, Type=1)
[1]   Mfr        0xFC  (experimental)
[2-3] UID        Little-Endian, aus MAC
--- Payload ---
[4-6]  Lat       LE 2-compl, lat*93206
[7-9]  Lon       LE 2-compl, lon*46603
[10-11] Typ+Alt  LE word: bit15 Online | bit12-14 Aircraft | bit11 AltScale(4x) | bit0-10 Alt[m]
[12]   Speed     bit7 Scale(5x) | bit0-6 in 0.5 km/h
[13]   Climb     bit7 Scale(5x) | bit0-6 2-compl in 0.1 m/s
[14]   Heading   360/256 deg
```

## Selbsttest-Beleg (Boot, ~6 s, `selfTestTx()`)

```
HEX: 01 FC 34 12 47 61 43 CD 5B 06 71 9A 4C 20 C0
 in : lat=47.37694 lon=8.94185 alt=2500 clb=+3.2 spd=38 hdg=270 ac=1
 out: lat=47.37694 lon=8.94185 alt=2500 clb=+3.2 spd=38 hdg=270 ac=1  PASS   (Alt 4x-Scaling)
HEX: 01 FC 34 12 FF 21 42 81 0C 05 52 93 18 67 40
 out: ... alt=850 clb=-2.5 ...                                          PASS   (negativer Climb)
HEX: 01 FC 34 12 0A D8 42 63 66 06 E8 CB 9E 02 FF
 out: ... alt=4000 spd=75 ...                                           PASS   (Speed 5x-Scaling)
=== ALLE PASS ===
```
HEX zusätzlich von Hand gegen die Spec dekodiert (nicht nur Round-Trip) → byte-genau.

---

## Gate D scharf schalten (Validierung gegen echten Decoder)

**Voraussetzung:** ein echtes FANET-Empfangsgerät — Skytraxx, oder Handy mit
go-fanet / OGN-Range / XCTrack (FANET-fähiger Empfänger).

1. In `src/fanet.h`: `#define FANET_TX_ENABLED 1`, neu flashen.
2. Krücke mit **GPS-Fix** (draußen), neben Skytraxx / Handy mit go-fanet.
3. **Boden-Test-Knopf** (gebaut): **FUNK → FANET antippen** → sendet **einmal** die aktuelle
   Position. Screen zeigt `FANET TX gesendet: <lat> <lon> <alt>m`.
   (Alternativ im Flug: der Loop sendet bei Zustand FLYING automatisch alle 5 s.)
4. Am Empfänger prüfen: erscheint die Krücke an **korrekter Position UND Höhe**?
   - Position nicht versetzt, Höhe plausibel (auch ≥2048 m korrekt → Scaling-Bit ok).
5. **Nur wenn beides stimmt** → Gate D grün, TX bleibt an, committen.
   Stimmt etwas nicht → sofort `FANET_TX_ENABLED 0` zurück.

### Boden-Test-Knopf — Verhalten (Sicherheit)
`FUNK → FANET` antippen ruft den TX-Test. **Der Gate-Schalter bleibt die einzige
Sicherung** — der Knopf kann nichts erzwingen:
- `FANET_TX_ENABLED 0` (Default): Screen zeigt `TX GESPERRT (Flag=0) - Frame im Log`,
  es geht **nichts** auf die Luft (Encoder läuft nur, Frame im Serial-Log).
- `FANET_TX_ENABLED 1`: sendet **eine** Type-1-Sendung der aktuellen Position.
- Ohne Fix: `kein GPS-Fix` — sendet nicht (keine Falsch-/Nullposition).
- **Nur am Boden:** im Flugzustand FLYING blockt der Knopf (`TX-Test nur am Boden`) —
  in der Luft sendet ohnehin der Auto-TX. So bleibt „einmal kontrolliert prüfen" sauber
  getrennt von „im Flug dauernd senden".

## HEILIG
- Falsch senden ist die rote Linie. Solange unvalidiert: `FANET_TX_ENABLED 0`.
- RX / andere Screens / Sensorlogik unangetastet.
