#pragma once
#include "esp_gmf_io.h"
#include "irm_out.h"

/* GMF-Schreib-IO, das PCM an eine irm_out_sink_t uebergibt (PDM oder interner DAC).
 * Ersetzt esp_gmf_io_i2s_pdm: dessen close() wartet unbegrenzt auf ein Event, das nur bei TX-Queue-Ueberlauf gesetzt wird. */
typedef struct {
    irm_out_sink_t  *sink;
    int              dir;       /* ESP_GMF_IO_DIR_WRITER */
    const char      *name;
    esp_gmf_io_cfg_t io_cfg;
} irm_io_sink_cfg_t;

#define IRM_IO_SINK_CFG_DEFAULT() { .sink = NULL, .dir = ESP_GMF_IO_DIR_WRITER, .name = NULL, \
    .io_cfg = { .thread = { .stack = 0, .prio = 0, .core = 0, .stack_in_ext = false }, \
                .buffer_cfg = { .io_size = 0, .buffer_size = 0, .read_filter = NULL }, .enable_speed_monitor = false } }

#define IRM_IO_SINK_TAG "io_irm_sink"

esp_gmf_err_t irm_io_sink_init(irm_io_sink_cfg_t *config, esp_gmf_io_handle_t *io);
