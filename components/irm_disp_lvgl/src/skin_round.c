/* Rundskala "Mende 148" (Altprojekt: displayd_lvgl_Mende148): Zeiger rotiert (Einheit 0,1 Grad) ueber Halbkreisskala. */
#include <stdlib.h>
#include <string.h>
#include "irm_assets.h"
#include "irm_skin.h"

#define ANGLE_LEFT   100     /* 10,0 Grad: erster Sender */
#define ANGLE_RIGHT  1900    /* 190,0 Grad: letzter Sender */

static lv_obj_t *s_needle, *s_label;
static int32_t s_angle = ANGLE_LEFT, s_last_index = -2;

static void set_angle(void *obj, int32_t v) { s_angle = v; lv_image_set_rotation((lv_obj_t *)obj, (int32_t)v); }

static void create(lv_obj_t *scr)
{
    lv_obj_t *bg = lv_image_create(scr);
    lv_image_set_src(bg, irm_asset(IRM_ASSET_SCALE_ROUND));
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);

    s_needle = lv_image_create(scr);
    lv_image_set_src(s_needle, irm_asset(IRM_ASSET_NEEDLE_ROUND));
    lv_obj_align(s_needle, LV_ALIGN_CENTER, 0, 80);   /* Drehpunkt = Bildmitte, unten an der Skala */
    lv_image_set_rotation(s_needle, ANGLE_LEFT);

    s_label = lv_label_create(scr);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(s_label, 300);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -4);
}

static void update(const irm_status_t *st)
{
    lv_label_set_text(s_label, (st->index >= 0 && st->name[0]) ? st->name : "");
    if (st->index == s_last_index) return;
    s_last_index = st->index;

    int32_t target = ANGLE_LEFT;
    if (st->index > 0 && st->count > 1) target = ANGLE_LEFT + (ANGLE_RIGHT - ANGLE_LEFT) * st->index / (st->count - 1);
    int32_t dist = abs(target - s_angle);
    lv_anim_delete(s_needle, set_angle);
    if (dist == 0) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_needle);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_values(&a, s_angle, target);
    lv_anim_set_duration(&a, dist / 3 < 200 ? 200 : (dist / 3 > 1500 ? 1500 : (uint32_t)dist / 3));
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

const irm_skin_t irm_skin_active = { "round", create, update };
