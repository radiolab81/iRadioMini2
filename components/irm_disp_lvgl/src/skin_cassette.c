/* Kassettenspieler (Altprojekt: displayd_lvgl_cass_sim): zwei Spulen drehen sich, solange gespielt wird. */
#include <string.h>
#include "irm_assets.h"
#include "irm_skin.h"

static lv_obj_t *s_left, *s_right, *s_label;
static bool s_spinning;

static void rotate(void *unused, int32_t angle)
{
    (void)unused;
    lv_image_set_rotation(s_left, angle);
    lv_image_set_rotation(s_right, angle);
}

static void create(lv_obj_t *scr)
{
    s_left = lv_image_create(scr);                    /* Spulen liegen hinter der Kassette */
    lv_image_set_src(s_left, irm_asset(IRM_ASSET_REEL));
    lv_obj_align(s_left, LV_ALIGN_CENTER, -66, -10);
    s_right = lv_image_create(scr);
    lv_image_set_src(s_right, irm_asset(IRM_ASSET_REEL));
    lv_obj_align(s_right, LV_ALIGN_CENTER, 66, -10);

    lv_obj_t *cass = lv_image_create(scr);
    lv_image_set_src(cass, irm_asset(IRM_ASSET_CASSETTE));
    lv_obj_align(cass, LV_ALIGN_CENTER, 0, 0);

    s_label = lv_label_create(scr);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(s_label, 100);
    lv_obj_align(s_label, LV_ALIGN_TOP_MID, 55, 42);
}

static void update(const irm_status_t *st)
{
    lv_label_set_text(s_label, (st->index >= 0 && st->name[0]) ? st->name : "");
    bool play = st->player == IRM_PLAYER_PLAYING;
    if (play == s_spinning) return;
    s_spinning = play;
    lv_anim_delete(s_left, rotate);
    if (!play) return;                                 /* Spulen bleiben in aktueller Stellung stehen */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_left);
    lv_anim_set_exec_cb(&a, rotate);
    lv_anim_set_values(&a, 0, 3600);                   /* 0,1-Grad-Einheiten: eine Umdrehung */
    lv_anim_set_duration(&a, 3000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

const irm_skin_t irm_skin_active = { "cassette", create, update };
