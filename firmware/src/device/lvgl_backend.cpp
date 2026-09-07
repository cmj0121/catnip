/*
 * lvgl_backend.cpp - STATUS: superseded scaffold. Does NOT compile.
 *
 * Issue #30 (device half): a catnip_render_backend that builds real LVGL
 * widgets from the ui.* tree. The traversal and the diff are host-tested in
 * catnip_render.c; this is the thin shim under its vtable.
 *
 * What is below predates that vtable. It was written against LVGL 8 names
 * (lv_btn_create, lv_scr_load) and against a backend of begin_screen / label /
 * button / end_screen, which had no notion of identity and could not carry an
 * update, a move or a destroy. Both are gone: see catnip_render.h for the
 * contract this file has to be rewritten against, and note that lifting the
 * #ifdef today would not build. The platformio.ini comment saying both
 * scaffolds compile the moment their guard is lifted is true of shell_ui.cpp
 * and no longer true of this one.
 */
#ifdef CATNIP_DEVICE_WIP
#include <lvgl.h>

extern "C" {
#include "../catnip_render.h"
}

static lv_obj_t *s_screen;

static void be_begin(void *)
{
    s_screen = lv_obj_create(nullptr); /* clean screen */
}
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
static void be_end(void *)
{
    lv_scr_load(s_screen);
}

extern "C" const catnip_render_backend *catnip_lvgl_backend()
{
    static const catnip_render_backend be = {nullptr, be_begin, be_label, be_button,
                                             be_end};
    return &be;
}
#endif /* CATNIP_DEVICE_WIP */
