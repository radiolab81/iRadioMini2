/* Serververzeichnis: Suchen, Blaettern, Sortieren, Laenderliste, Eintrag -> Kiwi-URI.
 * Reines C auf der kiwi_dir-Datenbank (kiwi_dir_db.h), ohne IDF-Abhaengigkeit -> host-testbar (test/test_irm_dir_query.c).
 * Alle Lesefunktionen duerfen aus beliebigen Tasks laufen, auch waehrend ein Update in den inaktiven Slot schreibt (siehe kiwi_dir_db.h). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kiwi_dir_db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { IRM_DIR_SORT_LIST = 0, IRM_DIR_SORT_SNR = 1 } irm_dir_sort_t;

typedef struct {
    char     text[24];     /* "" = egal; Teilstring in Name/Ort/Host/Land (Gross/Klein egal) */
    uint8_t  country;      /* 0 = egal, sonst Land-ID (irm_dir_countries) */
    uint8_t  min_snr;      /* 0 = egal, sonst Mindest-SNR in dB */
    bool     only_free;    /* nur Server mit freiem Kanal */
    uint32_t freq_hz;      /* 0 = egal, sonst muss der Empfangsbereich diese Frequenz abdecken */
    uint8_t  sort;         /* irm_dir_sort_t */
} irm_dir_filter_t;

typedef struct { uint32_t idx; kiwi_dir_rec_t rec; } irm_dir_hit_t;           /* idx = Index in der Datenbank */
typedef struct { uint8_t id; char name[KIWI_DIR_COUNTRY_LEN]; uint16_t count; } irm_dir_country_t;

#define IRM_DIR_ERR_NOMEM (-100)   /* irm_dir_info_t.err: zu wenig Speicher fuer das Update (sonst kiwi_dir_err_t) */

/* Zustand fuer die Anzeige (Web-UI); gefuellt von irm_dir_info() */
typedef enum { IRM_DIR_IDLE, IRM_DIR_UPDATING, IRM_DIR_OK, IRM_DIR_FAILED } irm_dir_state_t;
typedef struct {
    bool     available;    /* Flash-Partition vorhanden */
    bool     valid;        /* gueltige Datenbank geladen */
    uint32_t count, generation;
    uint8_t  state;        /* irm_dir_state_t */
    uint32_t bytes, servers;   /* Fortschritt des laufenden/letzten Updates */
    int      err;          /* kiwi_dir_err_t des letzten Updates (0 = ok) */
    uint32_t retry_in_s;   /* > 0: ein neues Update ist erst nach so vielen Sekunden ohne "force" erlaubt */
} irm_dir_info_t;

/**
 * Sucht Server. Es zaehlen nur verbindbare (kein offline/https/privat). Liefert die Treffer [offset, offset+limit) in der
 * Reihenfolge der Liste oder (IRM_DIR_SORT_SNR) nach SNR absteigend, bei Gleichstand weniger Nutzer zuerst.
 * @param total  Gesamtzahl der Treffer (darf NULL sein)
 * @return Anzahl in out geschriebener Treffer (<= limit), negativ = kiwi_dir_err_t
 */
int irm_dir_search(const kiwi_dir_db_t *db, const irm_dir_filter_t *f, uint32_t offset, uint32_t limit, irm_dir_hit_t *out, uint32_t *total);

/** Laender mit mindestens einem verbindbaren Server, alphabetisch. @return Anzahl (<= max) oder negativ = kiwi_dir_err_t. */
int irm_dir_countries(const kiwi_dir_db_t *db, irm_dir_country_t *out, size_t max);

/** Kiwi-URI fuer einen Eintrag bei freq_hz/mode (mode NULL/ungueltig -> "am"). Liegt die Frequenz ausserhalb des gemeldeten Empfangsbereichs,
 *  wird sie an dessen Rand gezogen. *used_freq (optional) = tatsaechlich verwendete Frequenz. */
bool irm_dir_build_uri(const kiwi_dir_rec_t *rec, uint32_t freq_hz, const char *mode, char *out, size_t n, uint32_t *used_freq);

/** Anzeigename "Name (Ort)" bzw. Host; wird nie mitten in einem UTF-8-Zeichen abgeschnitten. */
void irm_dir_display_name(const kiwi_dir_rec_t *rec, char *out, size_t n);

/** Klartext zu irm_dir_info_t.err (deutsch; 0 -> ""). */
const char *irm_dir_err_text(int err);

#ifdef __cplusplus
}
#endif
