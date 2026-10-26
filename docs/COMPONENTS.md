# Komponenten-Referenz

Alle Komponenten liegen in `components/`. Konfiguration: `idf.py menuconfig` → Menüs *iRadioMini2: …*.

## irm_formats  (rein C, host-testbar)
* `irm_playlist_*` – Liste aus M3U/Text: `new/free/count/uri/name/add/parse_m3u` (BOM, CRLF, `#EXTINF`-Name, Längenprüfung).
* `irm_wifi_conf_parse()` – wpa_supplicant-ähnliche `wifi.txt` (exakte Schlüssel, offene Netze via `key_mgmt=NONE`).
* `irm_eqfile_parse/format()` – Zeilenformat für EQ-Werte.
* `irm_mp4_*` – MP4/M4A-Atom-Scan, Offset-Korrektur (`stco`/`co64`) und virtuelles „faststart“-Lesen (`irm_mp4_fs_*`).
Test: `components/irm_formats/test/test_formats.c`.

## irm_core
Typen (`irm_status_t`, `irm_eq_t`, `irm_source_t`, `irm_player_state_t`), Kommando-Queue (`irm_cmd_post*`, `irm_cmd_receive`),
Status-Pub/Sub (`irm_status_update/get/subscribe`, Event-Base `IRM_EVENT`). Kommandoliste: `irm_cmd_id_t`.

## irm_storage
`irm_settings_*` (NVS: Lautstärke, Sender, EQ, WLAN) und `irm_files_*` (Datei lesen/schreiben, Playlist laden, SD-Medienscan).

## irm_out
Ausgabesenken für Boards ohne externen DAC: `irm_out_pdm_create()` (I²S0-PDM, Mono) und `irm_out_dac_create()` (interner 8-Bit-DAC, Stereo/Mono),
`irm_out_set_volume()`. Reine Helfer in `irm_pcm.h` (Gain-Kennlinie, Skalierung, Mono-Downmix, 16→8 Bit mit TPDF-Dither; Host-Test `test/test_pcm.c`).

## irm_board
`irm_board_init()` (je nach Kconfig Codec per `esp_codec_dev` mit I²C+I²S, PDM- oder DAC-Senke; `irm_board_codec()`/`irm_board_sink()`), `irm_board_mount_sd()` (SDMMC 1-Bit oder SPI), Zugriff `irm_board_i2c()/codec()`.
Kconfig: Board (AudioKit/eigenes), Codec (ES8388/ES8311/Dummy), Pins, Ausgaberate.

## irm_net
`irm_net_start(ssid, psk)`: WLAN-STA, Hostname, Reconnect mit Backoff, meldet `net_up/ip` in den Status und `IRM_CMD_INTERNAL_NET_CHANGED` an die Steuerung.

## irm_audio
`irm_audio_init(codec, sink, …)/play/stop/pause/resume/set_volume/set_eq` auf Basis von ESP-GMF. Ausgabe: Codec-IO oder eigenes Sink-IO (`irm_io_sink`). Ereignisse (`IRM_AUDIO_EVT_*`) per Callback.

## irm_control
Zustandsautomat/Controller (einzige Task, die Audio steuert): Quellenwahl (Radio/Medien), Playlist-Navigation, Retry-Backoff,
verzögertes Persistieren; `irm_control_item_count/irm_control_item` für Listenzugriff (z. B. Web).

## irm_input
Taster (Play/Pause kurz, Quellenwechsel lang; Weiter; Zurück; Lauter/Leiser mit Wiederholung) und PCNT-Drehgeber (Lautstärke oder Sender).

## irm_display + Backends
`irm_display_start(&backend)`; Backends: `irm_disp_ssd1306` (u8g2), `irm_disp_hd44780` (PCF8574), `irm_disp_servo` (LEDC),
`irm_disp_lvgl` (siehe `docs/LVGL.md`). Textaufbereitung `irm_view_*`.

## irm_web
REST-API + Web-UI (siehe README). Optional HTTP-Basic-Auth (`IRM_WEB_PASSWORD`).

## irm_tx
AM-Träger (AD9835/AD9832/AD9850/LEDC) und inet2RF (siehe `docs/RF-TX.md`).
