# Migration iRadioMini → iRadioMini2

## Plattform
| Alt | Neu | Grund |
|---|---|---|
| ESP-IDF 4.x, Make/CMake gemischt | ESP-IDF 6.1, reines CMake, Komponenten + `idf_component.yml` | Makefile/`component.mk` entfallen |
| ESP-ADF 2.x (`audio_pipeline`, `audio_element`, `esp_peripherals`, `audio_hal`) | **ESP-GMF** (`gmf_core/gmf_audio/gmf_io`) + `esp_codec_dev` | ADF v2 ist für IDF 6 nicht nutzbar; ADF v3 basiert selbst auf GMF |
| `adf_patch/` (ES8388-Lautstärke) | `IRM_ES8388_OUT_VOL` in `irm_board` | Der Patch setzte alle vier Ausgangsstufen (Register 0x2E..0x31); `esp_codec_dev` lässt LOUT2/ROUT2 bei -30 dB, daher jetzt per Kconfig nachgezogen (Standard 0 dB) |
| Legacy-I²C (`driver/i2c.h`), Legacy-PCNT (`driver/pcnt.h`) | `driver/i2c_master.h`, `driver/pulse_cnt.h` | in IDF 6 entfernt bzw. EOL |
| `json`-Komponente | `espressif/cjson` | in IDF 6.0 aus dem Kern entfernt |
| u8g2 als Kopie im Repo, eigener I²C-HAL | u8g2 als Git-Dependency, neuer HAL (`irm_disp_ssd1306.c`) | kein Vendoring |
| i2cdev/hd44780/pcf8574 (esp-idf-lib, Legacy-I²C) | eigener kompakter HD44780-Treiber auf `i2c_master` | |

## Interner DAC / PDM (V1: `CONFIG_USE_INTERNAL_AUDIODAC`)
V1 schaltete per Kconfig auf `I2S_STREAM_TX_PDM_CFG_DEFAULT`. Jetzt: Kconfig *Audio-Ausgabe* = `PDM` oder `Interner DAC` (siehe README).
Das fertige GMF-Element `esp_gmf_io_i2s_pdm` wird bewusst **nicht** verwendet: sein `close()` wartet unbegrenzt auf ein Event, das nur bei
TX-Queue-Überlauf gesetzt wird (Hängerrisiko beim Senderwechsel). Stattdessen ein eigenes Ausgabe-Element mit Software-Lautstärke.
Der Legacy-I²S-„DAC built-in“-Modus existiert in IDF 6 nicht mehr; der interne DAC läuft über `dac_continuous`.

## Designänderungen V1 -> V2
* **Globale Nachrichtenstruktur + Pointer-Queues** (Race Conditions) → by-value-Kommandos und Status-Schnappschüsse.
* **In Headern definierte Globals**, Queues erst nach dem Start der Tasks angelegt → Initialisierungsreihenfolge in `app_main`.
* **Polling alle 500 ms ohne Flankenerkennung** → Entprellung + Flanken, kurz/lang, Drehgeber über PCNT-Hardware.
* **Zwei I²C-Busse auf denselben Pins** (Codec/Display) → ein gemeinsamer Bus (`irm_board_i2c()`).
* **TLS-Prüfung abgeschaltet** → Zertifikats-Bundle aktiv.
* **WLAN-Passwort im Log**, `strstr` ohne NULL-Prüfung, fehlende Terminierung beim Parsen → eigener Parser mit Tests; Passwort wird nie geloggt.
* **Playlist-Strings wurden beim Anzeigen verändert** (Terminierung in den Quelldaten) → Kopien mit Längenprüfung.
* **HTML per `strcat` zusammengesetzt, Steuerung per GET-Parameter** → statische UI + JSON-REST, Änderungen nur per POST, optionale Basic-Auth.
* **WLAN-Daten nur von SD** → NVS-Persistenz; SD-Karte nach dem ersten Start entbehrlich.
* **Lautstärke/Sender nirgends gespeichert** → NVS, verzögert geschrieben.
* **Kein Wiederverbinden bei Streamfehlern** → Backoff-Retry; WLAN-Reconnect mit Backoff.

## Verhaltensänderungen V1 -> V2
* **Equalizer:** Der ADF-Equalizer nutzte −13…+13 dB mit „−13“ als Grundstellung der Bibliothek. Jetzt: 10 Peak-Filter, **0 dB = neutral, ±12 dB**. Die alte `eqsettings.txt` wird nicht mehr gelesen (Werte hätten andere Bedeutung); Einstellungen liegen im NVS.
* **LVGL/RF:** Skins nutzen LVGL-Animationen und Flash-Rohbilder statt C-Arrays; inet2RF subtrahiert die ZF (statt sie zu addieren) und wählt per GOTO; Details in `docs/LVGL.md`, `docs/RF-TX.md`.
* **Web-UI/URLs:** alte `/?command=...`-Links existieren nicht mehr; stattdessen `/api/*` (siehe README).
* **Playlist:** `playlist.m3u` mit optionalen `#EXTINF`-Namen; Eintrags-/Längenlimits über Kconfig.
* **Board-Auswahl:** statt ADF-Boardliste: `ESP32-A1S AudioKit (ES8388)` oder eigenes Board mit Pins/Codec per Kconfig. Weitere Boards = weitere Kconfig-Defaults in `irm_board/Kconfig`.

## Portierung der Altmodule (Übersicht)
| Altmodul | Neu |
|---|---|
| `iRadioMini.c`, `globals`, `messages`, `utils` | `main/app_main.c`, `irm_core`, `irm_control` |
| `player.c` (inkl. `USE_INTERNAL_AUDIODAC` / PDM) | `irm_audio` (GMF) + `irm_control`; Ausgabe über `irm_out` (PDM, interner DAC) bzw. Codec |
| `sdcard.c` | `irm_board` (SD-Mount), `irm_storage`, `irm_formats` |
| `gpiod.c`, `gpiod_rotary.c` | `irm_input` (Taster, PCNT-Drehgeber) |
| `httpd.c` | `irm_web` (REST + UI) |
| `display/ssd1306OLED` (+ `u8g2_esp32_hal`, SPI-Variante) | `irm_disp_ssd1306` (I²C; die ungenutzte SPI-Variante `SSD1306_SPI.c` entfällt) |
| `display/HD44780_I2C` | `irm_disp_hd44780` |
| `display/servo` | `irm_disp_servo` |
| `display/lvgl`, `lvgl_skalensim`, `lvgl_Mende148`, `lvgl_cassettensim` | `irm_disp_lvgl` (Skins `basic`, `linear`, `round`, `cassette`), siehe `docs/LVGL.md` |
| `tx/transmitterd`, `tx/AD9832|AD9835|AD9850|SigGenESP32` | `irm_tx` (ein Treiber per Kconfig), siehe `docs/RF-TX.md` |
| `tx/inet2RFd` | `irm_tx` (inet2RF, PCNT neue API) |
| `components/i2cdev|hd44780|pcf8574|pcf8575|esp_idf_lib_helpers` | entfallen (Legacy-I²C), ersetzt durch `i2c_master` im Board und in den Backends |

