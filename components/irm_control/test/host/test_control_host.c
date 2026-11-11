/* Host-Test des ECHTEN irm_control.c: Zustandsautomat mit drei Quellen gegen Fake-Engines (Audio, Kiwi), Fake-Board/-Dateien/-Einstellungen.
 * Kernpruefung: Radio/Medien (irm_audio) und KiwiSDR (irm_kiwi) halten die Ausgabesenke NIE gleichzeitig. */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "irm_audio.h"
#include "irm_board.h"
#include "irm_control.h"
#include "irm_core.h"
#include "irm_files.h"
#include "irm_kiwi.h"
#include "irm_playlist.h"
#include "irm_settings.h"
#include "esp_timer.h"
#include <freertos/task.h>

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
#define ESP_LOGx(...) 

/* ---------- Fake-Core: Kommando-Queue + Status ---------- */
static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t qc = PTHREAD_COND_INITIALIZER;
static irm_cmd_t q[64]; static int qh, qn;
esp_err_t irm_cmd_post(const irm_cmd_t *c, TickType_t w) { (void)w; pthread_mutex_lock(&qm); if (qn == 64) { pthread_mutex_unlock(&qm); return ESP_FAIL; } q[(qh + qn++) % 64] = *c; pthread_cond_signal(&qc); pthread_mutex_unlock(&qm); return ESP_OK; }
esp_err_t irm_cmd_post_id(irm_cmd_id_t id, int32_t v) { irm_cmd_t c = { .id = id }; c.value = v; return irm_cmd_post(&c, 0); }
esp_err_t irm_cmd_receive(irm_cmd_t *c, TickType_t w)
{
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    if (w == portMAX_DELAY) t.tv_sec += 3600; else { t.tv_nsec += (long)w * 1000000L; t.tv_sec += t.tv_nsec / 1000000000L; t.tv_nsec %= 1000000000L; }
    pthread_mutex_lock(&qm);
    while (qn == 0) if (pthread_cond_timedwait(&qc, &qm, &t)) { pthread_mutex_unlock(&qm); return ESP_ERR_TIMEOUT; }
    *c = q[qh]; qh = (qh + 1) % 64; qn--; pthread_mutex_unlock(&qm); return ESP_OK;
}
static irm_status_t g_st = { .index = -1, .kiwi_rssi_dbm = IRM_RSSI_NONE };
static pthread_mutex_t sm = PTHREAD_MUTEX_INITIALIZER;
void irm_status_update(irm_status_mutator_t fn, void *ctx) { pthread_mutex_lock(&sm); fn(&g_st, ctx); pthread_mutex_unlock(&sm); }
void irm_status_get(irm_status_t *o) { pthread_mutex_lock(&sm); *o = g_st; pthread_mutex_unlock(&sm); }
static irm_status_t S(void) { irm_status_t s; irm_status_get(&s); return s; }

/* ---------- Fake-Timer (manuell ausgeloest) ---------- */
struct esp_timer { void (*cb)(void *); void *arg; int armed; uint64_t delay_us; };
static struct esp_timer g_timer;
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) { g_timer.cb = a->callback; g_timer.arg = a->arg; *h = &g_timer; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) { h->armed = 1; h->delay_us = us; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t h) { h->armed = 0; return ESP_OK; }
static void fire_timer(void) { assert(g_timer.armed); g_timer.armed = 0; g_timer.cb(g_timer.arg); }
BaseType_t xTaskCreate(void (*fn)(void *), const char *n, uint32_t st, void *arg, int prio, TaskHandle_t *h) { return xTaskCreatePinnedToCore(fn, n, st, arg, prio, h, 0); }

/* ---------- Fake-Engines mit Exklusivitaetspruefung ---------- */
static volatile int a_active, k_active, violations;
static char a_uri[256], k_uri[256]; static int k_reset, a_plays, k_plays, a_stops, k_stops, a_pauses;
static irm_audio_cb_t a_cb; static irm_kiwi_cb_t k_cb;
static uint32_t k_tune_f; static int k_step, k_mode = -99; static int k_tunes;
esp_err_t irm_audio_init(esp_codec_dev_handle_t c, irm_out_sink_t *s, irm_audio_cb_t cb, void *x) { (void)c; (void)s; (void)x; a_cb = cb; return ESP_OK; }
esp_err_t irm_audio_play(const char *u) { if (k_active) { violations++; printf("  FEHLER: irm_audio_play bei aktiver Kiwi-Sitzung\n"); } a_active = 1; a_plays++; strcpy(a_uri, u); return ESP_OK; }
esp_err_t irm_audio_stop(void) { a_active = 0; a_stops++; return ESP_OK; }
esp_err_t irm_audio_pause(void) { a_pauses++; return ESP_OK; }
esp_err_t irm_audio_resume(void) { return ESP_OK; }
esp_err_t irm_audio_set_volume(uint8_t p) { (void)p; return ESP_OK; }
esp_err_t irm_audio_set_eq(const irm_eq_t *e) { (void)e; return ESP_OK; }
esp_err_t irm_kiwi_init(irm_out_sink_t *s, irm_kiwi_cb_t cb, void *x) { (void)s; (void)x; k_cb = cb; return ESP_OK; }
esp_err_t irm_kiwi_play(const char *u, bool reset) { if (a_active) { violations++; printf("  FEHLER: irm_kiwi_play bei aktiver Audio-Pipeline\n"); } k_active = 1; k_plays++; strcpy(k_uri, u); k_reset = reset; return ESP_OK; }
esp_err_t irm_kiwi_stop(void) { k_active = 0; k_stops++; return ESP_OK; }
esp_err_t irm_kiwi_tune(uint32_t f, const char *m) { (void)m; k_tune_f = f; k_tunes++; return ESP_OK; }
esp_err_t irm_kiwi_tune_step(int32_t d) { k_step = d; k_tunes++; return ESP_OK; }
esp_err_t irm_kiwi_set_mode_index(int i) { k_mode = i; k_tunes++; return ESP_OK; }
static void audio_evt(irm_audio_evt_type_t t) { irm_audio_evt_t e = { .type = t, .sample_rate = 44100, .channels = 2, .bits = 16, .bitrate = 128000 }; a_cb(&e, NULL); }
static void kiwi_evt(irm_kiwi_evt_type_t t) { irm_kiwi_evt_t e = { .type = t, .sample_rate = 12000, .bitrate = 192000, .err = -2 }; k_cb(&e, NULL); }

/* ---------- Fake-Board/-Dateien/-Einstellungen ---------- */
static int sd_mounted = 1, media_ok = 1;
static int saved_kiwi = -1, saved_radio = -1;
esp_codec_dev_handle_t irm_board_codec(void) { return NULL; }
irm_out_sink_t *irm_board_sink(void) { static irm_out_sink_t s; return &s; }
bool irm_board_sd_mounted(void) { return sd_mounted; }
esp_err_t irm_files_load_playlist(const char *path, irm_playlist_t *pl)
{
    if (strstr(path, "kiwi.m3u")) {
        irm_playlist_add(pl, "kiwi://a.example:8073/?f=77.5&mode=am", "DCF77");
        irm_playlist_add(pl, "kiwi://b.example/?f=kaputt", "Kaputt");
        irm_playlist_add(pl, "http://c.example:8073/?f=6070amz6", "Kurzwelle");
        irm_playlist_add(pl, "kiwi://d.example:8073/?f=14200usb", "20m");
        return ESP_OK;
    }
    irm_playlist_add(pl, "http://r0.example/s", "Radio0"); irm_playlist_add(pl, "http://r1.example/s", "Radio1"); irm_playlist_add(pl, "http://r2.example/s", "Radio2");
    return ESP_OK;
}
esp_err_t irm_files_scan_media(const char *root, const char *ext, int depth, irm_playlist_t *pl) { (void)root; (void)ext; (void)depth; if (!media_ok) return ESP_ERR_NOT_FOUND; irm_playlist_add(pl, "/sdcard/a.mp3", "a"); irm_playlist_add(pl, "/sdcard/b.mp3", "b"); return ESP_OK; }
esp_err_t irm_settings_load(irm_settings_t *o) { memset(o, 0, sizeof *o); o->volume = 30; o->station = 1; o->kiwi_station = 1; return ESP_OK; }
esp_err_t irm_settings_save_volume(uint8_t v) { (void)v; return ESP_OK; }
esp_err_t irm_settings_save_station(int32_t i) { saved_radio = i; return ESP_OK; }
esp_err_t irm_settings_save_kiwi_station(int32_t i) { saved_kiwi = i; return ESP_OK; }
esp_err_t irm_settings_save_eq(const irm_eq_t *e) { (void)e; return ESP_OK; }

/* ---------- Hilfen ---------- */
static void settle(void) { for (int i = 0; i < 100; i++) { usleep(5000); pthread_mutex_lock(&qm); int n = qn; pthread_mutex_unlock(&qm); if (!n && i > 4) break; } usleep(60000); }
static void cmd(irm_cmd_id_t id, int32_t v) { irm_cmd_post_id(id, v); settle(); }
static const char *srcname(irm_source_t s) { return s == IRM_SOURCE_RADIO ? "radio" : (s == IRM_SOURCE_MEDIA ? "media" : "kiwi"); }
static const char *pl_name(irm_player_state_t p) { static const char *n[] = { "idle", "connecting", "playing", "paused", "error" }; return n[p]; }
#define EXPECT_STATE(src, pl, idx) do { irm_status_t s_ = S(); CHECK(s_.source == (src) && s_.player == (pl) && s_.index == (idx), "Zustand: %s/%s/#%d (erwartet %s/%s/#%d)", srcname(s_.source), pl_name(s_.player), s_.index, srcname(src), pl_name(pl), (int)(idx)); } while (0)

int main(void)
{
    printf("== Start\n");
    CHECK(irm_control_start() == ESP_OK, "irm_control_start");
    settle();
    CHECK(irm_control_item_count(IRM_SOURCE_RADIO) == 3, "Radio: 3 Sender");
    CHECK(irm_control_item_count(IRM_SOURCE_KIWI) == 3, "Kiwi: 3 gueltige Eintraege (1 ungueltiger uebersprungen)");
    char name[64], uri[256];
    irm_control_item(IRM_SOURCE_KIWI, 1, name, sizeof name, uri, sizeof uri);
    CHECK(!strcmp(name, "Kurzwelle") && strstr(uri, "c.example"), "Kiwi #1 ist der Browser-URL-Eintrag (%s)", name);
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_IDLE, 1);
    CHECK(a_plays == 0 && k_plays == 0, "ohne Netz startet nichts");

    printf("== Radio\n");
    cmd(IRM_CMD_INTERNAL_NET_CHANGED, 1);
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_CONNECTING, 1);
    CHECK(a_active && !k_active && strstr(a_uri, "r1.example"), "Audio-Pipeline spielt Radio1");
    audio_evt(IRM_AUDIO_EVT_RUNNING); settle();
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_PLAYING, 1);

    printf("== Quellen reihum: Radio -> Medien -> KiwiSDR -> Radio\n");
    cmd(IRM_CMD_TOGGLE_SOURCE, 0);
    EXPECT_STATE(IRM_SOURCE_MEDIA, IRM_PLAYER_CONNECTING, 0);
    CHECK(a_active && !k_active && strstr(a_uri, "a.mp3"), "Medien-Datei laeuft ueber irm_audio");
    audio_evt(IRM_AUDIO_EVT_RUNNING); settle();
    cmd(IRM_CMD_TOGGLE_SOURCE, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 1);
    CHECK(!a_active && k_active && strstr(k_uri, "c.example") && k_reset, "KiwiSDR #1 (aus NVS), Pipeline gestoppt, reset_tune=true");
    CHECK(S().kiwi_freq_hz == 0, "Statusfelder zurueckgesetzt, bis irm_kiwi sie veroeffentlicht");
    kiwi_evt(IRM_KIWI_EVT_INFO); settle();
    { irm_status_t s = S(); CHECK(s.sample_rate == 12000 && s.channels == 1 && s.bits == 16 && s.bitrate == 192000, "INFO -> Format 12 kHz mono 16 bit (%u/%u/%u)", s.sample_rate, s.channels, s.bits); }
    kiwi_evt(IRM_KIWI_EVT_RUNNING); settle();
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_PLAYING, 1);

    printf("== Abstimmen (nur in der Kiwi-Quelle)\n");
    cmd(IRM_CMD_KIWI_TUNE, 7055500); CHECK(k_tune_f == 7055500, "KIWI_TUNE weitergegeben");
    cmd(IRM_CMD_KIWI_STEP, -500);    CHECK(k_step == -500, "KIWI_STEP weitergegeben");
    cmd(IRM_CMD_KIWI_MODE, -1);      CHECK(k_mode == -1, "KIWI_MODE weitergegeben");
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_PLAYING, 1);

    printf("== Fehler, Backoff, Wiederholen\n");
    int plays0 = k_plays;
    kiwi_evt(IRM_KIWI_EVT_ERROR); settle();
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_ERROR, 1);
    CHECK(g_timer.armed && g_timer.delay_us == 1000000, "Retry-Timer 1 s");
    fire_timer(); settle();
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 1);
    CHECK(k_plays == plays0 + 1 && !k_reset, "Wiederholen ohne reset_tune (zuletzt eingestellte Frequenz bleibt)");
    kiwi_evt(IRM_KIWI_EVT_ERROR); settle();
    CHECK(g_timer.armed && g_timer.delay_us == 2000000, "Backoff verdoppelt (2 s)");
    fire_timer(); settle();
    kiwi_evt(IRM_KIWI_EVT_RUNNING); settle();
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_PLAYING, 1);

    printf("== Ereignisse der falschen Engine werden ignoriert\n");
    audio_evt(IRM_AUDIO_EVT_ERROR); settle();
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_PLAYING, 1);
    CHECK(!g_timer.armed, "Audio-Fehler (origin 0) in Kiwi-Quelle loest kein Wiederholen aus");

    printf("== Pause / Fortsetzen / Eintragswechsel\n");
    cmd(IRM_CMD_PAUSE, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_PAUSED, 1);
    CHECK(!k_active && !a_active, "Pause trennt die Kiwi-Sitzung");
    cmd(IRM_CMD_PLAY, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 1);
    CHECK(k_active && !k_reset, "Fortsetzen: gleiche Abstimmung (reset_tune=false)");
    cmd(IRM_CMD_NEXT, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 2);
    CHECK(strstr(k_uri, "d.example") && k_reset, "NEXT -> Eintrag #2 mit reset_tune=true");
    cmd(IRM_CMD_NEXT, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 0);
    CHECK(strstr(k_uri, "a.example"), "NEXT umlaufend -> #0");
    cmd(IRM_CMD_PREV, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 2);
    usleep(400000); settle();
    CHECK(saved_kiwi == 2 && saved_radio == -1, "Kiwi-Eintrag #2 persistiert (kws=%d), Radio-Eintrag unberuehrt", saved_kiwi);

    printf("== Zurueck zum Radio: Kiwi-Sitzung wird beendet, Nachzuegler ignoriert\n");
    int ks = k_stops;
    cmd(IRM_CMD_TOGGLE_SOURCE, 0);
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_CONNECTING, 1);
    CHECK(a_active && !k_active && k_stops > ks, "irm_kiwi_stop vor irm_audio_play");
    kiwi_evt(IRM_KIWI_EVT_ERROR); kiwi_evt(IRM_KIWI_EVT_RUNNING); settle();     /* Nachzuegler der beendeten Sitzung */
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_CONNECTING, 1);
    CHECK(!g_timer.armed, "kein Wiederholen durch Nachzuegler");
    audio_evt(IRM_AUDIO_EVT_RUNNING); settle();
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_PLAYING, 1);
    cmd(IRM_CMD_KIWI_TUNE, 1000000); CHECK(k_tune_f == 7055500, "KIWI_TUNE im Radio-Betrieb wird nicht weitergegeben");

    printf("== Medien nicht verfuegbar: Umschalten ueberspringt sie\n");
    sd_mounted = 0;
    cmd(IRM_CMD_TOGGLE_SOURCE, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 2);
    CHECK(!a_active && k_active, "Radio -> KiwiSDR direkt");
    cmd(IRM_CMD_TOGGLE_SOURCE, 0);
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_CONNECTING, 1);
    cmd(IRM_CMD_SET_SOURCE, IRM_SOURCE_KIWI);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_CONNECTING, 2);
    cmd(IRM_CMD_SET_SOURCE, 7);
    EXPECT_STATE(IRM_SOURCE_RADIO, IRM_PLAYER_CONNECTING, 1);

    printf("== Netz weg und wieder da (Live-Quelle Kiwi)\n");
    cmd(IRM_CMD_SET_SOURCE, IRM_SOURCE_KIWI);
    kiwi_evt(IRM_KIWI_EVT_ERROR); settle();
    cmd(IRM_CMD_INTERNAL_NET_CHANGED, 0);
    cmd(IRM_CMD_INTERNAL_NET_CHANGED, 1);
    CHECK(k_active && S().player == IRM_PLAYER_CONNECTING, "NET_CHANGED startet die Kiwi-Sitzung neu");
    cmd(IRM_CMD_STOP, 0);
    EXPECT_STATE(IRM_SOURCE_KIWI, IRM_PLAYER_IDLE, 2);
    CHECK(!k_active && !a_active, "STOP beendet beide Engines");

    CHECK(violations == 0, "Exklusivitaet: %d Verletzungen ueber den gesamten Lauf (a_plays=%d, k_plays=%d)", violations, a_plays, k_plays);
    printf("== Ergebnis: %s (%d Fehler)\n", fails ? "FEHLER" : "OK", fails);
    return fails ? 1 : 0;
}
