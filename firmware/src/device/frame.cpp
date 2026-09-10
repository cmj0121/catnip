/* frame.cpp - see frame.h. */
#include "frame.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "../catnip_render.h"
#include "lvgl_backend.h"
#include "ui_input_core.h"

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
unsigned g_hint_mask; /* which directions are lit, for the draw below */
unsigned g_last_hint = ~0u;

/* The lit ink: an earthy yellow, the colour of a key you press rather than of
 * text you read. It is deliberately not the body white - the hint is not
 * writing, it is a picture of the control under your thumb, and giving it its
 * own colour is what stops it being read as a label. */
const uint32_t kColHintLit = 0xD9A441;

/* What the bar was last told, so a pass that changes nothing writes nothing.
 * The bar is redrawn from the tree every loop, and lv_label_set_text on an
 * unchanged string still invalidates the area. */
char g_last_title[40];
char g_last_counter[16];

/* The left cell is the battery and the status strip together, composed into one
 * label so the bar stays three cells and the title stays centred (#83). Held as
 * their pieces here and recomposed whenever either changes, because they are
 * written by different callers on different cadences - the battery every two
 * seconds, the strip whenever a card or the radio comes or goes. */
int g_batt_pct = -1;
bool g_has_card;
bool g_has_radio;
bool g_syncing;
char g_last_left[48];

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

/* The four arrows, drawn rather than typed.
 *
 * They were four font glyphs placed by hand at fixed offsets, and they could
 * not be made symmetric: LVGL's UP/DOWN and LEFT/RIGHT symbols have different
 * advance widths and heights, so four corner coordinates are four independent
 * guesses at a shared centre. The result leaned, and the bottom arrow ran past
 * the box and was clipped.
 *
 * Drawn, every arrow comes off the same centre with the same three numbers, so
 * the cross is symmetric by construction - it cannot drift when a font is
 * swapped, because no font is involved.
 *
 * A cross rather than a row, because the shape is the message: a row of four
 * arrows is a legend to be read, and a cross is the thing under the thumb. */
void hint_draw(lv_event_t *e)
{
    /* Measured from the centre: the tip, how far back the base is, and how wide
     * the base is. One set of numbers for all four arrows is what makes them
     * the same arrow pointing four ways. `kGap` is what is left in the middle -
     * a compact cross keeps it small, so the four read as one control. */
    const int kTip = 14; /* centre to point */
    const int kLen = 7;  /* point back to base */
    const int kHalf = 4; /* half the base */
    static const struct {
        int dx, dy;
        unsigned bit;
    } kDir[4] = {
        {0, -1, CATNIP_HINT_UP},
        {0, 1, CATNIP_HINT_DOWN},
        {-1, 0, CATNIP_HINT_LEFT},
        {1, 0, CATNIP_HINT_RIGHT},
    };
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    int cx, cy;

    lv_obj_get_coords(obj, &a);
    cx = a.x1 + lv_area_get_width(&a) / 2;
    cy = a.y1 + lv_area_get_height(&a) / 2;

    for (int i = 0; i < 4; i++) {
        int dx = kDir[i].dx, dy = kDir[i].dy;
        /* The perpendicular, which is what the base's two corners are offset
         * along - the same trick the diagnostic's arrow uses. */
        int px = -dy, py = dx;
        int tx = cx + dx * kTip;
        int ty = cy + dy * kTip;
        int bx = cx + dx * (kTip - kLen);
        int by = cy + dy * (kTip - kLen);
        lv_draw_triangle_dsc_t tri;

        lv_draw_triangle_dsc_init(&tri);
        /* Lit is the earthy yellow of a key; dimmed is the faint ink every
         * other "here but not now" on this device uses. */
        tri.bg_color = lv_color_hex((g_hint_mask & kDir[i].bit) ? kColHintLit
                                                                : catnip_color_faint());
        tri.bg_opa = LV_OPA_COVER;
        tri.p[0].x = (float)tx;
        tri.p[0].y = (float)ty;
        tri.p[1].x = (float)(bx + px * kHalf);
        tri.p[1].y = (float)(by + py * kHalf);
        tri.p[2].x = (float)(bx - px * kHalf);
        tri.p[2].y = (float)(by - py * kHalf);
        lv_draw_triangle(layer, &tri);
    }
}

bool ensure_hint(void)
{
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
    lv_obj_add_event_cb(g_hint, hint_draw, LV_EVENT_DRAW_MAIN, nullptr);
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
}

void catnip_frame_show_hint(bool on)
{
    static bool shown = false;

    if (!ensure_hint()) return;
    /* The same guard as the bar, for the same reason it gives at length: on a
     * display that renders the whole panel, an invalidation that changes
     * nothing still costs 153,600 bytes over the bus. */
    if (on == shown) return;
    shown = on;
    if (on) lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
}

void catnip_frame_set_hint(unsigned mask)
{
    if (!ensure_hint()) return;
    /* Only on a change, for the reason catnip_frame_show() gives at length:
     * this display renders the whole panel, so a repaint that changes nothing
     * still costs 153,600 bytes over the bus. */
    if (mask == g_last_hint) return;
    g_last_hint = mask;
    g_hint_mask = mask;
    lv_obj_invalidate(g_hint);
}

void catnip_frame_set_title(const char *title)
{
    if (!ensure_bar()) return;
    set_if_changed(g_title, g_last_title, sizeof(g_last_title), title ? title : "");
}

/* Battery, then a glyph for each thing the device has or is doing. Absent
 * rather than dimmed: a card that is not in the slot and a radio that is down
 * are nothing to show, where a direction that does nothing is a real state the
 * control hint has to dim. The sync glyph is here only while a sync is in
 * flight - a badge that never left would say "this device has networking",
 * which is not news. */
void compose_left(void)
{
    char buf[48];
    int off = 0;

    if (g_batt_pct < 0) buf[off] = '\0';
    else off = snprintf(buf, sizeof(buf), "%s %d%%", LV_SYMBOL_BATTERY_FULL, g_batt_pct);
    if (g_has_card && off < (int)sizeof(buf))
        off += snprintf(buf + off, sizeof(buf) - off, "  %s", LV_SYMBOL_SD_CARD);
    if (g_has_radio && off < (int)sizeof(buf))
        off += snprintf(buf + off, sizeof(buf) - off, " %s", LV_SYMBOL_WIFI);
    if (g_syncing && off < (int)sizeof(buf))
        off += snprintf(buf + off, sizeof(buf) - off, " %s", LV_SYMBOL_REFRESH);
    set_if_changed(g_battery, g_last_left, sizeof(g_last_left), buf);
}

void catnip_frame_set_battery(int percent)
{
    if (!ensure_bar()) return;
    /* Not measured draws nothing. A battery that reports -1 and a battery at
     * 0% are different facts, and the bar must not turn one into the other. */
    g_batt_pct = percent;
    compose_left();
}

void catnip_frame_set_status(bool card, bool radio, bool syncing)
{
    if (!ensure_bar()) return;
    g_has_card = card;
    g_has_radio = radio;
    g_syncing = syncing;
    compose_left();
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
