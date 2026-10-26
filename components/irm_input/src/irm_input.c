#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "irm_core.h"
#include "irm_input.h"

static const char *TAG = "irm_input";
#define TICK_MS     10
#define DEBOUNCE_TICKS 3   /* 30 ms stabil */

typedef struct {
    int gpio;
    irm_cmd_id_t short_cmd, long_cmd;
    bool repeat;                    /* Kurzbefehl bei gehaltener Taste wiederholen */
    /* Laufzeit */
    bool stable_down; uint8_t cnt; bool long_fired; uint32_t held_ms, since_rep_ms;
} button_t;

static button_t s_btn[] = {
    { .gpio = CONFIG_IRM_BTN_PLAY,     .short_cmd = IRM_CMD_TOGGLE_PAUSE, .long_cmd = IRM_CMD_TOGGLE_SOURCE },
    { .gpio = CONFIG_IRM_BTN_NEXT,     .short_cmd = IRM_CMD_NEXT },
    { .gpio = CONFIG_IRM_BTN_PREV,     .short_cmd = IRM_CMD_PREV },
    { .gpio = CONFIG_IRM_BTN_VOL_UP,   .short_cmd = IRM_CMD_VOLUME_UP,   .repeat = true },
    { .gpio = CONFIG_IRM_BTN_VOL_DOWN, .short_cmd = IRM_CMD_VOLUME_DOWN, .repeat = true },
};
#define N_BTN (sizeof(s_btn) / sizeof(s_btn[0]))

static pcnt_unit_handle_t s_pcnt;
static int s_last_count;

/* Entprellung per Zaehler + Flankenerkennung */
static void step_button(button_t *b)
{
    bool down = gpio_get_level(b->gpio) == 0;
    if (down == b->stable_down) { b->cnt = 0; }
    else if (++b->cnt >= DEBOUNCE_TICKS) {
        b->stable_down = down; b->cnt = 0;
        if (down) { b->held_ms = 0; b->since_rep_ms = 0; b->long_fired = false; }
        else if (!b->long_fired && !b->repeat) irm_cmd_post_id(b->short_cmd, 0);   /* Kurz: beim Loslassen */
    }
    if (b->stable_down) {
        b->held_ms += TICK_MS;
        if (b->repeat) {
            if (b->held_ms == TICK_MS || (b->held_ms > CONFIG_IRM_BTN_LONG_MS / 2 && (b->since_rep_ms += TICK_MS) >= CONFIG_IRM_BTN_REPEAT_MS)) {
                irm_cmd_post_id(b->short_cmd, 0);
                b->since_rep_ms = 0;
            }
        } else if (b->long_cmd && !b->long_fired && b->held_ms >= CONFIG_IRM_BTN_LONG_MS) {
            b->long_fired = true;
            irm_cmd_post_id(b->long_cmd, 0);
        }
    }
}

static void step_rotary(void)
{
    if (!s_pcnt) return;
    int cnt;
    if (pcnt_unit_get_count(s_pcnt, &cnt) != ESP_OK) return;
    int steps = (cnt - s_last_count) / CONFIG_IRM_ROTARY_COUNTS_PER_STEP;
    if (steps == 0) return;
    s_last_count += steps * CONFIG_IRM_ROTARY_COUNTS_PER_STEP;
    if (s_last_count > 8000 || s_last_count < -8000) { pcnt_unit_clear_count(s_pcnt); s_last_count = 0; }
    for (int i = 0; i < (steps < 0 ? -steps : steps); i++) {
#if CONFIG_IRM_ROTARY_VOLUME
        irm_cmd_post_id(steps > 0 ? IRM_CMD_VOLUME_UP : IRM_CMD_VOLUME_DOWN, 0);
#else
        irm_cmd_post_id(steps > 0 ? IRM_CMD_NEXT : IRM_CMD_PREV, 0);
#endif
    }
}

static esp_err_t init_rotary(void)
{
#if CONFIG_IRM_ROTARY_A < 0 || CONFIG_IRM_ROTARY_B < 0
    return ESP_OK;   /* kein Drehgeber konfiguriert */
#else
    const gpio_config_t io = { .pin_bit_mask = (1ULL << CONFIG_IRM_ROTARY_A) | (1ULL << CONFIG_IRM_ROTARY_B),
                               .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&io));
    const pcnt_unit_config_t uc = { .low_limit = -10000, .high_limit = 10000 };
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, &s_pcnt));
    const pcnt_glitch_filter_config_t gf = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &gf));
    pcnt_channel_handle_t ca, cb;
    const pcnt_chan_config_t ccfa = { .edge_gpio_num = CONFIG_IRM_ROTARY_A, .level_gpio_num = CONFIG_IRM_ROTARY_B };
    const pcnt_chan_config_t ccfb = { .edge_gpio_num = CONFIG_IRM_ROTARY_B, .level_gpio_num = CONFIG_IRM_ROTARY_A };
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &ccfa, &ca));
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &ccfb, &cb));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    return pcnt_unit_start(s_pcnt);
#endif
}

static void task(void *arg)
{
    (void)arg;
    TickType_t t = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&t, pdMS_TO_TICKS(TICK_MS));
        for (size_t i = 0; i < N_BTN; i++) if (s_btn[i].gpio >= 0) step_button(&s_btn[i]);
        step_rotary();
    }
}

esp_err_t irm_input_start(void)
{
    bool any = false;
    for (size_t i = 0; i < N_BTN; i++) {
        if (s_btn[i].gpio < 0) continue;
        any = true;
        /* GPIO34..39 besitzen keine internen Pull-ups -> externer Pull-up noetig (z.B. AudioKit) */
        const gpio_config_t io = { .pin_bit_mask = 1ULL << s_btn[i].gpio, .mode = GPIO_MODE_INPUT,
                                   .pull_up_en = GPIO_IS_VALID_OUTPUT_GPIO(s_btn[i].gpio) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE };
        ESP_ERROR_CHECK(gpio_config(&io));
    }
    ESP_ERROR_CHECK(init_rotary());
    if (!any && !s_pcnt) { ESP_LOGI(TAG, "keine Eingabegeraete konfiguriert"); return ESP_OK; }
    return xTaskCreate(task, "irm_input", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
