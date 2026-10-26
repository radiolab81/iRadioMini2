/* ESP32-interne Traegererzeugung: LEDC-Rechteck mit 2-Bit-Aufloesung (50 % Tastgrad). */
#include "driver/ledc.h"
#include "sdkconfig.h"
#include "irm_tx_driver.h"

#define MODE LEDC_HIGH_SPEED_MODE   /* nur ESP32 (Classic) */

static esp_err_t set_freq_hz(uint32_t hz) { return ledc_set_freq(MODE, LEDC_TIMER_0, hz); }

static esp_err_t init(void)
{
    const ledc_timer_config_t t = { .speed_mode = MODE, .duty_resolution = LEDC_TIMER_2_BIT, .timer_num = LEDC_TIMER_0,
                                    .freq_hz = 500000, .clk_cfg = LEDC_AUTO_CLK };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) return e;
    const ledc_channel_config_t c = { .gpio_num = CONFIG_IRM_TX_PIN_A, .speed_mode = MODE, .channel = LEDC_CHANNEL_0,
                                      .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0, .duty = 2, .hpoint = 0 };
    return ledc_channel_config(&c);
}

const irm_tx_driver_t irm_tx_driver = { "ESP32-LEDC", init, set_freq_hz };
