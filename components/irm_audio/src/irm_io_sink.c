#include <string.h>
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "irm_io_sink.h"

typedef struct {
    esp_gmf_io_t base;
    bool         is_open;
    /* Messung: Zeit zwischen zwei Schreibaufrufen = Arbeit der vorgelagerten Kette (Lesen, Decodieren, Umsetzen) */
    int64_t      last_end_us, win_start_us, wait_us, audio_us, max_gap_us;
    uint32_t     suspects;
    /* Signalkontrolle am Ausgang (digitaler Pegel), Gesamtlauf fuer die Echtzeit-Bilanz */
    uint32_t     clips, jumps;
    int16_t      prev[2];
    int64_t      first_us, total_audio_us, last_jump_log_us;
} io_sink_t;

static const char *TAG = "irm_io_sink";

static esp_gmf_err_t sink_new(void *cfg, esp_gmf_obj_handle_t *io) { return irm_io_sink_init(cfg, io); }

static esp_gmf_err_t sink_open(esp_gmf_io_handle_t io)
{
    io_sink_t *s = (io_sink_t *)io;
    irm_io_sink_cfg_t *cfg = (irm_io_sink_cfg_t *)OBJ_GET_CFG(s);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL;);
    ESP_GMF_NULL_CHECK(TAG, cfg->sink, return ESP_GMF_ERR_FAIL;);
    if (cfg->sink->open(cfg->sink) != ESP_OK) return ESP_GMF_ERR_FAIL;
    s->last_end_us = s->win_start_us = s->wait_us = s->audio_us = s->max_gap_us = 0;
    s->suspects = 0;
    s->clips = s->jumps = 0;
    s->prev[0] = s->prev[1] = 0;
    s->first_us = s->total_audio_us = s->last_jump_log_us = 0;
    s->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t sink_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t sink_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    io_sink_t *s = (io_sink_t *)handle;
    esp_gmf_payload_t *pl = (esp_gmf_payload_t *)payload;
    irm_io_sink_cfg_t *cfg = (irm_io_sink_cfg_t *)OBJ_GET_CFG(s);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    size_t frames = pl->valid_size / (2 * sizeof(int16_t));    /* 16 Bit, Stereo */
    const int64_t now = esp_timer_get_time();
    const uint32_t rate = cfg->sink->sample_rate ? cfg->sink->sample_rate : 48000;

    /* Luecke seit dem letzten Schreiben: so lange hat die Kette fuer diesen Block gebraucht. Ist sie laenger als der Ausgabepuffer
     * tief ist, laeuft der Puffer leer -> hoerbarer Aussetzer. */
    if (s->last_end_us) {
        int64_t gap = now - s->last_end_us;
        s->wait_us += gap;
        if (gap > s->max_gap_us) s->max_gap_us = gap;
        const int64_t buf_us = (int64_t)cfg->sink->buffer_ms * 1000;
        if (buf_us && gap > buf_us * 85 / 100) {
            s->suspects++;
            ESP_LOGW(TAG, "Pipeline lieferte %d ms lang keine Daten (Ausgabepuffer %u ms): Aussetzer moeglich",
                     (int)(gap / 1000), (unsigned)cfg->sink->buffer_ms);
        }
    }
    /* Digitale Signalkontrolle: Vollaussteuerung (Begrenzung) und grosse Spruenge zwischen Nachbarwerten (Wrap-Around/Decoderfehler) */
    {
        const int16_t *x = (const int16_t *)pl->buf;
        uint32_t jumps_here = 0; int worst = 0;
        for (size_t i = 0; i < frames * 2; i++) {
            int v = x[i];
            if (v >= 32767 || v <= -32768) s->clips++;
            int d = v - s->prev[i & 1];
            if (d < 0) d = -d;
            if (d > 30000) { jumps_here++; if (d > worst) worst = d; }
            s->prev[i & 1] = (int16_t)v;
        }
        if (jumps_here) {
            s->jumps += jumps_here;
            if (now - s->last_jump_log_us > 1000000) {   /* hoechstens 1 Meldung je Sekunde */
                s->last_jump_log_us = now;
                ESP_LOGW(TAG, "Signalsprung im Ausgangssignal (%u Stellen, groesster Sprung %d): Wrap-Around oder Decoderfehler", (unsigned)jumps_here, worst);
            }
        }
    }
    if (!s->first_us) s->first_us = now;
    if (frames && cfg->sink->write(cfg->sink, (const int16_t *)pl->buf, frames) != ESP_OK) {
        ESP_LOGE(TAG, "Senke %s: Schreiben fehlgeschlagen", cfg->sink->name);
        return ESP_GMF_IO_FAIL;
    }
    const int64_t end = esp_timer_get_time();
    s->last_end_us = end;
    s->audio_us += (int64_t)frames * 1000000 / rate;
    s->total_audio_us += (int64_t)frames * 1000000 / rate;
#if CONFIG_IRM_AUDIO_STATS_S > 0
    if (!s->win_start_us) s->win_start_us = end;
    if (end - s->win_start_us >= (int64_t)CONFIG_IRM_AUDIO_STATS_S * 1000000) {
        ESP_LOGI(TAG, "Audio: Kette belegt %d %% der Echtzeit, groesste Luecke %d ms (Puffer %u ms), Aussetzerverdacht %u | Vollaussteuerung %u, Spruenge %u",
                 s->audio_us ? (int)(s->wait_us * 100 / s->audio_us) : 0, (int)(s->max_gap_us / 1000),
                 (unsigned)cfg->sink->buffer_ms, (unsigned)s->suspects, (unsigned)s->clips, (unsigned)s->jumps);
        s->win_start_us = end; s->wait_us = s->audio_us = s->max_gap_us = 0; s->suspects = 0; s->clips = s->jumps = 0;
    }
#endif
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t sink_seek(esp_gmf_io_handle_t io, uint64_t pos) { return ESP_GMF_ERR_OK; }
static esp_gmf_err_t sink_reset(esp_gmf_io_handle_t io) { return ESP_GMF_ERR_OK; }

static esp_gmf_err_t sink_close(esp_gmf_io_handle_t io)
{
    io_sink_t *s = (io_sink_t *)io;
    irm_io_sink_cfg_t *cfg = (irm_io_sink_cfg_t *)OBJ_GET_CFG(s);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_IO_FAIL;);
    if (s->is_open) {
        if (s->first_us && s->total_audio_us > 3000000) {
            double wall = (double)(s->last_end_us - s->first_us) / 1e6, aud = (double)s->total_audio_us / 1e6;
            ESP_LOGI(TAG, "Wiedergabe: %.1f s Audio in %.1f s Wanduhrzeit (%.0f %%)%s", aud, wall, 100.0 * aud / wall,
                     aud < wall * 0.97 ? "  <-- Unterbrechungen im Datenfluss" : "");
        }
        cfg->sink->close(cfg->sink);
        s->is_open = false;
    }
    esp_gmf_io_set_pos((esp_gmf_io_handle_t)io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t sink_delete(esp_gmf_io_handle_t io)
{
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) esp_gmf_oal_free(cfg);
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(io);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t irm_io_sink_init(irm_io_sink_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, config->sink, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    io_sink_t *s = esp_gmf_oal_calloc(1, sizeof(io_sink_t));
    ESP_GMF_MEM_VERIFY(TAG, s, {return ESP_GMF_ERR_MEMORY_LACK;}, "sink io", sizeof(io_sink_t));
    s->base.dir = ESP_GMF_IO_DIR_WRITER;
    s->base.type = ESP_GMF_IO_TYPE_BYTE;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)s;
    obj->new_obj = sink_new;
    obj->del_obj = sink_delete;
    irm_io_sink_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto fail;}, "sink io cfg", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, config->name ? config->name : IRM_IO_SINK_TAG);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto fail, "tag");
    s->base.open = sink_open;
    s->base.close = sink_close;
    s->base.seek = sink_seek;
    s->base.reset = sink_reset;
    s->base.acquire_write = sink_acquire_write;
    s->base.release_write = sink_release_write;
    esp_gmf_io_cfg_t io_cfg = {
        .thread = { .stack = config->io_cfg.thread.stack, .prio = config->io_cfg.thread.prio,
                    .core = config->io_cfg.thread.core, .stack_in_ext = config->io_cfg.thread.stack_in_ext },
        .buffer_cfg = { .io_size = config->io_cfg.buffer_cfg.io_size, .buffer_size = config->io_cfg.buffer_cfg.buffer_size },
        .enable_speed_monitor = config->io_cfg.enable_speed_monitor,
    };
    ret = esp_gmf_io_init(obj, &io_cfg);
    if (ret != ESP_GMF_ERR_OK) goto fail;
    *io = obj;
    return ESP_GMF_ERR_OK;
fail:
    esp_gmf_obj_delete(obj);
    return ret;
}
