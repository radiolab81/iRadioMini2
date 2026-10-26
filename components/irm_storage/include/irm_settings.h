#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t  volume;
    int32_t  station;
    irm_eq_t eq;
    bool     has_eq;
} irm_settings_t;

esp_err_t irm_settings_init(void);   /* nvs_flash_init inkl. Erase bei Versionskonflikt */
esp_err_t irm_settings_load(irm_settings_t *out);   /* fehlende Werte -> Defaults */
esp_err_t irm_settings_save_volume(uint8_t v);
esp_err_t irm_settings_save_station(int32_t idx);
esp_err_t irm_settings_save_eq(const irm_eq_t *eq);
esp_err_t irm_settings_load_wifi(char *ssid, size_t ssid_sz, char *psk, size_t psk_sz);
esp_err_t irm_settings_save_wifi(const char *ssid, const char *psk);
#ifdef __cplusplus
}
#endif
