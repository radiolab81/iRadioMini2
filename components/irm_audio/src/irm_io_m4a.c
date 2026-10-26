#include <string.h>
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"
#include "irm_io_m4a.h"
#include "irm_mp4.h"

typedef struct {
    esp_gmf_io_t   base;
    irm_mp4_fs_t  *fs;
    bool           is_open;
} io_m4a_t;

static const char *TAG = "irm_io_m4a";

static esp_gmf_err_t m4a_new(void *cfg, esp_gmf_obj_handle_t *io) { return irm_io_m4a_init(cfg, io); }

static esp_gmf_err_t m4a_get_score(esp_gmf_io_handle_t handle, const char *url, int *score)
{
    *score = ESP_GMF_IO_SCORE_NONE;   /* wird nur explizit per Tag gewaehlt */
    return ESP_GMF_ERR_OK;
}

static const char *uri_to_path(const char *uri)
{
    if (uri[0] == '/') return uri;
    const char *s = strstr(uri, "://");
    if (!s) return NULL;
    s += 2;
    return (s[1] == '/') ? s + 1 : s;
}

static esp_gmf_err_t m4a_open(esp_gmf_io_handle_t io)
{
    io_m4a_t *m = (io_m4a_t *)io;
    char *uri = NULL;
    esp_gmf_io_get_uri(io, &uri);
    ESP_GMF_NULL_CHECK(TAG, uri, return ESP_GMF_ERR_FAIL;);
    const char *path = uri_to_path(uri);
    ESP_GMF_NULL_CHECK(TAG, path, return ESP_GMF_ERR_FAIL;);
    if (m->is_open) return ESP_GMF_ERR_FAIL;
    int r = irm_mp4_fs_open(path, &m->fs);
    if (r != 0) {
        ESP_LOGE(TAG, "M4A-Umordnung fuer %s nicht moeglich (Code %d)", path, r);
        return ESP_GMF_ERR_FAIL;
    }
    esp_gmf_io_set_size(io, irm_mp4_fs_size(m->fs));
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(io, &info);
    if (info.pos > 0) irm_mp4_fs_seek(m->fs, (uint64_t)info.pos);
    ESP_LOGI(TAG, "moov liegt hinter mdat: wird beim Lesen vorgezogen (%s, %u Byte)", path, (unsigned)irm_mp4_fs_size(m->fs));
    m->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t m4a_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    io_m4a_t *m = (io_m4a_t *)handle;
    esp_gmf_payload_t *pl = (esp_gmf_payload_t *)payload;
    size_t n = irm_mp4_fs_read(m->fs, pl->buf, wanted_size);
    pl->valid_size = n;
    if (n < wanted_size) {
        pl->is_done = true;
        ESP_LOGI(TAG, "No more data, ret: %u", (unsigned)n);
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t m4a_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks) { return ESP_GMF_IO_OK; }

static esp_gmf_err_t m4a_seek(esp_gmf_io_handle_t io, uint64_t pos)
{
    io_m4a_t *m = (io_m4a_t *)io;
    return (m->fs && irm_mp4_fs_seek(m->fs, pos) == 0) ? ESP_GMF_ERR_OK : ESP_GMF_ERR_FAIL;
}

static esp_gmf_err_t m4a_reset(esp_gmf_io_handle_t io)
{
    io_m4a_t *m = (io_m4a_t *)io;
    if (m->fs) irm_mp4_fs_seek(m->fs, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t m4a_close(esp_gmf_io_handle_t io)
{
    io_m4a_t *m = (io_m4a_t *)io;
    if (m->is_open) {
        irm_mp4_fs_close(m->fs);
        m->fs = NULL;
        m->is_open = false;
    }
    esp_gmf_io_set_pos(io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t m4a_delete(esp_gmf_io_handle_t io)
{
    io_m4a_t *m = (io_m4a_t *)io;
    if (m->fs) irm_mp4_fs_close(m->fs);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) esp_gmf_oal_free(cfg);
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(m);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t irm_io_m4a_init(irm_io_m4a_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, io, {return ESP_GMF_ERR_INVALID_ARG;});
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    io_m4a_t *m = esp_gmf_oal_calloc(1, sizeof(io_m4a_t));
    ESP_GMF_MEM_VERIFY(TAG, m, {return ESP_GMF_ERR_MEMORY_LACK;}, "m4a io", sizeof(io_m4a_t));
    m->base.dir = ESP_GMF_IO_DIR_READER;
    m->base.type = ESP_GMF_IO_TYPE_BYTE;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)m;
    obj->new_obj = m4a_new;
    obj->del_obj = m4a_delete;
    irm_io_m4a_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto fail;}, "m4a io cfg", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));
    ret = esp_gmf_obj_set_tag(obj, config->name ? config->name : IRM_IO_M4A_TAG);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto fail, "tag");
    m->base.get_score = m4a_get_score;
    m->base.open = m4a_open;
    m->base.close = m4a_close;
    m->base.seek = m4a_seek;
    m->base.reset = m4a_reset;
    m->base.acquire_read = m4a_acquire_read;
    m->base.release_read = m4a_release_read;
    esp_gmf_io_cfg_t io_cfg = {
        .thread = { .stack = config->io_cfg.thread.stack, .prio = config->io_cfg.thread.prio,
                    .core = config->io_cfg.thread.core, .stack_in_ext = config->io_cfg.thread.stack_in_ext },
        .buffer_cfg = { .io_size = config->io_cfg.buffer_cfg.io_size, .buffer_size = config->io_cfg.buffer_cfg.buffer_size },
        .enable_speed_monitor = config->io_cfg.enable_speed_monitor,
    };
    ret = esp_gmf_io_init(obj, &io_cfg);
    if (ret != ESP_GMF_ERR_OK) goto fail;
    *io = obj;
    return ESP_GMF_ERR_OK;
fail:
    esp_gmf_obj_delete(obj);
    return ret;
}
