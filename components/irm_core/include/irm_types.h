#pragma once
#include <stdbool.h>
#include <stdint.h>

#define IRM_URI_MAX_LEN  256
#define IRM_NAME_MAX_LEN 64
#define IRM_EQ_BANDS     10
#define IRM_EQ_MIN_DB   (-12)
#define IRM_EQ_MAX_DB   (12)

typedef enum { IRM_SOURCE_RADIO = 0, IRM_SOURCE_MEDIA = 1, IRM_SOURCE_KIWI = 2, IRM_SOURCE_NET = 3, IRM_SOURCE_COUNT } irm_source_t;

#define IRM_RSSI_NONE (-32768)   /* kiwi_rssi_dbm: noch keine Feldstaerke bekannt */

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
    /* Mediaplayer (Quelle Dateien): Zufallswiedergabe im aktuellen Ordner / laufenden Titel wiederholen; wirkt nur bei source == IRM_SOURCE_MEDIA, bleibt aber bei Quellenwechsel erhalten */
    bool               shuffle;
    bool               loop;
    /* nur bei source == IRM_SOURCE_KIWI gueltig (sonst 0 / "" / IRM_RSSI_NONE); wird von irm_kiwi veroeffentlicht */
    uint32_t           kiwi_freq_hz;
    int16_t            kiwi_rssi_dbm;
    char               kiwi_mode[8];
    /* nur bei source == IRM_SOURCE_NET gueltig (sonst "" / 0); wird von irm_netin veroeffentlicht */
    char               net_peer[16];     /* IP des aktiven Absenders, "" = wartet auf einen Sender */
    uint32_t           net_underruns;
    /* Aufnahme (irm_rec): laeuft/bereit und aufgenommene Sekunden; wird von irm_rec veroeffentlicht */
    bool               rec;
    uint32_t           rec_secs;
} irm_status_t;
