/* PDM-Ausgabe (V1: I2S_STREAM_TX_PDM_CFG_DEFAULT). ESP32: PDM-TX nur auf I2S0. Neue API: driver/i2s_pdm.h. */
#include <stdlib.h>
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_log.h"
#include "irm_out.h"
#include "irm_pcm.h"

#define CHUNK_FRAMES 256
static const char *TAG = "irm_out_pdm";

typedef struct {
    irm_out_sink_t base;
    i2s_chan_handle_t tx;
    int16_t mono[CHUNK_FRAMES];
} pdm_sink_t;

static esp_err_t pdm_open(irm_out_sink_t *s) { return i2s_channel_enable(((pdm_sink_t *)s)->tx); }
static void pdm_close(irm_out_sink_t *s) { i2s_channel_disable(((pdm_sink_t *)s)->tx); }

static esp_err_t pdm_write(irm_out_sink_t *s, const int16_t *stereo, size_t frames)
{
    pdm_sink_t *p = (pdm_sink_t *)s;
    while (frames) {
        size_t n = frames > CHUNK_FRAMES ? CHUNK_FRAMES : frames;
        irm_pcm_downmix_mono(p->mono, stereo, n, s->gain_q15);
        size_t off = 0, bytes = n * sizeof(int16_t);
        while (off < bytes) {
            size_t w = 0;
            ESP_RETURN_ON_ERROR(i2s_channel_write(p->tx, (const uint8_t *)p->mono + off, bytes - off, &w, 1000), TAG, "write");
            off += w;
        }
        stereo += 2 * n; frames -= n;
    }
    return ESP_OK;
}

esp_err_t irm_out_pdm_create(int clk_gpio, int dout_gpio, uint32_t rate, uint32_t buffer_ms, irm_out_sink_t **out)
{
    pdm_sink_t *p = calloc(1, sizeof(*p));
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "mem");
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.dma_frame_num = 480;
    cc.dma_desc_num = irm_out_dma_desc_num(rate, buffer_ms, cc.dma_frame_num);   /* groesserer Puffer faengt Verzoegerungen (SD, WLAN) ab */
    esp_err_t e = i2s_new_channel(&cc, &p->tx, NULL);
    if (e != ESP_OK) { free(p); return e; }
    const i2s_pdm_tx_config_t pc = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = clk_gpio < 0 ? I2S_GPIO_UNUSED : (gpio_num_t)clk_gpio, .dout = (gpio_num_t)dout_gpio },
    };
    e = i2s_channel_init_pdm_tx_mode(p->tx, &pc);
    if (e != ESP_OK) { i2s_del_channel(p->tx); free(p); return e; }
    p->base = (irm_out_sink_t){ .name = "pdm", .open = pdm_open, .write = pdm_write, .close = pdm_close, .gain_q15 = IRM_PCM_UNITY,
                                .sample_rate = rate, .buffer_ms = (uint32_t)((uint64_t)cc.dma_desc_num * cc.dma_frame_num * 1000u / rate) };
    *out = &p->base;
    return ESP_OK;
}
