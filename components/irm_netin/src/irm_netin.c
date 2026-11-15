#include "irm_netin.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "irm_core.h"
#include "irm_netin_policy.h"
#include "irm_resamp.h"

static const char *TAG = "irm_netin";

#define EV_WAKE         BIT0
#define STOP_WAIT_MS    2500
#define CHUNK           240                 /* Eingangsframes je Ausgabeschritt (5 ms bei 48 kHz) */
#define RX_MAX          1500                /* groesstes Datagramm / Lesegroesse bei TCP */
#define OUT_TASK_STACK  4096
#define NET_TASK_STACK  4096
#define SRC_RATE        CONFIG_IRM_NETIN_RATE
#define TIMEOUT_MS      CONFIG_IRM_NETIN_SENDER_TIMEOUT_MS

#ifndef CONFIG_IRM_NETIN_OUT_TASK_CORE
#define CONFIG_IRM_NETIN_OUT_TASK_CORE (-1)
#endif
#define OUT_CORE ((CONFIG_IRM_NETIN_OUT_TASK_CORE) < 0 ? tskNO_AFFINITY : (CONFIG_IRM_NETIN_OUT_TASK_CORE))

static struct {
    bool               inited;
    irm_out_sink_t    *out;
    irm_netin_cb_t     cb;
    void              *cb_ctx;
    SemaphoreHandle_t  lock;
    EventGroupHandle_t ev;
    volatile uint32_t  gen;          /* wird bei jedem play()/stop() erhoeht; eine laufende Sitzung endet, sobald sie es bemerkt */
    volatile bool      want;

    /* Ausgabestufe (nur zwischen out_start und out_stop gueltig) */
    volatile bool      out_active, out_run, out_done;
    volatile bool      flush;        /* neuer Absender: Reste des alten verwerfen und neu vorpuffern (Netz-Task setzt, Ausgabe-Task arbeitet ab; flush_until unter lock) */
    uint32_t           flush_until;  /* verworfen wird nur, was bis zu diesem Wert von fed im Puffer war - nicht schon Daten des neuen Absenders */
    volatile uint32_t  fed, consumed;   /* Byte-Zaehler (Wrap-sicher): in den Puffer geschrieben (Netz-Task) / entnommen (Ausgabe-Task) */
    volatile bool      sender_active;
    StreamBufferHandle_t sb;
    int16_t           *obuf;
    size_t             obuf_frames;
    irm_resamp_st_t    rs;
    irm_resamp_tab_t   lp;           /* Tiefpass-Tabelle bei Abwaertsabtastung (ca. 3 KB) */
    int16_t            in[CHUNK * 2];
    size_t             prebuf_bytes;
    volatile uint32_t  rx_bytes, overrun_bytes, underruns, denied, write_errors;
    volatile int32_t   avg_fill_ms, trim_ppm;

    /* Netz-Task */
    uint8_t            rx[RX_MAX + 4];
    uint32_t           net_ip, net_mask;     /* Zwischenspeicher der Schnittstellenadresse */
    int64_t            net_ip_ms;
} s;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void emit(irm_netin_evt_type_t t, int err)
{
    if (!s.cb) return;
    const irm_netin_evt_t e = { .type = t, .err = err };
    s.cb(&e, s.cb_ctx);
}

typedef struct { char peer[16]; uint32_t underruns; } pub_t;
static void mut_net(irm_status_t *st, void *ctx)
{
    const pub_t *p = ctx;
    strlcpy(st->net_peer, p->peer, sizeof(st->net_peer));
    st->net_underruns = p->underruns;
}
static void publish_net(const char *peer, uint32_t underruns)
{
    pub_t p = { .underruns = underruns };
    strlcpy(p.peer, peer, sizeof(p.peer));
    irm_status_update(mut_net, &p);
}

/* ---------------------------------------------------------------- Ausgabestufe */

static const int16_t s_zero[256 * 2];   /* Stille, solange nichts ankommt; das blockierende Schreiben gibt den Takt vor */

static void out_task(void *arg)
{
    (void)arg;
    const int32_t target_ms = CONFIG_IRM_NETIN_PREBUFFER_MS;
    int32_t avg_q8 = target_ms << 8;
    bool prebuf = true;

    while (s.out_run) {
        if (s.flush) {                               /* neuer Absender: Reste des alten verwerfen */
            xSemaphoreTake(s.lock, portMAX_DELAY);
            s.flush = false;
            uint32_t need = s.flush_until - s.consumed;     /* Wrap-sicher; Werte > 2^31 = schon alles entnommen */
            xSemaphoreGive(s.lock);
            if (need > 0x7FFFFFFFu) need = 0;
            while (need > 0) {
                const size_t got = xStreamBufferReceive(s.sb, s.in, need < sizeof(s.in) ? need : sizeof(s.in), 0);
                if (got == 0) break;
                s.consumed += (uint32_t)got;
                need -= (uint32_t)got;
            }
            irm_resamp_st_reset(&s.rs);                  /* nur Zustand, Tabelle bleibt (neu berechnen waere ein spuerbarer Aussetzer) */
            prebuf = true;
        }
        const size_t fill = xStreamBufferBytesAvailable(s.sb);
        if (prebuf) {
            if (fill < s.prebuf_bytes) {
                if (s.out->write(s.out, s_zero, 256) != ESP_OK) { s.write_errors++; vTaskDelay(1); }
                continue;
            }
            prebuf = false;
            avg_q8 = target_ms << 8;
        }
        const size_t got = xStreamBufferReceive(s.sb, s.in, sizeof(s.in), 0);   /* immer ein Vielfaches von 4 Byte: der Netz-Task schreibt nur ganze Frames */
        s.consumed += (uint32_t)got;
        const size_t nf = got / 4;
        if (nf == 0) {                               /* Unterlauf: neu vorpuffern (Stille), Resampler-Zustand bleibt */
            if (s.sender_active) s.underruns++;
            prebuf = true;
            continue;
        }
        const int32_t fill_ms = (int32_t)(((uint64_t)fill * 1000u) / ((uint64_t)SRC_RATE * 4u));
        const int32_t avg = irm_resamp_avg_update(&avg_q8, fill_ms);
        const int32_t ppm = irm_resamp_trim_ppm(avg - target_ms);
        irm_resamp_st_set_trim_ppm(&s.rs, ppm);
        s.avg_fill_ms = avg;
        s.trim_ppm = ppm;

        size_t off = 0;
        while (off < nf && s.out_run) {
            size_t used = 0;
            const size_t o = irm_resamp_st_process(&s.rs, s.in + 2 * off, nf - off, &used, s.obuf, s.obuf_frames, 32768);
            if (o && s.out->write(s.out, s.obuf, o) != ESP_OK) { s.write_errors++; vTaskDelay(1); }
            off += used;
            if (used == 0 && o == 0) break;          /* Schutz vor Endlosschleife */
        }
    }
    s.out_done = true;
    vTaskDelete(NULL);
}

static int out_start(void)
{
    s.prebuf_bytes = (size_t)SRC_RATE * 4u * CONFIG_IRM_NETIN_PREBUFFER_MS / 1000u;
    s.sb = xStreamBufferCreate((size_t)SRC_RATE * 4u * CONFIG_IRM_NETIN_BUFFER_MS / 1000u, 1);
    s.obuf_frames = irm_resamp_max_frames((uint32_t)SRC_RATE * 1000u, s.out->sample_rate, CHUNK);
    s.obuf = malloc(s.obuf_frames * 2 * sizeof(int16_t));
    if (!s.sb || !s.obuf) goto fail;
    irm_resamp_st_init(&s.rs, &s.lp, (uint32_t)SRC_RATE * 1000u, s.out->sample_rate);
    s.rx_bytes = s.overrun_bytes = s.underruns = s.denied = s.write_errors = 0;
    s.avg_fill_ms = CONFIG_IRM_NETIN_PREBUFFER_MS;
    s.trim_ppm = 0;
    s.flush = false; s.sender_active = false; s.fed = s.consumed = 0; s.flush_until = 0;
    if (s.out->open(s.out) != ESP_OK) { ESP_LOGE(TAG, "Ausgabesenke laesst sich nicht oeffnen"); goto fail; }
    s.out_active = true; s.out_run = true; s.out_done = false;
    if (xTaskCreatePinnedToCore(out_task, "irm_netin_out", OUT_TASK_STACK, NULL, CONFIG_IRM_NETIN_OUT_TASK_PRIO, NULL, OUT_CORE) != pdPASS) {
        s.out_run = false;
        s.out->close(s.out);
        s.out_active = false;
        goto fail;
    }
    ESP_LOGI(TAG, "Eingang: %u Hz stereo s16le -> %u Hz, Puffer %d/%d ms", (unsigned)SRC_RATE, (unsigned)s.out->sample_rate,
             CONFIG_IRM_NETIN_PREBUFFER_MS, CONFIG_IRM_NETIN_BUFFER_MS);
    return 0;
fail:
    if (s.sb) { vStreamBufferDelete(s.sb); s.sb = NULL; }
    free(s.obuf); s.obuf = NULL;
    return -1;
}

static void out_stop(void)
{
    if (!s.out_active) return;
    s.out_run = false;
    for (int i = 0; i < 300 && !s.out_done; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (!s.out_done) { ESP_LOGE(TAG, "Ausgabe-Task reagiert nicht - Senke bleibt belegt"); return; }
    s.out->close(s.out);
    vStreamBufferDelete(s.sb); s.sb = NULL;
    free(s.obuf); s.obuf = NULL;
    ESP_LOGI(TAG, "beendet: %u Byte empfangen, verworfen %u, Unterlaeufe %u, abgelehnt %u, Schreibfehler %u",
             (unsigned)s.rx_bytes, (unsigned)s.overrun_bytes, (unsigned)s.underruns, (unsigned)s.denied, (unsigned)s.write_errors);
    s.out_active = false;
}

/* ---------------------------------------------------------------- Netz-Task */

static bool local_net(uint32_t *ip, uint32_t *mask, int64_t now)
{
    if (now - s.net_ip_ms >= 1000 || s.net_ip_ms == 0) {
        s.net_ip = s.net_mask = 0;
        esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t i;
        if (n && esp_netif_get_ip_info(n, &i) == ESP_OK) { s.net_ip = ntohl(i.ip.addr); s.net_mask = ntohl(i.netmask.addr); }
        s.net_ip_ms = now ? now : 1;
    }
    *ip = s.net_ip; *mask = s.net_mask;
    return s.net_ip != 0;
}

/* true: Daten dieses Absenders annehmen. *is_new: neuer Absender -> Puffer leeren. */
static bool admit(irm_netin_lease_t *l, uint32_t peer, int64_t now, bool *is_new)
{
    uint32_t lip, mask;
    if (!local_net(&lip, &mask, now) || !irm_netin_peer_allowed(peer, lip, mask)) { s.denied++; return false; }
    const irm_netin_admit_t a = irm_netin_lease_admit(l, peer, now, TIMEOUT_MS);
    if (a == IRM_NETIN_DENY) { s.denied++; return false; }
    *is_new = (a == IRM_NETIN_ADMIT_NEW);
    return true;
}

static void on_new_sender(uint32_t peer, char *peer_str)
{
    irm_netin_ip_str(peer, peer_str, 16);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.flush_until = s.fed;                  /* alles bisher Gepufferte gehoert dem alten Absender */
    s.flush = true;
    xSemaphoreGive(s.lock);
    s.sender_active = true;
    publish_net(peer_str, s.underruns);
    ESP_LOGI(TAG, "Absender %s", peer_str);
}

/* n Bytes (Vielfaches von 4) in den Jitterpuffer. Der Netz-Task ist der einzige Schreiber, die Pruefung des freien Platzes ist daher verlaesslich. */
static void feed(const uint8_t *p, size_t n)
{
    if (!n) return;
    if (xStreamBufferSpacesAvailable(s.sb) < n) { s.overrun_bytes += (uint32_t)n; return; }
    xStreamBufferSend(s.sb, p, n, 0);
    s.fed += (uint32_t)n;
    s.rx_bytes += (uint32_t)n;
}

static int make_socket(int type, uint16_t port)
{
    const int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || (type == SOCK_STREAM && listen(fd, 1) != 0)) { const int e = errno; close(fd); errno = e; return -1; }
    return fd;
}

/* Eine Sitzung. Rueckgabe: 0 = beendet, weil play()/stop() es verlangt hat; < 0 = Fehler (errno oder -1). */
static int run_session(uint32_t gen)
{
    int err = 0, usock = -1, lsock = -1, csock = -1;
    irm_netin_lease_t lease = { 0 };
    char peer_str[16] = "";
    uint8_t carry[4]; size_t ncarry = 0;
    uint32_t csock_peer = 0;
    s.net_ip_ms = 0;

    if (out_start() != 0) return -1;
    usock = make_socket(SOCK_DGRAM, CONFIG_IRM_NETIN_PORT);
    if (usock < 0) { err = errno ? errno : -1; ESP_LOGE(TAG, "UDP-Port %d nicht bindbar (%d)", CONFIG_IRM_NETIN_PORT, err); goto done; }
    lsock = make_socket(SOCK_STREAM, CONFIG_IRM_NETIN_PORT);
    if (lsock < 0) { err = errno ? errno : -1; ESP_LOGE(TAG, "TCP-Port %d nicht bindbar (%d)", CONFIG_IRM_NETIN_PORT, err); goto done; }
    publish_net("", 0);
    ESP_LOGI(TAG, "bereit: UDP/TCP Port %d", CONFIG_IRM_NETIN_PORT);
    emit(IRM_NETIN_EVT_RUNNING, 0);

    int64_t t_stats = now_ms(), t_pub = t_stats;
    uint32_t pub_underruns = 0;
    while (gen == s.gen) {
        fd_set rd; FD_ZERO(&rd);
        int mx = usock > lsock ? usock : lsock;
        FD_SET(usock, &rd); FD_SET(lsock, &rd);
        const bool room = xStreamBufferSpacesAvailable(s.sb) >= (size_t)RX_MAX;
        if (csock >= 0 && room) { FD_SET(csock, &rd); if (csock > mx) mx = csock; }
        struct timeval tv = { 0, (csock >= 0 && !room) ? 10000 : 100000 };    /* TCP-Rueckstau: kurz warten, bis die Ausgabe Platz schafft */
        const int r = select(mx + 1, &rd, NULL, NULL, &tv);
        const int64_t t = now_ms();
        if (r < 0) { if (errno == EINTR) continue; err = errno; ESP_LOGE(TAG, "select: %d", err); break; }

        if (r > 0 && FD_ISSET(usock, &rd)) {
            for (int k = 0; k < 8; k++) {             /* mehrere Pakete je Durchlauf, aber den Rest des Tasks nicht aushungern */
                struct sockaddr_in from; socklen_t fl = sizeof(from);
                const int n = recvfrom(usock, s.rx, RX_MAX, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
                if (n <= 0) break;
                const uint32_t peer = ntohl(from.sin_addr.s_addr);
                bool is_new = false;
                if (!admit(&lease, peer, t, &is_new)) continue;
                if (is_new) on_new_sender(peer, peer_str);
                feed(s.rx, (size_t)n & ~(size_t)3);   /* ein Datagramm umfasst ganze Frames; ein krummer Rest wird verworfen */
            }
        }
        if (r > 0 && FD_ISSET(lsock, &rd)) {
            struct sockaddr_in from; socklen_t fl = sizeof(from);
            const int c = accept(lsock, (struct sockaddr *)&from, &fl);
            if (c >= 0) {
                const uint32_t peer = ntohl(from.sin_addr.s_addr);
                bool is_new = false;
                if (admit(&lease, peer, t, &is_new)) {
                    if (csock >= 0) close(csock);
                    csock = c; csock_peer = peer; ncarry = 0;
                    if (is_new) on_new_sender(peer, peer_str);
                } else close(c);
            }
        }
        if (csock >= 0 && r > 0 && FD_ISSET(csock, &rd)) {
            memcpy(s.rx, carry, ncarry);
            const int n = recv(csock, s.rx + ncarry, RX_MAX - ncarry, MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)) {   /* Gegenstelle hat geschlossen */
                close(csock); csock = -1; ncarry = 0;
                if (lease.active && lease.ip == csock_peer) { irm_netin_lease_release(&lease); s.sender_active = false; peer_str[0] = 0; publish_net("", s.underruns); }
            } else if (n > 0) {
                bool is_new = false;
                if (admit(&lease, csock_peer, t, &is_new)) {
                    if (is_new) on_new_sender(csock_peer, peer_str);
                    const size_t total = ncarry + (size_t)n, al = total & ~(size_t)3;
                    feed(s.rx, al);
                    ncarry = total - al;
                    memcpy(carry, s.rx + al, ncarry);
                }
            }
        }
        if (irm_netin_lease_expired(&lease, t, TIMEOUT_MS)) {      /* Absender still: Miete freigeben, damit ein anderer senden darf */
            ESP_LOGI(TAG, "Absender %s ist verstummt", peer_str);
            irm_netin_lease_release(&lease);
            s.sender_active = false;
            peer_str[0] = 0;
            if (csock >= 0) { close(csock); csock = -1; ncarry = 0; }
            publish_net("", s.underruns);
        }
        if (s.underruns != pub_underruns && t - t_pub >= 1000) { pub_underruns = s.underruns; t_pub = t; publish_net(peer_str, pub_underruns); }
        if (CONFIG_IRM_NETIN_STATS_S > 0 && t - t_stats >= CONFIG_IRM_NETIN_STATS_S * 1000) {
            t_stats = t;
            ESP_LOGI(TAG, "Puffer %d ms (Trim %+d ppm), empfangen %u, verworfen %u, Unterlaeufe %u, abgelehnt %u",
                     (int)s.avg_fill_ms, (int)s.trim_ppm, (unsigned)s.rx_bytes, (unsigned)s.overrun_bytes, (unsigned)s.underruns, (unsigned)s.denied);
        }
    }
done:
    if (csock >= 0) close(csock);
    if (lsock >= 0) close(lsock);
    if (usock >= 0) close(usock);
    s.sender_active = false;
    out_stop();
    publish_net("", 0);
    if (gen != s.gen) return 0;
    return err ? -abs(err) : -1;
}

static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        xEventGroupWaitBits(s.ev, EV_WAKE, pdTRUE, pdFALSE, portMAX_DELAY);
        for (;;) {
            xSemaphoreTake(s.lock, portMAX_DELAY);
            const bool want = s.want;
            const uint32_t gen = s.gen;
            xSemaphoreGive(s.lock);
            if (!want) break;
            const int e = run_session(gen);
            if (e < 0) {
                xSemaphoreTake(s.lock, portMAX_DELAY);
                const bool current = (gen == s.gen);
                if (current) s.want = false;   /* Wiederholen entscheidet irm_control (Backoff) */
                xSemaphoreGive(s.lock);
                if (current) { ESP_LOGW(TAG, "Sitzung beendet (%d)", e); emit(IRM_NETIN_EVT_ERROR, e); }
            }
        }
    }
}

/* ---------------------------------------------------------------- API */

esp_err_t irm_netin_init(irm_out_sink_t *sink, irm_netin_cb_t cb, void *ctx)
{
    if (s.inited) return ESP_OK;
    if (!sink) return ESP_ERR_INVALID_ARG;
    s.out = sink; s.cb = cb; s.cb_ctx = ctx;
    s.lock = xSemaphoreCreateMutex();
    s.ev = xEventGroupCreate();
    if (!s.lock || !s.ev) return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(net_task, "irm_netin", NET_TASK_STACK, NULL, CONFIG_IRM_NETIN_NET_TASK_PRIO, NULL, tskNO_AFFINITY) != pdPASS)
        return ESP_ERR_NO_MEM;
    s.inited = true;
    return ESP_OK;
}

esp_err_t irm_netin_play(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.want = true;
    s.gen++;
    xSemaphoreGive(s.lock);
    xEventGroupSetBits(s.ev, EV_WAKE);
    return ESP_OK;
}

esp_err_t irm_netin_stop(void)
{
    if (!s.inited) return ESP_OK;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.want = false;
    s.gen++;
    xSemaphoreGive(s.lock);
    for (int i = 0; i < STOP_WAIT_MS / 10 && s.out_active; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (s.out_active) { ESP_LOGE(TAG, "Senke nach %d ms noch belegt", STOP_WAIT_MS); return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}

void irm_netin_get_stats(irm_netin_stats_t *o)
{
    o->rx_bytes = s.rx_bytes; o->overrun_bytes = s.overrun_bytes; o->underruns = s.underruns; o->denied_packets = s.denied;
    o->fill_ms = s.avg_fill_ms; o->trim_ppm = s.trim_ppm;
}

uint16_t irm_netin_port(void) { return CONFIG_IRM_NETIN_PORT; }
uint32_t irm_netin_rate(void) { return CONFIG_IRM_NETIN_RATE; }
