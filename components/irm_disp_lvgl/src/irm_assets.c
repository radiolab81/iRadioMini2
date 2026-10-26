#include "irm_assets.h"
#include "assets_dims.h"
#include "sdkconfig.h"

#define EMB(n) extern const uint8_t n##_start[] asm("_binary_" #n "_bin_start"); \
               extern const uint8_t n##_end[]   asm("_binary_" #n "_bin_end")
#define DSC(sym, W, H, ALPHA) { \
    .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = (ALPHA) ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565, \
                .w = (W), .h = (H), .stride = (W) * 2 }, \
    .data_size = (W) * (H) * ((ALPHA) ? 3 : 2), .data = sym##_start }

#if CONFIG_IRM_LVGL_SKIN_LINEAR
EMB(scale_linear); EMB(needle_linear);
static const lv_image_dsc_t k_scale_linear  = DSC(scale_linear,  IRM_ASSET_SCALE_LINEAR_W,  IRM_ASSET_SCALE_LINEAR_H,  IRM_ASSET_SCALE_LINEAR_ALPHA);
static const lv_image_dsc_t k_needle_linear = DSC(needle_linear, IRM_ASSET_NEEDLE_LINEAR_W, IRM_ASSET_NEEDLE_LINEAR_H, IRM_ASSET_NEEDLE_LINEAR_ALPHA);
#endif
#if CONFIG_IRM_LVGL_SKIN_ROUND
EMB(scale_round); EMB(needle_round);
static const lv_image_dsc_t k_scale_round  = DSC(scale_round,  IRM_ASSET_SCALE_ROUND_W,  IRM_ASSET_SCALE_ROUND_H,  IRM_ASSET_SCALE_ROUND_ALPHA);
static const lv_image_dsc_t k_needle_round = DSC(needle_round, IRM_ASSET_NEEDLE_ROUND_W, IRM_ASSET_NEEDLE_ROUND_H, IRM_ASSET_NEEDLE_ROUND_ALPHA);
#endif
#if CONFIG_IRM_LVGL_SKIN_CASSETTE
EMB(cassette); EMB(reel);
static const lv_image_dsc_t k_cassette = DSC(cassette, IRM_ASSET_CASSETTE_W, IRM_ASSET_CASSETTE_H, IRM_ASSET_CASSETTE_ALPHA);
static const lv_image_dsc_t k_reel     = DSC(reel,     IRM_ASSET_REEL_W,     IRM_ASSET_REEL_H,     IRM_ASSET_REEL_ALPHA);
#endif

const lv_image_dsc_t *irm_asset(irm_asset_id_t id)
{
    switch (id) {
#if CONFIG_IRM_LVGL_SKIN_LINEAR
    case IRM_ASSET_SCALE_LINEAR:  return &k_scale_linear;
    case IRM_ASSET_NEEDLE_LINEAR: return &k_needle_linear;
#endif
#if CONFIG_IRM_LVGL_SKIN_ROUND
    case IRM_ASSET_SCALE_ROUND:   return &k_scale_round;
    case IRM_ASSET_NEEDLE_ROUND:  return &k_needle_round;
#endif
#if CONFIG_IRM_LVGL_SKIN_CASSETTE
    case IRM_ASSET_CASSETTE:      return &k_cassette;
    case IRM_ASSET_REEL:          return &k_reel;
#endif
    default: return NULL;
    }
}
