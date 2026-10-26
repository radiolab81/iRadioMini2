#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "irm_audio.h"
#include "irm_board.h"
#include "irm_control.h"
#include "irm_core.h"
#include "irm_files.h"
#include "irm_settings.h"

static const char *TAG = "irm_ctrl";

enum { P_PLAYER = 1, P_ITEM = 2, P_VOL = 4, P_FMT = 8, P_EQ = 16 };
typedef struct { unsigned mask; irm_status_t v; } patch_t;

static struct {
    SemaphoreHandle_t  list_lock;
    irm_playlist_t    *lists[2];
    irm_source_t       source;
    int32_t            index[2];
    bool               want_play;
    bool               net_up;
    uint8_t            volume;
    irm_eq_t           eq;
    int64_t            persist_deadline_us;  /* 0 = nichts offen */
    bool               dirty_vol, dirty_idx;
    int                retry_s;
    esp_timer_handle_t retry_timer;
} c;

static void apply_patch(irm_status_t *s, void *ctx)
{
    const patch_t *p = ctx;
    if (p->mask & P_PLAYER) s->player = p->v.player;
    if (p->mask & P_ITEM) {
        s->source = p->v.source; s->index = p->v.index; s->count = p->v.count;
        memcpy(s->name, p->v.name, sizeof(s->name)); memcpy(s->uri, p->v.uri, sizeof(s->uri));
        s->sample_rate = 0; s->channels = 0; s->bits = 0; s->bitrate = 0;
    }
    if (p->mask & P_VOL) s->volume = p->v.volume;
    if (p->mask & P_FMT) { s->sample_rate = p->v.sample_rate; s->channels = p->v.channels; s->bits = p->v.bits; s->bitrate = p->v.bitrate; }
    if (p->mask & P_EQ) s->eq = p->v.eq;
}
static void publish(patch_t *p) { irm_status_update(apply_patch, p); }
static void set_player(irm_player_state_t st) { patch_t p = { .mask = P_PLAYER }; p.v.player = st; publish(&p); }

static size_t count_of(irm_source_t s) { return irm_playlist_count(c.lists[s]); }

size_t irm_control_item_count(irm_source_t src)
{
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    size_t n = count_of(src);
    xSemaphoreGive(c.list_lock);
    return n;
}

bool irm_control_item(irm_source_t src, size_t idx, char *name, size_t nsz, char *uri, size_t usz)
{
    bool ok = false;
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    const char *u = irm_playlist_uri(c.lists[src], idx);
    if (u) {
        if (uri && usz) strlcpy(uri, u, usz);
        if (name && nsz) strlcpy(name, irm_playlist_name(c.lists[src], idx), nsz);
        ok = true;
    }
    xSemaphoreGive(c.list_lock);
    return ok;
}

static void publish_item(void)
{
    patch_t p = { .mask = P_ITEM };
    p.v.source = c.source;
    p.v.index = c.index[c.source];
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    p.v.count = (int32_t)count_of(c.source);
    const char *u = irm_playlist_uri(c.lists[c.source], (size_t)p.v.index);
    if (u) { strlcpy(p.v.uri, u, sizeof(p.v.uri)); strlcpy(p.v.name, irm_playlist_name(c.lists[c.source], (size_t)p.v.index), sizeof(p.v.name)); }
    else p.v.index = -1;
    xSemaphoreGive(c.list_lock);
    publish(&p);
}

static void schedule_persist(bool vol, bool idx)
{
    c.dirty_vol |= vol; c.dirty_idx |= idx;
    c.persist_deadline_us = esp_timer_get_time() + (int64_t)CONFIG_IRM_PERSIST_DELAY_MS * 1000;
}

static void start_current(void)
{
    irm_status_t st; irm_status_get(&st);
    if (!c.want_play || st.index < 0) { irm_audio_stop(); set_player(IRM_PLAYER_IDLE); return; }
    if (c.source == IRM_SOURCE_RADIO && !c.net_up) { ESP_LOGI(TAG, "warte auf Netzwerk"); irm_audio_stop(); set_player(IRM_PLAYER_IDLE); return; }
    set_player(IRM_PLAYER_CONNECTING);
    if (irm_audio_play(st.uri) != ESP_OK) {
        set_player(IRM_PLAYER_ERROR);
        goto retry;
    }
    return;
retry:
    if (c.source == IRM_SOURCE_RADIO) {
        esp_timer_stop(c.retry_timer);
        esp_timer_start_once(c.retry_timer, (uint64_t)c.retry_s * 1000000ULL);
        c.retry_s = (c.retry_s * 2 > CONFIG_IRM_RETRY_MAX_S) ? CONFIG_IRM_RETRY_MAX_S : c.retry_s * 2;
    }
}

static void select_index(int32_t idx)
{
    int32_t n = (int32_t)irm_control_item_count(c.source);
    if (n == 0) { c.index[c.source] = -1; publish_item(); irm_audio_stop(); set_player(IRM_PLAYER_IDLE); return; }
    c.index[c.source] = ((idx % n) + n) % n;
    c.retry_s = 1;
    c.want_play = true;
    publish_item();
    start_current();
    if (c.source == IRM_SOURCE_RADIO) schedule_persist(false, true);
}

static void switch_source(irm_source_t src)
{
    if (src == c.source) return;
    if (src == IRM_SOURCE_MEDIA) {
        if (!irm_board_sd_mounted()) { ESP_LOGW(TAG, "Mediaplayer: keine SD-Karte"); return; }
        irm_playlist_t *pl = irm_playlist_new(CONFIG_IRM_MAX_MEDIA_FILES);
        if (!pl || irm_files_scan_media(IRM_SD_MOUNT_POINT, CONFIG_IRM_MEDIA_EXTENSIONS, 4, pl) != ESP_OK) {
            ESP_LOGW(TAG, "Mediaplayer: keine Dateien gefunden");
            irm_playlist_free(pl);
            return;
        }
        xSemaphoreTake(c.list_lock, portMAX_DELAY);
        irm_playlist_free(c.lists[IRM_SOURCE_MEDIA]);
        c.lists[IRM_SOURCE_MEDIA] = pl;
        xSemaphoreGive(c.list_lock);
        c.index[IRM_SOURCE_MEDIA] = 0;
    }
    c.source = src;
    select_index(c.index[src] < 0 ? 0 : c.index[src]);
}

static void on_audio(const irm_cmd_t *cmd)
{
    irm_status_t st; irm_status_get(&st);
    switch ((irm_audio_evt_type_t)cmd->audio.type) {
    case IRM_AUDIO_EVT_INFO: {
        patch_t p = { .mask = P_FMT };
        p.v.sample_rate = cmd->audio.rate; p.v.channels = cmd->audio.ch; p.v.bits = cmd->audio.bits; p.v.bitrate = cmd->audio.bitrate;
        publish(&p);
        break;
    }
    case IRM_AUDIO_EVT_RUNNING:
        if (st.player == IRM_PLAYER_CONNECTING || st.player == IRM_PLAYER_PLAYING) { set_player(IRM_PLAYER_PLAYING); c.retry_s = 1; }
        break;
    case IRM_AUDIO_EVT_FINISHED:
        if (st.player != IRM_PLAYER_PLAYING && st.player != IRM_PLAYER_CONNECTING) break;
        if (c.source == IRM_SOURCE_MEDIA) select_index(c.index[c.source] + 1);   /* naechster Titel */
        else { set_player(IRM_PLAYER_ERROR); esp_timer_start_once(c.retry_timer, (uint64_t)c.retry_s * 1000000ULL); }
        break;
    case IRM_AUDIO_EVT_ERROR:
        if (st.player != IRM_PLAYER_PLAYING && st.player != IRM_PLAYER_CONNECTING) break;
        ESP_LOGW(TAG, "Wiedergabefehler (RAM frei %u KiB, PSRAM frei %u KiB, groesster PSRAM-Block %u KiB)",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
        set_player(IRM_PLAYER_ERROR);
        if (c.source == IRM_SOURCE_RADIO) {
            esp_timer_stop(c.retry_timer);
            esp_timer_start_once(c.retry_timer, (uint64_t)c.retry_s * 1000000ULL);
            c.retry_s = (c.retry_s * 2 > CONFIG_IRM_RETRY_MAX_S) ? CONFIG_IRM_RETRY_MAX_S : c.retry_s * 2;
        } else select_index(c.index[c.source] + 1);   /* defekte Datei ueberspringen */
        break;
    default: break;
    }
}

static void set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    c.volume = (uint8_t)v;
    irm_audio_set_volume(c.volume);
    patch_t p = { .mask = P_VOL }; p.v.volume = c.volume; publish(&p);
    schedule_persist(true, false);
}

static void set_eq(const irm_eq_t *eq)
{
    c.eq = *eq;
    irm_audio_set_eq(&c.eq);
    patch_t p = { .mask = P_EQ, .v.eq = c.eq }; publish(&p);
}

static void handle(const irm_cmd_t *m)
{
    irm_status_t st; irm_status_get(&st);
    switch (m->id) {
    case IRM_CMD_NEXT: select_index(c.index[c.source] + 1); break;
    case IRM_CMD_PREV: select_index(c.index[c.source] - 1); break;
    case IRM_CMD_GOTO: select_index(m->value); break;
    case IRM_CMD_PLAY:
        c.want_play = true;
        if (st.player == IRM_PLAYER_PAUSED && c.source == IRM_SOURCE_MEDIA) { irm_audio_resume(); set_player(IRM_PLAYER_PLAYING); }
        else if (st.player != IRM_PLAYER_PLAYING && st.player != IRM_PLAYER_CONNECTING) { c.retry_s = 1; start_current(); }
        break;
    case IRM_CMD_PAUSE:
        c.want_play = false;
        esp_timer_stop(c.retry_timer);
        if (c.source == IRM_SOURCE_MEDIA) irm_audio_pause();
        else irm_audio_stop();        /* Live-Stream: pausieren sinnlos -> trennen */
        set_player(IRM_PLAYER_PAUSED);
        break;
    case IRM_CMD_TOGGLE_PAUSE: {
        irm_cmd_t n = { .id = (st.player == IRM_PLAYER_PLAYING || st.player == IRM_PLAYER_CONNECTING) ? IRM_CMD_PAUSE : IRM_CMD_PLAY };
        handle(&n);
        break;
    }
    case IRM_CMD_STOP:
        c.want_play = false; esp_timer_stop(c.retry_timer); irm_audio_stop(); set_player(IRM_PLAYER_IDLE); break;
    case IRM_CMD_VOLUME_UP:   set_volume(c.volume + CONFIG_IRM_VOLUME_STEP); break;
    case IRM_CMD_VOLUME_DOWN: set_volume((int)c.volume - CONFIG_IRM_VOLUME_STEP); break;
    case IRM_CMD_SET_VOLUME:  set_volume(m->value); break;
    case IRM_CMD_SET_SOURCE:  switch_source(m->value == IRM_SOURCE_MEDIA ? IRM_SOURCE_MEDIA : IRM_SOURCE_RADIO); break;
    case IRM_CMD_TOGGLE_SOURCE: switch_source(c.source == IRM_SOURCE_RADIO ? IRM_SOURCE_MEDIA : IRM_SOURCE_RADIO); break;
    case IRM_CMD_SET_EQ:   set_eq(&m->eq); break;
    case IRM_CMD_RESET_EQ: { irm_eq_t z = { 0 }; set_eq(&z); break; }
    case IRM_CMD_SAVE_EQ:  irm_settings_save_eq(&c.eq); break;
    case IRM_CMD_INTERNAL_AUDIO_EVENT: on_audio(m); break;
    case IRM_CMD_INTERNAL_NET_CHANGED:
        c.net_up = m->value != 0;
        if (c.net_up && c.want_play && c.source == IRM_SOURCE_RADIO &&
            (st.player == IRM_PLAYER_IDLE || st.player == IRM_PLAYER_ERROR)) { c.retry_s = 1; start_current(); }
        break;
    case IRM_CMD_INTERNAL_RETRY:
        if (c.want_play && c.source == IRM_SOURCE_RADIO && c.net_up && st.player == IRM_PLAYER_ERROR) start_current();
        break;
    default: break;
    }
}

static void audio_cb(const irm_audio_evt_t *e, void *ctx)
{
    (void)ctx;
    irm_cmd_t m = { .id = IRM_CMD_INTERNAL_AUDIO_EVENT };
    m.audio.type = e->type; m.audio.rate = e->sample_rate; m.audio.ch = e->channels; m.audio.bits = e->bits; m.audio.bitrate = e->bitrate;
    irm_cmd_post(&m, 0);
}
static void retry_cb(void *arg) { (void)arg; irm_cmd_post_id(IRM_CMD_INTERNAL_RETRY, 0); }

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (c.persist_deadline_us) {
            int64_t left = c.persist_deadline_us - esp_timer_get_time();
            wait = left <= 0 ? 0 : pdMS_TO_TICKS(left / 1000) + 1;
        }
        irm_cmd_t m;
        if (irm_cmd_receive(&m, wait) == ESP_OK) handle(&m);
        if (c.persist_deadline_us && esp_timer_get_time() >= c.persist_deadline_us) {
            if (c.dirty_vol) irm_settings_save_volume(c.volume);
            if (c.dirty_idx && c.index[IRM_SOURCE_RADIO] >= 0) irm_settings_save_station(c.index[IRM_SOURCE_RADIO]);
            c.dirty_vol = c.dirty_idx = false; c.persist_deadline_us = 0;
        }
    }
}

esp_err_t irm_control_start(void)
{
    c.list_lock = xSemaphoreCreateMutex();
    c.index[0] = c.index[1] = -1;
    c.want_play = true; c.retry_s = 1;
    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "irm_retry" };
    if (!c.list_lock || esp_timer_create(&ta, &c.retry_timer) != ESP_OK) return ESP_ERR_NO_MEM;

    irm_settings_t cfg; irm_settings_load(&cfg);
    c.volume = cfg.volume;
    if (cfg.has_eq) c.eq = cfg.eq;

    c.lists[IRM_SOURCE_RADIO] = irm_playlist_new(CONFIG_IRM_MAX_STATIONS);
    if (irm_board_sd_mounted()) irm_files_load_playlist(IRM_SD_MOUNT_POINT "/playlist.m3u", c.lists[IRM_SOURCE_RADIO]);
    c.lists[IRM_SOURCE_MEDIA] = NULL;
    int n = (int)count_of(IRM_SOURCE_RADIO);
    if (n == 0) ESP_LOGW(TAG, "keine Sender: /sdcard/playlist.m3u fehlt oder ist leer");
    c.index[IRM_SOURCE_RADIO] = n ? (cfg.station >= 0 && cfg.station < n ? cfg.station : 0) : -1;

    esp_err_t e = irm_audio_init(irm_board_codec(), irm_board_sink(), audio_cb, NULL);
    if (e != ESP_OK) return e;
    irm_audio_set_volume(c.volume);
    irm_audio_set_eq(&c.eq);
    patch_t p = { .mask = P_VOL | P_EQ }; p.v.volume = c.volume; p.v.eq = c.eq; publish(&p);
    publish_item();

    if (xTaskCreate(task, "irm_ctrl", 6144, NULL, 6, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    irm_cmd_post_id(IRM_CMD_PLAY, 0);   /* Startwiedergabe; wartet intern auf Netz */
    return ESP_OK;
}
