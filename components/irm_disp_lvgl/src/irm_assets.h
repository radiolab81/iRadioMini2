#pragma once
#include "lvgl.h"

typedef enum {
    IRM_ASSET_SCALE_LINEAR, IRM_ASSET_NEEDLE_LINEAR,
    IRM_ASSET_SCALE_ROUND, IRM_ASSET_NEEDLE_ROUND,
    IRM_ASSET_CASSETTE, IRM_ASSET_REEL,
} irm_asset_id_t;

/** Liefert den Bilddeskriptor (Daten liegen direkt im Flash; keine Dekodierung, kein RAM). */
const lv_image_dsc_t *irm_asset(irm_asset_id_t id);
