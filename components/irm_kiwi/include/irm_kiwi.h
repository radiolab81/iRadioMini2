/* KiwiSDR-Audioclient als iRadioMini2-Quelle.
 *
 * Eine Verbindung (kiwi_core) liefert 12-kHz-Mono-PCM. Ein Ausgabe-Task entnimmt es einem Jitterpuffer, bringt es per Resampler (mit Drift-
 * regelung) auf die Senken-Rate und schreibt es als Stereo in dieselbe irm_out_sink_t wie die irm_audio-Pipeline. Beide Engines duerfen die
 * Senke NIE gleichzeitig nutzen: irm_control ruft vor irm_kiwi_play() irm_audio_stop() und vor irm_audio_play() irm_kiwi_stop().
 *
 * Wiederverbinden nach Fehlern ist Sache von irm_control (wie beim Radio): irm_kiwi meldet IRM_KIWI_EVT_ERROR und wartet auf das naechste
 * irm_kiwi_play(). Frequenz/Modus/Feldstaerke werden direkt in den Status (irm_status_t.kiwi_*) veroeffentlicht.
 * Alle Funktionen sind thread-sicher; gedacht fuer den Aufruf aus dem Control-Task. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "irm_out.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IRM_KIWI_EVT_INFO,      /* Audioformat bekannt: sample_rate, bitrate */
    IRM_KIWI_EVT_RUNNING,   /* Vorpufferung abgeschlossen, Ton laeuft */
    IRM_KIWI_EVT_ERROR,     /* Sitzung beendet (Server weg, Passwort, Timeout, ...), err = kiwi_err_t */
} irm_kiwi_evt_type_t;

typedef struct {
    irm_kiwi_evt_type_t type;
    uint32_t sample_rate;
    uint32_t bitrate;
    int      err;
} irm_kiwi_evt_t;

/** Wird aus dem Netz- oder Ausgabe-Task gerufen: kurz halten, nicht blockieren (irm_control postet nur ein Kommando). */
typedef void (*irm_kiwi_cb_t)(const irm_kiwi_evt_t *evt, void *ctx);

esp_err_t irm_kiwi_init(irm_out_sink_t *sink, irm_kiwi_cb_t cb, void *ctx);

/** Eintrag abspielen (URI-Formate: irm_kiwi_uri.h). Kehrt sofort zurueck; ESP_ERR_INVALID_ARG bei ungueltiger URI.
 *  reset_tune = true: Frequenz/Modus aus der URI; false und gleiche URI wie zuvor: zuletzt eingestellte Abstimmung behalten (Pause/Fortsetzen, Wiederverbinden). */
esp_err_t irm_kiwi_play(const char *uri, bool reset_tune);

/** Beendet Sitzung und gibt die Senke frei (blockiert hoechstens ca. 2,5 s, meist < 0,5 s). Ohne laufende Sitzung sofort zurueck. Sendet kein Event. */
esp_err_t irm_kiwi_stop(void);

/** Abstimmen; auch ohne laufende Verbindung (gilt dann fuer den naechsten Start). freq_hz = 0: Frequenz behalten; mode = NULL: Modus behalten. */
esp_err_t irm_kiwi_tune(uint32_t freq_hz, const char *mode);
esp_err_t irm_kiwi_tune_step(int32_t delta_hz);
/** idx >= 0: Modus aus irm_kiwi_mode_name(); idx < 0: naechster Modus der Tabelle. */
esp_err_t irm_kiwi_set_mode_index(int idx);

#ifdef __cplusplus
}
#endif
