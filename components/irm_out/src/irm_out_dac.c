/* Interner 8-Bit-DAC (ESP32) per dac_continuous (I2S0-DMA). Ersetzt den Legacy-"I2S DAC built-in"-Modus des Altprojekts.
 * Stereo: Modus ALTER (Bytes L,R,L,R; jeder Kanal wird mit der halben Wandlerfrequenz aktualisiert -> Wandlerfrequenz = 2 x Samplerate).
 * Mono:   Modus SIMUL  (jedes Byte geht auf beide Kanaele). */
#include <stdlib.h>
#include "driver/dac_continuous.h"
#include "esp_check.h"
#include "esp_log.h"
#include "irm_out.h"
#include "irm_pcm.h"

#define CHUNK_FRAMES 256
static const char *TAG = "irm_out_dac";
#define BUF_SIZE 2048                 /* Bytes je DMA-Deskriptor */

typedef struct {
    irm_out_sink_t base;
    dac_continuous_handle_t dac;
    bool stereo;
    uint32_t dither;
    int16_t pcm[2 * CHUNK_FRAMES];
    uint8_t u8[2 * CHUNK_FRAMES];
} dac_sink_t;

static esp_err_t dac_open(irm_out_sink_t *s) { return dac_continuous_enable(((dac_sink_t *)s)->dac); }
static void dac_close(irm_out_sink_t *s) { dac_continuous_disable(((dac_sink_t *)s)->dac); }

static esp_err_t dac_write(irm_out_sink_t *s, const int16_t *stereo, size_t frames)
{
    dac_sink_t *d = (dac_sink_t *)s;
    while (frames) {
        size_t n = frames > CHUNK_FRAMES ? CHUNK_FRAMES : frames;
        size_t samples;
        if (d->stereo) {
            irm_pcm_scale(d->pcm, stereo, 2 * n, s->gain_q15);
            samples = 2 * n;
        } else {
            irm_pcm_downmix_mono(d->pcm, stereo, n, s->gain_q15);
            samples = n;
        }
        irm_pcm_to_u8(d->u8, d->pcm, samples, &d->dither);
        size_t off = 0;
        while (off < samples) {
            size_t w = 0;
            ESP_RETURN_ON_ERROR(dac_continuous_write(d->dac, d->u8 + off, samples - off, &w, 1000), TAG, "write");
            off += w;
        }
        stereo += 2 * n; frames -= n;
    }
    return ESP_OK;
}

esp_err_t irm_out_dac_create(bool stereo, uint32_t rate, uint32_t buffer_ms, irm_out_sink_t **out)
{
    /* Puffertiefe: Bytes je Sekunde = Wandlerrate x Kanaele (8 Bit je Kanal) */
    uint32_t bytes_per_s = stereo ? rate * 2 : rate;   /* ALTER: L,R,L,R mit 2 x Rate; SIMUL: ein Byte je Frame */
    uint32_t DESC_NUM = (uint32_t)(((uint64_t)bytes_per_s * buffer_ms / 1000u + BUF_SIZE - 1) / BUF_SIZE);
    if (DESC_NUM < 3) DESC_NUM = 3;
    if (DESC_NUM > 64) DESC_NUM = 64;
    dac_sink_t *d = calloc(1, sizeof(*d));
    ESP_RETURN_ON_FALSE(d, ESP_ERR_NO_MEM, TAG, "mem");
    d->stereo = stereo;
    d->dither = 1;
    const dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = DESC_NUM,
        .buf_size = BUF_SIZE,
        .freq_hz = stereo ? rate * 2 : rate,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = stereo ? DAC_CHANNEL_MODE_ALTER : DAC_CHANNEL_MODE_SIMUL,
    };
    esp_err_t e = dac_continuous_new_channels(&cfg, &d->dac);
    if (e != ESP_OK) { free(d); return e; }
    d->base = (irm_out_sink_t){ .name = "dac", .open = dac_open, .write = dac_write, .close = dac_close, .gain_q15 = IRM_PCM_UNITY,
                                .sample_rate = rate, .buffer_ms = (uint32_t)((uint64_t)DESC_NUM * BUF_SIZE * 1000u / bytes_per_s) };
    *out = &d->base;
    return ESP_OK;
}
