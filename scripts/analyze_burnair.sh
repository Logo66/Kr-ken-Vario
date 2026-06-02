#!/usr/bin/env bash
# burnair Stack-Analyse — reproduzierbare A+B-Erhebung.
#
# Zweck: Tech-Benchmarking als Referenz fuer Flight Buddy KI. NUR beobachten/dokumentieren.
# Out of scope: Auth-Umgehen, Tile-/Daten-Abgriff, Keys/Secrets-Extraktion, Re-Hosting.
#
# Voraussetzungen:
#   - Aufgabe A (Web): Netzzugang zu burnair.* (sonst HTTP 403 host_not_allowed).
#   - Aufgabe B (APK): mind. eine *.apk unter ./burnair_apk/  +  `unzip` (optional `apktool`).
#
# Nutzung:
#   bash scripts/analyze_burnair.sh           # A + B
#   WEB=0 bash scripts/analyze_burnair.sh     # nur APK
#   APK=0 bash scripts/analyze_burnair.sh     # nur Web
#
# Ergebnisse landen unter ./burnair_analyse_out/

set -uo pipefail
OUT="burnair_analyse_out"
mkdir -p "$OUT"
WEB="${WEB:-1}"
APK="${APK:-1}"

log() { printf '\n=== %s ===\n' "$*"; }

# --------------------------------------------------------------------------
# AUFGABE A — Web-Map
# --------------------------------------------------------------------------
if [ "$WEB" = "1" ]; then
  log "AUFGABE A: Web-Map (burnair)"
  URLS="https://map.burnair.cloud/ https://www.burnair.ch/ https://burnair.cloud/"
  for u in $URLS; do
    name="$OUT/$(echo "$u" | sed 's#https\?://##; s#[/.]#_#g')"
    echo "[*] $u"
    # Header (Caching/Offline/CSP)
    curl -sSI --max-time 20 "$u" > "${name}.headers.txt" 2>&1
    grep -iE 'http/|content-security-policy|cache-control|etag|last-modified|service-worker|vary|content-type' "${name}.headers.txt" || true
    # Roh-HTML
    curl -sSL --max-time 30 "$u" -o "${name}.html" 2>/dev/null
    # Asset-Liste
    grep -oE '(src|href)="[^"]+"' "${name}.html" 2>/dev/null | sed 's/^\(src\|href\)="//; s/"$//' | sort -u > "${name}.assets.txt"
    echo "    Assets -> ${name}.assets.txt ($(wc -l < "${name}.assets.txt" 2>/dev/null || echo 0) Zeilen)"
  done

  # JS/CSS-Bundles ziehen und durchsuchen
  log "Bundles laden & Engine/Framework/Tiles greppen"
  : > "$OUT/_all_assets.txt"
  cat "$OUT"/*.assets.txt 2>/dev/null | sort -u > "$OUT/_all_assets.txt"
  while read -r a; do
    case "$a" in
      *.js|*.css|*.mjs)
        # Relative URLs gegen map.burnair.cloud aufloesen
        case "$a" in http*) full="$a";; /*) full="https://map.burnair.cloud$a";; *) full="https://map.burnair.cloud/$a";; esac
        fn="$OUT/bundle_$(echo "$full" | md5sum | cut -c1-10)_$(basename "$a" | cut -c1-40)"
        curl -sSL --max-time 30 "$full" -o "$fn" 2>/dev/null
        ;;
    esac
  done < "$OUT/_all_assets.txt"

  echo "--- Karten-Engine ---"
  grep -aoiE 'maplibre-gl|mapbox-gl|leaflet|openlayers|cesium|deck\.gl|\bthree\b' "$OUT"/bundle_* "$OUT"/*.html 2>/dev/null | sort | uniq -c | sort -rn
  echo "--- Web-Framework/Bundler ---"
  grep -aoiE '\breact\b|\bvue\b|\bangular\b|\bsvelte\b|webpack|vite|parcel|__vite|sourceMappingURL=[^ "]+' "$OUT"/bundle_* "$OUT"/*.html 2>/dev/null | sort | uniq -c | sort -rn | head -40
  echo "--- Tile-/Datenformate & Endpoints ---"
  grep -aoE 'https?://[a-zA-Z0-9._/-]+\.(pbf|mvt|pmtiles|mbtiles|json)|/tiles?/[a-zA-Z0-9._{}/-]+|\{z\}/\{x\}/\{y\}' "$OUT"/bundle_* "$OUT"/*.html 2>/dev/null | sort -u | head -60
fi

# --------------------------------------------------------------------------
# AUFGABE B — APK
# --------------------------------------------------------------------------
if [ "$APK" = "1" ]; then
  log "AUFGABE B: APK-Analyse"
  shopt -s nullglob
  apks=( burnair_apk/*.apk )
  if [ ${#apks[@]} -eq 0 ]; then
    echo "[i] Keine *.apk unter ./burnair_apk/ gefunden — Aufgabe B uebersprungen."
  fi
  for apk in "${apks[@]}"; do
    base="$(basename "$apk" .apk)"
    dir="$OUT/apk_$base"
    echo "[*] $apk -> $dir"
    rm -rf "$dir"; mkdir -p "$dir"
    unzip -qo "$apk" -d "$dir" 2>/dev/null

    echo "  --- Framework-Marker ---"
    [ -n "$(ls "$dir"/lib/*/libflutter.so 2>/dev/null)" ] && echo "    Flutter: libflutter.so gefunden"
    [ -n "$(ls "$dir"/lib/*/libapp.so 2>/dev/null)" ]     && echo "    Flutter: libapp.so gefunden"
    [ -f "$dir/assets/index.android.bundle" ]             && echo "    React Native: index.android.bundle"
    [ -n "$(ls "$dir"/lib/*/libhermes.so 2>/dev/null)" ]  && echo "    React Native: libhermes.so (Hermes)"
    [ -d "$dir/assets/www" ]                              && echo "    Capacitor/Cordova: assets/www/"
    [ -f "$dir/res/xml/config.xml" ]                      && echo "    Cordova: config.xml"
    ls "$dir"/lib/*/ 2>/dev/null | grep -i capacitor      && echo "    Capacitor-Lib"

    echo "  --- .so-Bibliotheken ---"
    ls "$dir"/lib/*/ 2>/dev/null | sort -u

    echo "  --- Map-/Netzwerk-Libs (in .so) ---"
    strings "$dir"/lib/*/*.so 2>/dev/null | grep -aoiE 'maplibre|mapbox|osmdroid|tangram|mapsforge|okhttp|retrofit|grpc' | sort | uniq -c | sort -rn | head -20

    echo "  --- Referenzierte Hosts (NUR Hosts, keine Keys/Query-Strings) ---"
    { strings "$dir"/lib/*/*.so 2>/dev/null; strings "$dir"/classes*.dex 2>/dev/null; } \
      | grep -aoE 'https?://[a-zA-Z0-9.-]+' \
      | sed -E 's#https?://##' \
      | grep -viE 'schemas\.android|w3\.org|apache\.org|googleapis\.com/auth|example\.com' \
      | sort -u | head -60
  done
fi

log "Fertig. Rohdaten unter ./$OUT/  — bitte Befunde in BURNAIR_STACK_ANALYSE.md eintragen."
