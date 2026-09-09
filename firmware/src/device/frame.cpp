/* frame.cpp - see frame.h. */
#include "frame.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "../catnip_render.h"
#include "lvgl_backend.h"

namespace {

/* A shade under the ground it floats over, so the bar reads as a bar rather
 * than as a stripe of a different material. */
const uint32_t kColBarBg = 0x18243A;

lv_obj_t *g_bar;
lv_obj_t *g_battery;
lv_obj_t *g_title;
lv_obj_t *g_counter;

/* What the bar was last told, so a pass that changes nothing writes nothing.
 * The bar is redrawn from the tree every loop, and lv_label_set_text on an
 * unchanged string still invalidates the area. */
char g_last_battery[16];
char g_last_title[40];
char g_last_counter[16];

void set_if_changed(lv_obj_t *label, char *last, size_t cap, const char *text)
{
    if (!label || strncmp(last, text, cap - 1) == 0) return;
    snprintf(last, cap, "%s", text);
    lv_label_set_text(label, last);
}

lv_obj_t *make_cell(lv_obj_t *parent, lv_text_align_t align, uint32_t ink)
{
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return nullptr;
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ink), 0);
    lv_label_set_text(label, "");
    return label;
}

bool ensure_bar(void)
{
    if (g_bar) return true;
    /* Nothing exists to draw on until an app's first widget brings LVGL up, and
     * a bar over a black screen would be the only thing on it. */
    if (!catnip_lvgl_backend_active()) return false;

    g_bar = lv_obj_create(lv_layer_top());
    if (!g_bar) return false;
    lv_obj_set_size(g_bar, LV_PCT(100), CATNIP_FRAME_BAR_H);
    lv_obj_set_pos(g_bar, 0, 0);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(kColBarBg), 0);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_bar, 0, 0);
    lv_obj_set_style_radius(g_bar, 0, 0);
    lv_obj_set_style_pad_ver(g_bar, 3, 0);
    lv_obj_set_style_pad_hor(g_bar, 6, 0);
    lv_obj_set_flex_flow(g_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_SCROLLABLE);
    /* The bar is above every screen, so it would otherwise eat taps meant for
     * the row underneath it - and the app owns everything below the bar. */
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_CLICKABLE);

    /* Left to right, and each cell takes an equal share so the header stays
     * centred on the screen rather than on whatever is left over. */
    g_battery = make_cell(g_bar, LV_TEXT_ALIGN_LEFT, catnip_color_faint());
    g_title = make_cell(g_bar, LV_TEXT_ALIGN_CENTER, catnip_color_text());
    g_counter = make_cell(g_bar, LV_TEXT_ALIGN_RIGHT, catnip_color_faint());
    return g_battery && g_title && g_counter;
}

} /* namespace */

void catnip_frame_show(bool on)
{
    static bool shown = false;

    if (!ensure_bar()) return;
    /* Only on a change, and the guard is not a nicety. lv_obj_remove_flag()
     * invalidates the object and marks the layout dirty whenever HIDDEN is in
     * the mask, without first asking whether the flag was set - and this
     * display renders LV_DISPLAY_RENDER_MODE_FULL, where any invalid area
     * becomes the whole screen. Called unconditionally from the loop, that
     * repainted and re-blitted all 320x240 every pass, on a screen where
     * nothing had changed, and defeated every dirty flag downstream of it. */
    if (on == shown) return;
    shown = on;
    if (on) lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
}

void catnip_frame_set_title(const char *title)
{
    if (!ensure_bar()) return;
    set_if_changed(g_title, g_last_title, sizeof(g_last_title), title ? title : "");
}

void catnip_frame_set_battery(int percent)
{
    char buf[16];

    if (!ensure_bar()) return;
    /* Not measured draws nothing. A battery that reports -1 and a battery at
     * 0% are different facts, and the bar must not turn one into the other. */
    if (percent < 0) buf[0] = '\0';
    else snprintf(buf, sizeof(buf), "%s %d%%", LV_SYMBOL_BATTERY_FULL, percent);
    set_if_changed(g_battery, g_last_battery, sizeof(g_last_battery), buf);
}

void catnip_frame_step(catnip_rt *rt)
{
    char buf[16];
    int n = 0, total = 0;

    if (!ensure_bar()) return;
    /* Blank, not 0/0: a screen with no list has nothing to count, and a zero
     * would read as a list that is empty rather than as no list at all. */
    if (rt && catnip_render_counter(rt, catnip_lvgl_backend_focused(), &n, &total))
        snprintf(buf, sizeof(buf), "%d/%d", n, total);
    else buf[0] = '\0';
    set_if_changed(g_counter, g_last_counter, sizeof(g_last_counter), buf);
}
