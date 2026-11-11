#include "irm_kiwi.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "irm_core.h"
#include "irm_kiwi_uri.h"
#include "irm_resamp.h"
#include "kiwi_client.h"

static const char *TAG = "irm_kiwi";

#define IN_CHUNK        128                      /* Eingangssamples je Ausgabe-Schritt (10,7 ms bei 12 kHz) */
#define EV_WAKE         BIT0
#define STOP_WAIT_MS    2500
#define OUT_TASK_STACK  4096
#define NET_TASK_STACK  7168

#ifndef CONFIG_IRM_KIWI_OUT_TASK_CORE
#define CONFIG_IRM_KIWI_OUT_TASK_CORE (-1)
#endif
#define OUT_CORE ((CONFIG_IRM_KIWI_OUT_TASK_CORE) < 0 ? tskNO_AFFINITY : (CONFIG_IRM_KIWI_OUT_TASK_CORE))

static struct {
    bool               inited;
    irm_out_sink_t    *out;
    irm_kiwi_cb_t      cb;
    void              *cb_ctx;
    SemaphoreHandle_t  lock;
    EventGroupHandle_t ev;
    audio_sink_t       ksink;
    uint32_t           gain_q15;

    /* Auftrag und Abstimmung (unter lock; gen/want werden vom Netz-Task auch lock-frei gelesen) */
    volatile uint32_t  gen;          /* wird bei jedem play()/stop() erhoeht; ein laufende Sitzung endet, sobald sie es bemerkt */
    volatile bool      want;
    irm_kiwi_uri_t     cur;          /* Server + aktuelle Abstimmung */
    char               uri[IRM_URI_MAX_LEN];
    bool               have_uri;
    bool               sess_active;  /* Verbindung steht: set_tune ist erlaubt */
    kiwi_client_t      cl;           /* ca. 7,5 KB, statisch */

    /* Ausgabestufe (nur zwischen ks_open und ks_close gueltig) */
    volatile bool      out_active;   /* Senke ist geoeffnet */
    volatile bool      out_run;
    volatile bool      out_done;
    StreamBufferHandle_t sb;
    int16_t           *obuf;
    size_t             obuf_frames;
    irm_resamp_t       rs;
    uint32_t           src_rate;
    uint32_t           prebuf_bytes;
    bool               running_sent;
    volatile uint32_t  overrun_samples, underruns, write_errors;
    volatile int32_t   avg_fill_ms, trim_ppm;
} s;

/* ---------------------------------------------------------------- Hilfen */

static void emit(irm_kiwi_evt_type_t t, uint32_t rate, uint32_t bitrate, int err)
{
    if (!s.cb) return;
    const irm_kiwi_evt_t e = { .type = t, .sample_rate = rate, .bitrate = bitrate, .err = err };
    s.cb(&e, s.cb_ctx);
}

typedef struct { uint32_t freq_hz; char mode[IRM_KIWI_MODE_MAX]; } tune_pub_t;

static void mut_tune(irm_status_t *st, void *ctx)
{
    const tune_pub_t *t = ctx;
    st->kiwi_freq_hz = t->freq_hz;
    memcpy(st->kiwi_mode, t->mode, sizeof(st->kiwi_mode) < sizeof(t->mode) ? sizeof(st->kiwi_mode) : sizeof(t->mode));
    st->kiwi_mode[sizeof(st->kiwi_mode) - 1] = 0;
}

static void mut_rssi(irm_status_t *st, void *ctx) { st->kiwi_rssi_dbm = *(const int16_t *)ctx; }

static void publish_tune(uint32_t f, const char *mode)
{
    tune_pub_t t = { .freq_hz = f };
    strlcpy(t.mode, mode, sizeof(t.mode));
    irm_status_update(mut_tune, &t);
}

static void publish_rssi(int16_t dbm) { irm_status_update(mut_rssi, &dbm); }

/* ---------------------------------------------------------------- Ausgabestufe */

static const int16_t s_zero[256 * 2];   /* Stille waehrend der Vorpufferung; das blockierende Schreiben gibt den Takt vor */

static void out_task(void *arg)
{
    (void)arg;
    int16_t in[IN_CHUNK];
    const int32_t target_ms = CONFIG_IRM_KIWI_PREBUFFER_MS;
    int32_t avg_q8 = target_ms << 8;
    bool prebuf = true;

    while (s.out_run) {
        const size_t fill = xStreamBufferBytesAvailable(s.sb);
        if (prebuf) {
            if (fill < s.prebuf_bytes) {
                if (s.out->write(s.out, s_zero, 256) != ESP_OK) { s.write_errors++; vTaskDelay(1); }
                continue;
            }
            prebuf = false;
            avg_q8 = target_ms << 8;
            if (!s.running_sent) { s.running_sent = true; emit(IRM_KIWI_EVT_RUNNING, s.src_rate, 0, 0); }
        }
        const size_t got = xStreamBufferReceive(s.sb, in, sizeof(in), 0);
        const size_t ns = got / sizeof(int16_t);
        if (ns == 0) {                       /* Unterlauf: neu vorpuffern (Stille), Resampler-Zustand bleibt */
            s.underruns++;
            prebuf = true;
            continue;
        }
        const int32_t fill_ms = (int32_t)(((uint64_t)fill * 1000u) / ((uint64_t)s.src_rate * sizeof(int16_t)));
        const int32_t avg = irm_resamp_avg_update(&avg_q8, fill_ms);
        const int32_t ppm = irm_resamp_trim_ppm(avg - target_ms);
        irm_resamp_set_trim_ppm(&s.rs, ppm);
        s.avg_fill_ms = avg;
        s.trim_ppm = ppm;

        size_t off = 0;
        while (off < ns && s.out_run) {
            size_t used = 0;
            const size_t o = irm_resamp_mono_to_stereo(&s.rs, in + off, ns - off, &used, s.obuf, s.obuf_frames, s.gain_q15);
            if (o && s.out->write(s.out, s.obuf, o) != ESP_OK) { s.write_errors++; vTaskDelay(1); }
            off += used;
            if (used == 0 && o == 0) break;   /* kann bei richtig dimensioniertem obuf nicht vorkommen; Schutz vor Endlosschleife */
        }
    }
    s.out_done = true;
    vTaskDelete(NULL);
}

/* kiwi_core-Senke: laeuft im Netz-Task */

static int ks_open(void *ctx, uint32_t rate)
{
    (void)ctx;
    uint32_t mhz = s.cl.sample_rate_mhz;
    if (mhz < rate * 990u || mhz > rate * 1010u) mhz = rate * 1000u;   /* ungemessen/unplausibel: nominell */
    s.src_rate = rate;
    s.prebuf_bytes = rate * sizeof(int16_t) * CONFIG_IRM_KIWI_PREBUFFER_MS / 1000u;
    s.sb = xStreamBufferCreate(rate * sizeof(int16_t) * CONFIG_IRM_KIWI_BUFFER_MS / 1000u, 1);
    s.obuf_frames = irm_resamp_max_frames(mhz, s.out->sample_rate, IN_CHUNK);
    s.obuf = malloc(s.obuf_frames * 2 * sizeof(int16_t));
    if (!s.sb || !s.obuf) goto fail;
    irm_resamp_init(&s.rs, mhz, s.out->sample_rate);
    s.running_sent = false;
    s.overrun_samples = s.underruns = s.write_errors = 0;
    s.avg_fill_ms = CONFIG_IRM_KIWI_PREBUFFER_MS;
    s.trim_ppm = 0;
    if (s.out->open(s.out) != ESP_OK) { ESP_LOGE(TAG, "Ausgabesenke laesst sich nicht oeffnen"); goto fail; }
    s.out_active = true;
    s.out_run = true;
    s.out_done = false;
    if (xTaskCreatePinnedToCore(out_task, "irm_kiwi_out", OUT_TASK_STACK, NULL, CONFIG_IRM_KIWI_OUT_TASK_PRIO, NULL, OUT_CORE) != pdPASS) {
        s.out_run = false;
        s.out->close(s.out);
        s.out_active = false;
        goto fail;
    }
    ESP_LOGI(TAG, "Audio: %u Hz (real %u.%03u Hz) mono -> %u Hz stereo, Puffer %d/%d ms",
             (unsigned)rate, (unsigned)(mhz / 1000), (unsigned)(mhz % 1000), (unsigned)s.out->sample_rate,
             CONFIG_IRM_KIWI_PREBUFFER_MS, CONFIG_IRM_KIWI_BUFFER_MS);
    emit(IRM_KIWI_EVT_INFO, rate,
#if CONFIG_IRM_KIWI_ADPCM
         rate * 4u,
#else
         rate * 16u,
#endif
         0);
    return 0;
fail:
    if (s.sb) { vStreamBufferDelete(s.sb); s.sb = NULL; }
    free(s.obuf); s.obuf = NULL;
    return -1;
}

static int ks_write(void *ctx, const int16_t *pcm, size_t n)
{
    (void)ctx;
    if (!s.out_run) return -1;
    const size_t bytes = n * sizeof(int16_t);
    const size_t sent = xStreamBufferSend(s.sb, pcm, bytes, 0);   /* nie blockieren: lieber Samples verwerfen als das Netz anhalten */
    if (sent < bytes) s.overrun_samples += (uint32_t)((bytes - sent) / sizeof(int16_t));
    return 0;
}

static void ks_close(void *ctx)
{
    (void)ctx;
    if (!s.out_active) return;   /* open ist nie gelungen */
    s.out_run = false;
    for (int i = 0; i < 300 && !s.out_done; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (!s.out_done) {
        ESP_LOGE(TAG, "Ausgabe-Task reagiert nicht - Senke bleibt belegt");
        return;
    }
    s.out->close(s.out);
    vStreamBufferDelete(s.sb); s.sb = NULL;
    free(s.obuf); s.obuf = NULL;
    ESP_LOGI(TAG, "Audio beendet: Ueberlaeufe %u Samples, Unterlaeufe %u, Schreibfehler %u",
             (unsigned)s.overrun_samples, (unsigned)s.underruns, (unsigned)s.write_errors);
    s.out_active = false;
}

/* ---------------------------------------------------------------- Netz-Task */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* Eine Sitzung. Rueckgabe: 0 = beendet, weil play()/stop() es verlangt hat; < 0 = Fehler (kiwi_err_t). */
static int run_session(uint32_t gen, const irm_kiwi_uri_t *c)
{
    const kiwi_config_t cfg = {
        .host = c->host, .port = c->port, .password = c->password, .user_name = CONFIG_IRM_KIWI_USER_NAME,
        .freq_hz = c->freq_hz, .mode = c->mode,
#if CONFIG_IRM_KIWI_ADPCM
        .compression = 1,
#endif
        .connect_timeout_ms = CONFIG_IRM_KIWI_CONNECT_TIMEOUT_MS,
        .sink = &s.ksink,
    };
    xSemaphoreTake(s.lock, portMAX_DELAY);
    int e = kiwi_client_init(&s.cl, &cfg);
    xSemaphoreGive(s.lock);
    if (e != KIWI_OK) return e;

    ESP_LOGI(TAG, "verbinde %s:%u, %u Hz %s", c->host, (unsigned)c->port, (unsigned)c->freq_hz, c->mode);
    e = kiwi_client_connect(&s.cl);
    if (e == KIWI_OK) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.sess_active = (gen == s.gen);
        xSemaphoreGive(s.lock);
        int64_t t_rssi = 0, t_stats = now_ms();
        int rssi_last = INT32_MIN;
        while (gen == s.gen) {
            e = kiwi_client_poll(&s.cl, 200);
            if (e != KIWI_OK) break;
            const int64_t t = now_ms();
            if (t - t_rssi >= 1000 && s.cl.st.frames > 0) {   /* hoechstens 1x/s und nur bei Aenderung ganzer dB (Display, Web) */
                t_rssi = t;
                const int v = s.cl.st.rssi_tenths_dbm;
                const int dbm = (v + (v >= 0 ? 5 : -5)) / 10;
                if (dbm != rssi_last) { rssi_last = dbm; publish_rssi((int16_t)dbm); }
            }
            if (CONFIG_IRM_KIWI_STATS_S > 0 && t - t_stats >= CONFIG_IRM_KIWI_STATS_S * 1000) {
                t_stats = t;
                ESP_LOGI(TAG, "Frames %u, verloren %u, RSSI %d dBm | Puffer %d ms (Trim %+d ppm), Ueberlauf %u, Unterlauf %u",
                         (unsigned)s.cl.st.frames, (unsigned)s.cl.st.lost_frames, rssi_last == INT32_MIN ? 0 : rssi_last,
                         (int)s.avg_fill_ms, (int)s.trim_ppm, (unsigned)s.overrun_samples, (unsigned)s.underruns);
            }
        }
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.sess_active = false;
        xSemaphoreGive(s.lock);
    }
    kiwi_client_close(&s.cl);   /* schliesst auch die Senke (ks_close) */
    if (gen != s.gen) return 0;
    return e < 0 ? e : KIWI_ERR_CLOSED;
}

static void net_task(void *arg)
{
    (void)arg;
    int64_t last_attempt = 0;
    bool have_attempt = false;
    for (;;) {
        xEventGroupWaitBits(s.ev, EV_WAKE, pdTRUE, pdFALSE, portMAX_DELAY);
        for (;;) {
            xSemaphoreTake(s.lock, portMAX_DELAY);
            const bool want = s.want;
            const uint32_t gen = s.gen;
            const irm_kiwi_uri_t c = s.cur;
            xSemaphoreGive(s.lock);
            if (!want) break;

            /* Mindestabstand zu Verbindungsversuchen; bei neuem Auftrag sofort abbrechen und neu lesen */
            while (gen == s.gen && have_attempt && now_ms() - last_attempt < CONFIG_IRM_KIWI_MIN_RECONNECT_S * 1000) vTaskDelay(pdMS_TO_TICKS(100));
            if (gen != s.gen) continue;
            last_attempt = now_ms();
            have_attempt = true;

            const int e = run_session(gen, &c);
            if (e < 0) {
                xSemaphoreTake(s.lock, portMAX_DELAY);
                const bool current = (gen == s.gen);
                if (current) s.want = false;   /* Wiederholen entscheidet irm_control (Backoff) */
                xSemaphoreGive(s.lock);
                if (current) {
                    ESP_LOGW(TAG, "Sitzung beendet: %s (%d)", kiwi_strerror(e), e);
                    emit(IRM_KIWI_EVT_ERROR, 0, 0, e);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- API */

esp_err_t irm_kiwi_init(irm_out_sink_t *sink, irm_kiwi_cb_t cb, void *ctx)
{
    if (s.inited) return ESP_OK;
    if (!sink) return ESP_ERR_INVALID_ARG;
    s.out = sink; s.cb = cb; s.cb_ctx = ctx;
    s.lock = xSemaphoreCreateMutex();
    s.ev = xEventGroupCreate();
    if (!s.lock || !s.ev) return ESP_ERR_NO_MEM;
    s.gain_q15 = (uint32_t)lrintf(32768.0f * powf(10.0f, (float)CONFIG_IRM_KIWI_GAIN_DB / 20.0f));
    s.ksink = (audio_sink_t){ .open = ks_open, .write = ks_write, .close = ks_close, .ctx = NULL };
    if (xTaskCreatePinnedToCore(net_task, "irm_kiwi", NET_TASK_STACK, NULL, CONFIG_IRM_KIWI_NET_TASK_PRIO, NULL, tskNO_AFFINITY) != pdPASS)
        return ESP_ERR_NO_MEM;
    s.inited = true;
    return ESP_OK;
}

esp_err_t irm_kiwi_play(const char *uri, bool reset_tune)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    irm_kiwi_uri_t u;
    if (!uri || !irm_kiwi_uri_parse(uri, &u)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const bool keep = !reset_tune && s.have_uri && strcmp(uri, s.uri) == 0;
    if (!keep) s.cur = u;
    strlcpy(s.uri, uri, sizeof(s.uri));
    s.have_uri = true;
    s.want = true;
    s.gen++;
    tune_pub_t t = { .freq_hz = s.cur.freq_hz };
    memcpy(t.mode, s.cur.mode, sizeof(t.mode));
    xSemaphoreGive(s.lock);
    publish_tune(t.freq_hz, t.mode);
    xEventGroupSetBits(s.ev, EV_WAKE);
    return ESP_OK;
}

esp_err_t irm_kiwi_stop(void)
{
    if (!s.inited) return ESP_OK;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.want = false;
    s.gen++;
    xSemaphoreGive(s.lock);
    /* Nur auf die Senke warten: haengt der Netz-Task noch im Verbindungsaufbau, hat er sie nie geoeffnet und beendet sich danach von selbst. */
    for (int i = 0; i < STOP_WAIT_MS / 10 && s.out_active; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s.out_active) {
        ESP_LOGE(TAG, "Senke nach %d ms noch belegt", STOP_WAIT_MS);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/* unter lock; liefert die zu veroeffentlichende Abstimmung */
static esp_err_t tune_locked(uint32_t f, const char *mode, tune_pub_t *pub)
{
    if (!s.have_uri) return ESP_ERR_INVALID_STATE;
    int mi = -1;
    if (mode) {
        mi = irm_kiwi_mode_index(mode);
        if (mi < 0) return ESP_ERR_INVALID_ARG;
    }
    if (f) {
        if (f < IRM_KIWI_FREQ_MIN_HZ) f = IRM_KIWI_FREQ_MIN_HZ;
        if (f > IRM_KIWI_FREQ_MAX_HZ) f = IRM_KIWI_FREQ_MAX_HZ;
        s.cur.freq_hz = f;
    }
    if (mi >= 0) strlcpy(s.cur.mode, irm_kiwi_mode_name((size_t)mi), sizeof(s.cur.mode));
    if (s.sess_active) {
        const int e = kiwi_client_set_tune(&s.cl, f, mi >= 0 ? s.cur.mode : NULL);
        if (e != KIWI_OK) ESP_LOGW(TAG, "Abstimmung abgelehnt: %s", kiwi_strerror(e));
    }
    pub->freq_hz = s.cur.freq_hz;
    memcpy(pub->mode, s.cur.mode, sizeof(pub->mode));
    return ESP_OK;
}

esp_err_t irm_kiwi_tune(uint32_t freq_hz, const char *mode)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    tune_pub_t p = { 0 };
    xSemaphoreTake(s.lock, portMAX_DELAY);
    const esp_err_t e = tune_locked(freq_hz, mode, &p);
    xSemaphoreGive(s.lock);
    if (e == ESP_OK) publish_tune(p.freq_hz, p.mode);
    return e;
}

esp_err_t irm_kiwi_tune_step(int32_t delta_hz)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    tune_pub_t p = { 0 };
    xSemaphoreTake(s.lock, portMAX_DELAY);
    int64_t f = (int64_t)s.cur.freq_hz + delta_hz;
    if (f < (int64_t)IRM_KIWI_FREQ_MIN_HZ) f = IRM_KIWI_FREQ_MIN_HZ;
    if (f > (int64_t)IRM_KIWI_FREQ_MAX_HZ) f = IRM_KIWI_FREQ_MAX_HZ;
    const esp_err_t e = tune_locked((uint32_t)f, NULL, &p);
    xSemaphoreGive(s.lock);
    if (e == ESP_OK) publish_tune(p.freq_hz, p.mode);
    return e;
}

esp_err_t irm_kiwi_set_mode_index(int idx)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    const int n = (int)irm_kiwi_mode_count();
    if (idx >= n) return ESP_ERR_INVALID_ARG;
    if (idx < 0) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        const int cur = irm_kiwi_mode_index(s.cur.mode);
        xSemaphoreGive(s.lock);
        idx = (cur + 1) % n;
    }
    return irm_kiwi_tune(0, irm_kiwi_mode_name((size_t)idx));
}
