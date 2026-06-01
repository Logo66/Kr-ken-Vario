# burnair Stack- & Architektur-Analyse

**Zweck:** Tech-Benchmarking als Referenz für *Flight Buddy KI*. Reines Beobachten/Dokumentieren —
kein Auth-Umgehen, kein Tile-/Daten-Abgriff, keine Keys/Secrets, kein Re-Hosting.

**Datum:** 2026-06-01
**Analyst-Umgebung:** Claude Code (Remote-Sandbox)

---

## ⚠️ Wichtiger Vorbehalt zur Datenlage (bitte zuerst lesen)

Die in **Aufgabe A** und **Aufgabe B** geforderte *direkte* Untersuchung war in dieser
Ausführungsumgebung **nicht möglich**:

| Versuch | Ergebnis | Beleg |
|---|---|---|
| `curl -I https://burnair.cloud/` | `HTTP/2 403` | Header `x-deny-reason: host_not_allowed` |
| `curl -I https://burnair.ch/` , `https://www.burnair.ch/` | `HTTP 403` | `x-deny-reason: host_not_allowed` |
| `curl -I https://map.burnair.cloud/` | blockiert | (über Fetch-Proxy `403`) |
| `WebFetch https://burnair.cloud/` , `…/map`, Play-Store-Seite | `HTTP 403 Forbidden` | Fetch-Proxy-Allowlist |
| Gegenprobe `https://github.com/` | `HTTP 200` | erreichbar |
| Gegenprobe `https://example.com/` | `HTTP 403 host_not_allowed` | Allowlist greift |
| APK-Ordner `./burnair_apk/` | **nicht vorhanden** | `ls` im Repo-Root |

**Konsequenz:** Die Netzwerk-Policy dieser Umgebung erlaubt nur eine Allowlist (u. a. GitHub).
Alle `burnair.*`-Hosts sind gesperrt → es konnten **keine** Roh-HTML-/JS-/CSS-Bundles geladen,
**keine** Live-HTTP-Header (CSP/Cache-Control/ETag) inspiziert und **keine** APK entpackt werden.

Alles unten ist daher entweder
(a) aus **öffentlichen Sekundärquellen** (App-Stores, Hilfecenter, Web-Suche) belegt, oder
(b) als **Vermutung** gekennzeichnet.
Die direkte technische Verifikation steht noch aus → siehe *Abschnitt 5 (Offene Fragen)* und
*Anhang: So vervollständigen*.

---

## 1. Web-Stack (Aufgabe A)

**Status: NICHT direkt verifiziert** (Hosts blockiert, s. o.). Sekundärquellen:

- Es existiert eine reine **Web-Version der Karte** unter `map.burnair.cloud` ("**burnair Map v3**"),
  die laut Hersteller **ohne App-Installation** im Browser läuft → spricht für eine
  **PWA / Single-Page-Web-App**.
  *Beleg:* Suchtreffer-Titel "burnair Map v3 — `https://map.burnair.cloud/`" sowie
  Produkttext "Website-Version, die keine App-Installation erfordert".
- Marketing-Aussage: "**komplett neue Maps-Engine, schneller und flüssiger**" (Version v3).
  *Beleg:* Produktbeschreibung burnair Map (App-Store-/Web-Suche).

**Konkrete Befunde zu Aufgabe A1–A5 (Engine, Bundler, Tiles, Header):**
→ **konnten nicht erhoben werden** (kein Asset-Zugriff). Keine belastbare Aussage zu
mapbox-gl / maplibre-gl / leaflet / openlayers, zu webpack/vite/parcel oder zu
CSP/Cache-Control/ETag möglich.

**Vermutung (NICHT belegt):** Für eine GPU-beschleunigte Vektor-Karte mit "neuer Engine" und
Schweiz-Fokus ist **MapLibre GL JS** (OSS-Fork von mapbox-gl) ein plausibler Kandidat, ggf. mit
**swisstopo**-Vektortiles (z. B. via MapTiler). Das ist eine **Hypothese**, kein Fund.

---

## 2. App-Stack(s) (Aufgabe B)

**Status: NICHT direkt verifiziert** (keine APK im Repo, Stores nicht ladbar). Sekundärquellen:

### burnair Map (Karten-App)
- **Android-Package:** `com.burnair.burnairmap`; **iOS** App-Store-ID `1495320175`.
- **Version** (öffentliche APK-Mirrors): Bereich **v1.16.x** (genannt u. a. 1.16.4 / 1.16.7).
- **APK-Downloadgröße:** ~**7,32 MB**; Drittquelle nennt "**24 libraries**".
  *Beleg:* APK-Mirror/AppBrain-Suchtreffer. **Hinweis:** Diese Zahlen stammen von
  Drittanbieter-Indexen und sind **nicht** am Original-Artefakt verifiziert.

> **Framework-Marker (Flutter / React Native / Capacitor / nativ):** **unbestimmt.**
> Ohne entpackte APK ließen sich `libflutter.so`/`libapp.so` (Flutter),
> `index.android.bundle`/`libhermes.so` (React Native) oder `assets/www/` (Capacitor/Cordova)
> **nicht** prüfen.
>
> *Vermutung (schwach, NICHT belegt):* Eine Downloadgröße von ~7 MB ist für eine
> vollwertige Flutter-App eher klein; das **könnte** auf eine schlanke native oder
> WebView-/PWA-Wrapper-Architektur hindeuten — aber die 7,32 MB stammen aus einer
> unbestätigten Drittquelle und können komprimiert/teil­geladen sein. **Keine Schlussfolgerung.**

### burnair Go (In-Flight-Navigation/Tracking)
- **iOS** App-Store-ID `1666354109`; eigenständige App neben burnair Map.
- Belegte Eigenschaften (Hersteller-/Hilfecenter-Texte):
  - "**GPS-genaue Zeitstempel** für saubere Tracks ohne Drift"
  - "**Regenradar** … schneller, präziser, weniger Datenverbrauch"
  - "**burnair Live Tracking**" standardmäßig integriert
  - **Bluetooth-Kopplung** mit **XC Tracer**; Empfang von **FANET**-Piloten auf der Karte
  - Strategie-Aussage: Varios werden **bewusst nicht** an burnair Go angebunden, da
    Smartphone-GPS als ausreichend genau gilt.
  *Beleg:* burnair Help Center "Live Tracking mit der burnair Go App" und
  "… via Bluetooth verbinden"; App-Store-Beschreibung.

---

## 3. Karten-/Tile-Architektur (Aufgabe A4)

**Status: weitgehend NICHT verifiziert.** Belegt nur aus Feature-Beschreibungen:

- **Overlay-/Fachlayer** vorhanden: **Wind**, **Wetter**, **Thermik/Thermik-Hotspots**,
  **Lee-Gebiete**, **Föhn-Vergleich**, **Regenradar**, **Hike & Fly**, **Live-Tracking**.
  *Beleg:* Produkt-/Store-Beschreibungen.
- **Live-Tracking-Resilienz:** "stabileres Live-Tracking, zuverlässiger im Hintergrund,
  robuste **lokale Speicherung der Trackpunkte** mit automatischem Nachladen".
  *Beleg:* burnair-Map-Release-/Produkttext.
  → Architektur-Signal: **Offline-First für Tracklog** (lokaler Puffer + Re-Sync), nicht nur
  Live-Stream.
- **Regenradar:** "schneller, präziser, **weniger Datenverbrauch**" → Hinweis auf
  optimiertes/komprimiertes Tile- oder Frame-Format (Vermutung), nicht belegt im Detail.

**Tile-Format (MVT/PBF vs. Raster), PMTiles/MBTiles, konkrete Tile-Endpoints:**
→ **nicht ermittelbar** ohne Asset-/Netzwerk-Zugriff. Keine belastbare Aussage.

**Vermutung (NICHT belegt):** Schweiz-zentrierte Outdoor-Karte → Basiskarte plausibel auf
**swisstopo**-Daten; "neue, schnellere Engine" passt zu **Vektortiles (MVT)** statt Raster.
Reine Hypothese.

---

## 4. Lehren für Flight Buddy KI (Übernehmen / Meiden)

> Diese Lehren stützen sich überwiegend auf **belegte Feature-/Strategie-Aussagen** von burnair
> sowie auf allgemeine Best Practices — **nicht** auf direkt verifizierte Implementierungsdetails.
> Wo eine Lehre auf einer Vermutung beruht, ist das markiert.

**Übernehmen:**
1. **Offline-First-Tracklog** — robuste lokale Persistenz der Trackpunkte mit
   automatischem Nachladen/Re-Sync. *Belegt* bei burnair; für ein Vario/Flight-Buddy mit
   instabilem Mobilfunk im Gebirge essenziell.
2. **Eine Web-Karte ohne Pflicht-Install (PWA)** zusätzlich zu nativen Apps —
   senkt Einstiegshürde, ein Code-/Style-Stand für Web + App. *Belegt* (map.burnair.cloud v3).
3. **Bandbreitenschonende Layer** (Regenradar "weniger Datenverbrauch") — bei Flugdaten/Wetter
   bewusst auf komprimierte/inkrementelle Formate setzen. *Belegt* (Aussage), Format unbekannt.
4. **GPS-genaue Zeitstempel** statt Geräte-Uhr → driftfreie Tracks. *Belegt*; einfach zu
   übernehmen, große Wirkung auf Track-Qualität/IGC-Konformität.
5. **Klarer Produktschnitt:** getrennte Apps für *Planung/Karte* (burnair Map) und
   *In-Flight* (burnair Go). *Belegt*; reduziert In-Flight-UI-Komplexität — relevant für ein
   E-Paper-Vario mit minimalem UI.
6. **Offene/standardisierte Konnektivität:** **FANET**-Empfang und **Bluetooth-NMEA**
   (XC Tracer / `$LK8EX1`/`$XCTRC`-Sätze sind im Ökosystem üblich). *Belegt* für burnair Go;
   für Flight Buddy KI als Interop-Standard übernehmen.
7. *(Vermutung)* **Vektortiles (MapLibre-Klasse) statt Raster** für glatte, GPU-beschleunigte
   Karten — falls eine eigene Web-Karte gebaut wird. Hypothese, nicht aus burnair verifiziert.

**Meiden / Vorsicht:**
- **Keine voreilige Engine-/Framework-Wahl auf Basis von Vermutungen** — burnairs konkreter
  Stack ist hier *nicht* verifiziert; nicht "weil burnair angeblich X nutzt" entscheiden.
- **Bewusst auf Vario-Kopplung verzichten?** burnair koppelt Varios *absichtlich nicht*
  (Phone-GPS reiche). Für ein **dediziertes Vario-Projekt** (Kr-ken/AURA) ist das **genau die
  Gegenstrategie** — also *nicht* übernehmen: die Hardware-Sensorik (BMP581, IMU) ist hier der
  Kernwert.
- **Proprietäre/lizenzpflichtige Tile-Quellen** ohne Offline-Lizenz meiden, wenn Offline-Nutzung
  im Funkloch Pflicht ist (Lizenzkosten + Offline-Recht prüfen).

---

## 5. Offene Fragen

1. **Karten-Engine?** maplibre-gl vs. mapbox-gl vs. leaflet/openlayers — **unbestätigt**.
2. **Web-Framework/Bundler?** react/vue/svelte/angular + vite/webpack — **unbestätigt**
   (keine Chunk-Namen / `sourceMappingURL` einsehbar).
3. **Tile-Format & Quelle?** MVT/PBF vs. Raster; swisstopo vs. eigene; PMTiles/MBTiles? —
   **unbestätigt**.
4. **HTTP-Caching/Offline-Strategie der Web-Map?** CSP, Cache-Control, ETag, Service-Worker —
   **nicht messbar** in dieser Umgebung.
5. **App-Framework?** Flutter / React Native / Capacitor / nativ — **unbestätigt**
   (keine APK-Marker geprüft). Die "~7,32 MB / 24 libraries" stammen aus unverifizierter
   Drittquelle.
6. **Overlay-Rendering:** Werden Wind/Thermik/Radar als Raster-Overlays, als animierte
   WebGL-Layer oder als deck.gl-/Custom-Layer gerendert? — **unbestätigt**.

---

## Anhang: So lässt sich die Analyse vervollständigen

Sobald eine Umgebung mit Netzzugang zu `burnair.*` **oder** lokal vorliegende APKs verfügbar sind:

**Web (A):**
```bash
# Roh-HTML + Asset-Liste
curl -sSL https://map.burnair.cloud/ -o burnair_index.html
grep -oE 'src="[^"]+"|href="[^"]+"' burnair_index.html | sort -u
# Engine/Framework
grep -aoiE 'maplibre-gl|mapbox-gl|leaflet|openlayers|cesium|deck\.gl|three|react|vue|svelte|angular' burnair_*.js
grep -aoE 'sourceMappingURL=[^ ]+' burnair_*.js
# Tiles/Format
grep -aoE 'https?://[^"]+\.(pbf|mvt|pmtiles|json)|/tiles?/' burnair_*.js
# Header / Caching
curl -sSI https://map.burnair.cloud/ | grep -iE 'content-security-policy|cache-control|etag|service-worker'
```

**App (B):** APK nach `./burnair_apk/` legen, dann:
```bash
unzip -o app.apk -d app_extracted
ls app_extracted/lib/*/                      # libflutter.so + libapp.so => Flutter
ls app_extracted/assets/index.android.bundle # => React Native (+ libhermes.so)
ls app_extracted/assets/www/                 # => Capacitor/Cordova WebView
# Map-/Netzwerk-Libs & Hosts (OHNE Keys/Secrets zu loggen):
strings app_extracted/lib/*/*.so | grep -iE 'maplibre|mapbox|osmdroid|tangram'
strings app_extracted/**/*.so | grep -aoE 'https?://[a-z0-9.-]+' | sort -u   # nur Hosts, keine Query-Strings/Keys
```

---

### Quellen (öffentliche Sekundärquellen, via Web-Suche)
- burnair Map App-Seite — `https://www.burnair.ch/app/`
- burnair Map v3 (Web) — `https://map.burnair.cloud/`
- Google Play `com.burnair.burnairmap` — `https://play.google.com/store/apps/details?id=com.burnair.burnairmap`
- Apple App Store burnair Map — `https://apps.apple.com/ch/app/burnair-map/id1495320175`
- Apple App Store burnair Go — `https://apps.apple.com/at/app/burnair-go/id1666354109`
- burnair Go App (Portfolio) — `https://www.burnair.ch/portfolio-item/burnair-go-app/`
- burnair Help Center (Live Tracking / Bluetooth) — `https://help.burnair.cloud/`
- APK-Index (Größe/Libs, unverifiziert) — `https://www.appbrain.com/app/burnair-map/com.burnair.burnairmap`

> Hinweis: Die `burnair.*`-Links wurden **nicht** abgerufen (Netz-Policy `host_not_allowed`);
> Inhalte stammen aus Suchergebnis-Snippets und sind als solche zu behandeln.
