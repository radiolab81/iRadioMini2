/* Host-Test des ECHTEN irm_kiwi.c (mit POSIX-Shim statt FreeRTOS) gegen einen Mock-KiwiSDR.
 * Aufruf: test_irm_kiwi_host <portA> <portB> <portTot> <dump.raw>   (gestartet von e2e_irm_kiwi.py, siehe README) */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "irm_core.h"
#include "irm_kiwi.h"

/* ---- Fake-Status (statt irm_core.c) ---- */
static irm_status_t g_st = { .kiwi_rssi_dbm = IRM_RSSI_NONE };
static pthread_mutex_t g_st_m = PTHREAD_MUTEX_INITIALIZER;
void irm_status_update(irm_status_mutator_t fn, void *ctx) { pthread_mutex_lock(&g_st_m); fn(&g_st, ctx); pthread_mutex_unlock(&g_st_m); }
void irm_status_get(irm_status_t *o) { pthread_mutex_lock(&g_st_m); *o = g_st; pthread_mutex_unlock(&g_st_m); }

/* ---- Fake-Senke: blockiert in Echtzeit wie ein DMA-Puffer (absoluter Takt, summiert keinen Sleep-Fehler) ---- */
#define OUT_RATE 48000u
static struct { volatile int opens, closes, active, bad; volatile long frames; FILE *dump; struct timespec next; } fk;
static esp_err_t f_open(irm_out_sink_t *s) { (void)s; if (fk.active) fk.bad++; fk.active = 1; fk.opens++; clock_gettime(CLOCK_MONOTONIC, &fk.next); return ESP_OK; }
static esp_err_t f_write(irm_out_sink_t *s, const int16_t *st, size_t n)
{
    (void)s;
    if (!fk.active) fk.bad++;
    fk.frames += (long)n;
    if (fk.dump) fwrite(st, 4, n, fk.dump);
    long ns = (long)((double)n * 1e9 / OUT_RATE);
    fk.next.tv_nsec += ns; while (fk.next.tv_nsec >= 1000000000L) { fk.next.tv_nsec -= 1000000000L; fk.next.tv_sec++; }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &fk.next, NULL);
    return ESP_OK;
}
static void f_close(irm_out_sink_t *s) { (void)s; if (!fk.active) fk.bad++; fk.active = 0; fk.closes++; }
static irm_out_sink_t g_sink = { .name = "fake", .open = f_open, .write = f_write, .close = f_close, .gain_q15 = 32768, .sample_rate = OUT_RATE, .buffer_ms = 100 };

/* ---- Ereignisse ---- */
static struct { irm_kiwi_evt_type_t t; uint32_t rate, bitrate; int err; double at; } ev[64];
static volatile int nev;
static pthread_mutex_t ev_m = PTHREAD_MUTEX_INITIALIZER;
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void cb(const irm_kiwi_evt_t *e, void *ctx)
{
    (void)ctx; pthread_mutex_lock(&ev_m);
    if (nev < 64) { ev[nev].t = e->type; ev[nev].rate = e->sample_rate; ev[nev].bitrate = e->bitrate; ev[nev].err = e->err; ev[nev].at = now_s(); nev++; }
    pthread_mutex_unlock(&ev_m);
}
static int count_ev(irm_kiwi_evt_type_t t) { int c = 0; pthread_mutex_lock(&ev_m); for (int i = 0; i < nev; i++) c += ev[i].t == t; pthread_mutex_unlock(&ev_m); return c; }
static int wait_ev(irm_kiwi_evt_type_t t, int already, double secs) { double end = now_s() + secs; while (now_s() < end) { if (count_ev(t) > already) return 1; usleep(20000); } return 0; }

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "Aufruf: %s portA portB portTot dump.raw\n", argv[0]); return 2; }
    char uriA[128], uriB[128], uriT[128];
    snprintf(uriA, sizeof uriA, "kiwi://127.0.0.1:%s/?f=225am", argv[1]);
    snprintf(uriB, sizeof uriB, "kiwi://127.0.0.1:%s/?f=198.5amn", argv[2]);
    snprintf(uriT, sizeof uriT, "kiwi://127.0.0.1:%s/?f=1000am", argv[3]);
    fk.dump = fopen(argv[4], "wb");
    irm_status_t st;

    printf("== Initialisierung\n");
    CHECK(irm_kiwi_play(uriA, true) == ESP_ERR_INVALID_STATE, "play vor init abgelehnt");
    CHECK(irm_kiwi_init(&g_sink, cb, NULL) == ESP_OK, "init");
    CHECK(irm_kiwi_init(&g_sink, cb, NULL) == ESP_OK, "init idempotent");
    CHECK(irm_kiwi_play("http://radio.example/stream.mp3", true) == ESP_ERR_INVALID_ARG, "Radio-URL als Kiwi-URI abgelehnt");
    CHECK(irm_kiwi_tune(1000, NULL) == ESP_ERR_INVALID_STATE, "tune ohne Eintrag abgelehnt");
    CHECK(irm_kiwi_stop() == ESP_OK, "stop ohne Sitzung kehrt sofort zurueck");

    printf("== Sitzung A: Verbinden, Vorpufferung, Wiedergabe\n");
    double t0 = now_s();
    CHECK(irm_kiwi_play(uriA, true) == ESP_OK, "play A");
    irm_status_get(&st);
    CHECK(st.kiwi_freq_hz == 225000 && !strcmp(st.kiwi_mode, "am"), "Status sofort: %u Hz %s", st.kiwi_freq_hz, st.kiwi_mode);
    CHECK(wait_ev(IRM_KIWI_EVT_RUNNING, 0, 10), "RUNNING nach %.2f s", now_s() - t0);
    CHECK(count_ev(IRM_KIWI_EVT_INFO) == 1 && ev[0].t == IRM_KIWI_EVT_INFO && ev[1].t == IRM_KIWI_EVT_RUNNING, "INFO vor RUNNING");
    CHECK(ev[0].rate == 12000 && ev[0].bitrate == 192000, "INFO: %u Hz, %u bit/s", ev[0].rate, ev[0].bitrate);
    CHECK(ev[1].at - t0 > 0.35, "Vorpufferung dauert >= ca. 400 ms (RUNNING erst %.2f s nach play)", ev[1].at - t0);
    CHECK(fk.opens == 1 && fk.active, "Senke genau einmal geoeffnet");
    long f_run = fk.frames;
    sleep(4);
    CHECK(fk.frames - f_run > 4 * OUT_RATE * 0.9, "Ausgabe laeuft kontinuierlich: %ld Frames in 4 s", fk.frames - f_run);
    irm_status_get(&st);
    CHECK(st.kiwi_rssi_dbm == IRM_RSSI_NONE || (st.kiwi_rssi_dbm > -140 && st.kiwi_rssi_dbm < 0), "RSSI plausibel oder unbekannt: %d", st.kiwi_rssi_dbm);

    printf("== Abstimmung zur Laufzeit\n");
    CHECK(irm_kiwi_tune(7055500, "usb") == ESP_OK, "tune 7055.5 usb");
    usleep(500000);   /* der Client uebernimmt nur den juengsten Wunsch je Poll-Zyklus: Abstand, damit jeder einzeln beim Server ankommt */
    irm_status_get(&st);
    CHECK(st.kiwi_freq_hz == 7055500 && !strcmp(st.kiwi_mode, "usb"), "Status: %u %s", st.kiwi_freq_hz, st.kiwi_mode);
    CHECK(irm_kiwi_tune_step(+1000) == ESP_OK, "step +1 kHz");
    usleep(500000);
    irm_status_get(&st); CHECK(st.kiwi_freq_hz == 7056500, "-> %u", st.kiwi_freq_hz);
    CHECK(irm_kiwi_set_mode_index(-1) == ESP_OK, "naechster Modus");
    usleep(500000);
    irm_status_get(&st); CHECK(!strcmp(st.kiwi_mode, "usn"), "usb -> %s", st.kiwi_mode);
    CHECK(irm_kiwi_set_mode_index(0) == ESP_OK, "Modus am per Index");
    CHECK(irm_kiwi_tune(0, "xyz") == ESP_ERR_INVALID_ARG && irm_kiwi_set_mode_index(99) == ESP_ERR_INVALID_ARG, "ungueltiger Modus abgelehnt");
    irm_status_get(&st); CHECK(!strcmp(st.kiwi_mode, "am") && st.kiwi_freq_hz == 7056500, "Zustand nach Ablehnung unveraendert");
    irm_kiwi_tune_step(-100000000); irm_status_get(&st); CHECK(st.kiwi_freq_hz == 10000, "Untergrenze 10 kHz (%u)", st.kiwi_freq_hz);
    irm_kiwi_tune_step(+100000000); irm_status_get(&st); CHECK(st.kiwi_freq_hz == 32000000, "Obergrenze 32 MHz (%u)", st.kiwi_freq_hz);
    /* schnelle Folge ohne Pause: der Client fasst zusammen, beim Server kommt der Endzustand an */
    irm_kiwi_tune(14100000, "lsb"); irm_kiwi_tune_step(+50000); irm_kiwi_tune(0, "usb");
    sleep(1);

    printf("== Stop\n");
    int err_before = count_ev(IRM_KIWI_EVT_ERROR);
    double ts = now_s();
    CHECK(irm_kiwi_stop() == ESP_OK, "stop");
    double dt = now_s() - ts;
    CHECK(dt < 2.5, "stop dauerte %.0f ms", dt * 1000);
    CHECK(!fk.active && fk.opens == 1 && fk.closes == 1, "Senke geschlossen (opens=%d closes=%d)", fk.opens, fk.closes);
    sleep(1);
    CHECK(count_ev(IRM_KIWI_EVT_ERROR) == err_before, "stop erzeugt kein ERROR-Ereignis");

    printf("== Sitzung B: Neustart, Mindestabstand, Abstimmung behalten\n");
    fclose(fk.dump); fk.dump = NULL;
    t0 = now_s();
    int run_before = count_ev(IRM_KIWI_EVT_RUNNING);
    CHECK(irm_kiwi_play(uriB, true) == ESP_OK, "play B");
    CHECK(wait_ev(IRM_KIWI_EVT_RUNNING, run_before, 12), "RUNNING B nach %.2f s", now_s() - t0);
    CHECK(fk.opens == 2 && fk.active, "Senke erneut geoeffnet");
    irm_kiwi_tune(3500000, "lsb");
    CHECK(irm_kiwi_stop() == ESP_OK && !fk.active && fk.closes == 2, "stop B");
    CHECK(irm_kiwi_play(uriB, false) == ESP_OK, "play B (reset_tune=false)");
    irm_status_get(&st);
    CHECK(st.kiwi_freq_hz == 3500000 && !strcmp(st.kiwi_mode, "lsb"), "Abstimmung behalten: %u %s", st.kiwi_freq_hz, st.kiwi_mode);
    irm_kiwi_stop();
    CHECK(irm_kiwi_play(uriB, true) == ESP_OK, "play B (reset_tune=true)");
    irm_status_get(&st);
    CHECK(st.kiwi_freq_hz == 198500 && !strcmp(st.kiwi_mode, "amn"), "Abstimmung aus URI: %u %s", st.kiwi_freq_hz, st.kiwi_mode);
    /* Mock B ist nach der ersten Sitzung beendet -> Verbindung wird verweigert -> genau ein ERROR, danach Ruhe */
    int e0 = count_ev(IRM_KIWI_EVT_ERROR);
    CHECK(wait_ev(IRM_KIWI_EVT_ERROR, e0, 10), "ERROR bei toter Gegenstelle");
    int e1 = count_ev(IRM_KIWI_EVT_ERROR);
    sleep(5);
    CHECK(count_ev(IRM_KIWI_EVT_ERROR) == e1 && !fk.active, "keine eigenmaechtige Wiederholung (Wiederholen ist Sache von irm_control)");

    printf("== Schnelles Umschalten (play/stop/play) bricht saubere Sitzungen ab\n");
    for (int i = 0; i < 5; i++) { irm_kiwi_play(uriT, true); usleep(50000); }
    CHECK(irm_kiwi_stop() == ESP_OK, "stop nach Serie");
    sleep(1);
    CHECK(!fk.active && fk.bad == 0, "Senke nicht belegt, keine Protokollverletzung (bad=%d)", fk.bad);

    printf("== Ergebnis: %s (%d Fehler)\n", fails ? "FEHLER" : "OK", fails);
    return fails ? 1 : 0;
}
