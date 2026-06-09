# burnair_apk/ — APK-Ablageordner für die Stack-Analyse

Lege die zu analysierende(n) APK-Datei(en) **hier** ab (z. B. `burnair-map-1.16.7.apk`,
`burnair-go-x.y.z.apk`) und committe/pushe sie auf den Branch. Danach läuft die Analyse via:

```bash
bash scripts/analyze_burnair.sh
```

## Hinweise
- Nur legal beschaffte APKs (z. B. eigene Geräte-Extraktion, offizieller Store-Export).
- Das Skript liest **nur** Framework-Marker, Map-/Netzwerk-Libs und referenzierte **Hosts** aus.
- **Keine** Keys/Secrets/Query-Strings werden protokolliert (das Skript filtert auf reine Hosts).
- Große `.apk`-Dateien ggf. via Git LFS oder nur lokal halten (siehe `.gitignore`-Eintrag unten).

> Dieser Ordner ist absichtlich (fast) leer — er existiert nur als definierter Ablageort.
