#pragma once
#include <stdbool.h>
#include "esp_codec_dev.h"
#include "irm_out.h"
#ifndef IRM_SD_MOUNT_POINT
#define IRM_SD_MOUNT_POINT "/sdcard"   /* Host-Test ueberschreibt es per -D mit einem Temp-Verzeichnis */
#endif
esp_codec_dev_handle_t irm_board_codec(void);
irm_out_sink_t *irm_board_sink(void);
bool irm_board_sd_mounted(void);
