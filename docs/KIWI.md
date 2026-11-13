# KiwiSDR als dritte Quelle (`irm_kiwi`)

Neben Internetradio und Mediaplayer lässt sich ein [KiwiSDR](http://kiwisdr.com)-Empfänger im Netz anhören. Die Taste
„Quelle wechseln“ (bzw. `toggle_source`) schaltet reihum **Radio → Medien → KiwiSDR → Radio**; nicht verfügbare Quellen
(keine SD-Karte, leere `kiwi.m3u`) werden übersprungen.

## Einrichtung
1. `examples/kiwi.m3u` auf die SD-Karte kopieren (`/kiwi.m3u`) und eigene Server eintragen – *die Beispielnamen sind Platzhalter*.
   Ohne SD-Karte kann in `menuconfig` → *iRadioMini2 KiwiSDR* ein einzelner Vorgabe-Eintrag (`IRM_KIWI_DEFAULT_URI`) gesetzt werden.
2. Eintragsformat (Frequenz in **kHz**, wie in der Adresszeile des Kiwi-Browsers):

| Schreibweise | Bedeutung |
|---|---|
| `kiwi://host[:port]/?f=7055.5usb` | Frequenz + Modus; Standardport 8073 |
| `kiwi://host/?f=7055.5&mode=usb` | Modus getrennt |
| `…&pw=geheim` | Passwort (`%XX` wird dekodiert) |
| `http://host:8073/?f=7055.5usbz8` | aus dem Browser kopiert (Zoom-Angabe wird ignoriert); ohne Port gilt hier 80 |

Modi (Mono): `am amn amw sam sal sau lsb lsn usb usn cw cwn nbfm nnfm`. Stereo-Modi (`iq`, `drm`, `qam`) werden nicht unterstützt.
Ungültige Zeilen werden beim Start im Log gemeldet und übersprungen.

## Bedienung
* **Next/Prev** (Tasten, Web, Display-Backends) wechselt den Eintrag, der zuletzt gehörte Eintrag wird gemerkt.
* **Abstimmen:** Web-UI (Frequenzfeld, ±100 Hz/1 kHz/5 kHz, Modus-Auswahl), REST oder – bei Drehgeber-Funktion „Sender/Titel“ –
  der Drehgeber (`IRM_KIWI_ROTARY_TUNES`, Schritt `IRM_KIWI_TUNE_STEP_HZ`). Pause/Fortsetzen und automatisches Wiederverbinden behalten die
  zuletzt eingestellte Frequenz; ein Eintragswechsel lädt Frequenz/Modus neu aus der Liste.
* **Anzeige:** statt Abtast-/Bitrate erscheint `PLAY 7055.5 USB -73dBm` (Frequenz kHz, Modus, Feldstärke, höchstens 1×/s aktualisiert).
* **REST:** `{"cmd":"kiwi_tune","value":7055500}` (Hz), `{"cmd":"kiwi_step","value":-1000}`, `{"cmd":"kiwi_mode","mode":"usb"}` (oder `"value":<Index>`;
  ohne Angabe: nächster Modus). `GET /api/status` enthält in der Kiwi-Quelle `kiwi: {freq_hz, mode, rssi_dbm, modes[]}`.

## Serververzeichnis (Suchen und Auswählen im Web-UI)
Statt Server per Hand in `kiwi.m3u` einzutragen, lässt sich die Liste der öffentlichen KiwiSDR-Server laden, durchsuchen und per Klick anhören.
Die Datenbank liegt kompakt (ca. 120 Byte je Server) in der Flash-Partition `kiwidb`.

1. **Einmalig:** `partitions.csv` enthält die Zeile `kiwidb` (2 × 192 KB bei `0x310000`) – neu flashen. Ohne Partition bleibt das Verzeichnis aus
   (das Web-UI sagt es; Radio, Medien und Kiwi-Presets funktionieren weiter).
2. Im Web-UI **„KiwiSDR-Server suchen“ → „Liste aktualisieren“**: lädt `IRM_DIR_URL` (ca. 0,9 MB per HTTP) im Hintergrund und schreibt die Datenbank
   in den inaktiven Slot; erst danach wird umgeschaltet. Schlägt das Update fehl (Netz, Format, Flash), bleibt die alte Liste unverändert gültig.
   Mindestabstand zwischen Updates: `IRM_DIR_MIN_INTERVAL_S` (die Oberfläche fragt, ob man trotzdem laden will). Während der kurzen Löschphase
   des Flashs sind Tonaussetzer möglich.
3. **Suchen:** Text (Name/Ort/Host/Land), Land, Mindest-SNR, nur freie Kanäle, „deckt die aktuelle Frequenz ab“, Sortierung nach Liste oder bester SNR.
4. **„Hören“** wählt den Server und schaltet auf KiwiSDR. Lief KiwiSDR bereits, bleiben **Frequenz und Modus** erhalten (gleiche Frequenz, anderer Empfänger);
   sonst gilt `IRM_DIR_DEFAULT_FREQ_KHZ`/`IRM_DIR_DEFAULT_MODE`. Liegt die Frequenz außerhalb des gemeldeten Empfangsbereichs des Servers, wird sie an dessen Rand gezogen.
5. Der gewählte Server ist ein **Direkteintrag** hinter den Favoriten (Next/Prev laufen auch dorthin); der nächste „Hören“-Klick ersetzt ihn.
   **„★ Favorit“** schreibt Server + eingestellte Frequenz/Modus in `/kiwi.m3u` (SD-Karte nötig; doppelte Einträge werden nicht angelegt).

REST (alle mit Basic Auth, falls gesetzt):

| Aufruf | Zweck |
|---|---|
| `GET /api/dir` | `{available, valid, count, generation, state: idle/updating/ok/failed, bytes, servers, err, msg, retry_in_s}` |
| `GET /api/dir/list?text=&country=&snr=&free=1&freq=<Hz>&sort=list\|snr&offset=&limit=` | Treffer (`limit` ≤ 40): `{total, offset, limit, items:[{i, name, place, country, host, port, users, max, snr, lo, hi}]}` |
| `GET /api/dir/countries` | `[{id, name, n}]`, alphabetisch |
| `POST /api/dir/update {"force":true}` | Update starten: `202` ok · `409` läuft · `429` zu früh (`retry_in_s`) · `501` keine Partition · `503` kein Speicher |
| `POST /api/dir/connect {"host":"…","port":8073}` | Server aus dem Verzeichnis hören (Host/Port statt Index: bleibt nach einem Update gültig; `404` wenn nicht in der Liste) |
| `POST /api/cmd {"cmd":"kiwi_fav"}` | aktuellen Eintrag als Favorit speichern |

Hinweise: Namen/Orte stammen vom Verzeichnisdienst (fremde Daten) – das JSON wird zu gültigem UTF-8 bereinigt, die Oberfläche gibt sie nur als Text aus.
Das Verzeichnis kennt nur Server, die **ohne TLS und ohne Passwort** erreichbar sind (offline/https/private werden nicht angeboten). Der Dienst hinter `IRM_DIR_URL`
ist eine externe Datenquelle; ändert sich dessen Format, schlägt das Update mit einer Fehlermeldung fehl, ohne die alte Liste anzutasten.

## Technik
```
kiwi_core (WebSocket, 12 kHz Mono PCM/ADPCM)  --Netz-Task-->  Jitterpuffer (1 s)  --Ausgabe-Task-->  Resampler  -->  irm_out_sink_t
```
* `components/kiwi_core` ist der **unveränderte** portable Client aus dem Projekt ESP32_KiwiSDR; `irm_kiwi` ist der Anschluss an
  iRadioMini2 (Senke, Status, Kommandos). Der Kern kann dadurch unabhängig weiterentwickelt/ersetzt werden.
* **Ausgabe über dieselbe Senke** wie Radio/Medien (Codec, PDM, interner DAC) – Lautstärke/Stumm wirken wie gewohnt. Beide Engines teilen
  sich die Senke exklusiv; `irm_control` stoppt immer zuerst die jeweils andere. Der EQ gilt **nicht** für KiwiSDR.
* **Resampler** (`irm_out/irm_resamp`): 24-Tap-Polyphasen-Sinc, Durchlassbereich ≈ 0,41 × 12 kHz (≈ 4,9 kHz = Kiwi-AM-Bandbreite),
  Spiegelanteile > 60 dB unterdrückt. Quellrate wird als gemessene Kiwi-Rate (z. B. 11998,895 Hz) in Milli-Hz verwendet.
* **Drift-Regelung:** Kiwi-Takt und I²S-Takt laufen nie gleich. Ein P-Regler führt den Pufferfüllstand über ±1000 ppm Verhältniskorrektur
  (≤ 1,7 Cent, unhörbar) auf den Sollwert; ohne ihn liefe der Puffer nach Minuten leer.
* **Fehler/Wiederverbinden:** `irm_kiwi` meldet nur das Ende der Sitzung; Wiederholen mit Backoff übernimmt `irm_control` wie beim Radio.
  Zwischen zwei Verbindungsversuchen liegen mindestens `IRM_KIWI_MIN_RECONNECT_S` Sekunden (schont öffentliche Server).
* **Grenzen:** nur `ws://` (kein TLS); IPv6-Literale nicht; ein hängender Verbindungsaufbau (bis `IRM_KIWI_CONNECT_TIMEOUT_MS`) verzögert das
  Umschalten auf einen anderen Eintrag; öffentliche Kiwis begrenzen Sitzungsdauer/Nutzerzahl (der Server trennt dann).
* **Ressourcen:** ca. 7,5 KB statisch (Client) + 24 KB Jitterpuffer (1 s) während der Wiedergabe + zwei Tasks. Das Verzeichnis-Update braucht kurzzeitig ca. 9 KB Heap und einen Task (8 KB Stack, Priorität 3); im Ruhezustand kostet es nur die Datenbank-Metadaten.
* Der AM-Träger/inet2RF (`irm_tx`) reagiert nur auf Radio-Sender und bleibt im Kiwi-Betrieb unverändert.

## Kconfig (`menuconfig` → *iRadioMini2 KiwiSDR*)
`IRM_KIWI_ENABLE`, `IRM_KIWI_DEFAULT_URI`, `IRM_KIWI_MAX_PRESETS`, `IRM_KIWI_USER_NAME`, `IRM_KIWI_ADPCM`, `IRM_KIWI_GAIN_DB` (Standard 6 dB),
`IRM_KIWI_BUFFER_MS` / `IRM_KIWI_PREBUFFER_MS`, `IRM_KIWI_CONNECT_TIMEOUT_MS`, `IRM_KIWI_MIN_RECONNECT_S`, Task-Prioritäten/-Core,
`IRM_KIWI_STATS_S` (Statistik-Logzeile), `IRM_KIWI_TUNE_STEP_HZ`, `IRM_KIWI_ROTARY_TUNES`; Verzeichnis (*iRadioMini2 Serververzeichnis*): `IRM_DIR_ENABLE`, `IRM_DIR_URL`, `IRM_DIR_MIN_INTERVAL_S`, `IRM_DIR_DEFAULT_FREQ_KHZ`, `IRM_DIR_DEFAULT_MODE`.

## Tests (Host)
Siehe README, Abschnitt *Tests*: URI-Parser, Resampler/Drift-Regler, `irm_kiwi.c` gegen den Mock-KiwiSDR und der Zustandsautomat `irm_control.c`.
Live-Tests gegen mehrere KiwiSDR Server.
