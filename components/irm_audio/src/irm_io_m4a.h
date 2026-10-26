#pragma once
#include "esp_gmf_io.h"

/* GMF-Lese-IO fuer lokale M4A/MP4-Dateien, deren "moov"-Atom hinter "mdat" liegt (z. B. ffmpeg ohne -movflags +faststart).
 * Der M4A-Parser der Espressif-Bibliothek verlangt moov vor mdat ("Not support mdat before moov"). Dieses IO liefert die Datei als
 * virtuellen Strom mit vorgezogenem, angepasstem moov (irm_mp4); es wird nur fuer solche Dateien gewaehlt. */
typedef struct {
    int              dir;       /* ESP_GMF_IO_DIR_READER */
    const char      *name;
    esp_gmf_io_cfg_t io_cfg;
} irm_io_m4a_cfg_t;

#define IRM_IO_M4A_CFG_DEFAULT() { .dir = ESP_GMF_IO_DIR_READER, .name = NULL, \
    .io_cfg = { .thread = { .stack = 0, .prio = 0, .core = 0, .stack_in_ext = false }, \
                .buffer_cfg = { .io_size = 0, .buffer_size = 0, .read_filter = NULL }, .enable_speed_monitor = false } }

#define IRM_IO_M4A_TAG "io_irm_m4a"

esp_gmf_err_t irm_io_m4a_init(irm_io_m4a_cfg_t *config, esp_gmf_io_handle_t *io);
