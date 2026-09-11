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
#include "catnip_busy.h"
#include "catnip_typescale.h"
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

    /* ---- a list shows whole rows, never half of one --------------------- */
    /* Eleven rows into a region that fits some number of them: what is asserted
     * is that the region is an exact number of rows tall, so the bottom edge
     * cannot cut one in half. `3/11` in the header is what says there is more,
     * and a half-row saying it as well reads as a fault rather than as an
     * invitation. */
    run("local rows = {}\n"
        "for i = 1, 11 do rows[i] = ui.label{ id = 'r' .. i, text = 'row ' .. i } end\n"
        "local list = ui.list{ id = 'rows', on_prev = function() end }\n"
        "ui.screen{ list }\n"
        "list:set_children(rows)\n");
    /* Twice. A screen settles over two passes - the list's layout is what tells
     * the screen how much of itself to reserve, and the rows are what say how
     * tall a row is - and the device runs continuously, so the second pass is
     * the state anyone ever sees. */
    pass();
    pass();
    shot("list-page");
    {
        lv_obj_t *list = obj("rows");
        lv_obj_t *r1 = obj("r1");

        CHECK(list && r1, "the list and its rows are drawn");
        if (list && r1) {
            int32_t gap = lv_obj_get_style_pad_row(list, 0);
            int32_t pitch = (int32_t)lv_obj_get_height(r1) + gap;
            int32_t inner = (int32_t)lv_obj_get_content_height(list);

            CHECK(pitch > gap, "a row has a height to measure");
            lv_obj_t *scr = lv_obj_get_parent(list);

            /* And the region it was cut out of is the one the frame left: the
             * bar off the top, the hint off the bottom. A column of rows is the
             * shape that reaches the bottom-left corner, so it is the shape
             * that pays for the hint. */
            CHECK(scr && lv_obj_get_style_pad_top(scr, 0) == CATNIP_FRAME_BAR_H + 4,
                  "the bar's height comes off the top of a list screen");
            /* At least the hint's share: what is under the last whole row is
             * the hint's strip plus whatever a half-row would have taken, and
             * the slack is why it is not exactly that. */
            CHECK(scr && lv_obj_get_style_pad_bottom(scr, 0) >= CATNIP_FRAME_HINT_H + 4,
                  "and the hint's off the bottom, because a row reaches its corner");
            CHECK(scr && lv_obj_get_style_pad_bottom(scr, 0) <
                             CATNIP_FRAME_HINT_H + 4 + pitch,
                  "and never more than one row's worth beyond it");
            CHECK(pitch > gap && (inner + gap) % pitch == 0,
                  "and the region is a whole number of rows tall");
            CHECK(inner < CATNIP_SCREEN_H,
                  "which is less than the panel, because the bar and the hint "
                  "take theirs first");
        }
    }

    /* ---- one line on an empty screen is a middle ------------------------ */
    /* A column stacks from the top, which is right for a page of things and
     * wrong for a page that is one sentence: a line alone at the top of an
     * empty screen reads as the first item of a list that never arrived. */
    run("ui.screen{ ui.list{ id = 'e_rows', hidden = true, on_prev = function() end },\n"
        "  ui.label{ id = 'e_say', text = 'nothing on the air', align = 'center' } }\n");
    pass();
    pass();
    shot("empty-page");
    {
        lv_obj_t *say = obj("e_say");
        int mid = CATNIP_SCREEN_H / 2;
        lv_area_t at;

        /* Absolute panel coordinates. lv_obj_get_y() answers relative to the
         * parent's *content* area - it subtracts the padding - so a position
         * compared against the panel's middle has to come from the coords. */
        if (say) lv_obj_get_coords(say, &at);

        CHECK(say != NULL, "the one line is drawn");
        CHECK(say && at.y1 < mid && at.y2 > mid,
              "and it straddles the middle of the panel rather than sitting on top");
        CHECK(say && lv_obj_get_style_text_align(say, 0) == LV_TEXT_ALIGN_CENTER,
              "centred across as well as down");
    }

    /* ---- the clock's face, as the app builds it ------------------------- */
    /* Four children with the centrepiece second: the date is named before it so
     * it is the top line, the weekday and the source line after it so they are
     * the bottom one, first-to-the-left and last-to-the-right. That is the whole
     * of what "the order the children are named in is the layout" means, and it
     * is what puts where-the-time-came-from in the bottom-right corner without
     * the app saying a coordinate.
     *
     * Bare, because the face is screen 1: no bar over it and no hint, so the
     * bottom line starts at the panel's edge rather than clearing a corner
     * nothing is drawn in. */
    catnip_lvgl_backend_set_bare(true);
    run("local days = {}\n"
        "for i, d in ipairs({ 'S', 'M', 'T', 'W', 'T', 'F', 'S' }) do\n"
        "  days[i] = ui.label{ id = 'day' .. i, text = d,\n"
        "                      style = i == 5 and 'body' or 'caption' }\n"
        "end\n"
        "local week = ui.list{ id = 'week', layout = 'row', align = 'center' }\n"
        "ui.screen{ id = 'face',\n"
        "  ui.label{ id = 'date', text = '2026-09-10', style = 'title',\n"
        "           align = 'left' },\n"
        "  ui.label{ id = 'time', text = '14:32', style = 'display' },\n"
        "  week,\n"
        "  ui.label{ id = 'src', text = 'NTP 14:30', style = 'body' } }\n"
        "week:set_children(days)\n");
    pass();
    pass();
    shot("clock-face");
    {
        lv_obj_t *date = obj("date");
        lv_obj_t *week = obj("week");
        lv_obj_t *src = obj("src");
        lv_obj_t *time_ = obj("time");
        int floor_ = CATNIP_SCREEN_H / 2;

        CHECK(date && week && src && time_, "the face is drawn");
        CHECK(date && lv_obj_get_y(date) < floor_,
              "the one named before the centrepiece is the top line");
        /* And in the corner it asked for. A line with nothing else on it is
         * both the first and the last thing on its line, and the order alone
         * cannot break that tie - `align` is what does. */
        CHECK(date && lv_obj_get_x(date) < CATNIP_SCREEN_W / 4,
              "and `align` put it at the left end rather than the middle");
        /* Seven letters at one size, so the line does not shift at midnight
         * when today moves from one of them to the next. */
        {
            lv_obj_t *d1 = obj("day1");
            lv_obj_t *d5 = obj("day5");
            CHECK(d1 && d5, "the strip is seven letters");
            CHECK(d1 && d5 && lv_obj_get_height(d1) == lv_obj_get_height(d5),
                  "the lit one is the same size as the rest, and differs only in ink");
        }
        CHECK(week && src && lv_obj_get_y(week) > floor_ && lv_obj_get_y(src) > floor_,
              "and the two named after it are the bottom line");
        /* The bottom line fits on the panel, both of it. Everything on a face
         * used to be 24 px, which made this one line 328 px wide on a 320 px
         * screen - it ran off the right-hand edge and crossed the strip on the
         * way, and neither of the two assertions below could see it. */
        CHECK(week && src &&
                  lv_obj_get_x(week) + (int)lv_obj_get_width(week) <= lv_obj_get_x(src),
              "and they do not overlap");
        CHECK(week && src && lv_obj_get_x(src) > lv_obj_get_x(week),
              "which runs first-to-the-left, last-to-the-right");
        CHECK(src && lv_obj_get_x(src) + (int)lv_obj_get_width(src) > CATNIP_SCREEN_W / 2,
              "so the source line ends up in the bottom-right corner");
        /* The strip is centred on the panel and the source line is hard against
         * the right edge: the two share the bottom line, and the one that is
         * looked at rather than read is the one in the middle of it. */
        CHECK(week && lv_obj_get_x(week) > CATNIP_SCREEN_W / 4 &&
                  lv_obj_get_x(week) + (int)lv_obj_get_width(week) <
                      3 * CATNIP_SCREEN_W / 4,
              "and `align` put the strip in the middle of the bottom line");
        CHECK(src &&
                  lv_obj_get_x(src) + (int)lv_obj_get_width(src) >= CATNIP_SCREEN_W - 12,
              "with the source line hard against the right edge");
    }
    /* The corner rule, from the other side, on a face that says no `align` at
     * all. A list reserves the hint's corner because its rows reach it; a bare
     * face reserves nothing, because nothing is drawn over one - and reserving
     * it anyway would be the platform taking 34 px for a hint it is not going
     * to draw. */
    run("ui.screen{ id = 'plain_face',\n"
        "  ui.label{ id = 'pf_time', text = '14:32', style = 'display' },\n"
        "  ui.label{ id = 'pf_a', text = 'left' },\n"
        "  ui.label{ id = 'pf_b', text = 'right' } }\n");
    pass();
    {
        lv_obj_t *a = obj("pf_a");
        CHECK(a && lv_obj_get_x(a) < CATNIP_FRAME_HINT_W,
              "a bare face holds nothing back for a hint it never gets");
    }
    catnip_lvgl_backend_set_bare(false);

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
     * standard frame it pushes itself under. It used to carry a caption saying
     * where the time came from; that line is on the face now, where a doubt
     * about the time is actually had, rather than over the shoulder of somebody
     * already answering it.
     *
     * The caption stays in this tree all the same, under a name that says what
     * it is for: `align` reached the renderer once and then did nothing for a
     * label that was not a row in a list, and the screen looked exactly as it
     * had. Something has to hold that path down. */
    run("ui.screen{ id = 'setter',\n"
        "  ui.list{ id = 'cols', layout = 'mixer', on_prev = function() end,\n"
        "    ui.label{ id = 'c1', text = 'Y', value = 50 },\n"
        "    ui.label{ id = 'c2', text = 'M', value = 50 } },\n"
        "  ui.label{ id = 'source', text = 'a line that must clear the corner',\n"
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
        CHECK(src != NULL, "a line under the columns exists");
        CHECK(src && lv_obj_get_y(src) + lv_obj_get_height(src) <= CATNIP_SCREEN_H,
              "and sits inside the panel rather than below its bottom edge");
        /* Ranged right, which is what keeps it out of the control hint's
         * corner on a screen that does reserve one. */
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
        lv_obj_t *facts;
        lv_obj_t *first;
        lv_obj_t *second;
        lv_obj_t *last;

        CHECK(info != NULL, "the device page is built");
        catnip_device_info_show(info, rows, 11);
        pass();
        pass();
        shot("device-page");
        facts = obj("info_list");
        first = obj("info1");
        second = obj("info2");
        last = obj("info11");
        CHECK(facts && first, "the facts are drawn");
        /* Nothing that acts is on it: the two tiles are behind A now, where
         * every operation in the device is. */
        CHECK(obj("info_left") == NULL && obj("info_right") == NULL,
              "and nothing on the page is a button");
        /* Lines, not rows. A row reserves 20 px in front of its text for an
         * icon slot; a line of prose starts at the margin, because there is no
         * icon coming and no column for one to line up with. */
        CHECK(first && lv_obj_get_x(first) < 20,
              "a line starts at the margin rather than behind an icon slot");
        /* And the column takes the region, since it is the only thing on the
         * screen now. */
        CHECK(facts && lv_obj_get_height(facts) > CATNIP_SCREEN_H / 2,
              "the facts take the room that is left");
        CHECK(second && lv_obj_get_y(second) > lv_obj_get_y(first),
              "and they run down it in the order they were given");
        /* A page, on a fixed boundary: eleven facts do not fit, and the ones
         * that do not are on the next page rather than below the fold. A window
         * that slid to follow a cursor would show a different set of lines
         * every time the page was opened, and nothing on it explains why. */
        CHECK(last && lv_obj_has_flag(last, LV_OBJ_FLAG_HIDDEN),
              "the eleventh is on the next page, not under the tenth");
        CHECK(first && !lv_obj_has_flag(first, LV_OBJ_FLAG_HIDDEN),
              "and the page starts at the first line, not wherever a cursor is");
        /* And the bar it puts up, drawn over the facts it was asked from. */
        {
            static const char *const kNames[3] = {"Prefs", "Sizes", "Diag"};
            static const catnip_icon kIcons[3] = {CATNIP_ICON_SETTINGS, CATNIP_ICON_FILE,
                                                  CATNIP_ICON_WARNING};
            catnip_frame_show(true);
            catnip_frame_set_actions(kNames, kIcons, 3, 0, true);
            pass();
            shot("device-page-actions");
            catnip_frame_set_actions(nullptr, nullptr, 0, 0, false);
            catnip_frame_show(false);
            pass();
        }
        catnip_device_info_free(info);
    }

    /* ---- every type size, drawn in itself ------------------------------- */
    /* The page exists to be looked at, so what is asserted is the one thing a
     * picture cannot be trusted for: that the four lines really are four
     * different sizes, largest first. */
    {
        catnip_typescale *ts = catnip_typescale_new(g_rt);
        CHECK(ts != NULL, "the type sample page is built");
        catnip_typescale_show(ts);
        pass();
        pass();
        shot("type-sizes");
        {
            lv_obj_t *big = obj("ts_display");
            lv_obj_t *t = obj("ts_title");
            lv_obj_t *b = obj("ts_body");
            lv_obj_t *c = obj("ts_caption");

            CHECK(big && t && b && c, "one line per size");
            CHECK(big && t && lv_obj_get_height(big) > lv_obj_get_height(t) && t && b &&
                      lv_obj_get_height(t) > lv_obj_get_height(b) && b && c &&
                      lv_obj_get_height(b) > lv_obj_get_height(c),
                  "and each is smaller than the one above it, which is the whole page");
        }
        catnip_typescale_free(ts);
    }

    /* ---- the app grid: many apps, wrapping, pinned ones marked --------- */
    catnip_lvgl_backend_set_bare(false);
    {
        catnip_app_grid *g = catnip_app_grid_new(g_rt);
        catnip_app_entry apps[8];
        lv_obj_t *c1;
        lv_obj_t *c6;
        lv_obj_t *c7;
        for (int i = 0; i < 8; i++) {
            memset(&apps[i], 0, sizeof(apps[i]));
            snprintf(apps[i].id, sizeof(apps[i].id), "app%d", i);
            snprintf(apps[i].name, sizeof(apps[i].name), "App %d", i);
            apps[i].compatible = 1;
        }
        CHECK(g != NULL, "the app grid is built");
        /* Eight, so there is a second page and a first page that is full. */
        catnip_app_grid_show(g, apps, 8, false, "app3"); /* app3 unpinned */
        pass();
        pass();
        shot("app-grid");
        c1 = obj("grid1");
        c6 = obj("grid6");
        c7 = obj("grid7");
        CHECK(c1 && c6 && c7, "the cells are drawn");
        CHECK(c1 && c6 && lv_obj_get_y(c6) > lv_obj_get_y(c1),
              "and wrap onto a second row rather than off the edge");
        /* Six across two rows, and the seventh is on the next page rather than
         * below the fold: it exists, it is simply not drawn. */
        CHECK(c7 && lv_obj_has_flag(c7, LV_OBJ_FLAG_HIDDEN),
              "the seventh is on the next page, not under the sixth");
        CHECK(c1 && !lv_obj_has_flag(c1, LV_OBJ_FLAG_HIDDEN) && c6 &&
                  !lv_obj_has_flag(c6, LV_OBJ_FLAG_HIDDEN),
              "and all six of this page are up");
        /* Both rows inside the region: a grid pages, so half a row is not a
         * thing it can show, and the second row must be whole. */
        CHECK(c6 && lv_obj_get_y(c6) + (int32_t)lv_obj_get_height(c6) <=
                        CATNIP_SCREEN_H - CATNIP_FRAME_HINT_H,
              "and the second row is clear of the hint's strip");
        catnip_app_grid_free(g);
    }

    /* ---- the scanner's group view: a grid whose cells carry counts ----- */
    /* The one shape the badge exists for. Four cells, two of them counting,
     * one bright because it is the radio listening right now, and two greyed
     * because this board cannot hear them at all. Every state the grid has to
     * say is said in one picture, which is the whole argument for the shape. */
    {
        lv_obj_t *w;
        lv_obj_t *n;
        run("local names = {'WiFi','BLE','IR','NFC'}\n"
            "local ic = {'wifi','ble','ir','nfc'}\n"
            "local badge = {8, 3}\n"
            "local cells = {}\n"
            "for i = 1, 4 do\n"
            "  cells[i] = ui.label{ id = 'sc' .. i, text = names[i], icon = ic[i],\n"
            "                       badge = badge[i], disabled = i > 2,\n"
            "                       style = i == 2 and 'body' or 'caption' }\n"
            "end\n"
            "local g = ui.list{ id = 'sc_grid', layout = 'grid' }\n"
            "g:set_children(cells)\n"
            "g.selected = 2\n"
            "ui.screen{ g }\n");
        pass();
        pass();
        shot("scanner-grid");
        w = obj("sc1");
        n = obj("sc4");
        CHECK(w && n, "the scanner's cells are drawn");
        /* Four across three columns is a second row, and both rows have to be
         * whole - a grid pages, so half a cell is not a thing it can show. */
        CHECK(w && n && lv_obj_get_y(n) > lv_obj_get_y(w),
              "the fourth wraps onto a second row");
        CHECK(n && lv_obj_get_y(n) + (int32_t)lv_obj_get_height(n) <=
                       CATNIP_SCREEN_H - CATNIP_FRAME_HINT_H,
              "and that row is clear of the hint's strip");
    }

    /* ---- the action bar, and the hint riding on it ---------------------- */
    /* What long A produces. Two things are checked and both are geometry the
     * rules turn on: it sits at the foot of the panel with the content still
     * visible above it, and the hint is lifted onto its shoulder rather than
     * left underneath it. */
    {
        static const char *const kNames[3] = {"View", "Delete", "Reset card"};
        static const catnip_icon kIcons[3] = {CATNIP_ICON_FILE, CATNIP_ICON_TRASH,
                                              CATNIP_ICON_WARNING};
        lv_obj_t *hint_obj;
        int hint_y_down, hint_y_up;

        catnip_frame_show(true);
        catnip_frame_show_hint(true);
        catnip_frame_set_hint(CATNIP_HINT_LEFT | CATNIP_HINT_RIGHT);
        catnip_frame_set_actions(nullptr, nullptr, 0, 0, false);
        pass();
        /* The hint by its size, which is the one thing about it that is fixed:
         * nothing in the node model owns it either, and after the bar exists it
         * is no longer the last child of the top layer. */
        hint_obj = nullptr;
        for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
            lv_obj_t *c = lv_obj_get_child(lv_layer_top(), (int32_t)i);
            if ((int)lv_obj_get_width(c) == CATNIP_FRAME_HINT_W &&
                (int)lv_obj_get_height(c) == CATNIP_FRAME_HINT_H)
                hint_obj = c;
        }
        hint_y_down = hint_obj ? (int)lv_obj_get_y(hint_obj) : 0;

        catnip_frame_set_actions(kNames, kIcons, 3, 1, true);
        pass();
        shot("action-bar");
        {
            lv_obj_t *act = nullptr;
            uint32_t n_top = lv_obj_get_child_count(lv_layer_top());
            /* The bar is the widest thing on the top layer that is not the
             * top bar: found by size rather than by a handle, because nothing
             * in the node model owns it - that is the point of it. */
            for (uint32_t i = 0; i < n_top; i++) {
                lv_obj_t *c = lv_obj_get_child(lv_layer_top(), (int32_t)i);
                if (lv_obj_get_y(c) > CATNIP_SCREEN_H / 2 &&
                    lv_obj_get_width(c) > CATNIP_SCREEN_W / 2)
                    act = c;
            }
            CHECK(act != NULL, "the action bar is drawn");
            CHECK(act && lv_obj_get_y(act) + (int32_t)lv_obj_get_height(act) <=
                             CATNIP_SCREEN_H,
                  "at the foot of the panel, inside it");
            CHECK(act && lv_obj_get_y(act) > CATNIP_SCREEN_H / 2,
                  "and only across the bottom, so the content stays visible above it");
            CHECK(act && lv_obj_get_child_count(act) == 3, "with a cell per action");
        }
        hint_y_up = hint_obj ? (int)lv_obj_get_y(hint_obj) : 0;
        CHECK(hint_obj && hint_y_up < hint_y_down,
              "and the hint has been lifted onto its shoulder rather than left under it");

        catnip_frame_set_actions(nullptr, nullptr, 0, 0, false);
        pass();
        CHECK(hint_obj && (int)lv_obj_get_y(hint_obj) == hint_y_down,
              "putting the bar away puts the hint back in its corner");
        catnip_frame_show_hint(false);
        catnip_frame_show(false);
    }

    /* ---- the busy ring -------------------------------------------------- */
    /* The shape is pinned in test_busy.c; what is checked here is that the
     * platform draws it over what is on screen rather than in place of it - the
     * thing being waited for is usually about what is already there. */
    {
        lv_obj_t *ring = nullptr;

        catnip_frame_set_busy("scanning");
        pass();
        shot("busy");
        for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
            lv_obj_t *c = lv_obj_get_child(lv_layer_top(), (int32_t)i);
            /* Eight dots and a word: the only thing on the top layer with nine
             * children. Found by shape because nothing in the node model owns
             * it, which is the whole point of it being the platform's. */
            if (lv_obj_get_child_count(c) == CATNIP_BUSY_DOTS + 1) ring = c;
        }
        CHECK(ring != NULL, "the busy ring is drawn");
        CHECK(ring && !lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN), "and it is up");
        /* The whole panel, not a dialog over one: while it is up there is
         * nothing true underneath to leave showing. */
        CHECK(ring && (int)lv_obj_get_width(ring) == CATNIP_SCREEN_W &&
                  (int)lv_obj_get_height(ring) == CATNIP_SCREEN_H,
              "and it takes the whole panel rather than floating over it");
        CHECK(ring && lv_obj_get_style_bg_opa(ring, 0) == LV_OPA_COVER,
              "opaque, so the screen it replaced is not read through it");
        catnip_frame_set_busy(nullptr);
        pass();
        CHECK(ring && lv_obj_has_flag(ring, LV_OBJ_FLAG_HIDDEN),
              "and nothing to wait for puts it away");
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
