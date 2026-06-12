# BLE-Datenvertrag — Krücke (Aura Vario) · maßgebliche Antwort des Krücke-Baumeisters

> **Quelle = der Encoder selbst:** `src/ble_manager.h` (NimBLE GATT-Server) + `src/ble_screen.h` (PIN/Name).
> Werte unten sind **verbatim aus dem Code**, nichts geraten. Stand 2026-06-11, Firmware-Branch `aura-kruecke/2-vario-map`.
> **Klarstellung vorweg:** Die Krücke ist **NICHT** das „AURA-BOX"-Format (12 B / 16 B). Anderes Service-UUID,
> 20-Byte-Vario als **float**, GPS als **double**. Siehe F-1.

---

## F-1 — Transport / UUIDs

**(b) Separate Characteristics je Datentyp** — KEIN gemeinsamer Char mit Header-Byte.
Drei Characteristics unter einem Service. **NICHT** identisch zur Aura `E7F5A3B1-…`.

| Rolle | UUID | Properties |
|---|---|---|
| **Service** | `4155524F-0001-0001-0001-000000000001` | — |
| **Vario** | `4155524F-0001-0001-0001-000000000002` | READ + **NOTIFY** |
| **GPS** | `4155524F-0001-0001-0001-000000000003` | READ + **NOTIFY** |
| **Status** | `4155524F-0001-0001-0001-000000000004` | READ (kein Notify) |
| **Umwelt+Wind** | `4155524F-0001-0001-0001-000000000005` | READ + **NOTIFY** (NEU) |

`4155524F` = ASCII „AURO". Advertising-Name = der konfigurierte Gerätename (Default „Aura Vario"), Scan-Response an.

---

## F-2 — Vario-Char, 20 Byte: vollständiges Layout

Struct `BleVarioData`, `__attribute__((packed))` (kein Padding). **Endianness durchgehend Little-Endian**
(ESP32-S3 ist LE; `setValue()` kopiert den Speicher 1:1). **Kein Header-Byte. Kein CRC/Prüfbyte.**
Die Werte sind **echte IEEE-754 floats in ihrer Einheit — keine Integer-Skalierung** (anders als AURA-BOX).

| Offset | Typ | Feld | Einheit | Skalierung | Endian |
|---|---|---|---|---|---|
| 0 | float32 | `altitude` | m MSL | keine (roher float) | LE |
| 4 | float32 | `vario` | m/s | keine | LE |
| 8 | float32 | `speed` | km/h | keine | LE |
| 12 | float32 | `heading` | Grad (0–360) | keine | LE |
| 16 | uint8 | `sats` | Anzahl | — | — |
| 17 | uint8 | `bat_pct` | % (0–100) | — | — |
| 18 | uint8 | `flags` | Bitfeld | siehe F-4 | — |
| 19 | uint8 | `reserved` | — | immer 0 | — |

Summe = 4+4+4+4+1+1+1+1 = **20 Byte**. Decoder: 4× `float32 LE`, dann 4× `u8`.

---

## F-3 — GPS-Char

Struct `BleGpsData`, packed. **Ja: 2× IEEE-754 `double` (je 8 Byte) = 16 Byte gesamt.**

| Offset | Typ | Feld | Endian |
|---|---|---|---|
| 0 | float64 (double) | `lat` | LE |
| 8 | float64 (double) | `lon` | LE |

- Reihenfolge: **lat, dann lon.** Grad als Dezimalzahl (z. B. 47.5973).
- **Keine** weiteren Felder im GPS-Char (keine Höhe/Fix/Sat hier — die stecken im Vario-Char).
- **Wird nur gesendet, wenn GPS-Fix gültig** (`gps_fix && lat != 0`). Vor dem Fix kommt **kein** GPS-Notify.

---

## NEU — Umwelt + Wind-Char (`…0005`, 24 Byte)

Eigener Notify-Char (additiv, ändert die anderen NICHT). Struct `BleEnvData`, packed, **alles `float32 LE`**. **Rate: 1×/Minute** (+ einmal sofort beim Verbinden).

| Offset | Typ | Feld | Einheit | Anmerkung |
|---|---|---|---|---|
| 0 | float32 LE | `temp` | °C | SHT45 |
| 4 | float32 LE | `humidity` | % rel. Feuchte | SHT45 |
| 8 | float32 LE | `dewpoint` | °C | abgeleitet |
| 12 | float32 LE | `base_est` | m | Wolkenbasis-Schätzung — nur beim Kurbeln sinnvoll, sonst letzter/0 |
| 16 | float32 LE | `wind_speed` | km/h | aus GPS-Kreisdrift; **0 bis zum ersten vollen Kreis** |
| 20 | float32 LE | `wind_dir` | Grad | Richtung **woher** der Wind kommt (meteorologisch); 0 bis geschätzt |

**Wind-Semantik:** geschätzt aus der Bodengeschwindigkeits-Schwankung im Kreis (XCSoar-Prinzip). Solange der Pilot nicht ≥1 vollen Kreis geflogen ist, sind `wind_speed`/`wind_dir` = 0 (= „noch keine Schätzung"). Danach geglättet (EMA). `temp`/`humidity`/`dewpoint` sind immer gültig (sobald SHT45 da ist).

---

## F-4 — Flags-Byte (Vario-Char, Offset 18)

`flags = (gps_fix?1:0) | (flying?2:0) | (fanet_ok?4:0)` — Bit 0 = LSB:

| Bit | Maske | Bedeutung |
|---|---|---|
| 0 | `0x01` | **GPS-Fix** (Position gültig) |
| 1 | `0x02` | **im Flug** (Flugerkennung = FLYING) |
| 2 | `0x04` | **FANET ok** (Funk initialisiert) |
| 3–7 | `0xF8` | ungenutzt, immer 0 |

---

## F-5 — Notify-Rate & Status-Char

- **Raten (pro Char unterschiedlich, nur wenn ein Client verbunden ist):**
  - **Vario-Char: ~10 Hz** (alle 100 ms) — Höhe/Vario flüssig fürs App-Vario/Ton.
  - **GPS-Char: 1 Hz** (nur bei gültigem Fix; schneller sinnlos — GPS-Modul liefert 1 Hz).
  - **Umwelt-Char: 1×/Minute** (Temp/Feuchte/Wind/Basis ändern sich langsam).
  - **Beim Verbinden** wird sofort einmal alles gesendet (kein Warten auf den Minutentakt).
- **Status-Char:** reiner **UTF-8-Text, read-only** (kein Notify). Aktueller Wert ist der **fest verdrahtete** String:
  ```
  "Aura Vario v0.3"
  ```
  ⚠️ **Ehrlicher Hinweis:** das ist ein hartkodierter String, **nicht** die echte Firmware-Version (`AURA_VERSION`).
  Die App sollte sich für die exakte FW-Version **nicht** auf diesen Char verlassen — besser via Heartbeat/`/devices/*`.
  (Kleiner Firmware-Fehler; kann ich dynamisch machen, sobald gewünscht — eigener kleiner Fix.)

---

## F-6 — Pairing / Security

Aus `ble_manager.h::init()` + `ble_screen.h`:
- `setSecurityAuth(true, true, true)` → **Bonding + MITM + Secure Connections = alle AN**.
- `setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY)` → **Gerät ZEIGT den Passkey** an → **die App muss ihn eingeben** (Passkey Entry). **NICHT** just-works.
- `setSecurityPasskey(pin)`.

**PIN:** Default **1234**, aber **pro Gerät änderbar** (BLE-Screen → PIN, persistent in `/ble.cfg`). Also nicht fix — benutzersetzbar, Default 1234. Das Gerät zeigt die aktive PIN im BLE-Screen an.

**→ Empfehlung für die App: Passkey-/PIN-Eingabe** (Nutzer liest die PIN am Krücke-Display, tippt sie im Handy ein).

⚠️ **Eine ehrliche Nuance:** Die drei Characteristics sind als `READ | NOTIFY` deklariert — **ohne** explizites `_ENC`-(Verschlüsselungs-)Flag. D. h. die Firmware **erzwingt** die Verschlüsselung pro-Characteristic heute **nicht** hart; die MITM-Passkey-Kopplung wird auf Geräte-Ebene angefordert. Wenn ihr „ohne Pairing kein Notify" **garantiert** wollt, setze ich die Chars auf `_ENC` (kleiner, sauberer Firmware-Fix) — sagt Bescheid, ob das in den Vertrag soll.

---

## Decoder-Kurzrezept (für Abschnitt B)

```text
Vario-Notify (20 B):  f32le alt | f32le vario | f32le speed | f32le heading | u8 sats | u8 bat | u8 flags | u8 _
GPS-Notify  (16 B):   f64le lat | f64le lon
Env-Notify  (24 B):   f32le temp | f32le humidity | f32le dewpoint | f32le base_est | f32le wind_speed | f32le wind_dir
Status-Read (Text):   UTF-8, z.B. "Aura Vario v0.3"
Connect:              Passkey-Entry, PIN vom Krücke-Display (Default 1234)
```

*Beantwortet vom Krücke-Baumeister aus `ble_manager.h` / `ble_screen.h`. Zwei kleine Firmware-Verbesserungen
angeboten (dynamische Status-Version, `_ENC`-Chars) — beide nur auf ausdrückliche Freigabe.*
