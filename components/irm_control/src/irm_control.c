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
#if CONFIG_IRM_KIWI_ENABLE
#include "irm_kiwi.h"
#include "irm_kiwi_uri.h"
#endif
#if CONFIG_IRM_DIR_ENABLE
#include "irm_dir.h"
#endif

static const char *TAG = "irm_ctrl";

enum { P_PLAYER = 1, P_ITEM = 2, P_VOL = 4, P_FMT = 8, P_EQ = 16 };
typedef struct { unsigned mask; irm_status_t v; } patch_t;

static struct {
    SemaphoreHandle_t  list_lock;
    irm_playlist_t    *lists[IRM_SOURCE_COUNT];
    irm_source_t       source;
    int32_t            index[IRM_SOURCE_COUNT];
    bool               want_play;
    bool               net_up;
    uint8_t            volume;
    irm_eq_t           eq;
    int64_t            persist_deadline_us;  /* 0 = nichts offen */
    bool               dirty_vol, dirty_idx, dirty_kiwi;
    bool               kiwi_ok;      /* irm_kiwi initialisiert */
    bool               dyn_valid;    /* "Direkteintrag": im Verzeichnis gewaehlter Server, steht hinter den Favoriten (virtueller Index = Anzahl Favoriten) */
    char               dyn_name[IRM_NAME_MAX_LEN], dyn_uri[IRM_URI_MAX_LEN];
    bool               kiwi_fresh;   /* naechster Kiwi-Start: Abstimmung aus dem Eintrag statt der zuletzt eingestellten */
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
        s->kiwi_freq_hz = 0; s->kiwi_mode[0] = 0; s->kiwi_rssi_dbm = IRM_RSSI_NONE;   /* irm_kiwi veroeffentlicht sie nach dem Start neu */
    }
    if (p->mask & P_VOL) s->volume = p->v.volume;
    if (p->mask & P_FMT) { s->sample_rate = p->v.sample_rate; s->channels = p->v.channels; s->bits = p->v.bits; s->bitrate = p->v.bitrate; }
    if (p->mask & P_EQ) s->eq = p->v.eq;
}
static void publish(patch_t *p) { irm_status_update(apply_patch, p); }
static void set_player(irm_player_state_t st) { patch_t p = { .mask = P_PLAYER }; p.v.player = st; publish(&p); }

/* Eintraege einer Quelle = Playlist (+ bei KiwiSDR der Direkteintrag aus dem Verzeichnis als letzter virtueller Eintrag). Aufrufer halten list_lock. */
static bool is_dyn(irm_source_t s, size_t idx) { return s == IRM_SOURCE_KIWI && c.dyn_valid && idx == irm_playlist_count(c.lists[s]); }
static size_t count_of(irm_source_t s) { return irm_playlist_count(c.lists[s]) + ((s == IRM_SOURCE_KIWI && c.dyn_valid) ? 1u : 0u); }
static const char *item_uri(irm_source_t s, size_t idx) { return is_dyn(s, idx) ? c.dyn_uri : irm_playlist_uri(c.lists[s], idx); }
static const char *item_name(irm_source_t s, size_t idx) { return is_dyn(s, idx) ? c.dyn_name : irm_playlist_name(c.lists[s], idx); }

/* Radio und KiwiSDR sind Live-Quellen (Netz noetig, Pause = trennen, Fehler -> Wiederholen); nur Dateien sind abspielbar/pausierbar. */
static bool is_live(irm_source_t s) { return s != IRM_SOURCE_MEDIA; }

/* Beide Engines teilen sich die Ausgabesenke: vor jedem Start muss die jeweils andere freigegeben sein. */
static void stop_engines(void)
{
    irm_audio_stop();
#if CONFIG_IRM_KIWI_ENABLE
    irm_kiwi_stop();
#endif
}

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
    const char *u = item_uri(src, idx);
    if (u) {
        if (uri && usz) strlcpy(uri, u, usz);
        if (name && nsz) strlcpy(name, item_name(src, idx), nsz);
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
    const char *u = item_uri(c.source, (size_t)p.v.index);
    if (u) { strlcpy(p.v.uri, u, sizeof(p.v.uri)); strlcpy(p.v.name, item_name(c.source, (size_t)p.v.index), sizeof(p.v.name)); }
    else p.v.index = -1;
    xSemaphoreGive(c.list_lock);
    publish(&p);
}

static void schedule_persist(bool vol, bool idx, bool kiwi)
{
    c.dirty_vol |= vol; c.dirty_idx |= idx; c.dirty_kiwi |= kiwi;
    c.persist_deadline_us = esp_timer_get_time() + (int64_t)CONFIG_IRM_PERSIST_DELAY_MS * 1000;
}

static void start_current(void)
{
    irm_status_t st; irm_status_get(&st);
    if (!c.want_play || st.index < 0) { stop_engines(); set_player(IRM_PLAYER_IDLE); return; }
    if (is_live(c.source) && !c.net_up) { ESP_LOGI(TAG, "warte auf Netzwerk"); stop_engines(); set_player(IRM_PLAYER_IDLE); return; }
    set_player(IRM_PLAYER_CONNECTING);
    esp_err_t e;
#if CONFIG_IRM_KIWI_ENABLE
    if (c.source == IRM_SOURCE_KIWI) {
        irm_audio_stop();                       /* Senke freigeben, dann die Kiwi-Sitzung starten */
        const bool fresh = c.kiwi_fresh;
        c.kiwi_fresh = false;
        e = irm_kiwi_play(st.uri, fresh);
        if (e == ESP_ERR_INVALID_ARG) {         /* kaputter Eintrag: Wiederholen waere sinnlos */
            ESP_LOGW(TAG, "KiwiSDR-Eintrag ungueltig: %s", st.uri);
            set_player(IRM_PLAYER_ERROR);
            return;
        }
    } else {
        irm_kiwi_stop();                        /* Kiwi-Sitzung beenden und Senke freigeben */
        e = irm_audio_play(st.uri);
    }
#else
    e = irm_audio_play(st.uri);
#endif
    if (e != ESP_OK) {
        set_player(IRM_PLAYER_ERROR);
        goto retry;
    }
    return;
retry:
    if (is_live(c.source)) {
        esp_timer_stop(c.retry_timer);
        esp_timer_start_once(c.retry_timer, (uint64_t)c.retry_s * 1000000ULL);
        c.retry_s = (c.retry_s * 2 > CONFIG_IRM_RETRY_MAX_S) ? CONFIG_IRM_RETRY_MAX_S : c.retry_s * 2;
    }
}

static void select_index(int32_t idx)
{
    int32_t n = (int32_t)irm_control_item_count(c.source);
    if (n == 0) { c.index[c.source] = -1; publish_item(); stop_engines(); set_player(IRM_PLAYER_IDLE); return; }
    c.index[c.source] = ((idx % n) + n) % n;
    c.retry_s = 1;
    c.want_play = true;
    c.kiwi_fresh = true;                        /* neuer Eintrag: Frequenz/Modus aus der Liste */
    publish_item();
    start_current();
    if (c.source == IRM_SOURCE_RADIO) schedule_persist(false, true, false);
    else if (c.source == IRM_SOURCE_KIWI) schedule_persist(false, false, true);
}

/* true, wenn die Quelle jetzt aktiv ist (oder es schon war) */
static bool switch_source(irm_source_t src)
{
    if (src == c.source) return true;
    if (src == IRM_SOURCE_MEDIA) {
        if (!irm_board_sd_mounted()) { ESP_LOGW(TAG, "Mediaplayer: keine SD-Karte"); return false; }
        irm_playlist_t *pl = irm_playlist_new(CONFIG_IRM_MAX_MEDIA_FILES);
        if (!pl || irm_files_scan_media(IRM_SD_MOUNT_POINT, CONFIG_IRM_MEDIA_EXTENSIONS, 4, pl) != ESP_OK) {
            ESP_LOGW(TAG, "Mediaplayer: keine Dateien gefunden");
            irm_playlist_free(pl);
            return false;
        }
        xSemaphoreTake(c.list_lock, portMAX_DELAY);
        irm_playlist_free(c.lists[IRM_SOURCE_MEDIA]);
        c.lists[IRM_SOURCE_MEDIA] = pl;
        xSemaphoreGive(c.list_lock);
        c.index[IRM_SOURCE_MEDIA] = 0;
    } else if (src == IRM_SOURCE_KIWI) {
        if (!c.kiwi_ok || count_of(IRM_SOURCE_KIWI) == 0) { ESP_LOGW(TAG, "KiwiSDR: keine Eintraege (kiwi.m3u fehlt/leer oder deaktiviert)"); return false; }
    }
    c.source = src;
    select_index(c.index[src] < 0 ? 0 : c.index[src]);
    return true;
}

/* Quellen reihum durchschalten; nicht verfuegbare werden uebersprungen */
static void cycle_source(void)
{
    for (int i = 1; i < IRM_SOURCE_COUNT; i++)
        if (switch_source((irm_source_t)((c.source + i) % IRM_SOURCE_COUNT))) return;
}

static void on_audio(const irm_cmd_t *cmd)
{
    /* Nachzuegler der anderen Engine (z. B. Fehler einer gerade beendeten Kiwi-Sitzung nach dem Wechsel zum Radio) ignorieren */
    if ((cmd->audio.origin == 1) != (c.source == IRM_SOURCE_KIWI)) return;
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
        if (is_live(c.source)) {
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
    schedule_persist(true, false, false);
}

static void set_eq(const irm_eq_t *eq)
{
    c.eq = *eq;
    irm_audio_set_eq(&c.eq);
    patch_t p = { .mask = P_EQ, .v.eq = c.eq }; publish(&p);
}

#if CONFIG_IRM_DIR_ENABLE
/* Verzeichnisserver waehlen: Quelle KiwiSDR, aktuelle Frequenz/Modus bleiben (wer schon auf 7055 kHz USB hoert, hoert dort weiter, nur von einem anderen Empfaenger) */
static void kiwi_connect_dir(uint32_t db_idx)
{
    kiwi_dir_rec_t rec;
    if (!c.kiwi_ok || !irm_dir_get(db_idx, &rec)) { ESP_LOGW(TAG, "Verzeichnis: Eintrag %u nicht lesbar", (unsigned)db_idx); return; }
    irm_status_t st; irm_status_get(&st);
    uint32_t f = (uint32_t)CONFIG_IRM_DIR_DEFAULT_FREQ_KHZ * 1000u;
    char mode[8];
    strlcpy(mode, CONFIG_IRM_DIR_DEFAULT_MODE, sizeof(mode));
    if (st.source == IRM_SOURCE_KIWI && st.kiwi_freq_hz) { f = st.kiwi_freq_hz; strlcpy(mode, st.kiwi_mode, sizeof(mode)); }
    char uri[IRM_URI_MAX_LEN], name[IRM_NAME_MAX_LEN];
    uint32_t used = 0;
    if (!irm_dir_build_uri(&rec, f, mode, uri, sizeof(uri), &used)) { ESP_LOGW(TAG, "Verzeichnis: URI fuer %s nicht baubar", rec.host); return; }
    if (used != f) ESP_LOGI(TAG, "Verzeichnis: %u Hz liegt ausserhalb des Empfangsbereichs von %s, verwende %u Hz", (unsigned)f, rec.host, (unsigned)used);
    irm_dir_display_name(&rec, name, sizeof(name));
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    strlcpy(c.dyn_uri, uri, sizeof(c.dyn_uri));
    strlcpy(c.dyn_name, name, sizeof(c.dyn_name));
    c.dyn_valid = true;
    const size_t favs = irm_playlist_count(c.lists[IRM_SOURCE_KIWI]);
    xSemaphoreGive(c.list_lock);
    c.source = IRM_SOURCE_KIWI;      /* switch_source() wuerde zuerst den alten Eintrag starten */
    select_index((int32_t)favs);
}

/* Aktuellen Kiwi-Eintrag samt eingestellter Frequenz/Modus als Favorit in /kiwi.m3u ablegen */
static void kiwi_add_favorite(void)
{
    irm_status_t st; irm_status_get(&st);
    if (c.source != IRM_SOURCE_KIWI || st.index < 0 || !st.kiwi_freq_hz) { ESP_LOGW(TAG, "Favorit: kein KiwiSDR-Eintrag aktiv"); return; }
    if (!irm_board_sd_mounted()) { ESP_LOGW(TAG, "Favorit: keine SD-Karte"); return; }
    irm_kiwi_uri_t u;
    char uri[IRM_URI_MAX_LEN];
    if (!irm_kiwi_uri_parse(st.uri, &u)) return;
    u.freq_hz = st.kiwi_freq_hz;
    strlcpy(u.mode, st.kiwi_mode, sizeof(u.mode));
    if (!irm_kiwi_uri_build(&u, uri, sizeof(uri))) { ESP_LOGW(TAG, "Favorit: URI nicht baubar"); return; }
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    bool dup = false;
    const size_t n = irm_playlist_count(c.lists[IRM_SOURCE_KIWI]);
    for (size_t i = 0; i < n; i++) if (!strcmp(irm_playlist_uri(c.lists[IRM_SOURCE_KIWI], i), uri)) dup = true;
    xSemaphoreGive(c.list_lock);
    if (dup) { ESP_LOGI(TAG, "Favorit: %s ist bereits gespeichert", uri); return; }
    if (irm_files_append_playlist(IRM_SD_MOUNT_POINT "/kiwi.m3u", st.name, uri) != ESP_OK) { ESP_LOGW(TAG, "Favorit: kiwi.m3u nicht beschreibbar"); return; }
    xSemaphoreTake(c.list_lock, portMAX_DELAY);
    const bool was_dyn = is_dyn(IRM_SOURCE_KIWI, (size_t)st.index);
    const bool added = irm_playlist_add(c.lists[IRM_SOURCE_KIWI], uri, st.name);
    if (added && was_dyn) c.dyn_valid = false;   /* der Direkteintrag ist jetzt ein Favorit, an derselben Indexposition */
    xSemaphoreGive(c.list_lock);
    if (added) ESP_LOGI(TAG, "Favorit gespeichert: %s", uri);
    else ESP_LOGW(TAG, "Favorit in kiwi.m3u, aber Liste im Speicher voll (nach Neustart verfuegbar): %s", uri);
    if (added) publish_item();
}
#endif

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
        else stop_engines();          /* Live-Stream (Radio/KiwiSDR): pausieren sinnlos -> trennen */
        set_player(IRM_PLAYER_PAUSED);
        break;
    case IRM_CMD_TOGGLE_PAUSE: {
        irm_cmd_t n = { .id = (st.player == IRM_PLAYER_PLAYING || st.player == IRM_PLAYER_CONNECTING) ? IRM_CMD_PAUSE : IRM_CMD_PLAY };
        handle(&n);
        break;
    }
    case IRM_CMD_STOP:
        c.want_play = false; esp_timer_stop(c.retry_timer); stop_engines(); set_player(IRM_PLAYER_IDLE); break;
    case IRM_CMD_VOLUME_UP:   set_volume(c.volume + CONFIG_IRM_VOLUME_STEP); break;
    case IRM_CMD_VOLUME_DOWN: set_volume((int)c.volume - CONFIG_IRM_VOLUME_STEP); break;
    case IRM_CMD_SET_VOLUME:  set_volume(m->value); break;
    case IRM_CMD_SET_SOURCE:  switch_source(m->value >= 0 && m->value < IRM_SOURCE_COUNT ? (irm_source_t)m->value : IRM_SOURCE_RADIO); break;
    case IRM_CMD_TOGGLE_SOURCE: cycle_source(); break;
#if CONFIG_IRM_KIWI_ENABLE
    case IRM_CMD_KIWI_TUNE: if (c.source == IRM_SOURCE_KIWI && m->value > 0) irm_kiwi_tune((uint32_t)m->value, NULL); break;
    case IRM_CMD_KIWI_STEP: if (c.source == IRM_SOURCE_KIWI) irm_kiwi_tune_step(m->value); break;
    case IRM_CMD_KIWI_MODE: if (c.source == IRM_SOURCE_KIWI) irm_kiwi_set_mode_index(m->value); break;
#if CONFIG_IRM_DIR_ENABLE
    case IRM_CMD_KIWI_CONNECT:  if (m->value >= 0) kiwi_connect_dir((uint32_t)m->value); break;
    case IRM_CMD_KIWI_FAVORITE: kiwi_add_favorite(); break;
#endif
#endif
    case IRM_CMD_SET_EQ:   set_eq(&m->eq); break;
    case IRM_CMD_RESET_EQ: { irm_eq_t z = { 0 }; set_eq(&z); break; }
    case IRM_CMD_SAVE_EQ:  irm_settings_save_eq(&c.eq); break;
    case IRM_CMD_INTERNAL_AUDIO_EVENT: on_audio(m); break;
    case IRM_CMD_INTERNAL_NET_CHANGED:
        c.net_up = m->value != 0;
        if (c.net_up && c.want_play && is_live(c.source) &&
            (st.player == IRM_PLAYER_IDLE || st.player == IRM_PLAYER_ERROR)) { c.retry_s = 1; start_current(); }
        break;
    case IRM_CMD_INTERNAL_RETRY:
        if (c.want_play && is_live(c.source) && c.net_up && st.player == IRM_PLAYER_ERROR) start_current();
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

#if CONFIG_IRM_KIWI_ENABLE
/* irm_kiwi-Ereignisse laufen ueber denselben Kommandoweg wie die der Audio-Pipeline (origin = 1) */
static void kiwi_cb(const irm_kiwi_evt_t *e, void *ctx)
{
    (void)ctx;
    irm_cmd_t m = { .id = IRM_CMD_INTERNAL_AUDIO_EVENT };
    m.audio.origin = 1;
    switch (e->type) {
    case IRM_KIWI_EVT_INFO:    m.audio.type = IRM_AUDIO_EVT_INFO; m.audio.rate = e->sample_rate; m.audio.ch = 1; m.audio.bits = 16; m.audio.bitrate = e->bitrate; break;
    case IRM_KIWI_EVT_RUNNING: m.audio.type = IRM_AUDIO_EVT_RUNNING; break;
    case IRM_KIWI_EVT_ERROR:   m.audio.type = IRM_AUDIO_EVT_ERROR; break;
    default: return;
    }
    irm_cmd_post(&m, 0);
}


/* kiwi.m3u laden; ungueltige Zeilen werden gemeldet und ausgelassen, damit sie spaeter nicht endlos "wiederholt" werden */
static void load_kiwi_list(void)
{
    irm_playlist_t *raw = irm_playlist_new(CONFIG_IRM_KIWI_MAX_PRESETS), *ok = irm_playlist_new(CONFIG_IRM_KIWI_MAX_PRESETS);
    if (!raw || !ok) { irm_playlist_free(raw); irm_playlist_free(ok); return; }
    if (irm_board_sd_mounted()) irm_files_load_playlist(IRM_SD_MOUNT_POINT "/kiwi.m3u", raw);
    irm_kiwi_uri_t t;
    for (size_t i = 0; i < irm_playlist_count(raw); i++) {
        const char *u = irm_playlist_uri(raw, i);
        if (irm_kiwi_uri_parse(u, &t)) irm_playlist_add(ok, u, irm_playlist_name(raw, i));
        else ESP_LOGW(TAG, "kiwi.m3u: Eintrag %u ungueltig, uebersprungen: %s", (unsigned)(i + 1), u);
    }
    irm_playlist_free(raw);
    if (irm_playlist_count(ok) == 0 && CONFIG_IRM_KIWI_DEFAULT_URI[0]) {
        if (irm_kiwi_uri_parse(CONFIG_IRM_KIWI_DEFAULT_URI, &t)) irm_playlist_add(ok, CONFIG_IRM_KIWI_DEFAULT_URI, "KiwiSDR");
        else ESP_LOGW(TAG, "IRM_KIWI_DEFAULT_URI ist ungueltig");
    }
    c.lists[IRM_SOURCE_KIWI] = ok;
}
#endif
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
            if (c.dirty_kiwi && c.index[IRM_SOURCE_KIWI] >= 0) irm_settings_save_kiwi_station(c.index[IRM_SOURCE_KIWI]);
            c.dirty_vol = c.dirty_idx = c.dirty_kiwi = false; c.persist_deadline_us = 0;
        }
    }
}

esp_err_t irm_control_start(void)
{
    c.list_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < IRM_SOURCE_COUNT; i++) c.index[i] = -1;
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
#if CONFIG_IRM_KIWI_ENABLE
    if (irm_kiwi_init(irm_board_sink(), kiwi_cb, NULL) == ESP_OK) {
        c.kiwi_ok = true;
#if CONFIG_IRM_DIR_ENABLE
        irm_dir_init();
#endif
        load_kiwi_list();
        const int kn = (int)count_of(IRM_SOURCE_KIWI);
        c.index[IRM_SOURCE_KIWI] = kn ? (cfg.kiwi_station >= 0 && cfg.kiwi_station < kn ? cfg.kiwi_station : 0) : -1;
        ESP_LOGI(TAG, "KiwiSDR: %d Eintraege", kn);
    } else ESP_LOGW(TAG, "KiwiSDR-Quelle nicht verfuegbar");
#endif
    irm_audio_set_volume(c.volume);
    irm_audio_set_eq(&c.eq);
    patch_t p = { .mask = P_VOL | P_EQ }; p.v.volume = c.volume; p.v.eq = c.eq; publish(&p);
    publish_item();

    if (xTaskCreate(task, "irm_ctrl", 6144, NULL, 6, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    irm_cmd_post_id(IRM_CMD_PLAY, 0);   /* Startwiedergabe; wartet intern auf Netz */
    return ESP_OK;
}
