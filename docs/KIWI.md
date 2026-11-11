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
* **Ressourcen:** ca. 7,5 KB statisch (Client) + 24 KB Jitterpuffer (1 s) während der Wiedergabe + zwei Tasks.
* Der AM-Träger/inet2RF (`irm_tx`) reagiert nur auf Radio-Sender und bleibt im Kiwi-Betrieb unverändert.

## Kconfig (`menuconfig` → *iRadioMini2 KiwiSDR*)
`IRM_KIWI_ENABLE`, `IRM_KIWI_DEFAULT_URI`, `IRM_KIWI_MAX_PRESETS`, `IRM_KIWI_USER_NAME`, `IRM_KIWI_ADPCM`, `IRM_KIWI_GAIN_DB` (Standard 6 dB),
`IRM_KIWI_BUFFER_MS` / `IRM_KIWI_PREBUFFER_MS`, `IRM_KIWI_CONNECT_TIMEOUT_MS`, `IRM_KIWI_MIN_RECONNECT_S`, Task-Prioritäten/-Core,
`IRM_KIWI_STATS_S` (Statistik-Logzeile), `IRM_KIWI_TUNE_STEP_HZ`, `IRM_KIWI_ROTARY_TUNES`.

## Tests (Host)
Siehe README, Abschnitt *Tests*: URI-Parser, Resampler/Drift-Regler, `irm_kiwi.c` gegen den Mock-KiwiSDR und der Zustandsautomat `irm_control.c`.
Live-Tests gegen mehrere KiwiSDR Server.
