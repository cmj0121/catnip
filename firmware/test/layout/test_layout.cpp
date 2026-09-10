/*
 * Where things actually end up (#81).
 *
 * The tests in test/native/ prove the tree: which nodes exist, what is
 * focusable, what a press reaches. Not one of them could have failed on the
 * three bugs that cost the most time on this project, because all three were a
 * *correct tree with wrong boxes*:
 *
 *   - the launch screen came out 80% mascot and 20% clock, because a carousel
 *     cell was sized to its content and the mascot is the panel's own size
 *   - the clock's setter collapsed into the top-left corner, because a per-app
 *     `frame: "bare"` reached a screen that wanted stacking
 *   - the clock's date sat underneath the control hint
 *
 * Those are the layout engine's answers, and the layout engine needs no panel.
 * LVGL renders into memory here and the flush callback throws the pixels away;
 * what is asserted is geometry - a height, a corner, an order.
 *
 * Geometry rather than screenshots, deliberately. A golden image would catch
 * these too and would also fail on a font hinting change, a one-pixel padding,
 * and every deliberate redesign - and the failure would say "3,412 pixels
 * differ" where these say which rule was broken.
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_device_info.h"
#include "catnip_app_grid.h"
#include "catnip_menu.h"
#include "catnip_ui.h"
#include "device/board.h"
#include "device/frame.h"
#include "device/ui_input_core.h"
#include "device/lvgl_backend.h"
#include "device/lvgl_port.h"
#include "host_shot.h"
#include <stdlib.h>

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

static catnip_rt *g_rt;
static const catnip_render_backend *g_real;

/* The real backend, with a note taken of every name as it goes past.
 *
 * A shim rather than a new entry point on the backend: what is under test is
 * the device's own lvgl_backend.cpp, and a test that made it grow a lookup for
 * the test's benefit would be testing something the device does not have. The
 * road from a handle back to an object already exists - the backend stores the
 * handle in lv_obj_set_user_data(), because that is how an LVGL click finds its
 * node - so this only has to remember which name went with which handle. */
#define NAMES 128
static struct {
    catnip_handle h;
    char id[32];
    int used;
} g_named[NAMES];

static void remember(catnip_handle h, const catnip_node_desc *d)
{
    if (!d->id || !d->id[0]) return;
    for (int i = 0; i < NAMES; i++) {
        if (g_named[i].used && g_named[i].h == h) return;
        if (g_named[i].used) continue;
        g_named[i].used = 1;
        g_named[i].h = h;
        snprintf(g_named[i].id, sizeof(g_named[i].id), "%s", d->id);
        return;
    }
}

static int shim_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                       const catnip_node_desc *d)
{
    remember(h, d);
    return g_real->create(ud, h, parent, index, d);
}

static void shim_update(void *ud, catnip_handle h, const catnip_node_desc *d)
{
    remember(h, d);
    g_real->update(ud, h, d);
}

static void shim_destroy(void *ud, catnip_handle h)
{
    for (int i = 0; i < NAMES; i++)
        if (g_named[i].used && g_named[i].h == h) g_named[i].used = 0;
    g_real->destroy(ud, h);
}

static catnip_render_backend g_be;

static void run(const char *lua)
{
    if (catnip_rt_dostring(g_rt, lua, "=t") != 0) printf("  (lua refused: %s)\n", lua);
}

/* One frame, the way the device does it: reconcile the tree, then let LVGL lay
 * it out and render it. The layout is computed during the render, so nothing
 * below can be asked before this has run. */
static void pass(void)
{
    catnip_render(g_rt, &g_be);
    catnip_lvgl_step();
    lv_refr_now(NULL);
}

/* And, when asked, a picture of what that came out as.
 *
 * Off unless CATNIP_SHOTS names a directory, because writing four PNGs on every
 * `make test` is four files nobody asked for. Nothing here asserts anything
 * about them: what a page *should* look like is a judgement, and a golden image
 * would make every deliberate redesign a failing test. This is for the moment
 * when the geometry all passes and the page is still wrong. */
static void shot(const char *name)
{
    const char *dir = getenv("CATNIP_SHOTS");
    char path[256];

    if (!dir || !dir[0]) return;
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    printf("    [wrote %s]\n", catnip_host_shot(path) ? path : "nothing");
}

static lv_obj_t *find_handle(lv_obj_t *root, catnip_handle h)
{
    if (!root) return NULL;
    if ((catnip_handle)(intptr_t)lv_obj_get_user_data(root) == h) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *hit = find_handle(lv_obj_get_child(root, (int32_t)i), h);
        if (hit) return hit;
    }
    return NULL;
}

/* The object behind a node, by the name the app gave it. */
static lv_obj_t *obj(const char *id)
{
    for (int i = 0; i < NAMES; i++)
        if (g_named[i].used && strcmp(g_named[i].id, id) == 0)
            return find_handle(lv_screen_active(), g_named[i].h);
    return NULL;
}

int main(void)
{
    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    g_real = catnip_lvgl_backend(g_rt);
    g_be = *g_real;
    g_be.create = shim_create;
    g_be.update = shim_update;
    g_be.destroy = shim_destroy;

    printf("where things end up, with no panel under them\n");
    CHECK(catnip_lvgl_begin(), "LVGL comes up on a buffer instead of a display");

    /* ---- the launcher, as the launcher builds it ------------------------ */
    /* The real menu rather than a tree shaped like one: what is under test is
     * the arrangement this device actually produces, and a hand-made carousel
     * would only prove that a hand-made carousel lays out. With no apps there
     * is one cell, the cat - which is exactly the shape that came out wrong. */
    catnip_lvgl_backend_set_bare(false);
    {
        catnip_menu *menu = catnip_menu_new(g_rt);
        lv_obj_t *ring;
        lv_obj_t *cell;

        CHECK(menu != NULL, "the launcher is built");
        catnip_menu_show(menu, NULL, 0, false, NULL);
        pass();
        shot("launcher");
        ring = obj("menu_list");
        cell = obj("menu_home");
        CHECK(ring && cell, "the ring and the cat are drawn");
        /* The bug: the mascot is the panel's own size and the cell was sized to
         * its content, so the cell came out taller than the ring it lives in
         * and the launch screen was 80% cat and 20% something else. A carousel
         * shows one cell at a time *because* the cell is the region. */
        CHECK(cell && ring && lv_obj_get_height(cell) == lv_obj_get_height(ring),
              "the cat's cell is exactly the ring, not taller and not a fraction");
        CHECK(ring && lv_obj_get_height(ring) == CATNIP_SCREEN_H,
              "and the ring is the whole panel, because a carousel reserves no bar");
        catnip_menu_free(menu);
    }

    /* ---- a face, and the corner it may not use -------------------------- */
    /* The clock's shape: the centrepiece first, so both of the others are on
     * the bottom line - which is what "the order the children are named in is
     * the layout" means, and what the clock's own face relies on. */
    run("ui.screen{ id = 'face',\n"
        "  ui.list{ id = 'canvas', layout = 'canvas',\n"
        "    ui.label{ id = 'time', text = '14:03', style = 'display' },\n"
        "    ui.label{ id = 'date', text = '2026-09-09', style = 'caption' },\n"
        "    ui.label{ id = 'week', text = 'Tue', style = 'caption' } } }\n");
    pass();
    shot("clock-face");
    {
        lv_obj_t *date = obj("date");
        lv_obj_t *week = obj("week");
        lv_obj_t *time_ = obj("time");
        lv_obj_t *canvas = obj("canvas");
        int floor_ = canvas ? (int)lv_obj_get_height(canvas) / 2 : 0;

        CHECK(date && week && time_, "the face is drawn");
        CHECK(date && week && lv_obj_get_y(date) > floor_ && lv_obj_get_y(week) > floor_,
              "both of the ones named after the centrepiece are on the bottom line");
        CHECK(date && week && lv_obj_get_x(week) > lv_obj_get_x(date),
              "which runs first-to-the-left, last-to-the-right");
        /* And the bug the hint introduced the day it was drawn: the date sat
         * underneath four arrows. */
        CHECK(date && lv_obj_get_x(date) >= CATNIP_FRAME_HINT_W,
              "and starts to the right of the control hint");
    }

    /* ---- a pushed screen with no centrepiece stacks --------------------- */
    /* `frame: "bare"` belongs to the app, not to one of its screens, so every
     * screen it pushes is bare too - and a page of columns wants stacking, not
     * placing. Laid out as a face it collapsed into the top-left corner, which
     * is what "the editor is hard to modify" turned out to mean. */
    /* First the bare-screen case this section has always covered: a pushed
     * screen with no centrepiece stacks rather than being placed. */
    catnip_lvgl_backend_set_bare(true);
    run("ui.screen{ id = 'bare_setter',\n"
        "  ui.list{ id = 'bcols', layout = 'mixer', on_prev = function() end,\n"
        "    ui.label{ id = 'b1', text = 'Y', value = 50 },\n"
        "    ui.label{ id = 'b2', text = 'M', value = 50 } } }\n");
    pass();
    {
        lv_obj_t *bc = obj("bcols");
        CHECK(bc && lv_obj_get_height(bc) >= CATNIP_SCREEN_H - 20,
              "a bare screen with no centrepiece fills its height");
    }

    catnip_lvgl_backend_set_bare(false);
    /* The Clock app's setter as the app actually builds it: the mixer, and the
     * caption under it saying where the time came from (#84). The caption is
     * the thing being checked - a line added under a list that grows is the
     * classic way to add something nobody ever sees. */
    run("ui.screen{ id = 'setter',\n"
        "  ui.list{ id = 'cols', layout = 'mixer', on_prev = function() end,\n"
        "    ui.label{ id = 'c1', text = 'Y', value = 50 },\n"
        "    ui.label{ id = 'c2', text = 'M', value = 50 } },\n"
        "  ui.label{ id = 'source', text = 'network time, synced 14:18',\n"
        "           style = 'body', align = 'right' } }\n");
    pass();
    /* With the hint drawn, because both live in the bottom-left corner and the
     * hint is on the layer above: if the reserved corner is not actually being
     * respected, this is where it shows. */
    catnip_frame_show(true);
    catnip_frame_show_hint(true);
    catnip_frame_set_hint(CATNIP_HINT_LEFT | CATNIP_HINT_RIGHT);
    pass();
    shot("setter");
    catnip_frame_show_hint(false);
    catnip_frame_show(false);
    {
        lv_obj_t *src = obj("source");
        CHECK(src != NULL, "the setter's source line exists");
        CHECK(src && lv_obj_get_y(src) + lv_obj_get_height(src) <= CATNIP_SCREEN_H,
              "and sits inside the panel rather than below its bottom edge");
        /* Ranged right, which is what keeps it out of the control hint's
         * corner. Pinned because `align` reached the renderer and then did
         * nothing for a label that was not a row in a list - it applied on one
         * path and not the other, and the screen looked exactly as it had. */
        CHECK(src && lv_obj_get_style_text_align(src, 0) == LV_TEXT_ALIGN_RIGHT,
              "and is ranged right, clear of the hint's corner");
        CHECK(src && lv_obj_get_width(src) > CATNIP_SCREEN_W / 2,
              "with the width to be ranged within");
    }
    {
        lv_obj_t *cols = obj("cols");
        lv_obj_t *c1 = obj("c1");
        lv_obj_t *c2 = obj("c2");

        CHECK(cols && lv_obj_get_width(cols) >= CATNIP_SCREEN_W - 20,
              "the setter's columns fill the width");
        /* The mixer takes what is left after the caption, so it is most of the
         * panel but not all of it - which is the point: a list that took all of
         * it would push the caption off the bottom. */
        CHECK(cols && lv_obj_get_height(cols) > CATNIP_SCREEN_H / 2,
              "and most of the height, leaving the caption its line");
        CHECK(c1 && c2 && lv_obj_get_x(c2) > lv_obj_get_x(c1),
              "and its columns run across it rather than stacking");
        CHECK(c1 && c2 && lv_obj_get_height(c1) == lv_obj_get_height(c2),
              "each as tall as the region, so their values read against each other");
    }

    /* ---- the device page: facts that grow, a strip that does not -------- */
    catnip_lvgl_backend_set_bare(false);
    {
        catnip_device_info *info = catnip_device_info_new(g_rt);
        /* The real page's shape: eleven facts, of which the clock is the
         * seventh. What a reader can actually see without scrolling is the
         * question a screenshot answers and a node assertion cannot. */
        const char *rows[11] = {"catnip v0.4.0",
                                "clock 2026-09-10 14:18 (ntp)",
                                "battery 82%",
                                "card none",
                                "built 2026-09-10",
                                "ESP32-S3 rev 2, 240 MHz",
                                "flash 16 MB",
                                "psram 7168 KB free of 8 MB",
                                "heap 213 KB free",
                                "mac 8C:BF:EA:11:22:33",
                                "i2c 18 19 34 38 41 51 68"};
        const catnip_info_key pref = {"Preference", "settings"};
        const catnip_info_key diag = {"Diagnostic", "warning"};
        lv_obj_t *facts;
        lv_obj_t *keys;
        lv_obj_t *k1;
        lv_obj_t *k2;

        CHECK(info != NULL, "the device page is built");
        catnip_device_info_show(info, rows, 11, &pref, &diag);
        pass();
        shot("device-page");
        facts = obj("info_list");
        keys = obj("info_keys");
        k1 = obj("info_left");
        k2 = obj("info_right");
        CHECK(facts && keys && k1 && k2, "the facts and both tiles are drawn");
        /* A list grows by default, because a list is usually the thing on a
         * screen that should take what is left; a strip is the opposite, and
         * one that also grew split the screen down the middle. */
        CHECK(facts && keys && lv_obj_get_height(facts) > lv_obj_get_height(keys),
              "the facts take the room and the strip takes only what it needs");
        CHECK(k1 && k2 && lv_obj_get_x(k2) > lv_obj_get_x(k1),
              "the two tiles sit side by side");
        CHECK(k1 && k2 && lv_obj_get_width(k1) + lv_obj_get_width(k2) <= CATNIP_SCREEN_W,
              "and share the width rather than overflowing it");
        /* An icon over a word, which is what makes it a tile rather than a
         * caption with a picture in front of it. */
        CHECK(k1 && lv_obj_get_child_count(k1) == 2 &&
                  lv_obj_get_y(lv_obj_get_child(k1, 0)) <
                      lv_obj_get_y(lv_obj_get_child(k1, 1)),
              "and each is its icon above its word");
        catnip_device_info_free(info);
    }

    /* ---- the app grid: many apps, wrapping, pinned ones marked --------- */
    catnip_lvgl_backend_set_bare(false);
    {
        catnip_app_grid *g = catnip_app_grid_new(g_rt);
        catnip_app_entry apps[6];
        lv_obj_t *c1;
        lv_obj_t *c6;
        for (int i = 0; i < 6; i++) {
            memset(&apps[i], 0, sizeof(apps[i]));
            snprintf(apps[i].id, sizeof(apps[i].id), "app%d", i);
            snprintf(apps[i].name, sizeof(apps[i].name), "App %d", i);
            apps[i].compatible = 1;
        }
        CHECK(g != NULL, "the app grid is built");
        catnip_app_grid_show(g, apps, 6, false, "app3"); /* app3 unpinned */
        pass();
        shot("app-grid");
        c1 = obj("grid1");
        c6 = obj("grid6");
        CHECK(c1 && c6, "the cells are drawn");
        CHECK(c1 && c6 && lv_obj_get_y(c6) > lv_obj_get_y(c1),
              "and wrap onto further rows rather than off the edge");
        catnip_app_grid_free(g);
    }

    /* ---- the control hint, drawn over a page ---------------------------- */
    {
        /* Three lit, one dim - the state the cat is in - so the shot shows both
         * inks and the alignment of all four at once. */
        static const struct {
            unsigned mask;
            const char *name;
        } kStates[4] = {
            {CATNIP_HINT_UP | CATNIP_HINT_DOWN | CATNIP_HINT_LEFT | CATNIP_HINT_RIGHT,
             "hint-all"},
            {CATNIP_HINT_LEFT | CATNIP_HINT_RIGHT | CATNIP_HINT_DOWN, "hint-cat"},
            {CATNIP_HINT_UP | CATNIP_HINT_DOWN, "hint-column"},
            {0, "hint-none"},
        };
        catnip_frame_show(true);
        catnip_frame_show_hint(true);
        for (int i = 0; i < 4; i++) {
            catnip_frame_set_hint(kStates[i].mask);
            pass();
            shot(kStates[i].name);
        }
        catnip_frame_show_hint(false);
        catnip_frame_show(false);
        CHECK(1, "the control hint is drawn");
    }

    catnip_rt_free(g_rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
