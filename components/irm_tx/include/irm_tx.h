#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Traegererzeugung starten. Frequenz: /sdcard/AM.txt (kHz) > Kconfig. Ohne CONFIG_IRM_TX_ENABLE: ESP_ERR_NOT_SUPPORTED. */
esp_err_t irm_tx_start(void);
/** Traegerfrequenz setzen (thread-sicher). */
esp_err_t irm_tx_set_carrier_khz(uint32_t khz);
uint32_t  irm_tx_get_carrier_khz(void);
#ifdef __cplusplus
}
#endif
