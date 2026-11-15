# Netzwerk-Eingang (Quelle „Netzwerk“)

Das Radio kann als **Empfänger für Systemaudio** dienen: Ein PC im selben Subnetz schickt rohes PCM, das Radio gibt es über die vorhandene Ausgabe (I²S/Codec/PDM/DAC) aus.
Umgesetzt in `components/irm_netin` (Engine, Stil wie `irm_kiwi`), Umrechnung in `irm_out` (`irm_resamp_st_*`), Zulassungsregeln in `irm_netin_policy.c` (rein C).

## Format und Ports
* rohes **PCM, signed 16 Bit little endian, stereo, ohne Header**, Abtastrate `IRM_NETIN_RATE` (Standard **48000 Hz**)
* Port `IRM_NETIN_PORT` (Standard **4010**), **UDP und TCP** gleichzeitig
* Weicht die Rate von der Ausgaberate (`IRM_AUDIO_OUT_RATE`) ab, wird umgerechnet. Bei Abwärtsabtastung (z. B. 48 → 44,1 kHz) mit Tiefpass; Anteile knapp über der halben Ausgaberate werden nur teilweise gedämpft (gemessen: 23,5 kHz-Ton ca. −14 dB).

## Sender (Linux, PulseAudio/PipeWire)
TCP (robust, bremst den Sender bei vollem Puffer):
```bash
parec --format=s16le --rate=48000 --channels=2 -d @DEFAULT_MONITOR@ | nc <IP des Radios> 4010
```
UDP (Datagramme müssen klein bleiben; `dd` schneidet auf 1440 Byte = 360 Frames, sonst werden die Pakete fragmentiert und je nach lwIP-Konfiguration verworfen):
```bash
parec --format=s16le --rate=48000 --channels=2 --latency-msec=20 -d @DEFAULT_MONITOR@ \
  | dd bs=1440 iflag=fullblock status=none | nc -u <IP des Radios> 4010
```
Das Web-UI zeigt in der Quelle *Netzwerk* Status (Absender, Aussetzer) und diese Befehle mit der richtigen IP an. Andere Programme gehen ebenso, z. B. `ffmpeg … -f s16le -ar 48000 -ac 2 tcp://<IP>:4010`.

## Verhalten
* **Port nur in der Quelle Netzwerk offen** (zu bei Radio/Dateien/KiwiSDR, bei Pause und Stop). Beim Wechsel in die Quelle sind Ports und Ausgabe sofort bereit, ein Absender ist nicht nötig.
* **Nur Absender aus dem Subnetz** der WLAN-Schnittstelle (IP/Netzmaske des Geräts); Netz-/Broadcast-/Multicast-Adressen werden abgelehnt.
* **Immer nur ein Absender:** wer zuerst Daten schickt, hält die „Miete“, bis er `IRM_NETIN_SENDER_TIMEOUT_MS` (Standard 3 s) nichts mehr sendet (bei TCP sofort mit dem Schließen der Verbindung). Andere werden in der Zeit ignoriert (UDP) bzw. sofort getrennt (TCP). Bei einem Absenderwechsel wird nur das noch gepufferte Material des alten verworfen.
* **Kein Passwort.** Solange die Quelle aktiv ist, kann jedes Gerät im Subnetz Ton einspielen. Die Lautstärke bleibt über den Regler des Radios begrenzbar.
* **Verzögerung** ≈ Vorpufferung `IRM_NETIN_PREBUFFER_MS` (Standard 150 ms) + Ausgabepuffer + WLAN. Für Video mit Lippensynchronität ggf. im Player ausgleichen.
* **Taktdrift** zwischen PC und Ausgabe-I²S wird über den Puffer-Füllstand mit ±1000 ppm (Resampler) ausgeregelt, ohne Knackser; die Tonhöhe verschiebt sich dabei um höchstens 0,1 %.
* **Pufferüberlauf:** TCP wird dann nicht mehr gelesen (Rückstau), UDP-Pakete werden verworfen (Zähler `overrun_bytes`). **Unterlauf:** kurze Stille, dann neu vorgepuffert (Anzeige „Aussetzer“).

## Ressourcen
* Dauerhaft ca. 6 KB RAM (Statik), während der Wiedergabe der Jitterpuffer: `RATE · 4 Byte · BUFFER_MS` (48 kHz, 400 ms ≈ 77 KB) plus ca. 3 KB bei Abwärtsabtastung.
* Rechenlast: Stereo-Resampler ca. 5 Mio. Operationen/s bei 48 kHz, auch ohne Ratenänderung (Phasenregelung läuft immer).
* Zwei Tasks: Netz (Priorität `IRM_NETIN_NET_TASK_PRIO`) und Ausgabe (`IRM_NETIN_OUT_TASK_PRIO`, wie bei KiwiSDR hoch gewählt).

## Hinweise für den Betrieb
* WLAN-Sparmodus ist im Projekt aus (sonst wären Jitter und TCP-Durchsatz deutlich schlechter).
* TCP-Durchsatz hängt vom lwIP-Empfangsfenster ab (`CONFIG_LWIP_TCP_WND_DEFAULT`); reicht es bei langsamem WLAN nicht, UDP verwenden oder das Fenster vergrößern. Die UDP-Empfangswarteschlange fasst nur wenige Pakete (`CONFIG_LWIP_UDP_RECVMBOX_SIZE`): Sender mit kleinen Blöcken (≤ 20 ms) verwenden.
* Quelle abschalten: `IRM_NETIN_ENABLE=n`.

