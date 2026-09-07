/*
 * Native test for issues #26/#27: the File Browser demo app, driven the way the
 * device will drive it - events posted to the handles the renderer handed the
 * backend, and nothing else. The app exports no global for this test to reach
 * into, which is the point: what is exercised here is the contract #31 will
 * deliver against.
 *
 * The recording backend is the one from test_render.c, and the transcripts are
 * the assertion. A test that only checked the screen still showed the right
 * rows would have passed against the version of this app that called ui.screen
 * on every keypress and had the whole screen destroyed and drawn again. So the
 * costs are asserted too: moving the selection is one update, and a directory
 * change pays for the difference in row count rather than for the listing.
 *
 * Verbs, as in test_render.c: `[` `]` bracket a pass, `+` creates, `~` updates,
 * `>` moves, `-` destroys, `!` shows a screen.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catnip_api.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

#ifndef APP_MAIN
#define APP_MAIN "../apps/filebrowser/main.lua"
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

#define CHECK_OPS(expect, name)                                                          \
    do {                                                                                 \
        if (strcmp(g_ops, expect) == 0) {                                                \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n         want %s\n         got  %s\n", name, expect,    \
                   g_ops);                                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* ---- the recording backend ---------------------------------------------- */

static char g_ops[2048];

static void rec(const char *fmt, ...)
{
    char line[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    strncat(g_ops, line, sizeof(g_ops) - strlen(g_ops) - 1);
}

typedef struct {
    catnip_handle h;
    char name[32];
    int used;
} robj;
static robj g_objs[128];

static void obj_put(catnip_handle h, const char *name)
{
    for (int i = 0; i < 128; i++) {
        if (!g_objs[i].used) {
            g_objs[i].used = 1;
            g_objs[i].h = h;
            snprintf(g_objs[i].name, sizeof(g_objs[i].name), "%s", name);
            return;
        }
    }
}
static void obj_del(catnip_handle h)
{
    for (int i = 0; i < 128; i++)
        if (g_objs[i].used && g_objs[i].h == h) g_objs[i].used = 0;
}
static const char *obj_name(catnip_handle h)
{
    if (h == CATNIP_HANDLE_NONE) return "root";
    for (int i = 0; i < 128; i++)
        if (g_objs[i].used && g_objs[i].h == h) return g_objs[i].name;
    return "?";
}
static catnip_handle obj_handle(const char *name)
{
    for (int i = 0; i < 128; i++)
        if (g_objs[i].used && strcmp(g_objs[i].name, name) == 0) return g_objs[i].h;
    return CATNIP_HANDLE_NONE;
}

static const char *kind_name(catnip_node_kind k)
{
    switch (k) {
    case CATNIP_NODE_SCREEN: return "screen";
    case CATNIP_NODE_BUTTON: return "button";
    case CATNIP_NODE_LIST: return "list";
    default: return "label";
    }
}
static const char *desc_name(const catnip_node_desc *d)
{
    return d->id[0] ? d->id : kind_name(d->kind);
}

/* The list's `selected`, which the transcript does not carry. */
static int g_rows_sel = -2;

static void b_begin(void *ud)
{
    (void)ud;
    rec("[");
}
static void b_end(void *ud)
{
    (void)ud;
    rec("]");
}
static int b_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                    const catnip_node_desc *d)
{
    (void)ud;
    obj_put(h, desc_name(d));
    if (strcmp(desc_name(d), "rows") == 0) g_rows_sel = d->selected;
    rec("+%s@%s[%d]", desc_name(d), obj_name(parent), index);
    return 0;
}
static void b_update(void *ud, catnip_handle h, const catnip_node_desc *d)
{
    (void)ud;
    if (strcmp(desc_name(d), "rows") == 0) g_rows_sel = d->selected;
    rec("~%s='%s'", obj_name(h), d->text);
}
static void b_move(void *ud, catnip_handle h, int index)
{
    (void)ud;
    rec(">%s[%d]", obj_name(h), index);
}
static void b_destroy(void *ud, catnip_handle h)
{
    (void)ud;
    rec("-%s", obj_name(h));
    obj_del(h);
}
static void b_show(void *ud, catnip_handle h)
{
    (void)ud;
    rec("!%s", obj_name(h));
}

static const catnip_render_backend BE = {NULL,     b_begin, b_end,     b_create,
                                         b_update, b_move,  b_destroy, b_show};

static char g_log[2048];
static void log_sink(void *ud, const char *msg, size_t len)
{
    (void)ud;
    size_t have = strlen(g_log);
    if (have + len + 2 >= sizeof(g_log)) return;
    memcpy(g_log + have, msg, len);
    g_log[have + len] = '\n';
    g_log[have + len + 1] = '\0';
}

/* ---- driving the app ---------------------------------------------------- */

static catnip_rt *g_rt;

static int pass(void)
{
    g_ops[0] = '\0';
    return catnip_render(g_rt, &BE);
}

/* One press: post the event the input layer would post, let the drain run the
 * handler, then reconcile. Exactly the main loop's order. */
static int press(const char *object, const char *event)
{
    catnip_handle h = obj_handle(object);
    if (h == CATNIP_HANDLE_NONE) {
        printf("  FAIL - no live object named '%s'\n", object);
        failures++;
        return -1;
    }
    catnip_render_post(g_rt, h, event);
    catnip_render_drain(g_rt);
    return pass();
}

static int count_ch(const char *s, char ch)
{
    int n = 0;
    for (; *s; s++)
        if (*s == ch) n++;
    return n;
}

/* What a row on the screen says, read the way a user reads it. */
static const char *row_text(int i)
{
    static char buf[128];
    lua_State *L = catnip_rt_lua(g_rt);
    char q[96];
    snprintf(q, sizeof(q), "local n = ui.get('row%d') ROW = n and n.text or ''", i);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "ROW");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

static const char *status_text(void)
{
    static char buf[128];
    lua_State *L = catnip_rt_lua(g_rt);
    catnip_rt_dostring(g_rt, "local n = ui.get('status') ST = n and n.text or ''", "=q");
    lua_getglobal(L, "ST");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

/* Walk the selection to the row whose text contains `want`, one press at a
 * time, which is all the input layer can do. Returns how many presses it took,
 * or -1 if no row says that. */
static int select_row(const char *want)
{
    for (int i = 1; i <= 16; i++) {
        const char *t = row_text(i);
        if (!t[0]) break;
        if (strstr(t, want)) {
            int presses = 0;
            while (g_rows_sel != i - 1 && presses < 16) {
                press("rows", g_rows_sel < i - 1 ? "next" : "prev");
                presses++;
            }
            return presses;
        }
    }
    return -1;
}

/* ---- the fixture -------------------------------------------------------- */

static int g_reset_calls;
static int m_sd_reset(void *ud)
{
    (void)ud;
    g_reset_calls++;
    return 0;
}

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
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
    char base[] = "/tmp/catnip_fb_XXXXXX";
    if (!mkdtemp(base)) {
        printf("FAIL - mkdtemp\n");
        return 1;
    }
    char p[512];
    snprintf(p, sizeof(p), "%s/a.txt", base);
    write_file(p, "hello");
    snprintf(p, sizeof(p), "%s/b.txt", base);
    write_file(p, "bye");
    snprintf(p, sizeof(p), "%s/docs", base);
    mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/docs/note.txt", base);
    write_file(p, "note");

    char *app = read_file(APP_MAIN);
    if (!app) {
        printf("FAIL - cannot read %s\n", APP_MAIN);
        return 1;
    }

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.fs_base = base;
    /* Left NULL to start with, because that is the device's state: #46 has not
     * wired sd_reset up, so fs.reset() is a call that fails. */
    hal.sd_reset = NULL;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);

    CHECK(catnip_rt_dostring(g_rt, app, "=filebrowser") == 0, "file browser app loads");

    printf("the screen is built once\n");
    CHECK(pass() == 10, "loading the app builds the whole screen and nothing more");
    CHECK_OPS("[+screen@root[0]+title@screen[0]+rows@screen[1]+row1@rows[0]"
              "+row2@rows[1]+row3@rows[2]+status@screen[2]+up@screen[3]"
              "+delete@screen[4]+reset@screen[5]!screen]",
              "a title, a list of three rows, and the buttons that act on them");
    CHECK(pass() == 0, "and a pass that follows costs nothing");

    printf("moving the selection\n");
    catnip_handle row1 = obj_handle("row1");
    catnip_handle rows = obj_handle("rows");
    /* The assertion this test exists for. The app this replaced called
     * ui.screen() from its render function, so one press destroyed and rebuilt
     * every widget on the screen - ten calls for three rows, and worse the
     * longer the directory. Selection is a property of the list, so it is one
     * update, whatever the directory holds. */
    CHECK(press("rows", "next") == 1, "one press of next is one call");
    CHECK_OPS("[~rows='']", "and that call is a single update on the list");
    CHECK(g_rows_sel == 1, "which carries the new selection, zero-based");
    CHECK(obj_handle("row1") == row1, "no row was rebuilt");

    CHECK(press("rows", "prev") == 1, "and prev is one call as well");
    CHECK(g_rows_sel == 0, "back to the first row");

    press("rows", "prev");
    CHECK(g_rows_sel == 0, "prev at the top of the list stays put");

    printf("changing directory reuses the rows\n");
    CHECK(select_row("docs") >= 0, "the listing shows the docs folder");
    CHECK(press("rows", "click") == 4, "entering it costs the difference, not the list");
    CHECK_OPS("[~title='SD:/docs'~row1='note.txt'-row2-row3]",
              "the title and the surviving row update, the surplus rows go");
    CHECK(obj_handle("row1") == row1, "the row that stayed kept its very object");
    CHECK(obj_handle("rows") == rows, "and so did the list around it");

    /* The other direction, which is the case the app's comment is about: the
     * new directory has more rows than there are row nodes, so the surplus are
     * new nodes and have to be created. Everything that was already there is
     * still reused. */
    CHECK(press("up", "click") == 4, "going back up creates only the rows it gained");
    CHECK(count_ch(g_ops, '+') == 2, "two rows created, for the two it did not have");
    CHECK(count_ch(g_ops, '-') == 0, "and nothing destroyed");
    CHECK(obj_handle("row1") == row1, "the first row is still the same object");

    printf("reading a file\n");
    CHECK(select_row("a.txt") >= 0, "a.txt is on the screen");
    (void)press("rows", "click");
    CHECK_OPS("[+viewer@root[1]+file_name@viewer[0]+file_text@viewer[1]"
              "+file_back@viewer[2]!viewer]",
              "opening a file pushes a screen over the browser");
    CHECK(obj_handle("row1") == row1, "the browser underneath keeps its widgets");
    lua_State *L = catnip_rt_lua(g_rt);
    catnip_rt_dostring(g_rt, "VIEW = ui.get('file_text').text", "=q");
    lua_getglobal(L, "VIEW");
    CHECK(lua_tostring(L, -1) && strcmp(lua_tostring(L, -1), "hello") == 0,
          "and it shows what the file holds");
    lua_pop(L, 1);
    (void)press("file_back", "click");
    CHECK_OPS("[-file_name-file_text-file_back-viewer!screen]",
              "back pops it and the browser is shown again, never rebuilt");

    printf("deleting a file asks first\n");
    CHECK(select_row("b.txt") >= 0, "b.txt is on the screen");
    (void)press("delete", "click");
    CHECK_OPS("[+confirm@root[1]+confirm_text@confirm[0]+confirm_yes@confirm[1]"
              "+confirm_no@confirm[2]!confirm]",
              "the question is a pushed screen, not a mode the browser renders");
    CHECK(obj_handle("row1") == row1, "so the list keeps its widgets while it is up");
    (void)press("confirm_no", "click");
    catnip_rt_dostring(g_rt, "GONE = not fs.exists('b.txt')", "=q");
    lua_getglobal(L, "GONE");
    CHECK(!lua_toboolean(L, -1), "cancel leaves the file alone");
    lua_pop(L, 1);

    CHECK(select_row("b.txt") >= 0, "b.txt is still on the screen");
    (void)press("delete", "click");
    (void)press("confirm_yes", "click");
    catnip_rt_dostring(g_rt, "GONE = not fs.exists('b.txt')", "=q");
    lua_getglobal(L, "GONE");
    CHECK(lua_toboolean(L, -1), "confirm deletes it");
    lua_pop(L, 1);
    CHECK(count_ch(g_ops, '-') == 5,
          "and the row it held is destroyed with the question");

    printf("resetting the card\n");
    (void)press("reset", "click");
    (void)press("confirm_yes", "click");
    /* #46: sd_reset is not wired up, so this is the only outcome the app can
     * reach today, and it says so rather than refreshing as though it had
     * worked. */
    CHECK(strcmp(status_text(), "sd reset not available") == 0,
          "a reset that cannot run says why");

    /* With a card that can be reset, the call still reaches the HAL. There is
     * nothing to assert after it: #46 reboots the device on success. */
    hal.sd_reset = m_sd_reset;
    (void)press("reset", "click");
    (void)press("confirm_yes", "click");
    CHECK(g_reset_calls == 1, "reset SD reached the HAL");

    CHECK(g_log[0] == '\0', "nothing was logged along the way");

    free(app);
    catnip_render_reset(g_rt, &BE);
    catnip_rt_free(g_rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
