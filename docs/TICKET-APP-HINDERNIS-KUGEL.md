# TICKET → App-Baumeister: Hindernis-Warnung „3D-Schutzkugel" in der App

## Stand 2026-06-14 · Krücke-Firmware bereit (`dd9fa6d`), App-Seite offen

---

## Was die Krücke schon kann
Neue **Hindernis-Warnung** als **3D-Schutzkugel um den Piloten**. Zwei konzentrische Kugeln, beide Radien **live einstellbar** über den **bestehenden** BLE-Schreibweg — **kein neues Protokoll**.

- **Eine echte Kugel:** 3D-Distanz `√(horizontal² + vertikal²)` zu jedem Objekt.
- **Äussere Kugel** → **Vorwarnung** (leiser Chirp, kein Block-Screen → kein Gaggle-Spam).
- **Innere Kugel** → **Alarm** (lauter Warble + Banner „!! HINDERNIS !!" mit Distanz + Himmelsrichtung).
- **Quellen:** FANET-**Verkehr** (live, mit Höhe) + **Gipfel** (fest). Nur im Flug aktiv.

| Key | Typ | Einheit | Bereich | Default |
|---|---|---|---|---|
| **`warn.obstacle`** | bool | — | true/false | **true** (an) |
| **`warn.sphere_outer_m`** | integer | **Meter** | 50 … 2000 | **300** |
| **`warn.sphere_inner_m`** | integer | **Meter** | 20 … 1000 | **100** |

> **innen < aussen** — die Firmware erzwingt das (setzt innen = aussen/2, falls innen ≥ aussen). Bitte trotzdem in der App so validieren, dass der innere Slider nie über den äusseren kann.

---

## Was du in der App einpflegen musst

1. **UI im Warn-/Sicherheits-Reiter** der Konfiguration:
   - **Schalter** „Hindernis-Warnung" → `warn.obstacle`
   - **Slider „Äussere Kugel (Vorwarnung)"** in Metern, empfohlen **100–600 m**, Schritt 25 m, Default **300**
   - **Slider „Innere Kugel (Alarm)"** in Metern, empfohlen **50–300 m**, Schritt 10 m, Default **100**
   - Hinweistext z. B.: *„Warnt vor anderen Luftfahrzeugen (FANET) und Gipfeln innerhalb der Kugel."*
2. **Schreiben** beim Ändern — über den schon implementierten Settings-Weg (Char `…0006`, `kind:settings`):
   ```json
   { "kind":"settings", "k":"warn.sphere_outer_m", "v":300 }
   { "kind":"settings", "k":"warn.sphere_inner_m", "v":100 }
   { "kind":"settings", "k":"warn.obstacle", "v":true }
   ```
   Erwartetes Echo (Notify auf `…0006`):
   ```json
   { "ack":"settings", "k":"warn.sphere_outer_m", "ok":true }
   ```
   Bei out-of-range: `{ "ack":"settings", "k":"…", "ok":false, "err":"range" }`.
3. **Persistenz:** liegt im versionierten Settings-Modell der Krücke (`schema_version`/`updated_at`) und übersteht Neustart — App muss nichts cachen, nur schreiben.

---

## HEILIG (Vertrag §5b) — bitte beachten
**Abgeschaltete Warnung bleibt am Gerät sichtbar, nie still aus.** Setzt die App `warn.obstacle=false`, zeigt die Krücke oben in der Statusleiste **„⚠ HIND-AUS"** (analog `warn.airspace=false` → „LR-AUS", beide aus → „WARN-AUS"). Die App darf das Abschalten also ruhig anbieten — der Pilot sieht es am Gerät weiter.

---

## Wirkung am Gerät (zur Kontrolle)
- Boot-Log: `[KUGEL] Hindernis-Warnung AN  aussen=300m innen=100m` + Selbsttest `[KUGEL-TEST] 85m→2 250m→1 500m→0`.
- Änderung greift **live** (kein Neustart): `warn.*` → sofort `warnLoad()`.
- Echter Alarm braucht ein Objekt in der Kugel (FANET-Gegenstelle oder Gipfel im Flug) — am Tisch nur über das Selbsttest-Log prüfbar.

---

## Kontext
**Derselbe Schreibweg** wie alle Settings (M2). Du fügst im Warn-Reiter **einen Schalter + zwei Slider** hinzu. Die Keys stehen im **KONFIG-VERTRAG Teil 2** (Gruppe `warn`) ergänzt. **BAZL-Hindernisse** (Antennen/Kabel) sind noch nicht in der Kugel — die liegen nur als Download auf der SD; der GeoJSON-Parser ist der nächste Ausbau.
