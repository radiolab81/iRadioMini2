#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_gmf_alc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_task.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"

#include "irm_audio.h"
#include "irm_io_sink.h"
#include "irm_io_m4a.h"
#include "irm_mp4.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "irm_audio";

static const uint32_t k_eq_fc[IRM_EQ_BANDS] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

static struct {
    SemaphoreHandle_t      lock;
    esp_codec_dev_handle_t codec;
    irm_out_sink_t        *sink;
    esp_gmf_pool_handle_t  pool;
    esp_gmf_task_handle_t  task;
    esp_gmf_pipeline_handle_t pipe;
    irm_audio_cb_t         cb;
    void                  *cb_ctx;
    irm_eq_t               eq;
    uint8_t                volume;
    void                  *tail;       /* letztes Element (Equalizer) */
    void                  *dec;        /* Decoder-Element */
    void                  *rate;       /* Rate-Konverter-Element */
    int64_t                t_start, t_end;  /* Zeitmessung je Wiedergabe (us) */
    bool                   finished_sent;   /* FINISHED nur einmal je Wiedergabe melden */
    bool                   rate_checked;    /* Formatpruefung nur einmal je Wiedergabe */
    bool                   stopping;   /* unterdrueckt STOPPED-Events beim eigenen Abbau */
} s;

static void emit(irm_audio_evt_type_t t, const esp_gmf_info_sound_t *i)
{
    if (!s.cb) return;
    irm_audio_evt_t e = { .type = t };
    if (i) { e.sample_rate = (uint32_t)i->sample_rates; e.channels = i->channels; e.bits = i->bits; e.bitrate = (uint32_t)i->bitrate; }
    s.cb(&e, s.cb_ctx);
}

/* Selbstpruefung der Abtastraten-Umsetzung: Quellrate des Konverters muss der des Decoders entsprechen. Sonst (Info-Weitergabe zwischen den
 * GMF-Elementen ausgeblieben) korrigieren und Konverter neu oeffnen lassen. Laeuft einmal je Wiedergabe, sobald die ersten Daten das Ende erreichen. */
static void verify_rate(void)
{
    if (s.rate_checked || !s.dec || !s.rate) return;
    esp_gmf_info_sound_t di = {0};
    esp_gmf_audio_el_get_snd_info((esp_gmf_audio_element_handle_t)s.dec, &di);
    if (di.sample_rates == 0) return;     /* Decoder kennt das Format noch nicht: spaeter erneut pruefen */
    s.rate_checked = true;
    esp_ae_rate_cvt_cfg_t *rc = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG((esp_gmf_obj_handle_t)s.rate);
    if (!rc) return;
    ESP_LOGI(TAG, "Format: Decoder %u Hz/%u ch/%u bit -> Konverter %u Hz => %u Hz (Ausgabe %d Hz)",
             (unsigned)di.sample_rates, (unsigned)di.channels, (unsigned)di.bits, (unsigned)rc->src_rate, (unsigned)rc->dest_rate,
             CONFIG_IRM_AUDIO_OUT_RATE);
    if (di.sample_rates && (rc->src_rate != di.sample_rates || rc->dest_rate != CONFIG_IRM_AUDIO_OUT_RATE)) {
        ESP_LOGW(TAG, "Konverter-Quellrate %u != Decoderrate %u: korrigiere", (unsigned)rc->src_rate, (unsigned)di.sample_rates);
        rc->src_rate = di.sample_rates;
        rc->dest_rate = CONFIG_IRM_AUDIO_OUT_RATE;
        esp_gmf_rate_cvt_set_dest_rate((esp_gmf_element_handle_t)s.rate, CONFIG_IRM_AUDIO_OUT_RATE + 1);   /* erzwingt Neuoeffnen */
        esp_gmf_rate_cvt_set_dest_rate((esp_gmf_element_handle_t)s.rate, CONFIG_IRM_AUDIO_OUT_RATE);
    }
}

static esp_gmf_err_t pipeline_event(esp_gmf_event_pkt_t *pkt, void *ctx)
{
    (void)ctx;
    if (pkt->type == ESP_GMF_EVT_TYPE_REPORT_INFO && pkt->sub == ESP_GMF_INFO_SOUND && pkt->payload) {
        emit(IRM_AUDIO_EVT_INFO, (const esp_gmf_info_sound_t *)pkt->payload);
        verify_rate();
    } else if (pkt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE && pkt->from) {
        /* Zwei Quellen: Pipeline-Ereignisse (from = Pipeline: ERROR/STOPPED/FINISHED/PAUSED, Ende der ganzen Task) und
         * Element-Ereignisse des letzten Elements (RUNNING ...). FINISHED kommt von der Pipeline, nicht vom Element. */
        switch (pkt->sub) {
        case ESP_GMF_EVENT_STATE_RUNNING:  verify_rate(); emit(IRM_AUDIO_EVT_RUNNING, NULL); break;
        case ESP_GMF_EVENT_STATE_PAUSED:   emit(IRM_AUDIO_EVT_PAUSED, NULL); break;
        case ESP_GMF_EVENT_STATE_FINISHED:
            if (!s.finished_sent) { s.finished_sent = true; s.t_end = esp_timer_get_time(); emit(IRM_AUDIO_EVT_FINISHED, NULL); }   /* genau einmal je Wiedergabe */
            break;
        case ESP_GMF_EVENT_STATE_ERROR:    if (!s.t_end) s.t_end = esp_timer_get_time(); emit(IRM_AUDIO_EVT_ERROR, NULL); break;
        case ESP_GMF_EVENT_STATE_STOPPED:  if (!s.stopping) emit(IRM_AUDIO_EVT_STOPPED, NULL); break;
        default: break;
        }
    }
    return ESP_GMF_ERR_OK;
}

/* Graphik-EQ als Peak-Filterbank: 0 dB = neutral (Altcode: -13 dB als "Standard" der ADF-Bibliothek). */
static void apply_eq_locked(void)
{
    if (!s.pipe) return;
    esp_gmf_element_handle_t eq = NULL;
    if (esp_gmf_pipeline_get_el_by_name(s.pipe, "aud_eq", &eq) != ESP_GMF_ERR_OK || !eq) return;
    for (int i = 0; i < IRM_EQ_BANDS; i++) {
        esp_ae_eq_filter_para_t p = {
            .filter_type = ESP_AE_EQ_FILTER_PEAK, .fc = k_eq_fc[i],
            .q = CONFIG_IRM_EQ_Q / 100.0f, .gain = (float)s.eq.gain_db[i],
        };
        esp_gmf_eq_set_para(eq, (uint8_t)i, &p);
        esp_gmf_eq_enable_filter(eq, (uint8_t)i, s.eq.gain_db[i] != 0);
    }
}

static void destroy_pipe_locked(void)
{
    if (!s.pipe) return;
    s.stopping = true;
    esp_gmf_pipeline_stop(s.pipe);
    /* Reihenfolge wie in den ESP-GMF-Beispielen: erst die Task (haelt Job-Listen mit Verweisen auf die Elemente) beenden,
     * dann die Pipeline zerstoeren. Eine langlebige, wiederverwendete Task fuehrt zu Zugriffen auf freigegebene Elemente. */
    if (s.task) {
        esp_gmf_task_deinit(s.task);
        s.task = NULL;
    }
    esp_gmf_pipeline_destroy(s.pipe);
    s.pipe = NULL;
    s.tail = NULL;
    s.dec = NULL;
    s.rate = NULL;
    s.stopping = false;
}

/* HTTP-IO nutzt fest 30 s Timeout; vor jeder Anfrage auf den konfigurierten Wert setzen (schnellere Wiederverbindung). */
static int http_hook(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_PRE_REQUEST && msg->http_client) {
        esp_http_client_set_timeout_ms((esp_http_client_handle_t)msg->http_client, CONFIG_IRM_HTTP_TIMEOUT_MS);
    }
    return 0;
}

esp_err_t irm_audio_init(esp_codec_dev_handle_t codec, irm_out_sink_t *sink, irm_audio_cb_t cb, void *ctx)
{
    ESP_RETURN_ON_FALSE(sink, ESP_ERR_INVALID_ARG, TAG, "Ausgabesenke fehlt");
    s.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s.lock, ESP_ERR_NO_MEM, TAG, "mutex");
    s.codec = codec; s.sink = sink; s.cb = cb; s.cb_ctx = ctx;

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    ESP_RETURN_ON_FALSE(esp_gmf_pool_init(&s.pool) == ESP_GMF_ERR_OK, ESP_FAIL, TAG, "pool");

    http_io_cfg_t http = HTTP_STREAM_CFG_DEFAULT();
    http.dir = ESP_GMF_IO_DIR_READER;
    http.event_handle = http_hook;
    http.crt_bundle_attach = esp_crt_bundle_attach;   /* TLS-Pruefung aktiv */
    esp_gmf_io_handle_t h_http = NULL;
    esp_gmf_io_http_init(&http, &h_http);
    esp_gmf_pool_register_io(s.pool, h_http, NULL);

    file_io_cfg_t file = FILE_IO_CFG_DEFAULT();
    file.dir = ESP_GMF_IO_DIR_READER;
#if CONFIG_IRM_FILE_PREFETCH_KB > 0
    /* Vorlesen aus der SD-Karte in eigener Task mit Ringpuffer: Latenzspitzen der Karte erreichen den Ausgang nicht mehr direkt */
    file.io_cfg.thread.stack = 3072;
    file.io_cfg.thread.prio = CONFIG_IRM_AUDIO_TASK_PRIO;
    file.io_cfg.thread.core = 0;
    file.io_cfg.thread.stack_in_ext = false;
    file.io_cfg.buffer_cfg.io_size = 4096;
    file.io_cfg.buffer_cfg.buffer_size = CONFIG_IRM_FILE_PREFETCH_KB * 1024;
#endif
    esp_gmf_io_handle_t h_file = NULL;
    esp_gmf_io_file_init(&file, &h_file);
    esp_gmf_pool_register_io(s.pool, h_file, NULL);

    /* Lese-IO fuer M4A mit moov hinter mdat (virtuelles "faststart") */
    irm_io_m4a_cfg_t m4a = IRM_IO_M4A_CFG_DEFAULT();
#if CONFIG_IRM_FILE_PREFETCH_KB > 0
    m4a.io_cfg.thread.stack = 3072;
    m4a.io_cfg.thread.prio = CONFIG_IRM_AUDIO_TASK_PRIO;
    m4a.io_cfg.thread.core = 0;
    m4a.io_cfg.thread.stack_in_ext = false;
    m4a.io_cfg.buffer_cfg.io_size = 4096;
    m4a.io_cfg.buffer_cfg.buffer_size = CONFIG_IRM_FILE_PREFETCH_KB * 1024;
#endif
    esp_gmf_io_handle_t h_m4a = NULL;
    if (irm_io_m4a_init(&m4a, &h_m4a) == ESP_GMF_ERR_OK) esp_gmf_pool_register_io(s.pool, h_m4a, NULL);

    esp_gmf_io_handle_t h_out = NULL;
    irm_io_sink_cfg_t out = IRM_IO_SINK_CFG_DEFAULT();
    out.sink = sink;
    irm_io_sink_init(&out, &h_out);
    ESP_RETURN_ON_FALSE(h_out, ESP_FAIL, TAG, "Ausgabe-IO");
    esp_gmf_pool_register_io(s.pool, h_out, NULL);

    esp_audio_simple_dec_cfg_t dc = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    /* Format-Erkennung: GMF behandelt "kein Typ" (NONE) als Frame-Decoder und oeffnet ihn sofort mit Typ 0 -> Fehler
     * "Fail to get default decode for 0". Nur ein Container-Typ als Ausgangspunkt aktiviert die Erkennung anhand der Stream-Header
     * (MP3, AAC/ADTS, M4A, WAV, FLAC, TS, OGG ...); MP3 ist der Standard, falls nichts erkannt wird (wie im Simple Player). */
    dc.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dc.use_frame_dec = false;
    esp_gmf_element_handle_t el = NULL;
    esp_gmf_audio_dec_init(&dc, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);

    /* Ausgabeformat fest: Rate/Kanaele/Bits wie am Codec geoeffnet */
    esp_ae_rate_cvt_cfg_t rc = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    rc.dest_rate = CONFIG_IRM_AUDIO_OUT_RATE;
    esp_gmf_rate_cvt_init(&rc, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);

    esp_ae_ch_cvt_cfg_t cc = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    cc.dest_ch = 2;
    esp_gmf_ch_cvt_init(&cc, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);

    esp_ae_bit_cvt_cfg_t bc = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    bc.dest_bits = 16;
    esp_gmf_bit_cvt_init(&bc, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);

#if CONFIG_IRM_PRE_GAIN_DB != 0
    /* Pegelreserve (Headroom) vor Equalizer und Ausgabe: verhindert Uebersteuern lauter Aufnahmen */
    esp_ae_alc_cfg_t ac = DEFAULT_ESP_GMF_ALC_CONFIG();
    ac.sample_rate = CONFIG_IRM_AUDIO_OUT_RATE;
    esp_gmf_alc_init(&ac, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);
#endif

    esp_ae_eq_cfg_t ec = DEFAULT_ESP_GMF_EQ_CONFIG();
    ec.sample_rate = CONFIG_IRM_AUDIO_OUT_RATE;
    ec.filter_num = IRM_EQ_BANDS;
    esp_ae_eq_filter_para_t init[IRM_EQ_BANDS];
    for (int i = 0; i < IRM_EQ_BANDS; i++) {
        init[i] = (esp_ae_eq_filter_para_t){ .filter_type = ESP_AE_EQ_FILTER_PEAK, .fc = k_eq_fc[i], .q = CONFIG_IRM_EQ_Q / 100.0f, .gain = 0 };
    }
    ec.para = init;
    esp_gmf_eq_init(&ec, &el);
    esp_gmf_pool_register_element(s.pool, el, NULL);

    esp_log_level_set("ESP_GMF_EQ", ESP_LOG_WARN);   /* sonst je Band eine INFO-Zeile bei jedem Pipeline-Aufbau */
    return ESP_OK;
}

esp_err_t irm_audio_play(const char *uri)
{
    ESP_RETURN_ON_FALSE(uri && uri[0] && s.pool, ESP_ERR_INVALID_ARG, TAG, "uri");
    char *in_tag = NULL;   /* Zeiger auf den Tag des im Pool registrierten IO - gehoert GMF, NICHT freigeben */
    xSemaphoreTake(s.lock, portMAX_DELAY);
    destroy_pipe_locked();
    ESP_LOGI(TAG, "Start: %s | RAM frei %u KiB, PSRAM frei %u KiB (groesster Block %u KiB)", uri,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));

    esp_err_t ret = ESP_FAIL;
    if (esp_gmf_pool_get_io_tag_by_url(s.pool, uri, ESP_GMF_IO_DIR_READER, &in_tag) != ESP_GMF_ERR_OK || !in_tag) {
        ESP_LOGE(TAG, "keine passende Eingabe fuer %s", uri);
        goto out;
    }
    if (uri[0] == '/' && irm_mp4_has_mp4_extension(uri)) {
        bool needs = false;
        if (irm_mp4_probe(uri, &needs) == 0 && needs) {
            ESP_LOGI(TAG, "%s: moov hinter mdat -> virtuelles Faststart-Lesen", uri);
            in_tag = (char *)IRM_IO_M4A_TAG;   /* Zeiger auf Konstante, gehoert nicht uns */
        }
    }
    const char *names[] = { "aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt",
#if CONFIG_IRM_PRE_GAIN_DB != 0
                            "aud_alc",
#endif
                            "aud_eq" };
    const char *out_tag = IRM_IO_SINK_TAG;
    if (esp_gmf_pool_new_pipeline(s.pool, in_tag, names, sizeof(names) / sizeof(names[0]), out_tag, &s.pipe) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Pipeline konnte nicht erzeugt werden");
        s.pipe = NULL;
        goto out;
    }
    esp_gmf_pipeline_set_event(s.pipe, pipeline_event, NULL);
    esp_gmf_pipeline_set_in_uri(s.pipe, uri);
    esp_gmf_task_cfg_t tc = DEFAULT_ESP_GMF_TASK_CONFIG();
    tc.name = "irm_pipe";
    tc.thread.stack = CONFIG_IRM_AUDIO_TASK_STACK;
    tc.thread.core = CONFIG_IRM_AUDIO_TASK_CORE;
    tc.thread.prio = CONFIG_IRM_AUDIO_TASK_PRIO;   /* GMF-Standard 5 = gleich wie der HTTP-Server */
    if (esp_gmf_task_init(&tc, &s.task) != ESP_GMF_ERR_OK) { s.task = NULL; destroy_pipe_locked(); goto out; }
    esp_gmf_pipeline_bind_task(s.pipe, s.task);
    esp_gmf_pipeline_loading_jobs(s.pipe);
    esp_gmf_pipeline_get_el_by_name(s.pipe, "aud_eq", (esp_gmf_element_handle_t *)&s.tail);
    esp_gmf_pipeline_get_el_by_name(s.pipe, "aud_dec", (esp_gmf_element_handle_t *)&s.dec);
    esp_gmf_pipeline_get_el_by_name(s.pipe, "aud_rate_cvt", (esp_gmf_element_handle_t *)&s.rate);
#if CONFIG_IRM_PRE_GAIN_DB != 0
    {
        esp_gmf_element_handle_t alc = NULL;
        if (esp_gmf_pipeline_get_el_by_name(s.pipe, "aud_alc", &alc) == ESP_GMF_ERR_OK && alc) {
            esp_gmf_alc_set_gain(alc, 0, CONFIG_IRM_PRE_GAIN_DB);
            esp_gmf_alc_set_gain(alc, 1, CONFIG_IRM_PRE_GAIN_DB);
        }
    }
#endif
    s.finished_sent = false;
    s.rate_checked = false;
    s.t_start = esp_timer_get_time();
    s.t_end = 0;
    apply_eq_locked();
    if (esp_gmf_pipeline_run(s.pipe) != ESP_GMF_ERR_OK) { destroy_pipe_locked(); goto out; }
    ret = ESP_OK;
out:
    xSemaphoreGive(s.lock);
    return ret;
}

esp_err_t irm_audio_stop(void)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    destroy_pipe_locked();
    xSemaphoreGive(s.lock);
    emit(IRM_AUDIO_EVT_STOPPED, NULL);
    return ESP_OK;
}

static esp_err_t with_pipe(esp_gmf_err_t (*fn)(esp_gmf_pipeline_handle_t))
{
    esp_err_t r = ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (s.pipe) r = (fn(s.pipe) == ESP_GMF_ERR_OK) ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(s.lock);
    return r;
}
esp_err_t irm_audio_pause(void)  { return with_pipe(esp_gmf_pipeline_pause); }
esp_err_t irm_audio_resume(void) { return with_pipe(esp_gmf_pipeline_resume); }

esp_err_t irm_audio_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s.volume = percent;
    if (!s.codec) { irm_out_set_volume(s.sink, percent); return ESP_OK; }   /* PDM/DAC: Software-Lautstaerke */
    return esp_codec_dev_set_out_vol(s.codec, percent) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t irm_audio_set_eq(const irm_eq_t *eq)
{
    ESP_RETURN_ON_FALSE(eq, ESP_ERR_INVALID_ARG, TAG, "eq");
    xSemaphoreTake(s.lock, portMAX_DELAY);
    for (int i = 0; i < IRM_EQ_BANDS; i++) {
        int g = eq->gain_db[i];
        s.eq.gain_db[i] = (int8_t)(g < IRM_EQ_MIN_DB ? IRM_EQ_MIN_DB : (g > IRM_EQ_MAX_DB ? IRM_EQ_MAX_DB : g));
    }
    apply_eq_locked();
    xSemaphoreGive(s.lock);
    return ESP_OK;
}
