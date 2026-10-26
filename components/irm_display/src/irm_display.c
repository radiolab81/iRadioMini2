#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "irm_core.h"
#include "irm_display.h"

static const char *TAG = "irm_display";
static QueueHandle_t s_mailbox;   /* Laenge 1 + Overwrite: es zaehlt nur der neueste Zustand */
static const irm_display_backend_t *s_be;

static void on_status(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    xQueueOverwrite(s_mailbox, data);
}

static void task(void *arg)
{
    (void)arg;
    irm_status_t st;
    for (;;) {
        if (xQueueReceive(s_mailbox, &st, portMAX_DELAY) == pdTRUE) s_be->render(&st);
    }
}

esp_err_t irm_display_start(const irm_display_backend_t *be)
{
    if (!be) return ESP_OK;
    esp_err_t e = be->init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "Backend %s: init fehlgeschlagen (%s)", be->name, esp_err_to_name(e)); return e; }
    s_be = be;
    s_mailbox = xQueueCreate(1, sizeof(irm_status_t));
    if (!s_mailbox) return ESP_ERR_NO_MEM;
    if (xTaskCreate(task, "irm_disp", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(irm_status_subscribe(on_status, NULL));
    irm_status_t st; irm_status_get(&st);
    xQueueOverwrite(s_mailbox, &st);
    ESP_LOGI(TAG, "Backend: %s", be->name);
    return ESP_OK;
}

