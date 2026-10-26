/* Basis-Skin (Altprojekt: displayd_lvgl): Programmnummer, Name, Vor/Zurueck. Die Tasten sind bei Touch-Displays
 * bedienbar (Touch-Treiber ueber lvgl_port_add_touch() einhaengen, siehe docs/LVGL.md). */
#include <stdio.h>
#include "irm_core.h"
#include "irm_skin.h"

static lv_obj_t *s_num, *s_name, *s_state;

static void on_btn(lv_event_t *e)
{
    irm_cmd_post_id((irm_cmd_id_t)(intptr_t)lv_event_get_user_data(e), 0);
}

static lv_obj_t *button(lv_obj_t *scr, const char *symbol, int x, irm_cmd_id_t cmd)
{
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_set_pos(b, x, 170);
    lv_obj_set_size(b, 60, 40);
    lv_obj_add_event_cb(b, on_btn, LV_EVENT_CLICKED, (void *)(intptr_t)cmd);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

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
}

static void update(const irm_status_t *st)
{
    static const char *const ps[] = { "Stop", "Verbinde...", "Wiedergabe", "Pause", "Fehler" };
    char n[12];
    snprintf(n, sizeof(n), "%d", st->index >= 0 ? (int)st->index + 1 : 0);
    lv_label_set_text(s_num, n);
    lv_label_set_text(s_name, st->name[0] ? st->name : "-");
    lv_label_set_text(s_state, ps[st->player <= IRM_PLAYER_ERROR ? st->player : 0]);
}

const irm_skin_t irm_skin_active = { "basic", create, update };
