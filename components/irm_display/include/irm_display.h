#pragma once
#include "esp_err.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Anzeige-Backend: wird ausschliesslich aus der Display-Task aufgerufen (kein Locking noetig). */
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    void      (*render)(const irm_status_t *st);
    void      (*deinit)(void);
} irm_display_backend_t;

/** Startet den Anzeigedienst mit einem Backend (NULL: no-op). Das Backend waehlt die Anwendung (siehe main). */
esp_err_t irm_display_start(const irm_display_backend_t *backend);

/* Gemeinsame Textaufbereitung fuer Zeichen-Backends */
void irm_view_title(const irm_status_t *st, char *out, size_t n);   /* Sendername / Titel */
void irm_view_info(const irm_status_t *st, char *out, size_t n);    /* "PLAY 44.1k 128k" bzw. Netzstatus */
void irm_view_volume(const irm_status_t *st, char *out, size_t n);  /* "Vol 30%" */

#ifdef __cplusplus
}
#endif
