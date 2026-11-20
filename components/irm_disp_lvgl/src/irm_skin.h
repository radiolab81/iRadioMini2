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

/* Symbole fuer Zufall/Wiederholen; nur in der Quelle Dateien und nur, wenn etwas aktiv ist (sonst leer) */
static inline const char *irm_skin_mode_text(const irm_status_t *st)
{
    if (st->source != IRM_SOURCE_MEDIA) return "";
    if (st->shuffle && st->loop) return LV_SYMBOL_SHUFFLE " " LV_SYMBOL_LOOP;
    return st->shuffle ? LV_SYMBOL_SHUFFLE : (st->loop ? LV_SYMBOL_LOOP : "");
}

extern const irm_skin_t irm_skin_active;   /* vom Kconfig gewaehlt, genau eine Definition wird gelinkt */
