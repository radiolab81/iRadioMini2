/* inet2RF: Oszillatorfrequenz eines Analogradios per PCNT messen -> Skalenposition -> Sender + Traegerfrequenz.
 * Aenderungen gegenueber dem Altprojekt: neue PCNT-API (Hardware-Ueberlauf-Akkumulation), zeitbasierte Messung,
 * ZF-Offset wird subtrahiert (f_Empfang = f_Osz - ZF), Senderwahl per GOTO statt NEXT/PREV, Entprellung der Skalenposition. */
#include <stdlib.h>
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "irm_core.h"
#include "irm_tx.h"

static const char *TAG = "inet2rf";
#define SAMPLE_MS 100
#define STABLE_SAMPLES 3      /* gleiche Position in n aufeinanderfolgenden Messungen (300 ms) */

static pcnt_unit_handle_t s_unit;

static int channel_for(int32_t rf_khz, int count)
{
    int spacing_x = (CONFIG_IRM_INET2RF_MAX_KHZ - CONFIG_IRM_INET2RF_MIN_KHZ);   /* Bandbreite */
    if (count < 2) return 0;
    int32_t rel = rf_khz - CONFIG_IRM_INET2RF_MIN_KHZ;
    int idx = (int)((rel * (count - 1) + spacing_x / 2) / spacing_x);              /* naechster Senderplatz */
    return idx < 0 ? 0 : (idx > count - 1 ? count - 1 : idx);
}

static void task(void *arg)
{
    (void)arg;
    int last_cnt = 0, cand = -1, stable = 0, active = -1;
    int64_t t_last = esp_timer_get_time();
    pcnt_unit_get_count(s_unit, &last_cnt);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        int cnt; pcnt_unit_get_count(s_unit, &cnt);
        int64_t now = esp_timer_get_time();
        uint32_t delta = (uint32_t)cnt - (uint32_t)last_cnt;
        int32_t osc_khz = (int32_t)(((uint64_t)delta * 1000ULL) / (uint64_t)(now - t_last));   /* Impulse/us * 1000 = kHz */
        last_cnt = cnt; t_last = now;

        irm_status_t st; irm_status_get(&st);
        if (st.source != IRM_SOURCE_RADIO || st.count < 1) continue;
        int idx = channel_for(osc_khz - CONFIG_IRM_INET2RF_IF_KHZ, st.count);
        if (idx == cand) { if (stable < STABLE_SAMPLES) stable++; } else { cand = idx; stable = 1; }
        if (stable >= STABLE_SAMPLES && idx != active) {
            active = idx;
            uint32_t carrier = CONFIG_IRM_INET2RF_MIN_KHZ + (uint32_t)(CONFIG_IRM_INET2RF_MAX_KHZ - CONFIG_IRM_INET2RF_MIN_KHZ) * (uint32_t)idx / (uint32_t)(st.count > 1 ? st.count - 1 : 1);
            ESP_LOGI(TAG, "Oszillator %d kHz -> Platz %d, Traeger %u kHz", (int)osc_khz, idx, (unsigned)carrier);
            irm_tx_set_carrier_khz(carrier);
            irm_cmd_post_id(IRM_CMD_GOTO, idx);
        }
    }
}

esp_err_t irm_inet2rf_start(void)
{
    const pcnt_unit_config_t uc = { .low_limit = -1, .high_limit = 30000, .flags.accum_count = true };
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, &s_unit));
    pcnt_chan_config_t cc = { .edge_gpio_num = CONFIG_IRM_INET2RF_GPIO, .level_gpio_num = -1 };
    pcnt_channel_handle_t ch;
    ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &cc, &ch));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_unit_enable(s_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_unit));
    return xTaskCreate(task, "inet2rf", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
