# burnair Stack- & Architektur-Analyse

**Zweck:** Tech-Benchmarking als Referenz für *Flight Buddy KI*. Reines Beobachten/Dokumentieren —
kein Auth-Umgehen, kein Tile-/Daten-Abgriff, keine Keys/Secrets, kein Re-Hosting.

**Datum:** 2026-06-09
**Artefakte:**
- **App:** `burnair Map 3.0.57` (APKPure-`.xapk`), Package `com.burnair.burnairmap`, versionCode `277`.
  SHA-256 (`.xapk`): `86c2b985a5627bdf36764bbaa4a8f571c551c385ceda6f017fa255d865f26636`.
- **Web:** gespeicherte Startseite von **`www.burnair.cloud`** (Titel „burnair Karte – Gleitschirm
  Fluggebiete"), App-Version-Tag `?v=2.184.43`.

**Belegführung:** „**[APK]**" = aus entpacktem App-Artefakt verifiziert ·
„**[WEB]**" = aus gespeicherter Web-Startseite verifiziert ·
„**[Vermutung]**" = hergeleitet, nicht bewiesen · „**[Sekundär]**" = öffentliche Quelle.

> **Korrektur ggü. früherem Stand:** Die anfängliche Vermutung „MapLibre" ist **widerlegt** —
> die Web-Karte nutzt **Leaflet + Mapbox GL JS** (s. Abschnitt 2). Datenstand jetzt großteils belegt.

---

## 0. Datenlage & Einschränkung

| Bereich | Status |
|---|---|
| **App/APK (Aufgabe B)** | ✅ direkt analysiert (entpackt) |
| **Web-Map `www.burnair.cloud` (Aufgabe A)** | ✅ Startseite + Asset-Liste analysiert (per Upload) |
| **Web-Map `map.burnair.cloud` (v3 „neue Engine")** | ⚠️ **nicht** inspiziert — kann abweichen |
| **Live-HTTP-Header (CSP/Cache-Control/ETag)** | ⚠️ nicht messbar (`burnair.*` per Netz-Policy gesperrt) |

Untersucht wurde die **klassische Karte** unter `www.burnair.cloud`. Die separat beworbene
„komplett neue Maps-Engine v3" unter `map.burnair.cloud` **[Sekundär]** wurde **nicht** geladen und
**kann eine andere Engine** verwenden. Klassennamen der App sind R8/ProGuard-**obfuskiert**.

---

## 1. App-Stack (Aufgabe B) — **verifiziert [APK]**

### Framework: **Native Android (Kotlin)** — kein Cross-Platform-Wrapper
- **Kein `lib/`** → kein nativer Code → **kein Flutter** (`libflutter.so`/`libapp.so` fehlen),
  **kein React Native** (`libhermes.so`/`index.android.bundle` fehlen). `.xapk`-Splits nur
  Sprachen + `hdpi`, **keine ABI-Splits**.
- **Kein `assets/www/`**, keine `capacitor.config`/`config.xml` → **kein Capacitor/Cordova**.
- **Kotlin 1.9.22**, Gradle 8.0; `kotlin-tooling-metadata.json` → **`isHmppEnabled: true`**
  (Indiz für **Kotlin Multiplatform**). **UI: kein Compose** → klassische Views (135 Layout-XMLs).

### Bibliotheken [APK]
OkHttp **+** Ktor (HTTP) · **Room/SQLite** (`burnair.db`) · kotlinx.serialization/coroutines ·
**Dagger** (DI) · **Firebase** (Cloud Messaging 17.1 + Analytics/Measurement + DataTransport) ·
Google Play Services (base 18.0.1).

### Karten-Rendering der App: **WebView-gehostete Web-Karte** — *jetzt gut belegt*
- **Keine native Map-Lib** (keine `.so`, keine `gms.maps`/`osmdroid`/`mapbox`-Klassen). **[APK]**
- WebView-Setup für eine **interaktive Web-Karte**: `setDomStorageEnabled`, `setGeolocationEnabled`
  + `onGeolocationPermissionsShowPrompt`, `addJavascriptInterface`/`@JavascriptInterface`,
  `evaluateJavascript`. **[APK]**
- **Quervergleich bestätigt:** App-Bridge-Callbacks `LiveTrackingCallback`/`…VisibilityCallback`
  **[APK]** ↔ Web-Asset **`livetracking.min.js`** **[WEB]** → die App hostet die burnair-Web-Karte
  im WebView und steuert Tracking über die JS-Bridge. **Keine Map-Assets in der APK gebündelt** →
  Karte wird **remote** geladen.

### In-Flight-Sensorik & Manifest [APK]
- **BLE** (`BluetoothGatt/Manager`), **NMEA**-Parsing, **XC Tracer** (`XCTRAC` + eigene Permission).
- `versionName 3.0.57`, minSdk 26 / targetSdk 35. Permissions u. a. `*_LOCATION` inkl. `BACKGROUND`,
  **`FOREGROUND_SERVICE_LOCATION`**, `CAMERA`, FCM `RECEIVE`, `BETA_URL`.
- Signatur: v2/v3-Block vorhanden; **volle Zertifikatsprüfung mangels `apksigner` nicht erfolgt**.

---

## 2. Web-Stack `www.burnair.cloud` (Aufgabe A) — **verifiziert [WEB]**

### Karten-Engine: **Leaflet (Kern) + Mapbox GL JS (Vektor-Layer)** — *nicht* MapLibre
- **Leaflet** ist die Haupt-Engine: 356 Treffer, `leaflet.js` + ~20 Leaflet-Plugins
  (`leaflet-hash` = das `#zoom/lat/lon`-Routing, `fullscreen`, `rotatedMarker`, `hotline`,
  `polylineDecorator`, `textpath`, `GoogleMutant`, `tilelayer-wmts`, …). **[WEB]**
- **Mapbox GL JS** als **Leaflet-Plugin** eingebunden (`leaflet-mapbox-gl.js`, `mapbox-gl.js`,
  `mapbox-gl.css`, `mapbox-styles.min.js`) → **Vektor-Tiles** als GL-Layer über Leaflet.
  Ein Mapbox-`access_token` ist vorhanden (**Wert nicht protokolliert** — nur Präsenz). **[WEB]**

### Framework/Bundler: **klassische jQuery-Multi-File-Seite — kein SPA-Bundler**
- **jQuery 3.6.3** + viele einzeln eingebundene `*.min.js`. **Kein** React/Vue/Angular/Svelte,
  **kein** Vite/Next/Webpack (die `vite`/`next`-Treffer waren Fehltreffer: „NextPublicTransport",
  „Previtemp", „next-hour"). **[WEB]**
- Cache-Busting über Query-Versionierung `?v=2.184.43`. **[WEB]**
- **PWA:** `manifest.webmanifest` vorhanden. Kein expliziter Service-Worker im statischen Snapshot
  sichtbar (ggf. in `main.js`, nicht vorliegend). **[WEB]**

### Begleit-Bibliotheken [WEB]
**Charts:** Highcharts + D3 v6 (Wind-, Föhn-, Top-Thermik-, Point-Forecast-, XC-Flight-Charts) ·
**Geo:** turf.js, **proj4 / proj4leaflet** (Schweizer Gitter, swisstopo) ·
axios, underscore, moment, sweetalert2, fancybox, html5-qrcode, hls.min.js.
**Domänen-Logik:** `livetracking.min.js`, `route-planner`, `fai-triangle`, `parseigc.js`,
`GPXParser`, `detect-peaks`, `poi-tool`, `drawing-tool`, `competition_management`,
`windbarb`, `point-forecast-chart`.

---

## 3. Karten-/Tile-/Daten-Architektur

### Basiskarten-Layer [WEB]
- **Google Maps** (via `Leaflet.GoogleMutant`, `maps.google.com` 83×) — Raster-Basis.
- **swisstopo** über **WMTS** (`leaflet-tilelayer-wmts.js`) — Schweizer Amtskarten.
- **MapTiler** referenziert (Basiskarten/Styles). **[WEB]**
- **Mapbox GL** Vektor-Layer (eigene/Mapbox-Styles, `mapbox-styles.min.js`). **[WEB]**
→ **Hybrid: Raster-Basis (Google/swisstopo-WMTS) + Vektor-Overlays (Mapbox GL)** über einen
gemeinsamen Leaflet-Kern. **Kein** Hinweis auf PMTiles/MBTiles in der Web-Seite.

### Hosts (nur Hostnamen; **keine** Keys/Query-Strings protokolliert)
| Host | Rolle (hergeleitet) | Quelle |
|---|---|---|
| `api.burnair.cloud` | Daten-/Backend-API | [WEB]/[APK] |
| `cf-ws.burnair.cloud` | WebSocket („ws") — Echtzeit | [WEB] |
| `cf-lt.burnair.cloud` | **Live-Tracking** („lt") | [WEB] |
| `static.burnair.cloud` | statische Assets/CDN | [WEB] |
| `map.burnair.cloud` / `dev-*` | Web-Karte v3 (im App-WebView) | [APK] |
| `burnair-regtherm.s3.eu-central-1…` | **S3-Bucket** Thermik-Daten („regtherm", Frankfurt) | [APK] |
| `academy/help.burnair.cloud` | Lern-/Hilfe-Inhalte | [WEB] |

> `cf-`-Präfix + Versionierungs-Querys → **Cloudflare-fronted**, Real-Time-Tracking über
> **WebSocket** (`cf-ws`/`cf-lt`). **[WEB-Indiz]**

### Persistenz/Offline
- App: **Room/SQLite** `burnair.db` (lokaler Track-Puffer). **[APK]**
- **Live-HTTP-Header (CSP, Cache-Control, ETag) weiterhin nicht messbar** (Hosts gesperrt).

---

## 4. Lehren für Flight Buddy KI (Übernehmen / Meiden)

1. **Bewährte, „langweilige" Web-Engine zahlt sich aus:** Leaflet + Mapbox-GL-Plugin + jQuery —
   kein SPA-Framework. **[WEB]** → *Übernehmen, wenn* Stabilität/viele Geo-Plugins zählen; eine
   riesige Plugin-Sammlung (FAI-Dreieck, IGC/GPX, Hotline, Windbarbs) gibt es für Leaflet fertig.
   *Abwägen:* moderne Vektor-Performance kommt hier über das **Mapbox-GL-Plugin**, nicht über reines
   Leaflet-Raster.
2. **Hybrid Raster-Basis + Vektor-Overlay:** Google/swisstopo-WMTS als Basis, Mapbox-GL-Vektor für
   eigene Layer. **[WEB]** → Übernehmen: amtliche/genaue Basiskarten extern, eigene Daten als
   performante Vektor-Tiles drüber.
3. **Statische Geodaten aus Objekt-Storage (S3 `regtherm`).** **[APK]** → Vorgerechnete Thermik-/
   Klima-Daten als statische, cache-bare Objekte serven statt teurer dynamischer Endpunkte.
4. **Echtzeit über WebSocket** (`cf-ws`/`cf-lt`) statt Polling. **[WEB-Indiz]** → Für Live-Tracking/
   FANET-Positionen übernehmen.
5. **Native Sensorik-Bridge + Web-Karte im WebView:** zeitkritische BLE/NMEA-Sensorik nativ, Karte
   als WebView, gekoppelt über schmale **JS-Bridge** (`LiveTracking*` ↔ `livetracking.min.js`).
   **[APK]+[WEB]** → sauberes Muster für „eine Karte, zwei Plattformen".
6. **Offline-First-Track: Room/SQLite-Puffer.** **[APK]** → Pflicht für Vario mit Funklöchern.
7. **Schlanker App-Stack** (~7,6 MB base): Kotlin + Coroutines + OkHttp/Ktor + Room + Dagger,
   klassische Views. **[APK]** → Framework-Gewicht meiden.
8. **Meiden / abweichen:**
   - burnair bündelt **keine** Offline-Map-Assets → ohne Netz **keine Karte**. Für ein autarkes
     Vario besser **native Offline-Tiles (PMTiles/MBTiles)** statt reiner WebView-Web-Karte.
   - burnairs **bewusster Vario-Verzicht** (Phone-GPS genüge) ist für ein **Hardware-Vario**
     (Kr-ken/AURA) die Gegenstrategie — *nicht* übernehmen. **[Sekundär]**
   - Reine **jQuery-Multi-File-Architektur** ist für eine *neue* Codebasis wartungstechnisch kein
     Vorbild; die Engine-Wahl (Leaflet/MapLibre/Mapbox-GL) ja, das Bundling nein.

---

## 5. Offene Fragen
1. **`map.burnair.cloud` (v3 „neue Engine")** wurde **nicht** inspiziert — nutzt es eine *andere*
   Engine (MapLibre? reines Mapbox/MapLibre-GL-SPA)? Die hier belegten Fakten gelten für die
   **klassische** `www.burnair.cloud`.
2. **Tile-Format der Mapbox-GL-Vektor-Layer** (MVT/PBF, Style-/TileJSON-Quelle) — Bundle-Inhalt
   (`mapbox-styles.min.js`) nicht im Detail geprüft.
3. **HTTP-Caching/Offline-Strategie** (CSP, Cache-Control, ETag, Service-Worker) — nicht messbar.
4. **KMP-Umfang** der App (`isHmppEnabled`) — wird Logik mit iOS geteilt? Nur Indiz.
5. **iOS-Apps** (`burnair Map`/`Go`) nicht analysiert (keine `.ipa`).

---

## Anhang: Reproduktion
- **App:** `scripts/analyze_burnair.sh` (entpackt `.xapk`→`base.apk`, Framework-Marker, Libs,
  Hosts, Signatur). APK/`.xapk` nach `burnair_apk/` legen.
- **Web (vollständig):** Umgebung mit Allowlist `burnair.cloud`/`map.burnair.cloud`, dann Bundles
  laden und `mapbox-styles.min.js`/`main.js` nach Style-/TileJSON-/Tile-URLs greppen + Live-Header
  (`curl -sSI … | grep -i 'content-security-policy|cache-control|etag'`).

### Quellen (Sekundär)
- `https://www.burnair.cloud/` · `https://map.burnair.cloud/` · Google Play `com.burnair.burnairmap`
- App Store `id1495320175` (Map) / `id1666354109` (Go) · `https://help.burnair.cloud/`

> `burnair.*`-Hosts wurden **nicht** serverseitig abgefragt (Netz-Policy `host_not_allowed`);
> Web-Befunde stammen aus der vom Nutzer hochgeladenen, lokal gespeicherten Startseite.
> Ein vorhandener Mapbox-`access_token` wurde **nicht** im Klartext protokolliert.
