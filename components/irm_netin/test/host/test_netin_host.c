/* Hinweis: Amplituden werden ueber kurze Fenster (250 ms) gemessen, weil die Driftregelung die Tonhoehe um bis zu 1000 ppm verschiebt (bei 3 kHz 3 Hz).
 * Host-Integrationstest der Engine irm_netin.c mit echten UDP/TCP-Sockets ueber Loopback (siehe Kommentar am Dateiende / README fuer den Build).
 * Die Senke ist ein Aufzeichner, der wie I2S blockiert (Echtzeit-Takt). Zweiter Absender = 127.0.0.2. */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "esp_netif.h"
#include "irm_core.h"
#include "irm_netin.h"
#include "irm_resamp.h"

#define PORT 40100
static int fails;
#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } else { printf("  FEHLER "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---------------------------------------------------------------- Fakes */
static uint32_t g_local = 0x7F000001u, g_mask = 0xFF000000u;   /* Host-Byte-Order */
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *k) { (void)k; return (esp_netif_t *)1; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *i) { (void)n; i->ip.addr = htonl(g_local); i->netmask.addr = htonl(g_mask); i->gw.addr = 0; return ESP_OK; }

static pthread_mutex_t st_m = PTHREAD_MUTEX_INITIALIZER;
static irm_status_t g_st;
void irm_status_update(irm_status_mutator_t fn, void *ctx) { pthread_mutex_lock(&st_m); fn(&g_st, ctx); pthread_mutex_unlock(&st_m); }
static void peer(char *o) { pthread_mutex_lock(&st_m); strcpy(o, g_st.net_peer); pthread_mutex_unlock(&st_m); }

static volatile int g_evt_running, g_evt_error;
static void on_evt(const irm_netin_evt_t *e, void *c) { (void)c; if (e->type == IRM_NETIN_EVT_RUNNING) g_evt_running++; else g_evt_error++; }

static int16_t *rec; static size_t rec_n, rec_cap; static volatile int sink_open; static pthread_mutex_t rec_m = PTHREAD_MUTEX_INITIALIZER;
static esp_err_t k_open(irm_out_sink_t *s) { (void)s; sink_open = 1; return ESP_OK; }
static void k_close(irm_out_sink_t *s) { (void)s; sink_open = 0; }
static esp_err_t k_write(irm_out_sink_t *s, const int16_t *st, size_t fr)
{
    pthread_mutex_lock(&rec_m);
    if (rec_n + fr > rec_cap) { rec_cap = (rec_n + fr) * 2 + 100000; rec = realloc(rec, rec_cap * 4); }
    memcpy(rec + 2 * rec_n, st, fr * 4); rec_n += fr;
    pthread_mutex_unlock(&rec_m);
    struct timespec t = { 0, (long)((uint64_t)fr * 1000000000ull / s->sample_rate) };   /* wie I2S: blockiert in Echtzeit */
    nanosleep(&t, NULL);
    return ESP_OK;
}
static irm_out_sink_t sink = { .name = "fake", .open = k_open, .write = k_write, .close = k_close, .gain_q15 = 32768, .sample_rate = 48000, .buffer_ms = 40 };

/* ---------------------------------------------------------------- Hilfen */
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static int64_t mono_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static bool wait_peer_nonempty(int ms) { char p[16]; for (int i = 0; i < ms / 10; i++) { peer(p); if (p[0]) return true; sleep_ms(10); } return false; }
static bool wait_peer(const char *want, int ms) { char p[16]; for (int i = 0; i < ms / 10; i++) { peer(p); if (!strcmp(p, want)) return true; sleep_ms(10); } return false; }

static int16_t *make_tone(size_t frames, double rate, double fl, double fr, double amp)
{
    int16_t *b = malloc(frames * 4);
    for (size_t i = 0; i < frames; i++) {
        b[2 * i] = (int16_t)lrint(amp * sin(2 * M_PI * fl * (double)i / rate));
        b[2 * i + 1] = (int16_t)lrint(amp * sin(2 * M_PI * fr * (double)i / rate));
    }
    return b;
}
static void rec_reset(void) { pthread_mutex_lock(&rec_m); rec_n = 0; pthread_mutex_unlock(&rec_m); }
static size_t first_loud(void) { for (size_t i = 0; i < rec_n; i++) if (abs(rec[2 * i]) > 200 || abs(rec[2 * i + 1]) > 200) return i; return (size_t)-1; }
static size_t count_loud(void) { size_t n = 0; for (size_t i = 0; i < rec_n; i++) if (abs(rec[2 * i]) > 50 || abs(rec[2 * i + 1]) > 50) n++; return n; }
/* Luecken (>= 5 ms Stille) innerhalb des Tons [from, to) zaehlen; gibt die Position der ersten in *first aus */
static int gaps(size_t from, size_t to, size_t *first, size_t *longest)
{
    int n = 0; size_t run = 0; *longest = 0; *first = 0;
    for (size_t i = from; i < to && i < rec_n; i++) {
        if (abs(rec[2 * i]) <= 30 && abs(rec[2 * i + 1]) <= 30) run++;
        else { if (run >= 240) { if (!n) *first = i - run; n++; } if (run > *longest) *longest = run; run = 0; }
    }
    return n;
}
static double amp_at(size_t start, size_t n, int ch, double rate, double f)
{
    double re = 0, im = 0;
    for (size_t i = start; i < start + n && i < rec_n; i++) { const double ph = 2 * M_PI * f * (double)(i - start) / rate; re += rec[2 * i + ch] * cos(ph); im -= rec[2 * i + ch] * sin(ph); }
    return 2.0 * sqrt(re * re + im * im) / (double)n;
}

typedef struct { const char *src; bool tcp; const uint8_t *data; size_t bytes; int pkt; bool realtime; double rate_bps; volatile int64_t took_ms; volatile int err; } send_t;
static void *sender(void *p)
{
    send_t *a = p;
    const int fd = socket(AF_INET, a->tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    struct sockaddr_in me = { .sin_family = AF_INET }, to = { .sin_family = AF_INET, .sin_port = htons(PORT) };
    inet_pton(AF_INET, a->src, &me.sin_addr); inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    if (bind(fd, (struct sockaddr *)&me, sizeof me) != 0) { a->err = errno; return NULL; }
    if (a->tcp && connect(fd, (struct sockaddr *)&to, sizeof to) != 0) { a->err = errno; close(fd); return NULL; }
    static const int odd[] = { 1001, 997, 4, 1500, 333, 2, 1200 };   /* TCP: absichtlich nicht auf Framegrenzen */
    const int64_t t0 = mono_ms(); size_t off = 0; int k = 0;
    while (off < a->bytes) {
        size_t n = a->tcp && a->pkt == 0 ? (size_t)odd[k++ % 7] : (size_t)a->pkt;
        if (n > a->bytes - off) n = a->bytes - off;
        const ssize_t w = a->tcp ? send(fd, a->data + off, n, 0) : sendto(fd, a->data + off, n, 0, (struct sockaddr *)&to, sizeof to);
        if (w < 0) { a->err = errno; break; }
        off += n;
        if (a->realtime) { const int64_t due = t0 + (int64_t)((double)off * 1000.0 / a->rate_bps); const int64_t d = due - mono_ms(); if (d > 0) sleep_ms((int)d); }
    }
    a->took_ms = mono_ms() - t0;
    close(fd);
    return NULL;
}
static void run_sender(send_t *a, pthread_t *t) { pthread_create(t, NULL, sender, a); }

int main(void)
{
    const double R = 48000, BPS = 192000;
    signal(SIGPIPE, SIG_IGN);            /* der Test sendet bewusst auf von der Engine geschlossene Verbindungen */
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("== Start (Senke 48 kHz = Eingangsrate)\n");
    CHECK(irm_netin_init(&sink, on_evt, NULL) == ESP_OK, "init");
    CHECK(irm_netin_play() == ESP_OK, "play");
    for (int i = 0; i < 300 && !g_evt_running; i++) sleep_ms(10);
    CHECK(g_evt_running == 1 && sink_open, "Ereignis RUNNING, Senke offen");
    { char p[16]; peer(p); CHECK(p[0] == 0, "kein Absender: Status-Peer leer"); }
    { const int fd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(PORT) }; inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
      CHECK(connect(fd, (struct sockaddr *)&to, sizeof to) == 0, "TCP-Port nimmt Verbindungen an"); close(fd); sleep_ms(150); }

    printf("== UDP von 127.0.0.1 (1,5 s, Echtzeit, 1440-Byte-Pakete); 127.0.0.2 stoert mit lautem 5-kHz-Ton\n");
    rec_reset();
    int16_t *tone = make_tone(72000, R, 1000, 3000, 8000), *evil = make_tone(24000, R, 5000, 5000, 20000);
    send_t a = { "127.0.0.1", false, (uint8_t *)tone, 72000 * 4, 1440, true, BPS, 0, 0 };
    send_t b = { "127.0.0.2", false, (uint8_t *)evil, 24000 * 4, 1440, true, BPS, 0, 0 };
    pthread_t ta, tb; run_sender(&a, &ta);
    CHECK(wait_peer("127.0.0.1", 1000), "Status-Peer = 127.0.0.1");
    sleep_ms(300); run_sender(&b, &tb);
    pthread_join(ta, NULL); pthread_join(tb, NULL); sleep_ms(300);
    CHECK(!a.err && !b.err, "Sender ohne Fehler (%d/%d)", a.err, b.err);
    size_t f0 = first_loud();
    CHECK(f0 != (size_t)-1, "Ton kommt an");
    if (f0 != (size_t)-1) {
        const double lat_ms = (double)f0 * 1000.0 / R;
        printf("       Info: erster Ton nach %.0f ms (Vorpufferung 100 ms)\n", lat_ms);
        const double l1 = amp_at(f0 + 9600, 12000, 0, R, 1000), r3 = amp_at(f0 + 9600, 12000, 1, R, 3000);
        CHECK(fabs(l1 - 8000) < 250 && fabs(r3 - 8000) < 250, "L 1 kHz = %.0f, R 3 kHz = %.0f (Soll 8000)", l1, r3);
        size_t gf, gl; const int ng = gaps(f0, f0 + 70000, &gf, &gl);
        CHECK(ng <= 1, "UDP-Strom ohne nennenswerte Aussetzer (Luecken >= 5 ms: %d, laengste %zu Frames)", ng, gl);
        const double e5l = amp_at(f0 + 9600, 12000, 0, R, 5000), e5r = amp_at(f0 + 9600, 12000, 1, R, 5000);
        CHECK(e5l < 150 && e5r < 150, "kein 5-kHz-Anteil des zweiten Absenders im Ton (%.0f/%.0f; wuerde 20000 sein)", e5l, e5r);
    }
    irm_netin_stats_t st; irm_netin_get_stats(&st);
    CHECK(st.rx_bytes >= 72000u * 4 * 99 / 100 && st.rx_bytes <= 72000u * 4, "angenommen: %u von %u Byte", st.rx_bytes, 72000u * 4);
    CHECK(st.denied_packets > 20, "fremder Absender abgelehnt (%u Pakete)", st.denied_packets);
    CHECK(st.overrun_bytes == 0, "kein Puffer-Ueberlauf");
    CHECK(st.underruns <= 2, "Unterlaeufe: %u", st.underruns);
    printf("       Info: Fuellstand %d ms, Trim %+d ppm\n", st.fill_ms, st.trim_ppm);
    const size_t loud = count_loud();
    CHECK(loud > 72000 * 97 / 100 && loud < 72000 * 103 / 100, "Dauer des Tons: %zu Frames (Soll 72000)", loud);

    printf("== Absender verstummt -> Miete laeuft ab -> anderer Absender darf\n");
    CHECK(wait_peer("", 2000), "Peer nach Timeout (700 ms) wieder leer");
    rec_reset();
    free(tone); free(evil);
    int16_t *t5 = make_tone(36000, R, 5000, 5000, 9000);
    send_t c = { "127.0.0.2", false, (uint8_t *)t5, 36000 * 4, 1440, true, BPS, 0, 0 };
    pthread_t tc; run_sender(&c, &tc);
    CHECK(wait_peer("127.0.0.2", 1000), "Status-Peer = 127.0.0.2");
    pthread_join(tc, NULL); sleep_ms(200);
    f0 = first_loud();
    CHECK(f0 != (size_t)-1 && fabs(amp_at(f0 + 4800, 12000, 0, R, 5000) - 9000) < 300, "Ton des neuen Absenders wird gespielt");
    CHECK(wait_peer("", 2000), "wieder frei");
    free(t5);

    printf("== TCP von 127.0.0.1 (2 s, Echtzeit, Stuecke mit krummen Groessen)\n");
    rec_reset();
    tone = make_tone(96000, R, 1000, 3000, 8000);
    send_t d = { "127.0.0.1", true, (uint8_t *)tone, 96000 * 4, 0, true, BPS, 0, 0 };
    pthread_t td; run_sender(&d, &td);
    CHECK(wait_peer("127.0.0.1", 1000), "Status-Peer = 127.0.0.1 (TCP)");
    pthread_join(td, NULL);
    CHECK(wait_peer("", 1500), "TCP-Verbindung zu: Absender sofort frei (innerhalb 1,5 s)");
    sleep_ms(200);
    f0 = first_loud();
    CHECK(!d.err && f0 != (size_t)-1, "TCP-Strom kommt an (Fehler %d)", d.err);
    if (f0 != (size_t)-1) {
        size_t gf, gl; const int ng = gaps(f0, f0 + 90000, &gf, &gl);
        CHECK(ng == 0, "keine Aussetzer im TCP-Strom (Luecken >= 5 ms: %d, erste bei +%zu ms, laengste %zu Frames)", ng, ng ? (gf - f0) / 48 : 0, gl);
        const double l1 = amp_at(f0 + 9600, 12000, 0, R, 1000), r3 = amp_at(f0 + 9600, 12000, 1, R, 3000), x = amp_at(f0 + 9600, 12000, 0, R, 3000);
        CHECK(fabs(l1 - 8000) < 250 && fabs(r3 - 8000) < 250 && x < 150, "Kanaele intakt trotz krummer TCP-Stuecke: L 1 kHz %.0f, R 3 kHz %.0f, Uebersprechen %.0f", l1, r3, x);
    }
    free(tone);

    printf("== TCP so schnell wie moeglich (3 s Material): nichts darf verloren gehen\n");
    rec_reset(); irm_netin_get_stats(&st); const uint32_t ov0 = st.overrun_bytes;
    tone = make_tone(144000, R, 1000, 3000, 8000);
    send_t e = { "127.0.0.1", true, (uint8_t *)tone, 144000 * 4, 1440, false, BPS, 0, 0 };
    pthread_t te; run_sender(&e, &te);
    CHECK(wait_peer_nonempty(1000), "Absender erkannt");
    pthread_join(te, NULL);
    printf("       Info: Sender war nach %lld ms fertig (Linux puffert im Kernel; die Engine liest nur, wenn Platz ist)\n", (long long)e.took_ms);
    CHECK(wait_peer("", 6000), "Strom zu Ende");
    sleep_ms(700);                         /* der Jitterpuffer (bis 400 ms) wird nach dem Ende der Verbindung noch abgespielt */
    irm_netin_get_stats(&st);
    const size_t ld = count_loud();
    { size_t gf, gl; const int ng = gaps(first_loud(), rec_n, &gf, &gl); printf("       Info: rx %u Byte, Aufnahme %zu Frames (davon laut %zu), erster Ton bei %zu, Luecken >= 5 ms: %d (laengste %zu Frames, erste bei +%zu ms)\n", st.rx_bytes, rec_n, ld, first_loud(), ng, gl, ng ? (gf - first_loud()) / 48 : 0); }
    CHECK(st.overrun_bytes == ov0, "TCP-Rueckstau: nichts verworfen (verworfen %u)", st.overrun_bytes - ov0);
    CHECK(ld > 144000 * 97 / 100, "komplett ausgegeben: %zu von 144000 Frames", ld);
    free(tone);

    printf("== Stop\n");
    const int64_t ts = mono_ms();
    CHECK(irm_netin_stop() == ESP_OK && !sink_open, "stop: Senke frei");
    printf("       Info: stop dauerte %lld ms\n", (long long)(mono_ms() - ts));
    { const int fd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(PORT) }; inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
      CHECK(connect(fd, (struct sockaddr *)&to, sizeof to) != 0 && errno == ECONNREFUSED, "TCP-Port ist zu (ECONNREFUSED)"); close(fd); }
    CHECK(irm_netin_stop() == ESP_OK, "zweites stop ohne Sitzung: sofort OK");

    printf("== Fremdes Subnetz (Geraet 192.168.5.10/24): Loopback-Absender wird abgelehnt\n");
    g_local = 0xC0A8050Au; g_mask = 0xFFFFFF00u; rec_reset();
    CHECK(irm_netin_play() == ESP_OK, "play");
    sleep_ms(300);
    tone = make_tone(24000, R, 1000, 3000, 8000);
    send_t f = { "127.0.0.1", false, (uint8_t *)tone, 24000 * 4, 1440, true, BPS, 0, 0 };
    pthread_t tf; run_sender(&f, &tf); pthread_join(tf, NULL); sleep_ms(200);
    irm_netin_get_stats(&st);
    CHECK(first_loud() == (size_t)-1 && st.rx_bytes == 0 && st.denied_packets > 10, "UDP: nichts gespielt, %u Pakete abgelehnt", st.denied_packets);
    send_t g = { "127.0.0.1", true, (uint8_t *)tone, 24000 * 4, 1440, false, BPS, 0, 0 };
    pthread_t tg; run_sender(&g, &tg); pthread_join(tg, NULL); sleep_ms(200);
    irm_netin_get_stats(&st);
    CHECK(first_loud() == (size_t)-1 && st.rx_bytes == 0, "TCP: Verbindung sofort geschlossen, nichts gespielt");
    free(tone);
    CHECK(irm_netin_stop() == ESP_OK, "stop");

    printf("== Senke mit 44,1 kHz: Umrechnung 48 -> 44,1 kHz\n");
    g_local = 0x7F000001u; g_mask = 0xFF000000u; sink.sample_rate = 44100; rec_reset();
    CHECK(irm_netin_play() == ESP_OK, "play");
    sleep_ms(300);
    tone = make_tone(72000, R, 1000, 3000, 8000);
    send_t h = { "127.0.0.1", false, (uint8_t *)tone, 72000 * 4, 1440, true, BPS, 0, 0 };
    pthread_t th; run_sender(&h, &th); pthread_join(th, NULL); sleep_ms(300);
    f0 = first_loud();
    if (f0 != (size_t)-1) {
        const double l1 = amp_at(f0 + 8820, 11025, 0, 44100, 1000), r3 = amp_at(f0 + 8820, 11025, 1, 44100, 3000);
        CHECK(fabs(l1 - 8000) < 250 && fabs(r3 - 8000) < 250, "L 1 kHz = %.0f, R 3 kHz = %.0f (Soll 8000)", l1, r3);
    } else CHECK(0, "kein Ton");
    const size_t l2 = count_loud();
    CHECK(l2 > 66150 * 97 / 100 && l2 < 66150 * 103 / 100, "Dauer: %zu Frames bei 44,1 kHz (Soll 66150)", l2);
    irm_netin_get_stats(&st);
    CHECK(st.underruns <= 2 && st.overrun_bytes == 0, "Unterlaeufe %u, verworfen %u", st.underruns, st.overrun_bytes);
    free(tone);
    CHECK(irm_netin_stop() == ESP_OK && !sink_open, "stop");
    CHECK(g_evt_error == 0, "keine Fehlerereignisse");
    printf("== Ergebnis: %s\n", fails ? "FEHLER" : "OK");
    return fails ? 1 : 0;
}
/* Build (aus components/):
 *  K=irm_kiwi/test/host; N=irm_netin; gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -I$N/test/host/shim -I$K/shim -Iirm_core/include -Iirm_out/include \
 *    -I$N/include $N/src/irm_netin.c $N/src/irm_netin_policy.c irm_out/src/irm_resamp.c $K/shim_rtos.c $N/test/host/test_netin_host.c -lpthread -lm -o /tmp/tnh && /tmp/tnh */
