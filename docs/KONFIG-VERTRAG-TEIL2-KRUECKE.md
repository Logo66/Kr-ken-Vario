# KONFIG-VERTRAG Teil 2 — Modell + BLE-Schreibweg (Krücke)
## Settings + Flugplan/Task · maßgebliche Antwort des Krücke-Baumeisters · 2026-06-11

> **Zweck:** Die Wahrheit hinter Teil 1 (App-UI). EIN BLE-Schreibweg trägt ZWEI Inhalte: **Settings** und **Task**.
> **Status:** alle ❓ aus dem Ticket **beantwortet/festgelegt**. Keys hier = Keys in Teil 1 (ZEICHENGLEICH, sonst Drift).
> **Quelle:** `ble_manager.h`, `sound_settings.h`, `ble_screen.h`, `wifi_screen.h`, `overlay_screen.h` der Firmware.

---

## 1. Settings-Modell (NVS, versioniert) — finale Keys

`schema_version` + `updated_at` sind **PFLICHT** (Cloud-Sync Phase 2). Werte unten = Defaults.

```json
{
  "schema_version": 1,
  "updated_at": 0,
  "sound":   { "volume": 3, "muted": false },
  "vario":   { "climb_threshold": 0.2, "sink_alarm": -3.0, "deadband": 0.1, "tone_curve": 0, "sink_tone": true, "avg_window_s": 20 },
  "units":   { "alt": "m",   "speed": "kmh", "vario": "ms", "temp": "c" },
  "display": { "backlight": true, "brightness": 80, "screens": ["cruise","thermal","goal","map","xsection"] },
  "wifi":    { "ssid": "", "pass": "" },
  "ble":     { "name": "Aura Vario", "pin": 1234, "enabled": false },
  "fanet":   { "enabled": true, "aircraft": 1, "pilot_name": "", "online_tracking": true, "tx_enabled": false },
  "pilot":   { "name": "", "glider": "", "weight_kg": 95 },
  "alt":     { "qnh": 1013.25 },
  "map":     { "region": "ch_v1", "layers": { "contours": true, "water": true, "airspace": true, "obstacles": true, "track": true } },
  "log":     { "igc": true, "imu_raw": false },
  "buddy":   { "pairing_code": "" },
  "warn":    { "buffer_h": 500, "buffer_v": 150, "airspace": true, "obstacle": true, "sphere_outer_m": 300, "sphere_inner_m": 100 }
}
```
- **units:** `alt` ∈ {`m`,`ft`} · `speed` ∈ {`kmh`,`mph`,`kt`} · `vario` ∈ {`ms`,`ftmin`,`kt`} · `temp` ∈ {`c`,`f`}.
- **fanet.aircraft:** FANET-Typ (1=Paraglider, 2=Hangglider, 3=Balloon, 4=Glider, 5=Powered, …).
- **warn:** EIN 3D-Puffer (`buffer_h`/`buffer_v` in **Metern**) für die Luftraum-Warnung, `airspace`/`obstacle` = Alarm an/aus **pro Kategorie**. **Neu (Hindernis-Warnung):** `sphere_outer_m`/`sphere_inner_m` = die zwei Radien der **3D-Schutzkugel** um den Piloten in **Metern** (aussen=Vorwarnung, innen=Alarm; Firmware erzwingt innen<aussen). **Quelle: nur BAZL-Luftfahrthindernisse** (Seile, Hochspannung, Seilbahnen, Masten, Windräder) — bewusst **keine Berge** (Sichtflug) und **kein Verkehr** (FANET dient nur dem Gesehen-werden via TX, keine Kollisionswarnung). Siehe `TICKET-APP-HINDERNIS-KUGEL.md`.
- **Migration:** die 4 Altpfade (NVS `sound`, `/ble.cfg`, `/wifi.cfg`, QNH-Laufzeit) werden beim ersten Boot einmalig ins Modell gezogen.

## 2. Task-Modell (SD `/tasks/`, mehrere)

```json
{
  "schema_version": 1, "name": "", "type": "route",
  "waypoints": [ { "name": "", "lat": 0.0, "lon": 0.0, "alt": 0, "radius": 400 } ],
  "start": null, "goal": null, "start_time": 0, "sss": null, "ess": null
}
```
- `type` ∈ {`route`,`competition`}. `radius` in m (Wettkampf-Zylinder). Mehrere Tasks auf SD; aktiver per Name referenziert.

---

## 3. DER GEMEINSAME BLE-SCHREIBWEG  *(❓ alle beantwortet)*

**Neue Characteristic (heute existiert nur Lesen/Notify):**

| Rolle | UUID | Properties |
|---|---|---|
| **Write/Control** | `4155524F-0001-0001-0001-000000000006` | **WRITE + NOTIFY**, **verschlüsselt (Pairing Pflicht)** |

- **❓ UUID:** `…0006` (reiht sich an Vario `…0002` / GPS `…0003` / Status `…0004` / Umwelt `…0005`).
- **❓ Sicherheit:** der Write-Char ist **`WRITE_ENC`/`AUTHEN`** → **Schreiben nur nach PIN+Bonding** (sonst lehnt das Gerät ab). Lesen/Notify bleiben wie gehabt.
- **Inhaltstyp:** jede Schreibnachricht ist **JSON** mit `"kind": "settings" | "task"`. **Ein Kanal, kein zweites Protokoll.**
- **Echo:** das Gerät **notifyt auf demselben Char …0006** die Bestätigung.

### 3a. Settings schreiben (key/value, eine Änderung pro Write)  *(❓ Format)*
App → Gerät:
```json
{ "kind":"settings", "k":"sound.volume", "v":3 }
```
- `k` = **dotted path** exakt wie das Modell (`sound.volume`, `vario.climb_threshold`, `units.alt`, `ble.name`, `fanet.tx_enabled`, `warn.buffer_h`, …).
- `v` = typisiert (number/bool/string/array).
- Gerät: Modell setzen → `updated_at` hochzählen → NVS speichern → `applyConfig()` → **Echo notify**:
```json
{ "ack":"settings", "k":"sound.volume", "v":3, "ok":true }
```
Fehler: `{ "ack":"settings", "k":"…", "ok":false, "err":"unknown_key|range|type" }`.

### 3b. Task schreiben (chunked + CRC)  *(❓ Chunking)*
Ein Task ist größer als ein BLE-Paket → **mehrteilig**. Jeder Chunk:
```json
{ "kind":"task", "i":0, "n":17, "crc":3735928559, "d":"<JSON-Teilstring>" }
```
- `i` = Chunk-Index (0-basiert), `n` = Gesamtzahl, `d` = Teilstring des Task-JSON.
- `crc` = **CRC32 des gesamten reassemblierten JSON**, **nur in `i:0`**.
- Gerät: bei `i:0` Puffer zurücksetzen, `d` in Reihenfolge anhängen; bei `i==n-1` → **CRC32 prüfen** → JSON parsen → SD `/tasks/<name>.json` → **Echo notify**:
```json
{ "ack":"task", "name":"Hörnli-Dreieck", "wp":5, "ok":true }
```
Fehler: `{ "ack":"task", "ok":false, "err":"crc|json|too_big" }`. Reassembly-Puffer ist auf **4 KB** begrenzt.
- **Chunk-Größe** = ausgehandelte MTU − Overhead. **Empfehlung: MTU auf ~247 anheben** (Gerät akzeptiert größere MTU) → wenige Chunks. Protokoll ist MTU-unabhängig (i/n-basiert).

---

## 4. Drei Editoren, ein Modell  *(❓ Touch)*
1. **Touch** (existiert): schreibt **ins Modell** (statt direkt NVS) — ab M1.
2. **BLE (Handy):** Settings + Task über §3.
3. **Web (Gerät hostet, Variante A):** auf dem IGC-Webserver, schreibt Settings; Task-Upload optional — später.

## 5. FANET-TX — DOPPELTE SICHERUNG (HEILIG)  *(❓ beantwortet)*
- `fanet.tx_enabled` Default **false**, über Editoren setzbar.
- **Live-TX = `FANET_TX_ENABLED` (Compile-Gate D) UND `config.fanet.tx_enabled` UND im-Flug.**
- Das Config-Flag **allein kann TX NICHT scharf schalten** — solange Gate D (Compile-Macro = 0) zu ist, bleibt TX aus, egal was die App schreibt. App zeigt „TX gesperrt (Gate D)". **TX nie über ein Häkchen.**

## 5b. Luftraum-Daten-Sync  *(❓ beantwortet → Server-Ticket nötig)*
- **`warn`-Settings** (3D-Puffer, an/aus) liegen IM Modell (Editoren). HEILIG: **abgeschaltete Warnung bleibt am Gerät sichtbar** (Status-Indikator), nie still aus.
- **Luftraum-DATEN** (OpenAir/GeoJSON auf SD `/airspace/`, `/obstacles/`) sind ein **Pack wie die Karte**, KEIN Setting.
- **❓ Server-Endpoint für Luftraum-Pack?** → **NEIN.** Heute kommen die Daten von **fest verdrahteten externen URLs** (`storage.googleapis.com/…/ch_asp.txt`, `data.geo.admin.ch` BAZL) — **ohne** Versions-/SHA-Verwaltung, **nicht** über den Buddy-Server. → **Eigenes Server-Ticket an den Architekten:** Luftraum-Pack-Endpoint analog `/maps/*` (Bearer, Version, SHA), damit „Sync bei Verbindung, SHA-verifiziert" möglich wird. Bis dahin: Warnung rechnet offline gegen die vorhandenen lokalen Daten.

## 6. Task-Render-Stufe  *(❓ beantwortet)*
- **Stufe 1 (M4):** **Wegpunkt-Navigation** — Peilung + Distanz zum aktiven Wegpunkt. **Wiederverwendung der bestehenden Ziel/XC-Screen-Logik** (macht schon Bearing/Distanz zu einem Ziel).
- **Stufe 2 (später):** Wettkampf-**Zylinder** (Radius rein/raus) + Start-Linie.
- **Stufe 3 (später):** Start-Zeit / SSS/ESS-Timing.

## 7. Phase 2 — Cloud-Hub (nur vorgesehen)
Heartbeat synct Modell (`updated_at`) gegen Server; Task ggf. aus Cloud. BLE bleibt Feld-Weg. Braucht später Sync-Vertrag Server↔Krücke.

## 8. Bau-Reihenfolge / Gates — STAND 2026-06-12
- **M1 ✅ ERLEDIGT** (`7ebcbdf`): 1 Settings-Modell (versioniert, NVS-JSON `cfg/model`, `schema_version`+`updated_at`), Migration der Altpfade (Ton-NVS, `/ble.cfg`, QNH). Alle 13 Vertrags-Gruppen schreibbar; Live-Keys (Ton/Vario/QNH/BLE) wenden sofort an, der Rest ist persistent gespeichert. *(Verifiziert: `pilot.name`/`units.alt`/`warn.airspace` → ok, `bogus.key` → unknown_key, Modell 761 B übersteht Neustart, Migration `ble.enabled`.)*
- **M2 ✅ ERLEDIGT** (`7b940c0`): Write-Char `…0006`, `kind:settings` (dotted-path key/value), Echo-Ack. *(Live verifiziert am echten Gerät: `sound.*`/`vario.*` → ok, auch Slider-Bursts.)*
- **M3 ✅ ERLEDIGT** (`05455af`): `kind:task` chunked + **CRC32 (Standard IEEE/zlib, Poly 0xEDB88820)** → SD `/tasks/<name>.json`, Echo „N WP". *(Selbsttest: 2 Chunks → CRC ok → 2 WP, 4 KB Reassembly-Puffer, MTU 247.)*
- **M4 ⏳ offen:** aktiver Task im Flug (Wegpunkt-Navigation, Stufe 1).
- **M5 ⏳ offen:** FANET-TX-Flag nur mit Compile-Gate D (doppelt).

> **⚠️ Sicherheits-Abweichung (Phase 1) — `e9033ac`:** Der Write-Char `…0006` ist **vorerst OFFEN** (`WRITE+NOTIFY`, **kein** `WRITE_ENC`), konsequent mit den ohnehin offenen Lese-Chars. **Grund:** NimBLE-Bonding hielt nicht (Disconnect unterbrach die Key-Verteilung → Android `BOND_NONE` → bei jeder Änderung neuer PIN; mit `WRITE_ENC` wurden Writes dann ohne Verschlüsselung abgewiesen → App „ausstehend"). **Disconnect-Fix ist drin** (kein Advertising-Neustart in `onConnect`; Buddy-Heartbeat-TLS pausiert während BLE-Session). **Phase-2-Härtung:** Verschlüsselung + persistenter Bond **konsequent für ALLE Chars** (Reads + Write) — NimBLE-Bond-Config (Public-Adresse, NVS-Persist, Key-Dist) ist bereits korrekt.

## 9. HEILIG
- **Ein Schreibweg, zwei Inhalte** (`settings`/`task`) — kein zweites BLE-Protokoll.
- Modell = einzige Wahrheit; `schema_version`+`updated_at` ab Start. FANET-TX doppelt gesichert.
- **Keys exakt wie Teil 1.** ~~Schreiben nur verschlüsselt (Pairing)~~ → **Phase 1: Write-Char offen** (siehe §8 Sicherheits-Abweichung `e9033ac`); Verschlüsselung + Bond konsequent für ALLE Chars als **Phase-2-Härtung**. Voll-Sicherung + sauberer Commit pro Schritt.
