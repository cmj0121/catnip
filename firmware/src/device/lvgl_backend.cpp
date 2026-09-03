/*
 * lvgl_backend.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #30 (device half): a catnip_render_backend that builds real LVGL
 * widgets from the ui.* tree. The traversal is already host-tested in
 * catnip_render.c; this is the thin shim under its vtable.
 */
#ifdef CATNIP_DEVICE_WIP
#include <lvgl.h>

extern "C" {
#include "../catnip_render.h"
}

static lv_obj_t *s_screen;

static void be_begin(void *) { s_screen = lv_obj_create(nullptr); /* clean screen */ }
static void be_label(void *, const char *id, const char *text)
{
    lv_obj_t *l = lv_label_create(s_screen);
    lv_label_set_text(l, text);
    (void)id; // TODO: keep an id->obj map for updates / focus
}
static void be_button(void *, const char *id, const char *text)
{
    lv_obj_t *b = lv_btn_create(s_screen);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    (void)id;
}
static void be_end(void *) { lv_scr_load(s_screen); }

extern "C" const catnip_render_backend *catnip_lvgl_backend()
{
    static const catnip_render_backend be = {nullptr, be_begin, be_label,
                                             be_button, be_end};
    return &be;
}
#endif /* CATNIP_DEVICE_WIP */
