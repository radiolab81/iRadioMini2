#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Startet den HTTP-Server (REST-API unter /api/, UI unter /). Idempotent. */
esp_err_t irm_web_start(void);
#ifdef __cplusplus
}
#endif
