#include "driver/ledc.h"
#include "sdkconfig.h"
#include "irm_display.h"
#include "irm_disp_servo.h"

#define RES_BITS  LEDC_TIMER_14_BIT
#define PERIOD_US 20000

static esp_err_t init(void)
{
    const ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = RES_BITS,
                                    .timer_num = LEDC_TIMER_1, .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) return e;
    const ledc_channel_config_t c = { .gpio_num = CONFIG_IRM_SERVO_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
                                      .channel = LEDC_CHANNEL_1, .timer_sel = LEDC_TIMER_1, .duty = 0, .hpoint = 0 };
    return ledc_channel_config(&c);
}

/* Zeiger ueber die Senderskala: Position = Index / (Anzahl-1) */
static void render(const irm_status_t *st)
{
    int span = CONFIG_IRM_SERVO_MAX_US - CONFIG_IRM_SERVO_MIN_US;
    int pos = (st->count > 1 && st->index >= 0) ? (span * st->index) / (st->count - 1) : 0;
    uint32_t us = (uint32_t)(CONFIG_IRM_SERVO_MIN_US + pos);
    uint32_t duty = (uint32_t)(((uint64_t)us << RES_BITS) / PERIOD_US);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static void deinit(void) { ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0); }

const irm_display_backend_t irm_disp_servo_backend = { "servo", init, render, deinit };
