# burnair Stack- & Architektur-Analyse

**Zweck:** Tech-Benchmarking als Referenz für *Flight Buddy KI*. Reines Beobachten/Dokumentieren —
kein Auth-Umgehen, kein Tile-/Daten-Abgriff, keine Keys/Secrets, kein Re-Hosting.

**Datum:** 2026-06-02
**Analysiertes Artefakt (Aufgabe B):** `burnair Map 3.0.57` (APKPure-`.xapk`),
Package `com.burnair.burnairmap`, versionCode `277`.
SHA-256 (`.xapk`): `86c2b985a5627bdf36764bbaa4a8f571c551c385ceda6f017fa255d865f26636`.

**Belegführung:** „**[APK]**" = direkt aus dem entpackten Artefakt verifiziert ·
„**[Vermutung]**" = plausibel hergeleitet, nicht bewiesen · „**[Sekundär]**" = öffentliche Quelle.

---

## 0. Datenlage & Einschränkung

| Bereich | Status | Grund |
|---|---|---|
| **Aufgabe B (App/APK)** | ✅ **direkt analysiert** | `.xapk` lokal entpackt & untersucht |
| **Aufgabe A (Web-Map live)** | ⚠️ **nur indirekt** | Netz-Policy sperrt `burnair.*` (`HTTP 403 host_not_allowed`); Roh-Bundles/Header nicht ladbar |

Die Web-Map-Aussagen unten stützen sich daher auf **Indizien aus der APK** (referenzierte Hosts,
WebView-Konfiguration) plus Sekundärquellen — **nicht** auf direkt geladene JS/CSS-Bundles oder
Live-HTTP-Header. Die Identität der Web-Karten-Engine (MapLibre o. ä.) bleibt **Vermutung**.

Tooling-Hinweis: Im Container fehlten `apksigner`/`aapt`; Klassennamen sind per **R8/ProGuard
verschleiert** (Library-Paketnamen teils gestrippt). Befunde stützen sich daher auf erhaltene
Framework-Klassen, Strings, Properties-Dateien und Ressourcen.

---

## 1. App-Stack (Aufgabe B) — **verifiziert**

### Framework-Klassifikation: **Native Android (Kotlin)** — kein Cross-Platform-Wrapper
- **Kein `lib/`-Verzeichnis** in der base-APK → **kein nativer Code** → **kein Flutter**
  (kein `libflutter.so`/`libapp.so`), **kein React Native** (kein `libhermes.so`/
  `index.android.bundle`). **[APK]**
- **Kein `assets/www/`**, keine `capacitor.config`/`config.xml` → **kein Capacitor/Cordova**. **[APK]**
- Split-APKs im `.xapk` sind **nur Sprachen + `hdpi`** — **keine ABI-Splits** (`arm64_v8a`/
  `armeabi_v7a`), konsistent mit „kein nativer Code". **[APK]**
- **Kotlin 1.9.22**, Gradle 8.0, `KotlinAndroidPluginWrapper`; `kotlin-tooling-metadata.json`
  zeigt **`isHmppEnabled: true`** → Hinweis auf **Kotlin Multiplatform (KMP)** (geteilte Logik
  Android/iOS). **[APK]** (KMP-Nutzung selbst: **[Vermutung]**, gestützt durch HMPP-Flag + Ktor.)
- **UI:** **kein Jetpack Compose** gefunden → klassische **Android Views/XML** (135 Layout-XMLs,
  667 XML-Ressourcen gesamt). **[APK]**

### Eingebettete Bibliotheken **[APK]**
| Zweck | Bibliothek | Beleg |
|---|---|---|
| HTTP (nativ) | **OkHttp** | `okhttp3/`-Verzeichnis, `mockwebserver` |
| HTTP (KMP) | **Ktor** (Client) | `io.ktor`-Strings, „Binary compatibility for Ktor" |
| Persistenz | **Room** über **SQLite** | `androidx/room`, `RoomDatabase`, `SupportSQLite`, DB-Name `burnair.db` |
| Serialisierung/Async | **kotlinx.serialization**, **kotlinx.coroutines** | 2185× `kotlinx`-Strings, `DebugProbesKt.bin` |
| Dependency Injection | **Dagger** | `dagger`-Strings |
| Push & Analytics | **Firebase** (Cloud Messaging 17.1, Measurement/Analytics, DataTransport 18.1.7) | `firebase-*.properties`, `c2dm` |
| Google-Basis | **Play Services** base 18.0.1 / basement / cloud-messaging / tasks | `play-services-*.properties` |

### Karten-Rendering der App: **WebView-gehostete Web-Karte** — *starkes Indiz*
- **Keine native Karten-Lib**: keine `.so`, keine `osmdroid`/`com.google.android.gms.maps`/
  `mapbox`/`maplibre`-Klassen auffindbar (auch nicht obfuskiert sichtbar). **[APK]**
- **WebView aktiv genutzt** und so konfiguriert, wie man eine **interaktive Web-Karte** einbettet:
  `WebView`/`WebViewClient`, `loadUrl`, `setJavaScriptEnabled`, **`setDomStorageEnabled`**,
  **`setGeolocationEnabled`** + **`onGeolocationPermissionsShowPrompt`**, **`addJavascriptInterface`**
  / `@JavascriptInterface`, `evaluateJavascript`. **[APK]**
- **Native↔JS-Bridge-Callbacks** unverschleiert: `LiveTrackingCallback`,
  `LiveTrackingVisibilityCallback`, `LiveTrackingTypeCallback` → die Web-Karte ruft native
  Funktionen (Tracking) zurück. **[APK]**
- Referenzierter Karten-Host `map.burnair.cloud` / `dev-map.burnair.cloud` (s. u.). **[APK]**

> **Schlussfolgerung:** Die App rendert die Karte **sehr wahrscheinlich** über einen WebView, der
> die Web-Karte (`map.burnair.cloud`) lädt — „**eine Karten-Engine für Web + App**". Geolocation +
> DOM-Storage + JS-Interface sind das typische Setup dafür. **[APK-Indizien; nicht 100 % bewiesen,
> da Klassen verschleiert.]** Es sind **keine** Web-Map-Assets in der APK gebündelt → die Karte wird
> **remote** geladen, nicht offline mitgeliefert. **[APK]**

### In-Flight-Sensorik / Konnektivität **[APK]**
- **Bluetooth (BLE)**: `BluetoothManager`, `BluetoothGatt`, `BluetoothButton`, `*BluetoothRequested`.
- **NMEA**-Parsing (`nMea`-Strings, 10×) → Empfang von GPS/Vario-Sätzen.
- **XC Tracer**: `XCTRAC`-Strings + eigene Permission `com.burnair.permission.XTRACER_TRACKING_PERMISSION`.

### Manifest-Eckdaten **[APK]** (`.xapk/manifest.json` + AndroidManifest)
- `versionName 3.0.57`, `versionCode 277`, **minSdk 26** (Android 8), **targetSdk 35** (Android 15).
- Permissions u. a.: `ACCESS_FINE/COARSE/BACKGROUND_LOCATION`, **`FOREGROUND_SERVICE_LOCATION`**
  (Hintergrund-Live-Tracking), `CAMERA`, `POST_NOTIFICATIONS`, `RECEIVE_BOOT_COMPLETED`,
  `WAKE_LOCK`, `REQUEST_IGNORE_BATTERY_OPTIMIZATIONS`, c2dm `RECEIVE` (FCM), eigene
  `BETA_URL`- und `XTRACER_TRACKING`-Permissions.
- **Signatur:** v2/v3-Signaturblock („APK Sig Block 42") vorhanden; **volle Zertifikatsprüfung
  mangels `apksigner` nicht durchgeführt** — Echtheit über Paketstruktur/Strings plausibel,
  aber nicht kryptografisch bestätigt. **[APK]**

---

## 2. Web-Stack (Aufgabe A) — **indirekt**

**Nicht direkt verifiziert** (Hosts per Netz-Policy gesperrt). Indizien aus der APK + Sekundärquellen:

- Es existiert eine **Web-Karte v3** unter `map.burnair.cloud`, die die App im WebView referenziert;
  laut Hersteller läuft sie auch **ohne App-Installation** im Browser → **PWA/SPA**. **[APK]/[Sekundär]**
- Marketing: „**komplett neue Maps-Engine, schneller und flüssiger**". **[Sekundär]**
- **Engine/Bundler/Tile-Format der Web-Map:** **unbestätigt** — JS/CSS-Bundles nicht ladbar,
  keine `sourceMappingURL`/Chunk-Namen einsehbar, keine Live-Header (CSP/Cache-Control/ETag).
- **[Vermutung]** GPU-beschleunigte **Vektor-Karte (MapLibre GL JS)** mit Vektortiles passt zu
  „neue, schnelle Engine" + WebView-Einbettung + Schweiz-Fokus (ggf. swisstopo-Daten). **Hypothese,
  kein Fund.**

---

## 3. Karten-/Tile-/Daten-Architektur

### Referenzierte Hosts **[APK]** (nur Hostnamen; keine Keys/Query-Strings protokolliert)
| Host | Rolle (hergeleitet) |
|---|---|
| `api.burnair.cloud` / `dev-api.burnair.cloud` | Backend-API (prod/dev) |
| `map.burnair.cloud` / `dev-map.burnair.cloud` | Web-Karte (im WebView geladen) |
| `www.burnair.cloud` | Website |
| `burnair-regtherm.s3.eu-central-1.amazonaws.com` | **S3-Bucket (Frankfurt)** — „**regtherm**" = regionale Thermik-Daten |
| `localhost` | **nur okhttp `MockWebServer`** (Test-Artefakt) — *kein* eingebetteter Server |

### Daten-/Rendering-Muster
- **Offline-/Lokal-Speicher:** **Room/SQLite** `burnair.db` → robuste lokale Persistenz (passt zu
  belegter „lokaler Trackpunkt-Speicherung mit Auto-Reload"). **[APK]/[Sekundär]**
- **Statische Geodaten über S3** (`burnair-regtherm…amazonaws.com`) → Thermik-Daten werden
  **als statische Objekte aus einem CDN/Bucket** ausgeliefert (kostengünstig, cache-bar). **[APK]**
- **Overlay-/Fachlayer** (belegt aus Produkttexten **[Sekundär]**): Wind, Wetter, Thermik-Hotspots,
  Lee, Föhn-Vergleich, **Regenradar** („schneller, präziser, weniger Daten"), Hike & Fly, Live-Tracking.
- **Tile-Format (MVT/PBF vs. Raster), PMTiles/MBTiles, konkrete Tile-Endpoints:** **nicht ermittelbar**
  (Web-Bundles gesperrt; keine Tile-Strings in der APK, da Karte im WebView remote rendert).

---

## 4. Lehren für Flight Buddy KI (Übernehmen / Meiden)

1. **„Eine Karten-Engine für Web + App" via WebView.** burnair rendert die Karte als Web-Karte
   im WebView und teilt sie mit der Browser-Version. **[APK-Indiz]**
   → *Übernehmen, wenn* schnelle Iteration & ein Karten-Code-Stand zählen.
   → *Meiden/abwägen, wenn* echte **Offline-Karten im Funkloch** Pflicht sind: burnair bündelt
   **keine** Map-Assets → ohne Netz keine Karte. Für ein autarkes Vario ggf. **native Offline-Tiles**
   (PMTiles/MBTiles) statt reiner WebView-Lösung.
2. **Offline-First für Tracks: Room/SQLite-Persistenz** (`burnair.db`). **[APK]**
   → Übernehmen — für ein Vario mit instabilem Mobilfunk essenziell; lokal puffern, später re-syncen.
3. **Statische Geodaten aus S3/Objekt-Storage** (`regtherm`-Bucket). **[APK]**
   → Übernehmen: vorgerechnete/seltener ändernde Daten (Thermik, Klimatologie) als statische,
   cache-bare Objekte serven statt teurer dynamischer Endpunkte.
4. **Native Sensorik-Bridge trotz Web-UI:** BLE/NMEA/XC-Tracer nativ, Karte im WebView,
   gekoppelt über **JS-Interface** (`LiveTracking*`-Callbacks). **[APK]**
   → Übernehmen als Muster: zeitkritische Sensorik nativ, Darstellung im Web-Layer, schmale Bridge.
5. **Schlanker, fokussierter Stack:** Kotlin + Coroutines/Serialization + OkHttp/Ktor + Room +
   Dagger + Firebase, **klassische Views** (kein Compose), **kein** schwergewichtiges Cross-Platform-
   Framework → base-APK nur **~7,6 MB**. **[APK]**
   → Übernehmen: bewusst klein/standardnah bauen; Framework-Gewicht vermeiden.
6. **KMP als mögliche Code-Sharing-Strategie** (`isHmppEnabled`, Ktor). **[APK-Indiz]**
   → Prüfen, falls iOS+Android+Backend Logik teilen sollen (Geo-/Tracking-Modelle).
7. **Push & Telemetrie out-of-the-box:** Firebase Cloud Messaging (Alarme/Updates) + Analytics. **[APK]**
   → Für Alerts (Wetter/Lee/Luftraum) übernehmbar — Datenschutz/Opt-in bedenken.
8. **Meiden:** burnairs **bewusster Verzicht auf Vario-Kopplung** (Phone-GPS genüge) ist für ein
   **dediziertes Hardware-Vario** (Kr-ken/AURA) die **Gegenstrategie** — *nicht* übernehmen: die
   Sensorik (BMP581, IMU) ist hier der Kernwert. **[Sekundär]**

---

## 5. Offene Fragen

1. **Web-Karten-Engine?** maplibre-gl vs. mapbox-gl vs. anderes — **unbestätigt** (Web-Bundles
   gesperrt). Aktuell nur **Vermutung** MapLibre.
2. **Web-Bundler/Framework** (react/vue/svelte + vite/webpack) — **unbestätigt**.
3. **Tile-Format & -Quelle** (MVT/PBF, swisstopo, PMTiles/MBTiles, Caching-Header) — **unbestätigt**.
4. **Rendert der WebView *die Karte* oder auch Hilfsseiten?** Starkes Indiz für Karte, aber wegen
   R8-Obfuskation **nicht 100 % bewiesen**.
5. **KMP-Umfang:** Wird Ktor/Logik tatsächlich mit iOS geteilt? `isHmppEnabled` ist nur ein Indiz.
6. **iOS-App (`burnair Go`/`burnair Map`)**: nicht analysiert (keine `.ipa`).
7. **Signatur-Echtheit** nicht kryptografisch bestätigt (kein `apksigner` im Container).

---

## Anhang A: So lässt sich Aufgabe A (Web) vervollständigen
In einer Umgebung mit Netzzugang zu `burnair.*`:
```bash
curl -sSL https://map.burnair.cloud/ -o burnair_index.html
grep -oE '(src|href)="[^"]+"' burnair_index.html | sort -u
grep -aoiE 'maplibre-gl|mapbox-gl|leaflet|openlayers|deck\.gl' bundle_*.js
grep -aoE 'sourceMappingURL=[^ ]+' bundle_*.js
grep -aoE 'https?://[^"]+\.(pbf|mvt|pmtiles|json)|\{z\}/\{x\}/\{y\}' bundle_*.js
curl -sSI https://map.burnair.cloud/ | grep -iE 'content-security-policy|cache-control|etag|service-worker'
```

## Anhang B: Reproduktion Aufgabe B
`scripts/analyze_burnair.sh` (im Repo) entpackt `.xapk`→`base.apk`, prüft Framework-Marker,
Libs, Hosts und Signatur. APK/`.xapk` nach `burnair_apk/` legen, dann `bash scripts/analyze_burnair.sh`.

---

### Quellen (öffentliche Sekundärquellen)
- burnair Map (Web v3) — `https://map.burnair.cloud/`
- Google Play `com.burnair.burnairmap` — `https://play.google.com/store/apps/details?id=com.burnair.burnairmap`
- App Store burnair Map / burnair Go — `id1495320175` / `id1666354109`
- burnair Help Center (Live Tracking / XC Tracer Bluetooth) — `https://help.burnair.cloud/`

> Die `burnair.*`-Links wurden **nicht** abgerufen (Netz-Policy `host_not_allowed`); Sekundär-Inhalte
> stammen aus Suchergebnis-Snippets. Alle mit **[APK]** markierten Fakten sind direkt am Artefakt verifiziert.
