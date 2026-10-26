# iRadioMini2

Baukasten für ESP32-Internetradios auf **ESP-IDF 6.1** – Neuaufbau von iRadioMini (ESP-IDF 4.x / ESP-ADF 2.x https://github.com/radiolab81/iRadioMini).
Streaming (HTTP/HTTPS, MP3/AAC/…), Mediaplayer von SD-Karte, 10-Band-Equalizer, Web-UI/REST-API,
Taster und Drehgeber, austauschbare Anzeige-Backends.

## Schnellstart

```bash
. $IDF_PATH/export.sh            # ESP-IDF v6.1
idf.py set-target esp32
idf.py menuconfig                # Menü "iRadioMini2: ..." (Board, Pins, Display, WLAN-Fallback, Web)
idf.py build flash monitor
```

Abhängigkeiten (ESP-GMF, `esp_codec_dev`, `cjson`, bei Bedarf `u8g2` bzw. LVGL) lädt der Component Manager automatisch.
Display-spezifische Pakete werden nur geladen, wenn die Anzeige in `menuconfig` gewählt ist. Nach dem Wechsel der Anzeige normalerweise
einfach erneut bauen; falls das Paket fehlt: `idf.py reconfigure`. Die SSD1306-Anzeige bezieht `u8g2` als Git-Abhängigkeit,
daher muss `git` im `PATH` der Shell liegen, in der `idf.py` läuft.

Auf die SD-Karte (FAT) kopieren: `playlist.m3u` (siehe `examples/playlist.m3u`) und optional `wifi.txt`
(siehe `examples/wifi.txt`). Die WLAN-Daten werden beim ersten Start in den NVS übernommen.
Reihenfolge der Zugangsdaten: `wifi.txt` > NVS > Kconfig-Fallback.

Die Web-Oberfläche liegt auf `http://<IP des Radios>/` (IP erscheint im Log und auf dem Display).
Bei Auslieferung ist **keine** Authentifizierung aktiv: `IRM_WEB_PASSWORD` setzen (HTTP Basic, unverschlüsselt!).

## Komponenten

| Komponente | Aufgabe | Abhängigkeiten |
|---|---|---|
| `irm_formats` | Parser für M3U, `wifi.txt`, EQ-Datei – reines C, auf dem Host testbar | – |
| `irm_core` | Typen, Kommando-Queue, Status-Publish/Subscribe | esp_event |
| `irm_storage` | NVS-Einstellungen, Dateihilfen, SD-Medienscan | irm_core, irm_formats |
| `irm_board` | Audio-Ausgabe (Codec per `esp_codec_dev`, **PDM** oder **interner DAC**), I²C-Bus, SD-Mount | Kconfig-gesteuert |
| `irm_out` | Ausgabesenken PDM/interner DAC + PCM-Helfer (Software-Lautstärke, Downmix, 8-Bit-Dither) | esp_driver_i2s/dac |
| `irm_net` | WLAN-STA mit Reconnect-Backoff | irm_core |
| `irm_audio` | ESP-GMF-Pipeline (HTTP/Datei → Decoder → Resampler → EQ → Codec) | gmf_*, esp_codec_dev |
| `irm_control` | Zustandsautomat (einzige Task, die Audio steuert) | irm_audio, irm_storage |
| `irm_input` | Taster (entprellt, kurz/lang) und PCNT-Drehgeber | irm_core |
| `irm_display` + `irm_disp_*` | Anzeige-Dienst + Backends SSD1306 (u8g2), HD44780/PCF8574, Servo, **LVGL** (4 Skins) | irm_core |
| `irm_web` | REST-API + eingebettete Web-UI | esp_http_server, cjson |
| `irm_tx` | optionaler AM-Träger (AD9835/AD9832/AD9850/LEDC) + inet2RF-Senderwahl | irm_core |

Dokumentation: [Architektur](docs/ARCHITECTURE.md) · [Komponenten-Referenz](docs/COMPONENTS.md) · [LVGL-Anzeige](docs/LVGL.md) · [AM-Sender/inet2RF](docs/RF-TX.md) · [Migration vom Altprojekt](docs/MIGRATION.md) · 

## Audio-Ausgabe (auch ohne externen DAC)

`menuconfig` → *iRadioMini2: Board* → **Audio-Ausgabe**:

| Wahl | Hardware | Lautstärke | Hinweise |
|---|---|---|---|
| I²S-Codec/-DAC | ES8388, ES8311, PCM5102, MAX98357 … | Hardware (Codec) bzw. Software (generischer DAC) | Standard |
| **PDM** | ein GPIO (Default 25), I²S0 | Software | Mono; RC-Tiefpass (z. B. 1 kΩ + 10 nF) und Verstärker extern |
| **Interner DAC** | GPIO25 (links) / GPIO26 (rechts) | Software | 8 Bit mit Dither; Stereo oder Mono auf beiden Pins; Pegel ca. 0–3,3 V, Gleichanteil per Koppelkondensator entfernen |

Das entspricht `USE_INTERNAL_AUDIODAC` aus iRadioMini V1 (dort nur PDM) und ergänzt den DAC-Pfad. Beide Wege teilen sich dieselbe
GMF-Pipeline (Decoder → Resampler → Equalizer); nur das letzte Element unterscheidet sich. Nicht hardwaregetestet; die Qualität ist durch
8 Bit bzw. PDM-Auflösung begrenzt. Der interne DAC läuft über I²S0-DMA und schließt eine gleichzeitige I²S-Codec-Ausgabe aus.

## AudioKit (ES8388): Lautsprecher und Kopfhörer

Beide Ausgänge nutzen unterschiedliche ES8388-Ausgangspaare (LOUT1/ROUT1 bzw. LOUT2/ROUT2). Der Pegel aller vier Stufen ist über
`IRM_ES8388_OUT_VOL` einstellbar (30 = 0 dB, Maximum 33 = +4,5 dB; gegebenenfalls Übersteuern/Verzerren prüfen). Die Lautstärke selbst regelt
die digitale DAC-Lautstärke. Die Kopfhörer-Erkennung (GPIO39) schreibt bei jedem Stecken/Ziehen eine Zeile `Kopfhoerer-Erkennung: GPIO39=… `
ins Log; stimmt der Text mit der Realität überein, kann `IRM_HP_MUTE_SPEAKER` den Lautsprecher bei Kopfhörerbetrieb abschalten (sonst spielen beide).

## I²C-Displays (SSD1306, HD44780) und Pins

Wie im Altprojekt bekommen I²C-Displays einen **eigenen Bus auf I²C-Controller 1**; der Codec (ES8388/ES8311) bleibt auf Controller 0, damit sich beide
nicht in die Quere kommen. In `menuconfig` → *iRadioMini2: Board* einstellbar: Controller (`I2C Controller`), **SDA/SCL** (Standard GPIO21/22), optional
„Display am Codec-Bus“. Die Adresse steht unter *iRadioMini2: Anzeige* (SSD1306 0x3C, HD44780-Adapter 0x27 oder 0x3F). Alle Pins (Display, Taster,
Drehgeber, LCD, Servo, Sender …) sind frei wählbar; **Kollisionen mit Onboard-Funktionen des Boards prüft das Projekt nicht** – das liegt beim Nutzer.
Beim AudioKit sind nur GPIO21, 22, 19, 23, 18 und 5 herausgeführt (GPIO21 ist dort zugleich der Endstufen-Enable, 23/18/5/19 sind Taster-Pins).

## M4A/MP4-Dateien

AAC im M4A-Container wird vom Decoder nur gelesen, wenn das `moov`-Atom **vor** den Mediendaten (`mdat`) liegt. Dateien, die ffmpeg ohne
`-movflags +faststart` erzeugt hat, haben es am Ende; iRadioMini2 erkennt das beim Start des Titels und zieht `moov` beim Lesen virtuell nach vorn
(Logzeile `moov liegt hinter mdat: wird beim Lesen vorgezogen`). Dafür wird `moov` (typisch 100–500 KiB) kurz im RAM gehalten (max. 8 MiB). Auf dem
PC lässt sich eine Datei auch dauerhaft umwandeln: `ffmpeg -i in.m4a -c copy -movflags +faststart out.m4a`. Streams (`http://…`) sind nicht betroffen.

## REST-API

| Methode/Pfad | Beschreibung |
|---|---|
| `GET /api/status` | Zustand (Quelle, Player, Sender, Lautstärke, Format, EQ) |
| `GET /api/list` | Namen der aktiven Liste (Sender bzw. Dateien) |
| `POST /api/cmd` | `{"cmd":"next\|prev\|play\|pause\|toggle\|stop\|vol_up\|vol_down\|volume\|goto\|source\|toggle_source","value":n}` |
| `POST /api/eq` | `{"gains":[10 Werte in dB, -12..12],"save":true}` bzw. `{"action":"reset"\|"save"}` |

Zustandsändernde Aufrufe sind ausschließlich `POST` mit JSON-Body (kein Seiteneffekt durch einfache Links/Bilder fremder Seiten).

## Tests

```bash
cd components/irm_formats
gcc -Wall -Wextra -fsanitize=address,undefined -Iinclude src/*.c test/test_formats.c -o /tmp/tf && /tmp/tf
cd ../irm_formats
gcc -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_mp4.c test/test_mp4.c -o /tmp/tm && /tmp/tm
cd ../irm_out
gcc -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_pcm.c test/test_pcm.c -o /tmp/tp && /tmp/tp
```

## Status

* Verifiziert wurde: Build unter ESP-IDF v6.1 (Ziel ESP32) für die Konfigurationen
  SSD1306 / HD44780 / Servo / LVGL (alle vier Skins, ILI9341 und ST7789), alle Träger-Treiber und inet2RF sowie die Ausgaben Codec, PDM und interner DAC (Stereo/Mono); dazu die Host-Tests der Parser.
  Die Abhängigkeiten wurden dabei aus Component-Manager(`idf.py build` mit Internetzugang) eingebunden; Versionsbereiche siehe `idf_component.yml` der Komponenten.
* Ausgabe: I²S-Codec (ES8388, ES8311), generischer I²S-DAC, **PDM auf einem GPIO** oder **interner 8-Bit-DAC** (GPIO25/26) – siehe Abschnitt „Audio-Ausgabe“. 
* Der AM-Sender ist standardmäßig deaktiviert und unterliegt nationalen Funkvorschriften (`docs/RF-TX.md`).
