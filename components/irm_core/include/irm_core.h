#pragma once
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Kommandos: Punkt-zu-Punkt (viele Erzeuger -> ein Konsument: irm_control) ---- */
typedef enum {
    IRM_CMD_NONE = 0,
    IRM_CMD_NEXT, IRM_CMD_PREV,
    IRM_CMD_PLAY, IRM_CMD_PAUSE, IRM_CMD_TOGGLE_PAUSE, IRM_CMD_STOP,
    IRM_CMD_VOLUME_UP, IRM_CMD_VOLUME_DOWN,
    IRM_CMD_SET_VOLUME,     /* value = 0..100 */
    IRM_CMD_GOTO,           /* value = Index in aktiver Liste */
    IRM_CMD_SET_SOURCE,     /* value = irm_source_t */
    IRM_CMD_TOGGLE_SOURCE,
    IRM_CMD_SET_EQ,         /* eq */
    IRM_CMD_SAVE_EQ,
    IRM_CMD_RESET_EQ,
    /* intern (nur von irm_control/irm_audio verwendet) */
    IRM_CMD_INTERNAL_AUDIO_EVENT,
    IRM_CMD_INTERNAL_NET_CHANGED,
    IRM_CMD_INTERNAL_RETRY,
} irm_cmd_id_t;

typedef struct {
    irm_cmd_id_t id;
    union {
        int32_t  value;
        irm_eq_t eq;
        struct { int32_t type; uint32_t rate; uint8_t ch; uint8_t bits; uint32_t bitrate; } audio;
    };
} irm_cmd_t;

esp_err_t irm_core_init(void);

/** Thread-sicher, nicht aus ISR. Das Kommando wird kopiert (kein geteilter Zustand). */
esp_err_t irm_cmd_post(const irm_cmd_t *cmd, TickType_t wait);
esp_err_t irm_cmd_post_id(irm_cmd_id_t id, int32_t value);
/** Nur fuer den einen Konsumenten (irm_control). */
esp_err_t irm_cmd_receive(irm_cmd_t *cmd, TickType_t wait);

/* ---- Status: Publish/Subscribe ueber eigene esp_event-Loop ---- */
ESP_EVENT_DECLARE_BASE(IRM_EVENT);
typedef enum { IRM_EVENT_STATUS = 1 } irm_event_id_t;

typedef void (*irm_status_mutator_t)(irm_status_t *status, void *ctx);

/** Status atomar aendern; bei Aenderung wird IRM_EVENT_STATUS (Payload: irm_status_t) gesendet. */
void irm_status_update(irm_status_mutator_t fn, void *ctx);
void irm_status_get(irm_status_t *out);
/** Handler laeuft im Task der Event-Loop: kurz halten, nicht blockieren. */
esp_err_t irm_status_subscribe(esp_event_handler_t handler, void *arg);

#ifdef __cplusplus
}
#endif
