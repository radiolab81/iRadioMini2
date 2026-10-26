#pragma once
#include "lvgl.h"
#include "irm_types.h"

/* Ein Skin baut seine Objekte einmal auf (create) und wird bei jeder Zustandsaenderung aktualisiert (update).
 * Beides laeuft unter gehaltenem LVGL-Lock. Layout ist fuer 320x240 ausgelegt. */
typedef struct {
    const char *name;
    void (*create)(lv_obj_t *screen);
    void (*update)(const irm_status_t *st);
} irm_skin_t;

extern const irm_skin_t irm_skin_active;   /* vom Kconfig gewaehlt, genau eine Definition wird gelinkt */
