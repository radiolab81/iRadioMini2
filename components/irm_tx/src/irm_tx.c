#include <stdlib.h>
#include "sdkconfig.h"
#include "irm_tx.h"

#if !CONFIG_IRM_TX_ENABLE
esp_err_t irm_tx_start(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t irm_tx_set_carrier_khz(uint32_t khz) { (void)khz; return ESP_ERR_NOT_SUPPORTED; }
uint32_t irm_tx_get_carrier_khz(void) { return 0; }
#else
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "irm_files.h"
#include "irm_tx.h"
#include "irm_tx_driver.h"

#if CONFIG_IRM_INET2RF
esp_err_t irm_inet2rf_start(void);
#endif

static const char *TAG = "irm_tx";
static SemaphoreHandle_t s_lock;
static uint32_t s_khz;

esp_err_t irm_tx_set_carrier_khz(uint32_t khz)
{
    if (!s_lock || khz == 0) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t e = irm_tx_driver.set_freq_hz(khz * 1000U);
    if (e == ESP_OK) s_khz = khz;
    xSemaphoreGive(s_lock);
    return e;
}

uint32_t irm_tx_get_carrier_khz(void) { return s_khz; }

/* /sdcard/AM.txt: eine Zahl = Traegerfrequenz in kHz (wie im Altprojekt) */
static uint32_t carrier_from_sd(void)
{
    char *txt; size_t len;
    if (irm_files_read("/sdcard/AM.txt", 32, &txt, &len) != ESP_OK) return 0;
    long v = strtol(txt, NULL, 10);
    free(txt);
    return (v >= 100 && v <= 30000) ? (uint32_t)v : 0;
}

esp_err_t irm_tx_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    esp_err_t e = irm_tx_driver.init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "Treiber %s: %s", irm_tx_driver.name, esp_err_to_name(e)); return e; }
    uint32_t khz = carrier_from_sd();
    if (!khz) khz = CONFIG_IRM_TX_CARRIER_KHZ;
    ESP_LOGI(TAG, "Treiber %s, Traeger %u kHz", irm_tx_driver.name, (unsigned)khz);
    e = irm_tx_set_carrier_khz(khz);
#if CONFIG_IRM_INET2RF
    if (e == ESP_OK) e = irm_inet2rf_start();
#endif
    return e;
}
#endif /* CONFIG_IRM_TX_ENABLE */
