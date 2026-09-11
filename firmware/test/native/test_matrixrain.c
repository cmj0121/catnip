/*
 * Native test for Matrix Rain (#88).
 *
 * The app has no input and no selection, so almost nothing here is about what a
 * press does. What it is about is the two things a screenshot cannot check: that
 * the frame is a full grid of the right shape, and that it advances - a drop
 * that is here this frame is lower the next, and a column that empties starts
 * again rather than going dark forever.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_api.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

#ifndef APP_MAIN
#define APP_MAIN "apps/matrixrain/main.lua"
#endif

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
static catnip_sched *g_sched;
static unsigned long g_millis;

static unsigned long m_now(void *ud)
{
    (void)ud;
    return g_millis;
}
static void m_pump(void *ud)
{
    (void)ud;
}
static void log_sink(void *ud, const char *msg, size_t len)
{
    (void)ud;
    printf("  lua: %.*s\n", (int)len, msg);
}

static void tick(void)
{
    g_millis += 60;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

/* The grid label's text, copied out. */
static char *grid_text(void)
{
    static char buf[2048];
    lua_State *L = catnip_rt_lua(g_rt);
    catnip_rt_dostring(g_rt, "local n = ui.get('grid') T = n and n.text or ''", "=q");
    lua_getglobal(L, "T");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

/* The grid's colour, read the way the renderer reads it. */
static const char *grid_color(void)
{
    static char buf[16];
    lua_State *L = catnip_rt_lua(g_rt);
    catnip_rt_dostring(g_rt, "C = tostring(ui.get('grid').color)", "=q");
    lua_getglobal(L, "C");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

static int count_lines(const char *s)
{
    int n = s && *s ? 1 : 0;
    for (; s && *s; s++)
        if (*s == '\n') n++;
    return n;
}

/* The longest line, in characters - every line is the same width in a grid. */
static int max_line(const char *s)
{
    int best = 0, cur = 0;
    for (; s && *s; s++) {
        if (*s == '\n') {
            if (cur > best) best = cur;
            cur = 0;
        } else {
            cur++;
        }
    }
    if (cur > best) best = cur;
    return best;
}

static int non_space(const char *s)
{
    int n = 0;
    for (; s && *s; s++)
        if (*s != ' ' && *s != '\n') n++;
    return n;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    return b;
}

int main(void)
{
    char *app = read_file(APP_MAIN);
    if (!app) {
        printf("FAIL - cannot read %s\n", APP_MAIN);
        return 1;
    }

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=rain") == 0, "the rain loads");

    printf("the frame is a full grid of the right shape\n");
    tick();
    {
        char *t = grid_text();
        int rows = count_lines(t);
        int cols = max_line(t);
        CHECK(rows >= 8 && rows <= 16, "the grid is a screenful of rows");
        CHECK(cols >= 20, "and each line spans the panel in characters");
        /* Every line the same width is what makes it a grid rather than ragged
         * text - a monospace block the platform never had to place a cell of. */
        int ragged = 0;
        for (const char *p = t, *start = t;; p++) {
            if (*p == '\n' || *p == '\0') {
                if ((int)(p - start) != cols && *p != '\0') ragged = 1;
                start = p + 1;
                if (*p == '\0') break;
            }
        }
        CHECK(!ragged, "and every line is the same width");
    }

    printf("the colour is the app's, not a role's - the point of the prop\n");
    CHECK(grid_color()[0] == '#', "the grid carries a hex colour");

    printf("it advances on its own clock, with nothing pressed\n");
    /* Two frames a few ticks apart differ: a drop that was here has fallen. A
     * still frame would mean the loop is not running. Compared as whole frames
     * because which cells changed is random - that they changed is not. */
    char before[2048];
    snprintf(before, sizeof(before), "%s", grid_text());
    int moved = 0;
    for (int i = 0; i < 5 && !moved; i++) {
        tick();
        if (strcmp(before, grid_text()) != 0) moved = 1;
    }
    CHECK(moved, "the rain is different a few frames later");

    printf("and it is never wholly empty and never wholly full\n");
    /* A frame with no glyphs is a dead loop; a frame that is all glyphs is not
     * rain, it is a fill. Over a run it stays between. */
    int saw_gap = 0, saw_glyph = 0;
    for (int i = 0; i < 20; i++) {
        tick();
        char *t = grid_text();
        int ink = non_space(t);
        int cells = count_lines(t) * max_line(t);
        if (ink > 0) saw_glyph = 1;
        if (ink < cells) saw_gap = 1;
    }
    CHECK(saw_glyph, "there are glyphs falling");
    CHECK(saw_gap, "and dark between them");

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
