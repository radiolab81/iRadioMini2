#include "irm_dir.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "irm_dir_store.h"
#include "kiwi_dir_update.h"

static const char *TAG = "irm_dir";

static struct {
    const kiwi_dir_store_t *store;
    kiwi_dir_db_t           db;
    atomic_int              running;
    volatile uint8_t        state;
    volatile uint32_t       bytes, servers;
    volatile int            err;
    int64_t                 last_attempt_us;
    bool                    have_attempt;
} s;

esp_err_t irm_dir_init(void)
{
    atomic_init(&s.db.active, -1);   /* ein statisch genulltes Handle gilt sonst als "Slot 0 ist gueltig" (active = 0) */
    s.store = irm_dir_store_get();
    if (!s.store) return ESP_OK;
    const int rc = kiwi_dir_db_open(&s.db, s.store, 1);   /* mit Pruefsumme: erkennt halbe/kaputte Daten */
    if (rc == KIWI_DIR_OK) ESP_LOGI(TAG, "Serverdatenbank: %u Server, Generation %u", (unsigned)kiwi_dir_db_count(&s.db), (unsigned)kiwi_dir_db_generation(&s.db));
    else ESP_LOGI(TAG, "Noch keine Serverdatenbank - Aktualisieren ueber das Web-UI laedt die Liste (%s)", CONFIG_IRM_DIR_URL);
    return ESP_OK;
}

const kiwi_dir_db_t *irm_dir_db(void) { return &s.db; }

bool irm_dir_get(uint32_t idx, kiwi_dir_rec_t *rec) { return rec && kiwi_dir_db_get(&s.db, idx, rec) == KIWI_DIR_OK; }

void irm_dir_info(irm_dir_info_t *o)
{
    memset(o, 0, sizeof(*o));
    o->available = s.store != NULL;
    o->valid = kiwi_dir_db_valid(&s.db);
    o->count = kiwi_dir_db_count(&s.db);
    o->generation = kiwi_dir_db_generation(&s.db);
    o->state = s.state;
    o->bytes = s.bytes;
    o->servers = s.servers;
    o->err = s.err;
    if (s.have_attempt) {
        const int64_t el = (esp_timer_get_time() - s.last_attempt_us) / 1000000;
        const int64_t min = CONFIG_IRM_DIR_MIN_INTERVAL_S;
        if (el < min) o->retry_in_s = (uint32_t)(min - el);
    }
}

static void on_progress(void *user, uint32_t bytes, uint32_t servers)
{
    (void)user;
    s.bytes = bytes;
    s.servers = servers;
}

static void update_task(void *arg)
{
    (void)arg;
    kiwi_dir_update_t *u = malloc(sizeof *u);   /* ca. 9 KB nur fuer die Dauer des Updates */
    if (!u) {
        s.err = IRM_DIR_ERR_NOMEM;
        s.state = IRM_DIR_FAILED;
        ESP_LOGW(TAG, "Update: zu wenig freier Speicher");
    } else {
        kiwi_dir_result_t r;
        ESP_LOGI(TAG, "Update: lade %s", CONFIG_IRM_DIR_URL);
        const int rc = kiwi_dir_update_http(u, s.store, &s.db, CONFIG_IRM_DIR_URL, on_progress, NULL, &r);
        free(u);
        if (rc == KIWI_DIR_OK) {
            s.bytes = r.bytes;
            s.servers = r.entries;
            s.err = 0;
            s.state = IRM_DIR_OK;
            ESP_LOGI(TAG, "Update ok: %u Server (vorher %u), %u Laender, Generation %u",
                     (unsigned)r.entries, (unsigned)r.old_entries, (unsigned)r.countries, (unsigned)r.generation);
        } else {
            s.err = rc;
            s.state = IRM_DIR_FAILED;
            ESP_LOGW(TAG, "Update fehlgeschlagen: %s (Datenbank unveraendert)", kiwi_dir_strerror(rc));
        }
    }
    atomic_store(&s.running, 0);
    vTaskDelete(NULL);
}

esp_err_t irm_dir_update(bool force)
{
    if (!s.store) return ESP_ERR_NOT_SUPPORTED;
    int expected = 0;
    if (!atomic_compare_exchange_strong(&s.running, &expected, 1)) return ESP_ERR_INVALID_STATE;
    if (!force && s.have_attempt && (esp_timer_get_time() - s.last_attempt_us) / 1000000 < CONFIG_IRM_DIR_MIN_INTERVAL_S) {
        atomic_store(&s.running, 0);
        return ESP_ERR_TIMEOUT;
    }
    s.have_attempt = true;
    s.last_attempt_us = esp_timer_get_time();
    s.bytes = s.servers = 0;
    s.err = 0;
    s.state = IRM_DIR_UPDATING;
    /* Prioritaet 3: unter Audio-, WLAN- und Netzwerk-Tasks, damit der Ton Vorrang behaelt */
    if (xTaskCreate(update_task, "irm_dir_upd", 8192, NULL, 3, NULL) != pdPASS) {
        s.state = IRM_DIR_IDLE;
        atomic_store(&s.running, 0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
