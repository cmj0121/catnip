/* toast.cpp - see toast.h. */
#include <lvgl.h>

#include <Arduino.h>

#include "catnip_font.h"
#include "lvgl_backend.h"
#include "toast.h"

namespace {

/* Long enough to read a line of it, short enough that a fault while something
 * is being adjusted does not sit over the thing being adjusted. */
const uint32_t kShowMs = 3000;

lv_obj_t *g_box;
lv_obj_t *g_label;
uint32_t g_until;

bool build(void)
{
    if (g_box) return true;
    /* Nothing to float over yet. A fault this early has no screen to appear on,
     * and the log still has it. */
    if (!catnip_lvgl_backend_active()) return false;

    /* The top layer, like the frame's bar: it has to appear over whatever is on
     * screen, and nothing on screen may be asked to make room for it. */
    g_box = lv_obj_create(lv_layer_top());
    if (!g_box) return false;
    g_label = lv_label_create(g_box);
    if (!g_label) {
        lv_obj_delete(g_box);
        g_box = nullptr;
        return false;
    }

    lv_obj_set_width(g_box, LV_PCT(92));
    lv_obj_set_height(g_box, LV_SIZE_CONTENT);
    lv_obj_align(g_box, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(g_box, lv_color_hex(catnip_color_danger()), 0);
    lv_obj_set_style_bg_opa(g_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_box, 0, 0);
    lv_obj_set_style_radius(g_box, 6, 0);
    lv_obj_set_style_pad_all(g_box, 6, 0);
    /* Neither of them takes a touch. A fault is a thing to read, not a thing to
     * press, and a box that swallowed taps would make the screen under it look
     * dead for as long as it was up. */
    lv_obj_remove_flag(g_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_width(g_label, LV_PCT(100));
    lv_label_set_long_mode(g_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_label, lv_color_hex(catnip_color_text()), 0);
    /* Twice the caption size. A fault is not a footnote: it is the one thing on
     * the screen worth reading at that moment, and something read at a glance
     * across a room has to be legible at that distance. */
    lv_obj_set_style_text_font(g_label, &catnip_font_20, 0);
    return true;
}

} // namespace

void catnip_toast_show(const char *msg)
{
    if (!msg || !*msg) return;
    if (!build()) return;

    lv_label_set_text(g_label, msg);
    lv_obj_remove_flag(g_box, LV_OBJ_FLAG_HIDDEN);
    g_until = (uint32_t)millis() + kShowMs;
}

void catnip_toast_step(void)
{
    if (!g_box || !g_until) return;
    /* Subtraction rather than a comparison, so the millisecond counter wrapping
     * once every forty-nine days leaves a toast up for three seconds rather
     * than for the rest of the uptime. */
    if ((int32_t)((uint32_t)millis() - g_until) < 0) return;
    g_until = 0;
    lv_obj_add_flag(g_box, LV_OBJ_FLAG_HIDDEN);
}
