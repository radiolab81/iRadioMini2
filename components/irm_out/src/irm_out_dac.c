/* Interner 8-Bit-DAC (ESP32) per dac_continuous (I2S0-DMA). Ersetzt den Legacy-"I2S DAC built-in"-Modus des Altprojekts.
 * Stereo: Modus ALTER (Bytes L,R,L,R; jeder Kanal wird mit der halben Wandlerfrequenz aktualisiert -> Wandlerfrequenz = 2 x Samplerate).
 * Mono:   Modus SIMUL  (jedes Byte geht auf beide Kanaele).
 *
 * Schreibweise: dac_continuous_write() belegt je Aufruf mindestens einen ganzen DMA-Deskriptor. Viele kleine Aufrufe lassen den DMA
 * staendig leerlaufen und neu starten (hier haengt der Treiber: "Get available descriptor timeout"). Deshalb werden die Samples
 * gesammelt und immer in vollen Deskriptor-Bloecken geschrieben (wie im IDF-Beispiel dac_audio mit grossen Bloecken). */
#include <stdlib.h>
#include <string.h>
#include "driver/dac_continuous.h"
#include "esp_check.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "sdkconfig.h"
#include "irm_out.h"
#include "irm_pcm.h"

#define CHUNK_FRAMES 256
static const char *TAG = "irm_out_dac";
#define BUF_SIZE 2048                 /* Bytes je DMA-Deskriptor */

/* Mit 16-Bit-Ausrichtung (ESP32, automatisch) belegt jedes 8-Bit-Sample 2 Byte im Deskriptor. */
#if SOC_DAC_DMA_16BIT_ALIGN
#define DMA_BYTES_PER_SAMPLE 2
#else
#define DMA_BYTES_PER_SAMPLE 1
#endif
#define BLOCK_SAMPLES (BUF_SIZE / DMA_BYTES_PER_SAMPLE)   /* Samples (Eingangsbytes) je Deskriptor */

typedef struct {
    irm_out_sink_t base;
    dac_continuous_handle_t dac;
    bool stereo;
    bool enabled;                 /* DAC bleibt nach dem ersten Oeffnen dauerhaft aktiv (kein Disable/Enable je Titel) */
    size_t fill;                  /* gesammelte Samples im Block */
    uint32_t dither;
    int16_t pcm[2 * CHUNK_FRAMES];
    uint8_t u8[2 * CHUNK_FRAMES];
    uint8_t block[BLOCK_SAMPLES];
} dac_sink_t;

static esp_err_t write_block(dac_sink_t *d, uint8_t *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        size_t w = 0;
        ESP_RETURN_ON_ERROR(dac_continuous_write(d->dac, p + off, n - off, &w, 1000), TAG, "write");
        off += w;
    }
    return ESP_OK;
}

static esp_err_t dac_open(irm_out_sink_t *s)
{
    dac_sink_t *d = (dac_sink_t *)s;
    d->fill = 0;
    if (d->enabled) return ESP_OK;
    esp_err_t e = dac_continuous_enable(d->dac);
    if (e == ESP_OK) d->enabled = true;
    return e;
}

/* Titelende: Rest des Blocks mit Stille (Mittelwert 0x80) auffuellen und noch einen Block Stille hinterher, damit der Ausgang
 * in der Mitte ruht statt auf dem letzten Sample. */
static void dac_close(irm_out_sink_t *s)
{
    dac_sink_t *d = (dac_sink_t *)s;
    if (!d->enabled) return;
    memset(d->block + d->fill, 0x80, BLOCK_SAMPLES - d->fill);
    if (write_block(d, d->block, BLOCK_SAMPLES) == ESP_OK) {
        memset(d->block, 0x80, BLOCK_SAMPLES);
        write_block(d, d->block, BLOCK_SAMPLES);
    }
    d->fill = 0;
}

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
            size_t take = BLOCK_SAMPLES - d->fill;
            if (take > samples - off) take = samples - off;
            memcpy(d->block + d->fill, d->u8 + off, take);
            d->fill += take; off += take;
            if (d->fill == BLOCK_SAMPLES) {
                ESP_RETURN_ON_ERROR(write_block(d, d->block, BLOCK_SAMPLES), TAG, "block");
                d->fill = 0;
            }
        }
        stereo += 2 * n; frames -= n;
    }
    return ESP_OK;
}

esp_err_t irm_out_dac_create(bool stereo, uint32_t rate, uint32_t buffer_ms, irm_out_sink_t **out)
{
    /* Samples je Sekunde = Wandlerrate x Kanaele (ALTER: L,R,L,R mit 2 x Rate; SIMUL: ein Byte je Frame).
     * Ein Deskriptor fasst BLOCK_SAMPLES Samples (nicht BUF_SIZE: 16-Bit-Ausrichtung). */
    uint32_t samples_per_s = stereo ? rate * 2 : rate;
    uint32_t DESC_NUM = (uint32_t)(((uint64_t)samples_per_s * buffer_ms / 1000u + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES);
    if (DESC_NUM < 4) DESC_NUM = 4;
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
                                .sample_rate = rate, .buffer_ms = (uint32_t)((uint64_t)DESC_NUM * BLOCK_SAMPLES * 1000u / samples_per_s) };
    ESP_LOGI(TAG, "DAC: %u Hz Wandlerfrequenz, %u Deskriptoren x %u Samples = %u ms Puffer", (unsigned)cfg.freq_hz, (unsigned)DESC_NUM,
             (unsigned)BLOCK_SAMPLES, (unsigned)d->base.buffer_ms);
    *out = &d->base;
    return ESP_OK;
}
