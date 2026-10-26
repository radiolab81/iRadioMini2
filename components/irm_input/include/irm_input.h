#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Startet Taster-/Drehgeber-Abfrage. Sendet ausschliesslich Kommandos (irm_cmd_post). */
esp_err_t irm_input_start(void);
#ifdef __cplusplus
}
#endif
