# Architektur

```
 Taster/Drehgeber ─┐                                   ┌─> irm_display ─> Backend (SSD1306 | HD44780 | Servo)
 Web (REST) ───────┼─> irm_cmd_post() ─> [Queue] ─> irm_control ──> irm_status_update() ─> esp_event (IRM_EVENT_STATUS)
 irm_net (WLAN) ───┘                                │  ▲                                  └─> weitere Abonnenten
                                                    ▼  │ Audio-Events (als Kommando)
                                                 irm_audio  ──>  ESP-GMF-Pipeline ──> esp_codec_dev ──> I²S
```

## Grundregeln

1. **Kommandos** sind Punkt-zu-Punkt: viele Erzeuger → eine FreeRTOS-Queue → genau ein Konsument (`irm_control`).
   Kommandos werden *by value* kopiert; es gibt keinen geteilten, veränderlichen Nachrichtenpuffer.
2. **Zustand** wird per Publish/Subscribe verteilt: `irm_status_update()` ändert den Status unter Mutex atomar und
   sendet einen Schnappschuss über eine eigene `esp_event`-Loop. Abonnenten (Display, später z. B. SSE/MQTT) blockieren nie die Steuerung.
3. **Eine Task steuert Audio.** Nur `irm_control` ruft `irm_audio_*` auf; Quellenwechsel, Retry und Fehlerbehandlung sind dort serialisiert.
4. **Hardwarenähe nur in `irm_board`, Backends und Treibern.** Pins/Codec/SD kommen aus Kconfig, nicht aus `#define`s im Code.
5. **Schichten:** `irm_formats` (rein) → `irm_core` → `irm_storage`/`irm_net`/`irm_audio` → `irm_control` → Peripherie/Web → `main` (nur Verdrahtung).
   Backends hängen von `irm_display` ab, nie umgekehrt; die Anwendung wählt das Backend.

## Zustandsautomat (Player)

`IDLE → CONNECTING → PLAYING`; Fehler/Stream-Ende im Radiobetrieb → `ERROR` + Wiederverbindung mit Backoff (1 s … `IRM_RETRY_MAX_S`);
im Mediaplayer → nächster Titel. „Pause“ trennt beim Live-Stream (Pausieren eines Streams ist sinnlos), im Mediaplayer wird die Pipeline pausiert.
Lautstärke/Sender werden verzögert (`IRM_PERSIST_DELAY_MS`) in den NVS geschrieben – schont den Flash.

## Audio (ESP-GMF)

Pro Wiedergabe wird die Pipeline neu aufgebaut: `io_http|io_file → aud_dec → aud_rate_cvt → aud_ch_cvt → aud_bit_cvt → aud_eq → io_codec_dev | io_irm_sink`.
Das Format erkennt der Decoder an den Stream-Headern (Radio-URLs haben oft keine Endung); Ausgangstyp ist MP3 (Container-Typ), ein „leerer“ Typ würde als Frame-Decoder direkt mit Typ 0 geöffnet und scheitern. Ausgabeformat fest (`IRM_AUDIO_OUT_RATE`, 2 ch, 16 bit),
Codec einmalig geöffnet; bei PDM/internem DAC übernimmt `io_irm_sink` (`irm_out`) die Ausgabe samt Software-Lautstärke. HTTPS prüft Zertifikate über das ESP-x509-Bundle. EQ: 10 Peak-Filter (31 Hz … 16 kHz, Q≈1,41), 0 dB = neutral, ±12 dB, live änderbar.

## Eigenes Anzeige-Backend

```c
static esp_err_t init(void);  static void render(const irm_status_t *st);  static void deinit(void);
const irm_display_backend_t my_backend = { "mein", init, render, deinit };
// in main: irm_display_start(&my_backend);
```
`render()` läuft in der Display-Task und sieht immer den neuesten Zustand (Mailbox der Länge 1, Zwischenzustände werden übersprungen).
Textaufbereitung: `irm_view_title/info/volume`. Ein LVGL-Backend lässt sich identisch anbinden (Display-Task = LVGL-Task).

## Host-Tests
`irm_formats` hat keinerlei IDF-Abhängigkeit und wird mit gcc/ASan getestet (siehe README).
