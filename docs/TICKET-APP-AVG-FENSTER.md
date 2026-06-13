# TICKET → App-Baumeister: AVG-Vario-Fenster (Mittelungszeit) in der App

## Stand 2026-06-13 · Krücke-Firmware bereit (`69a7c12`), App-Seite offen

---

## Was die Krücke schon kann
Cruise- **und** Thermik-Screen zeigen jetzt **dasselbe** Vario-Mittel, und die **Mittelungszeit ist live einstellbar**. Neuer Settings-Key im Modell (M1), schreibbar über den **bestehenden** BLE-Schreibweg — **kein neues Protokoll**.

| Key | Typ | Einheit | Bereich | Default |
|---|---|---|---|---|
| **`vario.avg_window_s`** | integer | **Sekunden** | 1 … 120 | **20** |

> **Wert in ganzen Sekunden** — wie bei jedem Vario (Integrations-/Mittelungszeit). Typischer nützlicher Bereich **5 … 30 s**; die Firmware akzeptiert 1 … 120.

---

## Was du in der App einpflegen musst

1. **UI-Element** im **Vario-Reiter** der Konfiguration:
   - Label z. B. **„Mittelungszeit / Vario-Integrator"** (oder „AVG climb")
   - **Slider oder Zahlenfeld in Sekunden**, empfohlen **1–30 s** (gern bis 60), Schrittweite 1 s
   - Default **20 s**
2. **Schreiben** beim Ändern — über den schon implementierten Settings-Weg (Char `…0006`, `kind:settings`):
   ```json
   { "kind":"settings", "k":"vario.avg_window_s", "v":20 }
   ```
   `v` = **ganze Sekunden** (number, integer). Erwartetes Echo (Notify auf `…0006`):
   ```json
   { "ack":"settings", "k":"vario.avg_window_s", "ok":true }
   ```
   Bei out-of-range: `{ "ack":"settings", "k":"vario.avg_window_s", "ok":false, "err":"range" }`.
3. **Persistenz:** der Wert liegt im versionierten Settings-Modell der Krücke (`schema_version`/`updated_at`) und übersteht Neustart — die App muss nichts zwischenspeichern, nur schreiben.

---

## Wirkung am Gerät (zur Kontrolle)
- Cruise zeigt **„avg N s"**, Thermik **„AVG CLIMB N s"** — beide mit demselben `N`.
- Änderung greift **live** (kein Neustart nötig).

---

## Kontext
Das ist **derselbe Schreibweg** wie alle anderen Settings (M2). Du fügst nur **ein Feld** im Vario-Reiter hinzu und schreibst den einen Key. Der Key steht im **KONFIG-VERTRAG Teil 2** (Settings-Modell, Gruppe `vario`) ergänzt.
