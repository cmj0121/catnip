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

/* The control hint (#80): four arrows in the bottom-left corner, each lit when
 * that direction does something here and dimmed when it does not.
 *
 * Its own object rather than part of the bar, because it is at the other end of
 * the screen - but on the same top layer and for the same reason: it belongs to
 * the platform, it outlives every screen the app pushes and pops, and an app
 * that could place it could place it in the wrong corner. */
lv_obj_t *g_hint;
lv_obj_t *g_arrow[4]; /* up, down, left, right - the order of CATNIP_HINT_* */
unsigned g_last_hint = ~0u;

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

/* Where each arrow sits in a small cross. A cross rather than a row, because
 * the shape is the message: a row of four arrows is a legend to be read, and a
 * cross is the thing under the user's thumb. */
bool ensure_hint(void)
{
    static const struct {
        const char *glyph;
        int x, y;
    } kAt[4] = {
        {LV_SYMBOL_UP, 12, 0},
        {LV_SYMBOL_DOWN, 12, 18},
        {LV_SYMBOL_LEFT, 0, 9},
        {LV_SYMBOL_RIGHT, 24, 9},
    };

    if (g_hint) return true;
    if (!catnip_lvgl_backend_active()) return false;
    g_hint = lv_obj_create(lv_layer_top());
    if (!g_hint) return false;
    lv_obj_remove_style_all(g_hint);
    lv_obj_set_size(g_hint, CATNIP_FRAME_HINT_W, CATNIP_FRAME_HINT_H);
    lv_obj_align(g_hint, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_SCROLLABLE);
    /* Nothing here is touchable. It says what the switches do; a hint you could
     * press would be a second, worse set of controls. */
    lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 4; i++) {
        g_arrow[i] = lv_label_create(g_hint);
        if (!g_arrow[i]) return false;
        lv_label_set_text(g_arrow[i], kAt[i].glyph);
        lv_obj_set_pos(g_arrow[i], kAt[i].x, kAt[i].y);
        lv_obj_remove_flag(g_arrow[i], LV_OBJ_FLAG_CLICKABLE);
    }
    return true;
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
    if (!ensure_hint()) return;
    if (on) lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
}

void catnip_frame_set_hint(unsigned mask)
{
    if (!ensure_hint()) return;
    /* Only on a change, for the reason catnip_frame_show() gives at length:
     * this display renders the whole panel, so a write that changes nothing
     * still costs 153,600 bytes over the bus. */
    if (mask == g_last_hint) return;
    g_last_hint = mask;
    for (int i = 0; i < 4; i++) {
        bool lit = (mask & (1u << i)) != 0;
        lv_obj_set_style_text_color(
            g_arrow[i], lv_color_hex(lit ? catnip_color_text() : catnip_color_faint()),
            0);
    }
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
