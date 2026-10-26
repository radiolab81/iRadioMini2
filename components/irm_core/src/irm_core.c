#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "irm_core.h"

ESP_EVENT_DEFINE_BASE(IRM_EVENT);

static const char *TAG = "irm_core";
static QueueHandle_t s_cmd_q;
static SemaphoreHandle_t s_lock;
static esp_event_loop_handle_t s_loop;
static irm_status_t s_status;

esp_err_t irm_core_init(void)
{
    if (s_loop) return ESP_OK;
    s_cmd_q = xQueueCreate(16, sizeof(irm_cmd_t));
    s_lock = xSemaphoreCreateMutex();
    if (!s_cmd_q || !s_lock) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    s_status.index = -1;
    const esp_event_loop_args_t args = {
        .queue_size = 16, .task_name = "irm_evt", .task_priority = 5,
        .task_stack_size = 4096, .task_core_id = tskNO_AFFINITY,
    };
    return esp_event_loop_create(&args, &s_loop);
}

esp_err_t irm_cmd_post(const irm_cmd_t *cmd, TickType_t wait)
{
    if (!s_cmd_q || !cmd) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(s_cmd_q, cmd, wait) != pdTRUE) {
        ESP_LOGW(TAG, "Kommando-Queue voll, Kommando %d verworfen", cmd->id);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t irm_cmd_post_id(irm_cmd_id_t id, int32_t value)
{
    irm_cmd_t c = { .id = id };
    c.value = value;
    return irm_cmd_post(&c, 0);
}

esp_err_t irm_cmd_receive(irm_cmd_t *cmd, TickType_t wait)
{
    return xQueueReceive(s_cmd_q, cmd, wait) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void irm_status_update(irm_status_mutator_t fn, void *ctx)
{
    irm_status_t copy;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    irm_status_t before = s_status;
    fn(&s_status, ctx);
    bool changed = memcmp(&before, &s_status, sizeof(before)) != 0;
    copy = s_status;
    xSemaphoreGive(s_lock);
    if (changed && s_loop) {
        esp_err_t e = esp_event_post_to(s_loop, IRM_EVENT, IRM_EVENT_STATUS, &copy, sizeof(copy), 0);
        if (e != ESP_OK) ESP_LOGW(TAG, "Status-Event nicht zugestellt: %s", esp_err_to_name(e));
    }
}

void irm_status_get(irm_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

esp_err_t irm_status_subscribe(esp_event_handler_t handler, void *arg)
{
    return esp_event_handler_register_with(s_loop, IRM_EVENT, IRM_EVENT_STATUS, handler, arg);
}
