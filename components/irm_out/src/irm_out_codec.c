/* Codec-Ausgabe (ES8388, ES8311, I2S-DAC ueber esp_codec_dev). Ersetzt das GMF-Element io_codec_dev, damit alle Ausgaben dasselbe IO
 * (irm_io_sink) und damit dieselbe Aussetzer-Messung nutzen. Die Lautstaerke regelt der Codec selbst (esp_codec_dev_set_out_vol). */
#include <stdlib.h>
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "irm_out.h"
#include "irm_pcm.h"

static const char *TAG = "irm_out_codec";

typedef struct { irm_out_sink_t base; esp_codec_dev_handle_t dev; } codec_sink_t;

static esp_err_t codec_open(irm_out_sink_t *s) { (void)s; return ESP_OK; }   /* Codec bleibt dauerhaft geoeffnet (irm_board) */
static void codec_close(irm_out_sink_t *s) { (void)s; }

static esp_err_t codec_write(irm_out_sink_t *s, const int16_t *stereo, size_t frames)
{
    codec_sink_t *c = (codec_sink_t *)s;
    size_t bytes = frames * 2 * sizeof(int16_t);
    int r = esp_codec_dev_write(c->dev, (void *)stereo, (int)bytes);
    return r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t irm_out_codec_create(void *dev, uint32_t rate, uint32_t buffer_ms, irm_out_sink_t **out)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "codec");
    codec_sink_t *c = calloc(1, sizeof(*c));
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "mem");
    c->dev = (esp_codec_dev_handle_t)dev;
    c->base = (irm_out_sink_t){ .name = "codec", .open = codec_open, .write = codec_write, .close = codec_close,
                                .gain_q15 = IRM_PCM_UNITY, .sample_rate = rate, .buffer_ms = buffer_ms };
    *out = &c->base;
    return ESP_OK;
}
