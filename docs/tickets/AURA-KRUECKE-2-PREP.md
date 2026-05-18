# AURA-KRUECKE-2-PREP · Verifikation vor Vario Core

- **Erstellt:** 2026-05-18
- **Prioritaet:** P0 (blockiert Ticket 2)
- **Typ:** Lese-Ticket — kein Code, nur Dokumentation
- **Geschaetzt:** 1-2 h
- **Status:** ✅ ABGESCHLOSSEN

---

## Aufgabe 1 · BLE-Beweis

**Frage:** Existieren die behaupteten UUIDs und das 0xAA/BB/CC Binary-Format wirklich im Flutter-Code? Oder hat die Recherche halluziniert?

### Ergebnis: ✅ ALLE 8 CLAIMS BESTAETIGT

Quellpfad: `G:\Meine Ablage\Aura KI Vario 1.0\Flight-BuddyKI-v5\flutter-app\lib\core\ble\`

| # | Claim | Status | Datei | Zeile | Beweis |
|---|-------|--------|-------|-------|--------|
| 1 | Service UUID `E7F5A3B1-2C8D-4E6F-9A0B-3D1C5E7F9A2B` | ✅ | `aura_binary_parser.dart` | 9 | `const kAuraServiceUuid = 'E7F5A3B1-2C8D-4E6F-9A0B-3D1C5E7F9A2B';` |
| 2 | Characteristic UUIDs enden auf 9A2C/9A2D/9A2E | ✅ | `aura_binary_parser.dart` | 10-12 | `kAuraVarioCharUuid`, `kAuraGpsCharUuid`, `kAuraStatusCharUuid` |
| 3 | VARIO = 12 Bytes, Header 0xAA | ✅ | `aura_binary_parser.dart` | 19, 31, 111 | `const kVarioHeader = 0xAA;` + `data.length < 12` check |
| 4 | GPS = 16 Bytes, Header 0xBB | ✅ | `aura_binary_parser.dart` | 20, 52, 114 | `const kGpsHeader = 0xBB;` + `data.length < 16` check |
| 5 | STATUS = 6 Bytes, Header 0xCC | ✅ | `aura_binary_parser.dart` | 21, 71, 117 | `const kStatusHeader = 0xCC;` + `data.length < 6` check |
| 6 | VARIO-Felder: vz/intVz/pressure/temp/flags | ✅ | `aura_binary_parser.dart` | 124-136 | Exakte Byte-Offsets + Skalierung verifiziert |
| 7 | Device-Detection via UUID + Name "aura" | ✅ | `ble_service.dart` | 81-101 | `_detectDeviceType()` prueft beides |
| 8 | LK8EX1 NMEA als Fallback | ✅ | `nmea_parser.dart` | 43-44, 119-143 | `_parseLk8ex1()` vollstaendig implementiert |

### Konsequenz fuer Ticket 2

Das Binary-Protokoll ist real und produktionsreif implementiert. Der Vario Core designed seine Output-Structs direkt passend zu den BLE-Packets. Kein Fallback auf LK8EX1 noetig — das existiert bereits als Skytraxx-Kompatibilitaets-Pfad in der App.

### Relevante Dateien (read-only Referenz)

| Datei | Zweck |
|-------|-------|
| `aura_binary_parser.dart` | Packet-Parsing (0xAA/BB/CC), UUID-Konstanten |
| `ble_service.dart` | Device-Discovery, Connection, Subscription |
| `nmea_parser.dart` | LK8EX1/PXGD Fallback fuer Nicht-AURA-Geraete |
| `vario_data.dart` | Unified VarioData-Klasse |
| `aura_mock_generator.dart` | Test-Packet-Generator (Validierungs-Referenz) |

---

## Aufgabe 2 · Lizenz-Check har-in-air

**Frage:** Duerfen wir den KF4D-Code aus har-in-air uebernehmen, oder muessen wir neu schreiben?

### Ergebnis: ⚠️ GPL — KEIN CODE UEBERNEHMEN, CLEAN-ROOM

| Repo | Lizenz | Copyleft |
|------|--------|----------|
| ESP32_IMU_BARO_GPS_VARIO | **GPL-3.0** | Ja — abgeleitete Werke muessen unter GPL stehen |
| ESP32C3_BLUETOOTH_AUDIO_VARIO | **GPL-3.0** | Ja |
| Kalmanfilter_altimeter_vario | **GPL-2.0** | Ja |

### Was das bedeutet

- ❌ **Code kopieren/adaptieren:** Nicht moeglich ohne AURA unter GPL zu stellen
- ❌ **Variablennamen/Struktur nachahmen:** Riskant, gilt als "derivative work"
- ✅ **Clean-Room Reimplementierung von der Mathematik:** Vollstaendig legal

### Warum Clean-Room kein Problem ist

Die Mathematik im "KF4D" ist **Standard-Kalman-Filter-Theorie**:

1. Zustandsvektor `x = [h, v, a, a_bias]` — klassische Physik (Weg, Geschwindigkeit, Beschleunigung)
2. State-Transition `F` — Newton'sche Bewegungsgleichung: `s = s₀ + v·dt + ½·a·dt²`
3. Messmodell — Barometer liefert Hoehe, IMU liefert Beschleunigung
4. Adaptive Varianz — Standard-Technik, beschrieben in jedem Kalman-Filter-Lehrbuch

Hari Nairs eigenes PDF (`imu_kalman_filter_notes.pdf`) referenziert ausschliesslich oeffentliche Quellen: Wikipedia, TU Muenchen, NXP Application Notes.

### Konkrete Vorgehensweise fuer Ticket 2

1. Kalman-Filter von den Gleichungen implementieren (Lehrbuch-Ansatz)
2. Eigene Variablen- und Funktionsnamen
3. Eigene Code-Struktur
4. har-in-air als **konzeptuelle Referenz** (welche States, welche Raten) — nicht als Code-Vorlage
5. Jupyter-Notebooks fuer Vergleichsdaten nutzen (Fakten/Zahlen sind nicht schuetzbar)

---

## Aufgabe 3 · Simulator-Spec

**Frage:** Wie nutzen wir die 2.5 Wochen Wartezeit produktiv?

### Ergebnis: ✅ SPEC FERTIG

Vollstaendige Spezifikation liegt unter [`docs/VARIO-SIMULATOR-SPEC.md`](../VARIO-SIMULATOR-SPEC.md).

### Kurzfassung

**6 Szenarien**, alle als deterministische CSV-Streams (fester Random-Seed):

| # | Szenario | Dauer | Prueft |
|---|----------|-------|--------|
| 01 | Ruhe (Still) | 60 s | Filter-Konvergenz, kein falscher Vario |
| 02 | Rampe (Linear Climb) | 30 s | Latenz < 200 ms, Ueberschwingen < 10% |
| 03 | Sinus (Wellenlift) | 120 s | Phasentreue < 100 ms, Amplitude ±5% |
| 04 | Thermik (Entry + Coring) | 180 s | Thermik-Erkennung < 5 s, kein False-Positive |
| 05 | Turbulenz-Burst (Collapse Reject) | 60 s | Kein falscher Thermik-Alarm |
| 06 | Zentripetal (Coring) | 120 s | Zentripetal-Rejection < 0.2 m/s Fehler |

**CSV-Format:** `t_ms, baro_pa, accel_z_ms2, gyro_z_dps, temp_c, baro2_pa`
**Runner:** PlatformIO `[env:native]` fuer identischen C++ Code + Python-Wrapper fuer Analyse/Plots.

### Akzeptanz-Metriken

| Metrik | Ziel |
|--------|------|
| Vario-Latenz | < 200 ms |
| RMS-Fehler | < 0.1 m/s |
| Max-Fehler | < 0.5 m/s |
| Thermik-Delay | < 5 s |
| False-Positive-Rate | 0% |
| Zentripetal-Rejection | < 0.2 m/s |

---

## Zusammenfassung

| Aufgabe | Ergebnis | Konsequenz |
|---------|----------|------------|
| BLE-Beweis | ✅ Alles bestaetigt | Binary-Protokoll verwenden, kein LK8EX1-Fallback noetig |
| Lizenz-Check | ⚠️ GPL-3.0 | Clean-Room von der Mathe, kein Code kopieren |
| Simulator-Spec | ✅ Fertig | 6 Szenarien, PlatformIO native + Python |

**Gruenes Licht fuer Ticket 2** — alle Voraussetzungen verifiziert.
