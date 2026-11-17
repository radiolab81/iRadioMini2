# iRadioMini2 – Aufnahme: Feinschliff nach Hardwaretest (2026-11-17)

* **Vorgaben:** `IRM_REC_BUFFER_KB` 64 (vorher 192: hörbares Stottern im Radiobetrieb), `IRM_REC_SYNC_S` 20 (vorher 5).
* **KiwiSDR-Absturz beim Aufnahmestopp:** Ring-Puffer jetzt mit statischem Kontrollblock (`xStreamBufferCreateStatic`) statt `...WithCaps`; in den Läufen danach kein Absturz mehr (Ursache nicht geklärt).
* **Diagnose:** Schalter `IRM_REC_DEBUG_NOWRITE`, `IRM_REC_BUFFER_INTERNAL`, `IRM_REC_HEAP_CHECK`; Stack des Schreib-Tasks einstellbar, Reserve wird beim Aufnahmeende geloggt. Befunde in `docs/RECORDING.md`.

---

# iRadioMini2 – Aufnahme (2026-11-17)

* **Aufnahme für Radio und KiwiSDR** wie beim Kassettenrecorder: Taster (`IRM_BTN_REC`), Web-UI-Taste oder `POST /api/cmd {"cmd":"rec_toggle"}`. Unkomprimiertes WAV (16 Bit) nach `/sdcard/recordings/JJJJ-MM-TT_HH-MM-SS.wav`, Details und Grenzen in `docs/RECORDING.md`.
* **Neue Komponente `irm_rec`:** hängt sich in die Ausgabesenke (Radio: Stereo, Ausgaberate) bzw. per `irm_kiwi_set_tap` in den Kiwi-Ausgabetask (12 kHz Mono, roh) ein; Ringpuffer + Schreib-Task, der Audio-Pfad blockiert nie. Datei entsteht beim ersten Ton, WAV-Kopf wird alle `IRM_REC_SYNC_S` s gesichert, Dateiwechsel bei `IRM_REC_MAX_FILE_MB`, Stopp bei fast voller Karte. Optionale Aufnahme-LED.
* **Uhr:** `irm_net` startet jetzt SNTP und setzt die Zeitzone (`IRM_TIMEZONE`, `IRM_SNTP_SERVER`). Ohne Uhr heißen Aufnahmen `rec_0001.wav` …
* **Steuerung:** `IRM_CMD_REC_START/STOP/TOGGLE`, Status `rec`/`rec_secs`. REC bei stehendem Band startet auch die Wiedergabe; Pause/Stopp und Quellenwechsel beenden die Aufnahme, Senderwechsel nicht.
* **Tests:** echtes `irm_rec.c` auf dem Host (bit-genauer Inhalt, Header, Dateiwechsel, Karte voll), `irm_control` (Bandlogik), WAV-Helfer, Web-UI. – siehe `docs/RECORDING.md`.

---

# iRadioMini2 – Netzwerk-Eingang (2026-11-15)

Neu gegenüber dem Dateibrowser-Release:
* **Vierte Quelle „Netzwerk“:** Ein PC im selben Subnetz schickt rohes PCM (s16le, stereo, Standard 48 kHz) per `nc`/UDP/TCP an Port 4010; das Radio gibt es über die vorhandene Ausgabe aus (`docs/NETIN.md`). Port nur in dieser Quelle offen, nur Absender aus dem eigenen Subnetz, immer nur einer gleichzeitig, **kein Passwort**.
* **Komponente `irm_netin`** (Engine nach dem Muster von `irm_kiwi`, `Kconfig`: Port, Rate, Puffer, Vorpufferung, Absender-Timeout, Prioritäten); Zulassungsregeln `irm_netin_policy` (rein C).
* **`irm_out`:** Stereo-Resampler `irm_resamp_st_*` mit Driftregelung (±1000 ppm), Abwärtsabtastung mit Tiefpass (z. B. 48 → 44,1 kHz).
* **Steuerung/Anzeige/Web:** `IRM_SOURCE_NET` (4 Quellen; die Quellentaste schaltet jetzt Radio → Dateien → KiwiSDR → Netzwerk → Radio, abschaltbar mit `IRM_NETIN_ENABLE=n`), Status `net_peer`/`net_underruns`, Display-Titel „Warte auf Sender“/Absender-IP, Web-UI mit Status und fertigen Sender-Befehlen, `/api/status` mit Block `net`.
* **Tests:** Engine gegen echte Sockets (UDP/TCP, krumme TCP-Stücke, Rückstau, zweiter Absender 127.0.0.2, fremdes Subnetz, 44,1-kHz-Senke), Policy, Stereo-Resampler, `irm_control` (Quellenzyklus, Retry, Nachzügler), Web-UI in Chromium. Dabei gefundener und behobener Fehler: beim Absenderwechsel gingen bis zu 400 ms der ersten Daten verloren.

---

# iRadioMini2 – Dateibrowser (2026-11-14)

Neu gegenüber dem Zwischenrelease:
* **Dateibrowser im Web-UI (Quelle Dateien):** Ordner und Unterordner anzeigen, hinein- und hinausgehen (Breadcrumb, „‥“), Datei antippen = abspielen, laufende Datei hervorgehoben, seitenweise Anzeige (‹ ›). Die Wiedergabeliste besteht aus den Dateien des gewählten Ordners (alphabetisch); Weiter/Zurück und Titelende folgen darin (auch an Tasten/Display). Button **Alle Titel** = bisherige rekursive Liste der ganzen SD-Karte (bleibt die Liste beim Wechsel zur Quelle Dateien).
* **REST:** `GET /api/files?path=&after=&limit=`, `POST /api/files/play {"path"}`, neues Kommando `media_all`; `/api/status` liefert in der Quelle Dateien zusätzlich `path`.
* **Firmware:** neue Komponente-Bausteine `irm_dirlist` (irm_formats, rein C), `irm_control_browse()`/`irm_control_play_path()`, Kommandos `IRM_CMD_MEDIA_ALL`/`IRM_CMD_MEDIA_PLAY_PATH`. `irm_web` benötigt zusätzlich `irm_board` (SD-Mountpunkt).
* **Sicherheit:** Pfade aus dem Netz werden normalisiert und geprüft (`..`, `\`, Steuerzeichen, Überlänge abgelehnt), nur Dateien mit Endungen aus `IRM_MEDIA_EXTENSIONS`; Namen in der Oberfläche nur als Text (kein XSS).
* **Speicher:** Listing mit Cursor statt Vollscan – Heapbedarf nur O(Seitengröße) (≈4 KB je Seite), Handler-Stack unverändert.
* **Grenzen:** Ordner mit mehr als `IRM_MAX_MEDIA_FILES` Dateien: Datei jenseits dieser Grenze spielt allein (ohne Weiter/Zurück im Ordner). Namen ≥ 100 Bytes und versteckte Einträge werden nicht angezeigt. FAT ohne LFN zeigt 8.3-Namen (`CONFIG_FATFS_LFN_*` prüfen).
* **Tests:** `irm_formats/test/test_dirlist.c` (Pfade, Cursor-Paging, JSON), `irm_control` Host-Test um Dateibrowser-Abschnitte erweitert (jetzt mit `-DIRM_SD_MOUNT_POINT`, siehe README), UI-Test `test_ui_files.js` (jsdom) / `test_ui_files_pw.js` (Chromium).

---

# iRadioMini2 – Zwischenrelease (2026-11-14)

Änderungen gegenüber v2:
* **Web-UI:** Der Bereich „KiwiSDR-Server suchen“ wird nur noch im Modus *KiwiSDR* angezeigt (Radio und Dateien: verborgen).
  Der Modus KiwiSDR ist nur wählbar, wenn ein Eintrag existiert → `kiwi.m3u` auf der SD-Karte (`examples/kiwi.m3u`) oder `IRM_KIWI_DEFAULT_URI` in `menuconfig`; siehe README („Schnellstart“).
* **Test:** `components/irm_web/test/ui/test_ui.js` an das neue Verhalten angepasst.
* Keine Änderungen an Firmware (C-Code), Kconfig oder REST-API.

---

# iRadioMini2 v2

Neuaufbau von iRadioMini für **ESP-IDF 6.1** (Ziel: ESP32).

## Highlights
* Komponentenarchitektur (`irm_*`), Kommando-Queue + Status-Pub/Sub, eine Steuer-Task.
* Audio: ESP-GMF + esp_codec_dev, 10-Band-EQ, HTTPS mit Zertifikatsprüfung.
* Anzeigen: SSD1306, HD44780, Servo, LVGL (Skins basic/linear/round/cassette).
* **KiwiSDR als dritte Quelle** (`irm_kiwi`, Client `kiwi_core`): Abstimmen per Web/REST/Drehgeber, Presets in `kiwi.m3u`, siehe `docs/KIWI.md`.
* **KiwiSDR-Serververzeichnis** (`irm_dir`, `kiwi_dir`): Liste im Flash (neue Partition `kiwidb`), im Web-UI laden/suchen/„Hören“/Favorit, siehe `docs/KIWI.md`. Neue REST-Endpunkte `/api/dir*`; HTTP-Server mit 16 statt 8 URI-Handlern.
* **Fix (Original):** `irm_out_dac.c` wurde auch bei Codec-/PDM-Ausgang kompiliert, `CONFIG_IRM_DAC_NS_ORDER` existiert dort aber nicht → Fallback-Wert, Build läuft wieder durch.
* Optional: AM-Träger (AD9835/AD9832/AD9850/LEDC) und inet2RF.
* Audio-Ausgabe auch ohne externen DAC: PDM (GPIO) und interner ESP32-DAC.
* Web-UI + REST-API, NVS-Persistenz, Reconnect-Backoff, Host-Tests der Parser.

## Verifikationsstand
KiwiSDR-Erweiterung: Live-Tests (URI-Parser, Resampler/Drift-Regelung, `irm_kiwi.c`, `irm_control.c` ); unter ESP-IDF gebaut und auf echter Hardware erprobt.

Build unter ESP-IDF v6.1 (ESP32) für alle Anzeige-/Treiber-Varianten mit Component-Manager-Auflösung mit Registry, reale Hardware.

