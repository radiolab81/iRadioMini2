#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Startet die Steuer-Task. Voraussetzung: irm_core, irm_settings, irm_board (Codec) initialisiert. */
esp_err_t irm_control_start(void);
/** Thread-sichere Lesezugriffe auf die Listen (z.B. fuer Web-UI). */
size_t irm_control_item_count(irm_source_t src);
bool   irm_control_item(irm_source_t src, size_t idx, char *name, size_t name_sz, char *uri, size_t uri_sz);
#ifdef __cplusplus
}
#endif
