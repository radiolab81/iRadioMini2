/* Skalensimulation (Altprojekt: displayd_lvgl_skale): Zeiger gleitet ueber eine lineare Senderskala. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_assets.h"
#include "irm_skin.h"

#define NEEDLE_LEFT   80     /* Zeigerposition erster Sender (px) */
#define NEEDLE_RIGHT  310    /* Zeigerposition letzter Sender (px) */

static lv_obj_t *s_needle, *s_label;
static int32_t s_last_index = -2;
static char s_pending[IRM_NAME_MAX_LEN];

static void set_x(void *obj, int32_t x) { lv_obj_set_x((lv_obj_t *)obj, x); }

static void arrived(lv_anim_t *a)
{
    (void)a;
    lv_label_set_text(s_label, s_pending);   /* Name erst anzeigen, wenn der Zeiger steht (wie im Altprojekt) */
}

static void create(lv_obj_t *scr)
{
    lv_obj_t *bg = lv_image_create(scr);
    lv_image_set_src(bg, irm_asset(IRM_ASSET_SCALE_LINEAR));
    lv_obj_set_pos(bg, 0, 0);

    s_needle = lv_image_create(scr);                 /* Zeiger liegt vor der Skala */
    lv_image_set_src(s_needle, irm_asset(IRM_ASSET_NEEDLE_LINEAR));
    lv_obj_set_pos(s_needle, NEEDLE_LEFT, 0);

    s_label = lv_label_create(scr);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(s_label, 250);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_align(s_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void update(const irm_status_t *st)
{
    snprintf(s_pending, sizeof(s_pending), "%s", st->index >= 0 ? st->name : "");
    if (st->index == s_last_index) { lv_label_set_text(s_label, s_pending); return; }
    s_last_index = st->index;

    int32_t target = NEEDLE_LEFT;
    if (st->index > 0 && st->count > 1) target = NEEDLE_LEFT + (NEEDLE_RIGHT - NEEDLE_LEFT) * st->index / (st->count - 1);
    int32_t now = lv_obj_get_x(s_needle);
    int32_t dist = abs(target - now);

    lv_label_set_text(s_label, "");
    lv_anim_delete(s_needle, set_x);
    if (dist == 0) { arrived(NULL); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_needle);
    lv_anim_set_exec_cb(&a, set_x);
    lv_anim_set_values(&a, now, target);
    lv_anim_set_duration(&a, dist * 6 < 150 ? 150 : (dist * 6 > 1200 ? 1200 : (uint32_t)dist * 6));
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, arrived);
    lv_anim_start(&a);
}

const irm_skin_t irm_skin_active = { "linear", create, update };
