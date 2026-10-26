#pragma once
#include <stdbool.h>
#include <stdint.h>

#define IRM_URI_MAX_LEN  256
#define IRM_NAME_MAX_LEN 64
#define IRM_EQ_BANDS     10
#define IRM_EQ_MIN_DB   (-12)
#define IRM_EQ_MAX_DB   (12)

typedef enum { IRM_SOURCE_RADIO = 0, IRM_SOURCE_MEDIA = 1 } irm_source_t;

typedef enum {
    IRM_PLAYER_IDLE = 0,
    IRM_PLAYER_CONNECTING,
    IRM_PLAYER_PLAYING,
    IRM_PLAYER_PAUSED,
    IRM_PLAYER_ERROR,
} irm_player_state_t;

typedef struct { int8_t gain_db[IRM_EQ_BANDS]; } irm_eq_t;

/** Unveraenderlicher Schnappschuss des Geraetezustands (wird by-value verteilt). */
typedef struct {
    irm_source_t       source;
    irm_player_state_t player;
    int32_t            index;        /* aktueller Eintrag, -1 = keiner */
    int32_t            count;        /* Eintraege in der aktiven Liste */
    uint8_t            volume;       /* 0..100 */
    bool               net_up;
    char               ip[16];
    char               name[IRM_NAME_MAX_LEN];
    char               uri[IRM_URI_MAX_LEN];
    uint32_t           sample_rate;
    uint8_t            channels;
    uint8_t            bits;
    uint32_t           bitrate;
    irm_eq_t           eq;
} irm_status_t;
