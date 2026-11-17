# Aufnahme (Radio, KiwiSDR)

Wie ein Kassettenrecorder: **REC** nimmt auf, was gerade läuft, **REC** nochmal (oder Pause/Stopp/Quellenwechsel) beendet. Ziel ist die SD-Karte.

## Bedienung

* Taster `IRM_BTN_REC` (menuconfig → Eingabe, Standard aus = -1): ein Druck = Start/Stopp.
* Web-UI: Taste „⏺ Aufnahme“ (nur sichtbar bei Radio/KiwiSDR und vorhandener SD), zeigt Dauer, Dateiname, MB; bei Fehler „Aufnahme beendet: Karte voll“ usw.
* REST: `POST /api/cmd` mit `rec_start`, `rec_stop`, `rec_toggle`; `/api/status` enthält `rec: {can,on,secs,kib,drop,file,end}`.
* Optionale LED (`IRM_REC_LED_GPIO`): Dauerlicht = nimmt auf, Blinken = bereit, wartet auf Ton.
* REC bei stehendem Band startet auch die Wiedergabe. Pause, Stopp und Quellenwechsel beenden die Aufnahme; Senderwechsel innerhalb von Radio bzw. Kiwi nicht (das Band läuft weiter, die Datei enthält beide Sender). Netzabbruch und automatisches Wiederverbinden unterbrechen die Datei nicht (die Lücke fehlt einfach).
* Dateien/Netzwerk: nicht aufnehmbar (Datei ist schon da, Netzwerk nimmt der PC auf).

## Datei

`/sdcard/recordings/2026-11-17_14-32-05.wav` (Ordner `IRM_REC_DIR`). Die Datei wird erst beim ersten Ton angelegt, bestehende Dateien werden nie überschrieben (gleiche Sekunde → `_2`, `_3`). Ohne gültige Uhr: `rec_0001.wav`, `rec_0002.wav` …
Die Uhr stellt SNTP nach dem WLAN-Login (neu: `irm_net`), Zeitzone `IRM_TIMEZONE` (POSIX-Format, Standard Mitteleuropa). Es gibt keine batteriegepufferte Uhr: nach dem Einschalten ohne WLAN bleibt sie ungestellt.

| Quelle | Format | Datenrate | Abgriff |
|---|---|---|---|
| Radio | 16 Bit Stereo, `IRM_AUDIO_OUT_RATE` (48 kHz) | 192 KB/s ≈ 11,5 MB/min | `irm_out_sink_t.write` (alle Ausgaben: Codec, PDM, DAC) |
| KiwiSDR | 16 Bit Mono 12 kHz | 24 KB/s ≈ 1,4 MB/min | Kiwi-Ausgabetask, roh vor Resampler/Verstärkung |

Die fertigen WAVs lassen sich über die Quelle „Dateien“ direkt wieder abspielen.

## Was genau aufgenommen wird (Abweichungen von einem „Line-Out-Mitschnitt“)

* Radio: Signal **vor** der Lautstärke (Codec: Hardwarelautstärke, PDM/DAC: Software-Gain im Sink), aber **nach** dem Equalizer und der Pegelreserve (`IRM_PRE_GAIN_DB`). Ein eingestellter EQ steckt also in der Aufnahme. Ein Abgriff vor dem EQ wäre ein eigenes GMF-Element – bei Bedarf.
* Das Radio wurde schon einmal dekodiert und ggf. auf die Ausgaberate gewandelt; eine Aufnahme im Originalformat (MP3-Stream bitgenau) ist hier bewusst nicht Ziel.
* KiwiSDR: ohne die +6 dB `IRM_KIWI_GAIN_DB` des Hörpfads, also leiser; am PC normalisieren.
* Kiwi-Samples kommen mit der Rate des Servers (nominell 12000, real ±einige Hz); der Kopf sagt 12000.

## Robustheit

* Audio-Pfad blockiert nie: Samples gehen in einen Ringpuffer (`IRM_REC_BUFFER_KB`, Vorgabe 64 KiB, PSRAM bevorzugt, nur während der Aufnahme belegt), ein eigener Task (Core 0, Prio 6) schreibt. Es werden nur ganze Blöcke verworfen, wenn der Puffer überläuft (Schreibpause der Karte); das erscheint im Log und im Web-UI („KiB verloren“); nie Links/Rechts-Versatz.
* WAV-Kopf und FAT werden alle `IRM_REC_SYNC_S` s (Vorgabe 20) gesichert: nach Stromausfall oder gezogener Karte ist die Datei bis auf die letzten Sekunden abspielbar.
* Neue Datei ab `IRM_REC_MAX_FILE_MB` (1024 MiB ≈ 90 min Stereo) ohne Lücke; harte Grenze FAT32/WAV 4 GiB.
* Stopp bei weniger als `IRM_REC_MIN_FREE_MB` frei, bei Schreibfehler oder Kartenentnahme.

## Erkenntnisse vom Testgerät (ESP32 mit PSRAM, SD per SDMMC, Radio mit WLAN)

* **Last:** Die Aufnahme kostet im Radiobetrieb etwa 10 Prozentpunkte Pipeline-Auslastung (MP3 192: 67 % → 77–79 %, MP3 128: 64–67 % → 69–75 %). Die größte Lücke im Ausgabepfad blieb mit 40–50 ms gleich. HE-AAC lag schon ohne Aufnahme bei 92–97 % und löste den Task-Watchdog aus (`sbr_dec`): AAC-Streams sind unabhängig von der Aufnahme an der Grenze des ESP32.
* **Stottern:** Mit 192 KiB Puffer im PSRAM war der Radioton schon beim Mithören gestört (auch in der Datei, die genau das enthält, was zum DAC geht); verworfen wurde dabei nichts. Ohne SD-Schreiben (`IRM_REC_DEBUG_NOWRITE`) war der Ton sauber, also hängt es am Schreiben auf die Karte. Mit 64 KiB (intern oder im PSRAM) ist das Stottern weg. Warum 192 KiB stören, ist nicht geklärt; die Vorgabe ist deshalb 64 KiB.
* **Puffer-Überlauf:** Mit 64 KiB lief der Puffer in über fünf Minuten Radio einmal kurz über (ca. 80 ms verworfen, im Log „VERWORFEN“). Verdacht: das periodische Sichern (WAV-Kopf + `fsync`); darum Vorgabe `IRM_REC_SYNC_S=20`. Bei weiterem Verwerfen: Wert erhöhen, Puffer auf 96 KiB oder eine andere Karte.
* **Absturz beim Beenden einer KiwiSDR-Aufnahme (behoben, Ursache offen):** Anfangs sporadisch `tlsf_free ... block already marked as free` beim Freigeben des Stream-Buffers, nur im Kiwi-Modus, nicht im Radiobetrieb. Der Heap war unmittelbar davor strukturell intakt (Prüfung OK); der Stack des Schreib-Tasks war es nicht (6,2 KB von 8 KB frei). Seit der Ring-Puffer einen statischen Kontrollblock nutzt (`xStreamBufferCreateStatic`, nur der Datenspeicher wird frei gegeben) trat der Fehler in den bisherigen Läufen nicht mehr auf. Das ist eine Umgehung; die eigentliche Ursache ist nicht gefunden. Bei erneutem Auftreten: Log mit `IRM_REC_HEAP_CHECK` und die Absturzzeile (Datenspeicher oder Kontrollblock) beobachten.

## Fehlersuche (Debug-Schalter in menuconfig, Menü „iRadioMini2: Aufnahme“)

* `IRM_REC_DEBUG_NOWRITE`: Abgriff und Puffer laufen, es wird aber nichts auf die Karte geschrieben. Trennt „Abgriff“ von „SD-Schreiben“.
* `IRM_REC_BUFFER_INTERNAL` (mit `IRM_REC_BUFFER_KB` 48–64): Aufnahmepuffer im internen RAM statt PSRAM. Das interne RAM ist knapp (192 KiB scheitern). Am Testgerät war das Stottern mit 64 KiB im PSRAM ebenfalls weg, der Schalter ist daher nicht nötig.
* `IRM_REC_HEAP_CHECK`: Heap-Prüfung vor Anlage/Freigabe des Puffers. Jede Prüfung sperrt den Heap 300–350 ms; die Pipeline meldet dabei „lieferte ... keine Daten“ – nur zum Diagnostizieren, für Hörtests ausschalten.
* Der Ringpuffer verwendet einen statischen Kontrollblock (`xStreamBufferCreateStatic`) und gibt nur den Datenspeicher frei (kein `...WithCaps`).
