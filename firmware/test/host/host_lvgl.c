/*
 * host_lvgl.c - LVGL on a machine with no panel (#81).
 *
 * The second layer of testing without a board. The first (test_pages.c) proved
 * the tree and the page graph; it could not have caught a single one of these:
 *
 *   - the launch screen came out 80% mascot and 20% clock, because a carousel
 *     cell was sized to its content and the mascot is the panel's own size
 *   - the clock's setter collapsed into the top-left corner, because a per-app
 *     `frame: "bare"` reached a screen that wanted stacking
 *   - the clock's date sat underneath the control hint
 *
 * Every one of those is a *correct tree with wrong boxes*, and no assertion
 * about nodes can fail on them. They need the layout engine, and the layout
 * engine does not need a screen: LVGL renders into memory and asks a flush
 * callback to put it somewhere. Here it is put nowhere.
 *
 * That is the whole of this file - the two functions lvgl_backend.cpp asks its
 * port for, answered with a buffer instead of a panel, plus the app-icon
 * lookups, which read a card that is not there.
 */
#include <stdlib.h>

#include "lvgl.h"

#include "device/app_icon.h"
#include "device/board.h"
#include "device/lvgl_port.h"

static bool g_up;
static lv_display_t *g_disp;
static uint32_t g_tick;

/* Nowhere. The pixels are rendered - which is the point, because rendering is
 * what runs the layout - and then dropped: what is asserted is where things
 * ended up, not what colour they were. */
static void flush_nowhere(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(d);
}

/* LVGL needs a clock and does not care whose. Nothing here animates, so it only
 * has to advance. */
static uint32_t tick_get(void)
{
    return g_tick += 5;
}

bool catnip_lvgl_begin(void)
{
    static uint8_t *buf;

    if (g_up) return true;
    lv_init();
    lv_tick_set_cb(tick_get);
    /* The same full-screen buffer the device uses, and the same render mode:
     * partial rendering would lay everything out identically, but the device
     * renders whole frames and a test that differed there could pass on a
     * geometry the device never produces. */
    buf = (uint8_t *)malloc((size_t)CATNIP_SCREEN_W * CATNIP_SCREEN_H * 2);
    if (!buf) return false;
    g_disp = lv_display_create(CATNIP_SCREEN_W, CATNIP_SCREEN_H);
    if (!g_disp) return false;
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(g_disp, flush_nowhere);
    lv_display_set_buffers(g_disp, buf, NULL,
                           (uint32_t)CATNIP_SCREEN_W * CATNIP_SCREEN_H * 2,
                           LV_DISPLAY_RENDER_MODE_FULL);
    g_up = true;
    return true;
}

void catnip_lvgl_step(void)
{
    if (!g_up) return;
    lv_timer_handler();
}

/* No card, so no app ever has an icon of its own and every one falls through to
 * its glyph - which is the state a device with an empty slot is in. */
void catnip_app_icons_load(const catnip_app_entry *apps, int n)
{
    (void)apps;
    (void)n;
}

const void *catnip_app_icon_find(const char *id)
{
    (void)id;
    return NULL;
}

void catnip_app_icons_free(void)
{
}
