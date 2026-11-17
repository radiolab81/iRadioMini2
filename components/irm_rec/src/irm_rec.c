#include "irm_rec.h"
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_IRM_REC_ENABLE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "irm_board.h"
#include "irm_core.h"
#include "irm_wav.h"

static const char *TAG = "irm_rec";

#if CONFIG_IRM_REC_HEAP_CHECK
#define HEAP_CHECK(where) do { const bool ok_ = heap_caps_check_integrity_all(true); ESP_LOGW(TAG, "Heap-Pruefung (%s): %s", (where), ok_ ? "OK" : "BESCHAEDIGT"); } while (0)
#else
#define HEAP_CHECK(where) do { } while (0)
#endif

#define CHUNK_BYTES     (16 * 1024)   /* = FAT-Clustergroesse der Karte (irm_board: allocation_unit_size), Vielfaches von 4 (Stereo-Frame) */
#define RX_TIMEOUT_MS   200
#define STOP_WAIT_MS    5000
#define LOCK_WAIT_MS    5             /* Audio-Pfad: hoechstens so lange auf die Haltezeit des Schlosses (nur Speicherkopien) warten */
#define LED_ON          (CONFIG_IRM_REC_LED_GPIO >= 0)

enum { M_OFF = 0, M_SINK, M_KIWI };   /* woher die Samples kommen: Ausgabesenke (Radio, Stereo) oder KiwiSDR-Abgriff (12 kHz Mono) */

static struct {
    bool               inited;
    SemaphoreHandle_t  lock;          /* schuetzt alles Folgende; nie waehrend Dateizugriffen gehalten */
    irm_out_sink_t    *sink;
    esp_err_t        (*orig_write)(irm_out_sink_t *, const int16_t *, size_t);
    volatile int       mode;
    volatile bool      running;       /* Task lebt (bis zu seinem letzten Handgriff) */
    volatile bool      stop_req;
    StreamBufferHandle_t sb;
    uint8_t           *store;         /* Speicher des Ringpuffers (selbst belegt, siehe sb_create) */
    uint32_t           rate;          /* vom ersten feed() festgelegt; 0 = noch kein Ton */
    uint16_t           ch;
    volatile uint32_t  dropped;
    irm_rec_info_t     info;
} r;

/* ---------------------------------------------------------------- Ringpuffer
 * Bewusst NICHT xStreamBufferCreateWithCaps: Dessen Kontrollblock waere ein kleiner Heap-Block, der bei jedem Aufnahmeende frei gegeben wird.
 * Hier liegt der Kontrollblock statisch (.bss, nie freigegeben); nur der Datenspeicher wird belegt/freigegeben (ein einziger Block). */

static StaticStreamBuffer_t s_sb_ctrl;

static StreamBufferHandle_t sb_create(size_t sz, uint8_t **store_out)
{
    uint8_t *st = NULL;
#if !CONFIG_IRM_REC_BUFFER_INTERNAL
    st = heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);     /* +1: Anforderung von xStreamBufferCreateStatic */
#endif
    if (!st) st = heap_caps_malloc(sz + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!st) return NULL;
    StreamBufferHandle_t sb = xStreamBufferCreateStatic(sz, 4096, st, &s_sb_ctrl);
    if (!sb) { heap_caps_free(st); return NULL; }
    *store_out = st;
    return sb;
}

static void sb_destroy(StreamBufferHandle_t sb, uint8_t *store)
{
    vStreamBufferDelete(sb);          /* Statischer Puffer: gibt nichts frei, macht ihn nur ungueltig */
    heap_caps_free(store);
}

/* ---------------------------------------------------------------- Status */

typedef struct { bool on; uint32_t secs; } rp_t;
static void mut_rec(irm_status_t *st, void *ctx) { const rp_t *p = ctx; st->rec = p->on; st->rec_secs = p->secs; }
static void publish(bool on, uint32_t secs) { rp_t p = { on, secs }; irm_status_update(mut_rec, &p); }

static void led(int on) { if (LED_ON) gpio_set_level(CONFIG_IRM_REC_LED_GPIO, on); }

/* ---------------------------------------------------------------- Abgriff (Audio-Pfad: nie blockieren, nie auf die Karte warten) */

static void feed(int mode, const int16_t *pcm, size_t samples, uint32_t rate, uint16_t ch)
{
    if (r.mode != mode || !samples) return;
    const size_t bytes = samples * sizeof(int16_t);
    if (xSemaphoreTake(r.lock, pdMS_TO_TICKS(LOCK_WAIT_MS)) != pdTRUE) { r.dropped += (uint32_t)bytes; return; }
    if (r.mode == mode && r.sb && !r.stop_req) {
        if (!r.rate) { r.ch = ch; r.rate = rate; }
        /* nur ganze Bloecke: ein Teilschreiben wuerde den Strom gegen die Frame-Grenzen verschieben (Links/Rechts vertauscht) */
        if (xStreamBufferSpacesAvailable(r.sb) >= bytes) xStreamBufferSend(r.sb, pcm, bytes, 0);
        else r.dropped += (uint32_t)bytes;
    }
    xSemaphoreGive(r.lock);
}

static esp_err_t tap_write(irm_out_sink_t *sink, const int16_t *stereo, size_t frames)
{
    if (r.mode == M_SINK) feed(M_SINK, stereo, frames * 2, sink->sample_rate, 2);
    return r.orig_write(sink, stereo, frames);
}

void irm_rec_feed_kiwi(const int16_t *mono, size_t n, uint32_t rate_hz) { feed(M_KIWI, mono, n, rate_hz, 1); }

/* ---------------------------------------------------------------- Schreib-Task */

typedef struct { int fd; uint64_t data; } wf_t;   /* aktuelle Datei, data = Nutzbytes darin */

static int write_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        const ssize_t w = write(fd, p, n);
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

/* Kopf mit dem aktuellen Stand schreiben und zurueck ans Dateiende */
static int write_header(const wf_t *f, uint32_t rate, uint16_t ch)
{
    uint8_t h[IRM_WAV_HDR_SIZE];
    irm_wav_header(h, rate, ch, 16, f->data > IRM_WAV_DATA_MAX ? IRM_WAV_DATA_MAX : (uint32_t)f->data);
    if (lseek(f->fd, 0, SEEK_SET) < 0 || write_all(f->fd, h, sizeof(h)) != 0) return -1;
    return lseek(f->fd, (off_t)(IRM_WAV_HDR_SIZE + f->data), SEEK_SET) < 0 ? -1 : 0;
}

static int sync_file(const wf_t *f, uint32_t rate, uint16_t ch)
{
    if (write_header(f, rate, ch) != 0) return -1;
    return fsync(f->fd);
}

static void close_file(wf_t *f, uint32_t rate, uint16_t ch)
{
    if (f->fd < 0) return;
    if (write_header(f, rate, ch) != 0) ESP_LOGW(TAG, "WAV-Kopf konnte nicht abschliessend geschrieben werden");
    fsync(f->fd);
    close(f->fd);
    f->fd = -1;
}

__attribute__((unused)) static bool open_file(wf_t *f, uint32_t rate, uint16_t ch, const char **why)
{
    char dir[64], path[128];
    snprintf(dir, sizeof(dir), "%s/%s", IRM_SD_MOUNT_POINT, CONFIG_IRM_REC_DIR);
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) { ESP_LOGE(TAG, "Ordner %s nicht anlegbar (errno %d)", dir, errno); *why = "Ordner nicht anlegbar"; return false; }
    const time_t t = time(NULL);
    struct tm tm;
    const bool have = irm_time_is_valid(t) && localtime_r(&t, &tm) != NULL;
    int fd = -1;
    path[0] = 0;
    /* O_EXCL: eine vorhandene Datei wird nie ueberschrieben (derselbe Zeitstempel beim Dateiwechsel -> _2, _3 ...; ohne Uhr rec_0001, rec_0002 ...) */
    for (unsigned seq = have ? 0 : 1; seq < 10000 && fd < 0; seq++) {
        if (have && seq == 1) continue;
        if (!irm_rec_filename(path, sizeof(path), dir, have ? &tm : NULL, seq)) break;
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) { ESP_LOGE(TAG, "Datei %s nicht anlegbar (errno %d)", path, errno); *why = "Datei nicht anlegbar"; return false; }
    f->fd = fd; f->data = 0;
    if (write_header(f, rate, ch) != 0) { ESP_LOGE(TAG, "Schreibfehler im Kopf von %s", path); close(fd); f->fd = -1; *why = "Schreibfehler"; return false; }
    const char *base = strrchr(path, '/');
    xSemaphoreTake(r.lock, portMAX_DELAY);
    strlcpy(r.info.file, base ? base + 1 : path, sizeof(r.info.file));
    r.info.files++;
    xSemaphoreGive(r.lock);
    ESP_LOGI(TAG, "Aufnahme: %s (%u Hz, %u Kanal, 16 Bit)%s", path, (unsigned)rate, (unsigned)ch, have ? "" : "  [Uhr nicht gestellt: Zaehlername]");
    return true;
}

static bool space_ok(void)
{
    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(IRM_SD_MOUNT_POINT, &total, &freeb) != ESP_OK) return true;   /* unbekannt: nicht deswegen abbrechen */
    return freeb >= (uint64_t)CONFIG_IRM_REC_MIN_FREE_MB * 1024u * 1024u;
}

static void rec_task(void *arg)
{
    (void)arg;
    uint8_t *buf = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) buf = malloc(CHUNK_BYTES);
    wf_t f = { .fd = -1 };
    const uint64_t max_bytes = (uint64_t)CONFIG_IRM_REC_MAX_FILE_MB * 1024u * 1024u;
    (void)max_bytes;
    uint64_t total = 0;
    uint32_t rate = 0;
    uint16_t ch = 0;
    const char *why = buf ? NULL : "Speicher";
    int64_t t_pub = esp_timer_get_time(), t_sync = t_pub;
    int led_state = -1;

    if (!why && !space_ok()) why = "Karte voll";
    while (!why) {
        const size_t n = xStreamBufferReceive(r.sb, buf, CHUNK_BYTES, pdMS_TO_TICKS(RX_TIMEOUT_MS));
        if (n) {
#if CONFIG_IRM_REC_DEBUG_NOWRITE
            rate = r.rate; ch = r.ch; total += n;             /* DEBUG: nur lesen und verwerfen, keine Datei */
#else
            if (f.fd < 0) {                                   /* erster Ton: Format steht jetzt fest, Datei anlegen */
                rate = r.rate; ch = r.ch;
                if (!open_file(&f, rate, ch, &why)) break;
            } else if (f.data + n > max_bytes) {              /* Dateiwechsel ohne Luecke: der Puffer fuellt sich solange */
                close_file(&f, rate, ch);
                if (!open_file(&f, rate, ch, &why)) break;
            }
            if (write_all(f.fd, buf, n) != 0) { ESP_LOGE(TAG, "Schreibfehler (errno %d): Karte voll oder entfernt?", errno); why = "Schreibfehler"; break; }
            f.data += n; total += n;
#endif
        } else if (r.stop_req) {
            break;                                            /* Puffer leer und Stop verlangt */
        }
        const int64_t now = esp_timer_get_time();
        if (now - t_pub >= 1000000) {
            t_pub = now;
            const uint32_t bps = rate * ch * 2u;
            const uint32_t secs = bps ? (uint32_t)(total / bps) : 0;
            xSemaphoreTake(r.lock, portMAX_DELAY);
            r.info.bytes = total; r.info.secs = secs;
            xSemaphoreGive(r.lock);
            publish(true, secs);
        }
        if (f.fd >= 0 && now - t_sync >= (int64_t)CONFIG_IRM_REC_SYNC_S * 1000000) {
            t_sync = now;
            if (sync_file(&f, rate, ch) != 0) { ESP_LOGE(TAG, "Schreibfehler beim Sichern (errno %d)", errno); why = "Schreibfehler"; break; }
            if (!space_ok()) { ESP_LOGW(TAG, "weniger als %d MiB frei: Aufnahme endet", CONFIG_IRM_REC_MIN_FREE_MB); why = "Karte voll"; break; }
        }
        const int want = (f.fd >= 0 || total > 0) ? 1 : (int)((now / 500000) & 1);   /* LED: Dauerlicht bei Aufnahme, Blinken bei "bereit, wartet auf Ton" */
        if (want != led_state) { led_state = want; led(want); }
    }

    close_file(&f, rate, ch);
    xSemaphoreTake(r.lock, portMAX_DELAY);
    r.info.bytes = total;
    r.info.secs = (rate && ch) ? (uint32_t)(total / (rate * ch * 2u)) : 0;
    r.info.dropped_bytes = r.dropped;
    if (why) strlcpy(r.info.end_reason, why, sizeof(r.info.end_reason));
    const irm_rec_info_t fin = r.info;
    xSemaphoreGive(r.lock);
    ESP_LOGI(TAG, "Aufnahme beendet%s%s: %u s, %u Datei(en), %llu Byte%s", why ? " - " : "", why ? why : "", (unsigned)fin.secs, (unsigned)fin.files,
             (unsigned long long)total, fin.dropped_bytes ? " | VERWORFEN: Puffer lief voll" : "");
    if (fin.dropped_bytes) ESP_LOGW(TAG, "%u Byte verworfen (Schreibpausen der Karte > Puffer): IRM_REC_BUFFER_KB erhoehen oder schnellere Karte", (unsigned)fin.dropped_bytes);
    ESP_LOGI(TAG, "Schreib-Task: Stack-Reserve minimal %u von %d Byte", (unsigned)uxTaskGetStackHighWaterMark(NULL), CONFIG_IRM_REC_TASK_STACK);
    publish(false, fin.secs);
    led(0);

    xSemaphoreTake(r.lock, portMAX_DELAY);       /* ab hier nimmt feed() nichts mehr an; erst jetzt darf der Puffer weg */
    r.mode = M_OFF;
    StreamBufferHandle_t sb = r.sb;
    uint8_t *store = r.store;
    r.sb = NULL; r.store = NULL;
    r.info.active = false;
    xSemaphoreGive(r.lock);
    HEAP_CHECK("vor Puffer-Freigabe");
    if (sb) sb_destroy(sb, store);
    HEAP_CHECK("nach Puffer-Freigabe");
    free(buf);
    r.running = false;                           /* letzter Handgriff: danach darf irm_rec_start() neu beginnen */
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------- API */

esp_err_t irm_rec_init(irm_out_sink_t *sink)
{
    if (r.inited) return ESP_OK;
    if (!sink || !sink->write) return ESP_ERR_INVALID_ARG;
    r.lock = xSemaphoreCreateMutex();
    if (!r.lock) return ESP_ERR_NO_MEM;
    if (LED_ON) {
        const gpio_config_t io = { .pin_bit_mask = 1ULL << (CONFIG_IRM_REC_LED_GPIO & 63), .mode = GPIO_MODE_OUTPUT };
        gpio_config(&io);
        gpio_set_level(CONFIG_IRM_REC_LED_GPIO, 0);
    }
    r.sink = sink;
    r.orig_write = sink->write;
    sink->write = tap_write;
    r.inited = true;
    return ESP_OK;
}

esp_err_t irm_rec_start(irm_source_t src)
{
    if (!r.inited) return ESP_ERR_INVALID_STATE;
    if (src != IRM_SOURCE_RADIO && src != IRM_SOURCE_KIWI) return ESP_ERR_NOT_SUPPORTED;
    if (!irm_board_sd_mounted()) return ESP_ERR_NOT_FOUND;
    xSemaphoreTake(r.lock, portMAX_DELAY);
    if (r.running) { xSemaphoreGive(r.lock); return ESP_ERR_INVALID_STATE; }
    const size_t sz = (size_t)CONFIG_IRM_REC_BUFFER_KB * 1024u;
    HEAP_CHECK("vor Puffer-Anlage");
    uint8_t *store = NULL;
    StreamBufferHandle_t sb = sb_create(sz, &store);
    if (!sb) { xSemaphoreGive(r.lock); ESP_LOGE(TAG, "kein Speicher fuer den Aufnahmepuffer (%u KiB)", (unsigned)(sz / 1024)); return ESP_ERR_NO_MEM; }
    r.sb = sb; r.store = store; r.rate = 0; r.ch = 0; r.dropped = 0; r.stop_req = false;
    memset(&r.info, 0, sizeof(r.info));
    r.info.active = true; r.info.source = src;
    r.running = true;
    r.mode = (src == IRM_SOURCE_KIWI) ? M_KIWI : M_SINK;
    xSemaphoreGive(r.lock);
    if (xTaskCreatePinnedToCore(rec_task, "irm_rec", CONFIG_IRM_REC_TASK_STACK, NULL, CONFIG_IRM_REC_TASK_PRIO, NULL,
                                CONFIG_IRM_REC_TASK_CORE < 0 ? tskNO_AFFINITY : CONFIG_IRM_REC_TASK_CORE) != pdPASS) {
        xSemaphoreTake(r.lock, portMAX_DELAY);
        r.mode = M_OFF; r.sb = NULL; r.store = NULL; r.info.active = false; r.running = false;
        xSemaphoreGive(r.lock);
        sb_destroy(sb, store);
        return ESP_ERR_NO_MEM;
    }
    publish(true, 0);
    ESP_LOGI(TAG, "Aufnahme bereit (%s): Datei wird beim ersten Ton angelegt", src == IRM_SOURCE_KIWI ? "KiwiSDR, 12 kHz Mono" : "Radio, Stereo");
    return ESP_OK;
}

esp_err_t irm_rec_stop(void)
{
    if (!r.inited) return ESP_OK;
    xSemaphoreTake(r.lock, portMAX_DELAY);
    if (!r.running) { xSemaphoreGive(r.lock); return ESP_OK; }
    r.stop_req = true;
    xSemaphoreGive(r.lock);
    for (int i = 0; i < STOP_WAIT_MS / 10 && r.running; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (r.running) { ESP_LOGE(TAG, "Schreib-Task beendet sich nicht (Karte haengt?)"); return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}

bool irm_rec_active(void) { return r.inited && r.running; }

void irm_rec_info(irm_rec_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!r.inited) return;
    xSemaphoreTake(r.lock, portMAX_DELAY);
    *out = r.info;
    out->dropped_bytes = r.dropped;
    xSemaphoreGive(r.lock);
}

#else /* !CONFIG_IRM_REC_ENABLE: Rahmen, damit Aufrufer ohne #if auskommen */

esp_err_t irm_rec_init(irm_out_sink_t *sink) { (void)sink; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t irm_rec_start(irm_source_t src) { (void)src; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t irm_rec_stop(void) { return ESP_OK; }
bool irm_rec_active(void) { return false; }
void irm_rec_info(irm_rec_info_t *out) { memset(out, 0, sizeof(*out)); }
void irm_rec_feed_kiwi(const int16_t *mono, size_t n, uint32_t rate_hz) { (void)mono; (void)n; (void)rate_hz; }

#endif
