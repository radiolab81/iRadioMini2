/* Basis-Skin (Altprojekt: displayd_lvgl): Programmnummer, Name, Vor/Zurueck. Die Tasten sind bei Touch-Displays
 * bedienbar (Touch-Treiber ueber lvgl_port_add_touch() einhaengen, siehe docs/LVGL.md). */
#include <stdio.h>
#include "irm_core.h"
#include "irm_skin.h"

static lv_obj_t *s_num, *s_name, *s_state, *s_shuf, *s_loop;   /* s_shuf/s_loop: Zufall/Wiederholen, nur in der Quelle Dateien sichtbar */

static void on_btn(lv_event_t *e)
{
    irm_cmd_post_id((irm_cmd_id_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static void on_toggle(lv_event_t *e)
{
    irm_cmd_post_id((irm_cmd_id_t)(intptr_t)lv_event_get_user_data(e), -1);   /* -1 = umschalten */
}

static lv_obj_t *button_at(lv_obj_t *scr, const char *symbol, int x, int y, int w, int h, lv_event_cb_t cb, irm_cmd_id_t cmd)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)cmd);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *button(lv_obj_t *scr, const char *symbol, int x, irm_cmd_id_t cmd) { return button_at(scr, symbol, x, 170, 60, 40, on_btn, cmd); }

static void create(lv_obj_t *scr)
{
    lv_obj_t *p = lv_label_create(scr);
    lv_label_set_text(p, "P");
    lv_obj_align(p, LV_ALIGN_CENTER, -14, -40);
    s_num = lv_label_create(scr);
    lv_obj_align(s_num, LV_ALIGN_CENTER, 4, -40);

    s_name = lv_label_create(scr);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_width(s_name, 250);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, -10);

    s_state = lv_label_create(scr);
    lv_obj_align(s_state, LV_ALIGN_BOTTOM_MID, 0, -4);

    button(scr, LV_SYMBOL_PREV, 20, IRM_CMD_PREV);
    button(scr, LV_SYMBOL_PLAY LV_SYMBOL_PAUSE, 130, IRM_CMD_TOGGLE_PAUSE);
    button(scr, LV_SYMBOL_NEXT, 240, IRM_CMD_NEXT);
    s_shuf = button_at(scr, LV_SYMBOL_SHUFFLE, 240, 8, 60, 34, on_toggle, IRM_CMD_SHUFFLE);   /* aktiv = hervorgehoben (LV_STATE_CHECKED) */
    s_loop = button_at(scr, LV_SYMBOL_LOOP, 170, 8, 60, 34, on_toggle, IRM_CMD_LOOP);
}

static void set_checked(lv_obj_t *b, bool on)
{
    if (on) lv_obj_add_state(b, LV_STATE_CHECKED); else lv_obj_remove_state(b, LV_STATE_CHECKED);
}

static void update(const irm_status_t *st)
{
    static const char *const ps[] = { "Stop", "Verbinde...", "Wiedergabe", "Pause", "Fehler" };
    char n[12];
    snprintf(n, sizeof(n), "%d", st->index >= 0 ? (int)st->index + 1 : 0);
    lv_label_set_text(s_num, n);
    lv_label_set_text(s_name, st->name[0] ? st->name : "-");
    lv_label_set_text(s_state, ps[st->player <= IRM_PLAYER_ERROR ? st->player : 0]);
    const bool media = st->source == IRM_SOURCE_MEDIA;
    lv_obj_t *const tog[] = { s_shuf, s_loop };
    for (size_t i = 0; i < 2; i++) {
        if (media) lv_obj_remove_flag(tog[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(tog[i], LV_OBJ_FLAG_HIDDEN);
    }
    set_checked(s_shuf, st->shuffle);
    set_checked(s_loop, st->loop);
}

const irm_skin_t irm_skin_active = { "basic", create, update };
