/* Aufnahme (Radio, KiwiSDR) als unkomprimiertes WAV auf die SD-Karte - "Kassettenrecorder".
 *
 * Abgriff:
 *  - Radio: irm_rec_init() haengt sich in irm_out_sink_t.write der Ausgabesenke ein (alle Pipelines enden dort). Aufgenommen wird, was ohne
 *    Lautstaerke am Ausgang ankommt: Decoder -> Rate/Kanal/Bit-Umsetzer -> (Pegelreserve) -> Equalizer. 16 Bit, Stereo, IRM_AUDIO_OUT_RATE.
 *  - KiwiSDR: irm_kiwi_set_tap(irm_rec_feed_kiwi) - die 12-kHz-Mono-Samples aus dem Jitterpuffer, vor Resampler und Software-Verstaerkung.
 *  - Dateien/Netzwerk: nicht aufnehmbar (ESP_ERR_NOT_SUPPORTED).
 *
 * Der Audio-Pfad blockiert nie: Samples gehen in einen Ringpuffer, ein eigener Task schreibt sie auf die Karte. Die Datei wird erst angelegt,
 * wenn der erste Ton ankommt. Ablauf/Grenzen: docs/RECORDING.md. Start/Stop kommen aus irm_control (Kommandos IRM_CMD_REC_*). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "irm_out.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool         active;        /* Aufnahme laeuft oder ist bereit (wartet auf den ersten Ton) */
    irm_source_t source;        /* Quelle, bei der sie gestartet wurde */
    uint32_t     secs;          /* aufgenommene Audiozeit (aus der Datenmenge, nicht Wanduhr) */
    uint64_t     bytes;         /* Nutzdaten seit Start (alle Dateien) */
    uint32_t     dropped_bytes; /* verworfen, weil der Puffer voll war (Schreibpause der Karte) */
    uint32_t     files;         /* begonnene Dateien seit Start */
    char         file[48];      /* aktuelle (bzw. zuletzt beschriebene) Datei ohne Pfad, "" solange noch keine angelegt ist */
    char         end_reason[24];/* nach einem selbststaendigen Ende (Karte voll, Schreibfehler ...) der Grund, sonst ""; wird beim naechsten Start geloescht */
} irm_rec_info_t;

/** Einmal beim Start (nach irm_board_init, SD-Mount). Haengt sich in sink->write ein; richtet die Aufnahme-LED ein. */
esp_err_t irm_rec_init(irm_out_sink_t *sink);

/** Aufnahme fuer die Quelle src starten. ESP_ERR_NOT_SUPPORTED: Quelle nicht aufnehmbar oder Funktion abgeschaltet; ESP_ERR_NOT_FOUND: keine SD-Karte;
 *  ESP_ERR_INVALID_STATE: laeuft schon; ESP_ERR_NO_MEM. Kehrt sofort zurueck. */
esp_err_t irm_rec_start(irm_source_t src);

/** Beendet die Aufnahme, wartet bis Puffer geleert, Kopf korrigiert und Datei geschlossen ist (max. ca. 5 s). Ohne laufende Aufnahme sofort OK. */
esp_err_t irm_rec_stop(void);

bool irm_rec_active(void);
void irm_rec_info(irm_rec_info_t *out);

/** Abgriff fuer irm_kiwi (siehe irm_kiwi_set_tap): mono, rate_hz = Quellrate (12000). Blockiert nie. */
void irm_rec_feed_kiwi(const int16_t *mono, size_t n, uint32_t rate_hz);

#ifdef __cplusplus
}
#endif
