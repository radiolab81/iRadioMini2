#pragma once
#include <stdint.h>
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "irm_out.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IRM_AUDIO_EVT_RUNNING,    /* Pipeline laeuft */
    IRM_AUDIO_EVT_PAUSED,
    IRM_AUDIO_EVT_STOPPED,    /* durch irm_audio_stop() */
    IRM_AUDIO_EVT_FINISHED,   /* Stream/Datei regulaer zu Ende */
    IRM_AUDIO_EVT_ERROR,      /* Verbindungs-/Decoderfehler */
    IRM_AUDIO_EVT_INFO,       /* Format des Streams bekannt */
} irm_audio_evt_type_t;

typedef struct {
    irm_audio_evt_type_t type;
    uint32_t sample_rate;
    uint8_t  channels, bits;
    uint32_t bitrate;
} irm_audio_evt_t;

/** Wird im Kontext der Pipeline-Task aufgerufen: nicht blockieren, nur weiterleiten. */
typedef void (*irm_audio_cb_t)(const irm_audio_evt_t *evt, void *ctx);

/** Ausgabe: entweder Codec (Hardware-Lautstaerke) ODER Senke (PDM/interner DAC, Software-Lautstaerke); genau einer ungleich NULL. */
esp_err_t irm_audio_init(esp_codec_dev_handle_t codec, irm_out_sink_t *sink, irm_audio_cb_t cb, void *ctx);
/** Neue Quelle starten (http(s)://... oder absoluter Dateipfad). Beendet laufende Wiedergabe. */
esp_err_t irm_audio_play(const char *uri);
esp_err_t irm_audio_stop(void);
esp_err_t irm_audio_pause(void);
esp_err_t irm_audio_resume(void);
esp_err_t irm_audio_set_volume(uint8_t percent);
/** Wirkt sofort und bleibt ueber Quellenwechsel erhalten. */
esp_err_t irm_audio_set_eq(const irm_eq_t *eq);

#ifdef __cplusplus
}
#endif
