# LVGL-Anzeige (`irm_disp_lvgl`)

Farbdisplay per SPI (ILI9341 oder ST7789) mit LVGL 9 und vier Skins. Ersetzt `displayd_lvgl*` und `lvgl_esp32_drivers` des Altprojekts.

## Aktivieren
`idf.py menuconfig` → *iRadioMini2: Anzeige* → **LVGL-Farbdisplay**, danach *iRadioMini2: LVGL-Display*:
Controller, Skin, Auflösung, Rotation (Swap/Mirror), Farbreihenfolge (BGR), Invertierung, SPI-Host/Takt und Pins
(Defaults kollidieren nicht mit den AudioKit-Defaults von Codec, SD und Tastern; bei eigener Hardware anpassen).
Der SPI-Takt über die GPIO-Matrix sollte ≤ 26 MHz bleiben (Default 20 MHz).

## Skins
| Skin | Altprojekt | Verhalten |
|---|---|---|
| `basic` | `displayd_lvgl` | Programmnummer, Name (Lauftext), Zustand, Tasten Zurück/Play-Pause/Weiter (per Touch) |
| `linear` | `displayd_lvgl_skale` | Zeiger gleitet über lineare Senderskala (80…310 px), Name erscheint nach der Fahrt |
| `round` | `displayd_lvgl_Mende148` | Zeiger dreht über Halbkreisskala (10°…190°) |
| `cassette` | `displayd_lvgl_cass_sim` | Zwei Spulen drehen sich, solange gespielt wird |

Layouts sind für **320×240** ausgelegt. Zeigerpositionen verteilen die Sender gleichmäßig von Anschlag zu Anschlag
(erster Sender links, letzter rechts; das Altprojekt endete wegen `index/count` vor dem rechten Anschlag).

## Änderungen gegenüber dem Altprojekt
* Tick, LVGL-Task, Locking, Puffer und Flush übernimmt `esp_lvgl_port`; keine eigene `lvgl_tick`-Task, kein Busy-Polling der Playerqueue.
* Zeiger/Spulen sind **LVGL-Animationen** (ease-in-out bzw. linear) statt blockierender `while`-Schleifen mit `vTaskDelay` im Display-Task.
* Bilder liegen als **RGB565/RGB565A8-Rohdaten im Flash** (`assets/*.bin`, zusammen ≈ 540 kB je nach Skin weniger; nur die Assets des gewählten Skins werden eingebettet).
  Die 2–4 MB großen C-Arrays des Altprojekts entfallen. Neu erzeugen/austauschen: `tools/png2lvgl.py name=bild.png …`
  (benötigt Pillow); Bildgrößen landen in `assets/assets_dims.h`.
* Eigener Skin: `irm_skin_t` (`create`/`update`) in `src/skin_<name>.c` implementieren, in `CMakeLists.txt` und Kconfig-Choice eintragen.

## Touch
Nicht Teil des Pakets (im Altprojekt nur über die `lvgl_esp32_drivers`-Menükonfiguration möglich). Einhängen: Touch-Panel-Handle
mit einem `esp_lcd_touch_*`-Treiber erzeugen und `lvgl_port_add_touch()` mit dem von `irm_disp_lvgl` erzeugten `lv_display_t` aufrufen
(`s_disp` in `irm_disp_lvgl.c`). Die Skins außer `basic` benötigen keine Eingabe.

## Grenzen
Standard-LVGL-Schrift (Montserrat 14) enthält keine Umlaute-/Sonderzeichen-Garantie für beliebige Sendernamen; bei Bedarf in
`menuconfig` (LVGL → Fonts) eine passende Schrift aktivieren bzw. mit der LVGL-Font-Konvertierung eine eigene erzeugen.
