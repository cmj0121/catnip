/*
 * Native test for issue #30: the ui tree renderer core.
 *
 * The recording backend below is what the LVGL backend will be a sibling of, so
 * most of what can be wrong about the diff is provable here with no device: the
 * traversal, the identity matching, what an unchanged tree costs, the order and
 * the count of destroys, a stale handle, a create that fails, the event queue,
 * and whether an app's screen is actually released when the app ends.
 *
 * The transcript uses one character per verb so an expected string reads as the
 * sequence it is: `[` and `]` bracket a pass, `+` creates, `~` updates, `>`
 * moves, `-` destroys, `x` is a create that failed, and `!` shows a screen.
 * Nodes are named by their `id`, or by their kind when the app named nothing -
 * the backend has to keep its own map from handle to object anyway, which is
 * exactly what the map below stands in for.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_icon_map.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

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
static void objs_clear(void)
{
    memset(g_objs, 0, sizeof(g_objs));
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

/* What the last descriptor for one watched node said, for the fields the
 * transcript does not carry. */
static char g_watch[32];
static catnip_style_role g_seen_style;
static int g_seen_sel;
static unsigned g_seen_flags;

static void watch(const catnip_node_desc *d)
{
    if (!g_watch[0] || strcmp(desc_name(d), g_watch) != 0) return;
    g_seen_style = d->style;
    g_seen_sel = d->selected;
    g_seen_flags = d->flags;
}

static char g_fail_name[32];    /* the one node whose create refuses, once */
static catnip_rt *g_post_rt;    /* set to post an event from inside a create */
static const char *g_post_when; /* the node whose create posts */

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
    if (g_fail_name[0] && strcmp(desc_name(d), g_fail_name) == 0) {
        rec("x%s", desc_name(d));
        return -1;
    }
    obj_put(h, desc_name(d));
    watch(d);
    rec("+%s@%s[%d]", desc_name(d), obj_name(parent), index);
    if (g_post_rt && g_post_when && strcmp(desc_name(d), g_post_when) == 0)
        catnip_render_post(g_post_rt, h, "click", CATNIP_INDEX_NONE);
    return 0;
}
static void b_update(void *ud, catnip_handle h, const catnip_node_desc *d)
{
    (void)ud;
    watch(d);
    /* The icon is appended only when there is one, so every transcript written
     * before icons existed still reads exactly as it did. By name and not by
     * id: an expected transcript holding a bare 2 would keep passing while
     * meaning something else the day the enum grows a member in the middle. */
    if (d->icon != CATNIP_ICON_NONE)
        rec("~%s='%s':%s", obj_name(h), d->text, catnip_icon_name(d->icon));
    else rec("~%s='%s'", obj_name(h), d->text);
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

/* ---- the log sink ------------------------------------------------------- */

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

/* ---- fixtures ----------------------------------------------------------- */

static catnip_rt *fresh(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_rt_set_log(rt, log_sink, NULL);
    g_log[0] = '\0';
    g_ops[0] = '\0';
    objs_clear();
    g_watch[0] = '\0';
    g_fail_name[0] = '\0';
    g_post_rt = NULL;
    g_post_when = NULL;
    return rt;
}

static int count_ch(const char *s, char ch)
{
    int n = 0;
    for (; *s; s++)
        if (*s == ch) n++;
    return n;
}

static int pass(catnip_rt *rt)
{
    g_ops[0] = '\0';
    return catnip_render(rt, &BE);
}

static void run(catnip_rt *rt, const char *code)
{
    catnip_rt_dostring(rt, code, "=t");
}

/* ---- the tests ---------------------------------------------------------- */

static void test_traversal(void)
{
    printf("traversal and the diff\n");
    catnip_rt *rt = fresh();

    CHECK(pass(rt) == CATNIP_RENDER_NO_SCREEN, "no screen renders nothing");
    CHECK_OPS("[]", "a pass with nothing to draw is still bracketed");

    run(rt, "ui.screen{\n"
            "  ui.label{ id = 'title', text = 'Hello', style = 'title' },\n"
            "  ui.list{ id = 'menu',\n"
            "    ui.button{ id = 'go', text = 'Go' },\n"
            "    ui.label{ id = 'note', text = 'n' },\n"
            "  },\n"
            "}\n");

    snprintf(g_watch, sizeof(g_watch), "title");
    int n = pass(rt);
    CHECK_OPS("[+screen@root[0]+title@screen[0]+menu@screen[1]+go@menu[0]"
              "+note@menu[1]!screen]",
              "creates arrive in tree order, recursing through the list");
    CHECK(n == 5, "the pass reports one call per node created");
    CHECK(g_seen_style == CATNIP_STYLE_TITLE, "a style name becomes a role");

    /* The property the whole cache exists for, and the reason it lives in the
     * renderer rather than in each backend: it can be asserted here. */
    CHECK(pass(rt) == 0, "an unchanged tree makes no calls at all");
    CHECK_OPS("[]", "and the second pass is an empty bracket");

    run(rt, "ui.get('title').text = 'Bye'");
    CHECK(pass(rt) == 1, "one changed property is one call");
    CHECK_OPS("[~title='Bye']", "and that call is a single update");

    /* `status.text = "passing"` on an already-passing label is most of what a
     * polling app writes. It sets `dirty`; it must still emit nothing. */
    run(rt, "ui.get('title').text = 'Bye'");
    CHECK(pass(rt) == 0, "a write of the same value emits nothing");
    CHECK_OPS("[]", "even though the node was marked dirty");

    /* The sixth role, and the one that is three times the size of the rest: it
     * has to survive the trip from a Lua string to the descriptor, or an app
     * that marked its one big number would be told it said nothing. */
    run(rt, "ui.get('title').style = 'display'");
    (void)pass(rt);
    CHECK(g_seen_style == CATNIP_STYLE_DISPLAY, "display is a role of its own");

    run(rt, "ui.get('title').style = 'nonesuch'");
    (void)pass(rt);
    CHECK(g_seen_style == CATNIP_STYLE_BODY, "an unknown style name falls back to body");

    /* And the fallback is still a fallback rather than the newest role, which is
     * the promise the enum's comment makes: an app written against a later
     * firmware degrades on an older one instead of failing to open. */
    run(rt, "ui.get('title').style = 'displa'");
    (void)pass(rt);
    CHECK(g_seen_style == CATNIP_STYLE_BODY, "and a near-miss is not display");

    catnip_rt_free(rt);
}

static void test_identity(void)
{
    printf("children are matched by node identity\n");
    catnip_rt *rt = fresh();

    run(rt, "MENU = ui.list{ id = 'menu', selected = 2,\n"
            "  ui.label{ id = 'r1', text = '1' },\n"
            "  ui.label{ id = 'r2', text = '2' } }\n"
            "ui.screen{ MENU }\n");
    snprintf(g_watch, sizeof(g_watch), "menu");
    (void)pass(rt);
    CHECK_OPS("[+screen@root[0]+menu@screen[0]+r1@menu[0]+r2@menu[1]!screen]",
              "a list and its rows are created in order");
    CHECK(g_seen_sel == 1, "Lua's one-based `selected` reaches the backend zero-based");

    catnip_handle r2_before = obj_handle("r2");
    run(rt, "table.insert(MENU.children, 1, ui.label{ id = 'r0', text = '0' })\n"
            "MENU.selected = 3\n");
    (void)pass(rt);
    CHECK_OPS("[~menu=''+r0@menu[0]>r1[1]>r2[2]]",
              "a row inserted at the head is one create and two moves");
    CHECK(obj_handle("r2") == r2_before,
          "and the row the user had selected kept the very same object");
    CHECK(g_seen_sel == 2, "the selection still names that row");

    run(rt, "local c = MENU.children; c[1], c[2] = c[2], c[1]");
    (void)pass(rt);
    CHECK_OPS("[>r1[0]>r0[1]]", "a reorder emits nothing but moves");

    /* The sharp edge, pinned as a known behaviour rather than left to be
     * discovered at fifty rows: rebuilding the rows into fresh tables is a full
     * rebuild, because identity is what the diff is keyed on. */
    run(rt, "local c = MENU.children\n"
            "for i = #c, 1, -1 do c[i] = nil end\n"
            "c[1] = ui.label{ id = 'n1', text = '1' }\n"
            "c[2] = ui.label{ id = 'n2', text = '2' }\n");
    (void)pass(rt);
    CHECK(count_ch(g_ops, '+') == 2 && count_ch(g_ops, '-') == 3 &&
              count_ch(g_ops, '>') == 0,
          "rows rebuilt into fresh tables are destroyed and created again");

    catnip_rt_free(rt);
}

static void test_destroy_order(void)
{
    printf("destroy walks leaf to root\n");
    catnip_rt *rt = fresh();

    run(rt, "ui.screen{ id = 's1',\n"
            "  ui.list{ id = 'outer',\n"
            "    ui.label{ id = 'a', text = 'a' },\n"
            "    ui.list{ id = 'inner',\n"
            "      ui.label{ id = 'b', text = 'b' },\n"
            "      ui.label{ id = 'c', text = 'c' } },\n"
            "  },\n"
            "}\n");
    CHECK(pass(rt) == 6, "six nodes, six creates");

    /* One call per node, children first, with nothing left to a toolkit's
     * cascade. A recording backend is happy either way, which is exactly why
     * the order and the count have to be asserted rather than assumed. */
    run(rt, "ui.screen{ id = 's2', ui.label{ id = 'fresh', text = 'f' } }");
    (void)pass(rt);
    CHECK_OPS("[-a-b-c-inner-outer-s1+s2@root[0]+fresh@s2[0]!s2]",
              "the old screen is destroyed leaf to root, once per node");

    catnip_rt_free(rt);
}

static void test_handles(void)
{
    printf("handles, slot reuse and staleness\n");
    catnip_rt *rt = fresh();

    run(rt, "S = ui.screen{ ui.label{ id = 'a', text = 'a' },\n"
            "               ui.label{ id = 'b', text = 'b' } }\n");
    (void)pass(rt);
    catnip_handle hb = obj_handle("b");
    CHECK(catnip_render_post(rt, hb, "click", CATNIP_INDEX_NONE) == 0,
          "a live handle takes an event");
    CHECK(catnip_render_drain(rt) == 0, "with no dispatcher installed nothing runs");

    run(rt, "local c = S.children; c[2] = nil");
    (void)pass(rt);
    CHECK_OPS("[-b]", "dropping a child destroys exactly that one");
    CHECK(catnip_render_post(rt, hb, "click", CATNIP_INDEX_NONE) == -1,
          "the handle of a destroyed node resolves to nothing");

    run(rt, "local c = S.children; c[2] = ui.label{ id = 'c', text = 'c' }");
    (void)pass(rt);
    catnip_handle hc = obj_handle("c");
    CHECK((hc & 0xFFFF) == (hb & 0xFFFF), "the new node did reuse the freed slot");
    CHECK(hc != hb, "but its handle is not the old one");
    CHECK(catnip_render_post(rt, hb, "click", CATNIP_INDEX_NONE) == -1,
          "so the stale handle still resolves to nothing, not to its successor");
    CHECK(catnip_render_post(rt, hc, "click", CATNIP_INDEX_NONE) == 0,
          "while the new one works");

    catnip_rt_free(rt);
}

static void test_create_failure(void)
{
    printf("a create that fails\n");
    catnip_rt *rt = fresh();

    run(rt, "ui.screen{ ui.label{ id = 'ok1', text = '1' },\n"
            "           ui.label{ id = 'bad', text = '2' },\n"
            "           ui.label{ id = 'ok2', text = '3' } }\n");
    snprintf(g_fail_name, sizeof(g_fail_name), "bad");
    CHECK(pass(rt) == CATNIP_RENDER_FAILED, "the pass reports that it failed");
    CHECK_OPS("[+screen@root[0]+ok1@screen[0]xbad+ok2@screen[2]!screen]",
              "the rest of the screen is still drawn");
    CHECK(strstr(g_log, "could not create 'bad'") != NULL, "and it is logged");

    /* Retrying every pass would hammer a failing allocator at frame rate and
     * write one log line per frame, which is how a memory problem turns into a
     * hang. The failure sticks to the node until the screen is replaced. */
    g_fail_name[0] = '\0';
    size_t logged = strlen(g_log);
    CHECK(pass(rt) == 0, "the failed node is not retried on the next pass");
    CHECK_OPS("[]", "and nothing else is disturbed");
    CHECK(strlen(g_log) == logged, "the log is written once, not once a frame");

    run(rt, "ui.screen{ ui.label{ id = 'again', text = 'a' } }");
    CHECK(pass(rt) == 5, "a new screen clears the record and draws normally");

    catnip_rt_free(rt);
}

static void test_screen_stack(void)
{
    printf("the screen stack\n");
    catnip_rt *rt = fresh();

    run(rt, "ui.screen{ id = 's1', ui.label{ id = 'a1', text = 'a' } }");
    (void)pass(rt);
    CHECK_OPS("[+s1@root[0]+a1@s1[0]!s1]", "the first screen is built and shown");
    catnip_handle a1 = obj_handle("a1");

    run(rt, "ui.push{ id = 's2', ui.label{ id = 'b1', text = 'b' } }");
    (void)pass(rt);
    CHECK_OPS("[+s2@root[1]+b1@s2[0]!s2]", "a push builds the new screen and shows it");
    CHECK(obj_handle("a1") == a1, "the screen underneath keeps its widgets");

    run(rt, "ui.get('a1').text = 'changed'");
    CHECK(pass(rt) == 0, "and is not reconciled while it is invisible");

    run(rt, "ui.pop()");
    (void)pass(rt);
    CHECK_OPS("[-b1-s2~a1='changed'!s1]",
              "a pop destroys only the top and the screen below catches up at once");

    run(rt, "ui.push{ id = 'p2' } ui.push{ id = 'p3' } ui.push{ id = 'p4' }\n"
            "DEEP = ui.push{ id = 'p5' }\n");
    CHECK(strstr(g_log, "already 4 deep") != NULL, "a fifth screen is refused");
    run(rt, "REFUSED = (DEEP == nil)");
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, "REFUSED");
    CHECK(lua_toboolean(L, -1), "and ui.push says so to the app");
    lua_pop(L, 1);

    catnip_rt_free(rt);
}

/* ---- the event queue ---------------------------------------------------- */

static char g_fired[512];
static int t_dispatch(void *ud, catnip_rt *rt, int node_ref, const char *event, int index,
                      const char *arg)
{
    (void)arg;
    (void)ud;
    (void)index;
    lua_State *L = catnip_rt_lua(rt);
    lua_rawgeti(L, LUA_REGISTRYINDEX, node_ref);
    lua_pushstring(L, "id");
    lua_rawget(L, -2);
    char line[64];
    snprintf(line, sizeof(line), "%s:%s ", lua_tostring(L, -1), event);
    strncat(g_fired, line, sizeof(g_fired) - strlen(g_fired) - 1);
    lua_pop(L, 2);
    luaL_unref(L, LUA_REGISTRYINDEX, node_ref); /* the dispatcher owns the ref */
    return 0;
}

static void test_events(void)
{
    printf("the event queue\n");
    catnip_rt *rt = fresh();
    catnip_render_set_dispatch(rt, t_dispatch, NULL);
    g_fired[0] = '\0';

    /* Posting from inside a backend callback is the shape the real thing has:
     * LVGL calls the backend, the backend posts. Nothing may run during it. */
    g_post_rt = rt;
    g_post_when = "go";
    run(rt, "ui.screen{ ui.button{ id = 'go', text = 'Go' },\n"
            "           ui.button{ id = 'no', text = 'No' } }\n");
    (void)pass(rt);
    g_post_rt = NULL;
    CHECK(g_fired[0] == '\0', "a post during a pass is not delivered during it");
    CHECK(catnip_render_drain(rt) == 1, "the drain delivers it afterwards");
    CHECK(strcmp(g_fired, "go:click ") == 0, "to the node the handle names");

    g_fired[0] = '\0';
    catnip_render_post(rt, obj_handle("go"), "a", CATNIP_INDEX_NONE);
    catnip_render_post(rt, obj_handle("no"), "b", CATNIP_INDEX_NONE);
    catnip_render_post(rt, obj_handle("go"), "c", CATNIP_INDEX_NONE);
    CHECK(catnip_render_drain(rt) == 3, "everything queued is delivered");
    CHECK(strcmp(g_fired, "go:a no:b go:c ") == 0, "in the order it was posted");

    /* Overflow drops the newest: a release delivered without its press is worse
     * than a press that did not register. */
    g_fired[0] = '\0';
    g_log[0] = '\0';
    char want[512] = "";
    int taken = 0, refused = 0;
    for (int i = 0; i < 24; i++) {
        char ev[8];
        snprintf(ev, sizeof(ev), "e%d", i);
        if (catnip_render_post(rt, obj_handle("go"), ev, CATNIP_INDEX_NONE) == 0) {
            char line[32];
            snprintf(line, sizeof(line), "go:%s ", ev);
            strncat(want, line, sizeof(want) - strlen(want) - 1);
            taken++;
        } else {
            refused++;
        }
    }
    CHECK(refused > 0, "a full queue refuses further posts");
    CHECK(catnip_render_drain(rt) == taken, "and delivers exactly what it accepted");
    CHECK(strcmp(g_fired, want) == 0, "which is the oldest ones, in order");
    CHECK(strstr(g_log, "dropped") != NULL, "the drops are logged once per drain");

    /* Queued, then the node goes away before the drain. This is the generation
     * doing its second job. */
    g_fired[0] = '\0';
    catnip_render_post(rt, obj_handle("go"), "click", CATNIP_INDEX_NONE);
    run(rt, "ui.screen{ ui.label{ id = 'other', text = 'o' } }");
    (void)pass(rt);
    CHECK(catnip_render_drain(rt) == 0, "an event for a destroyed node is dropped");
    CHECK(g_fired[0] == '\0', "and no handler runs");

    catnip_rt_free(rt);
}

static void test_dispatch_on_coroutine(void)
{
    printf("handlers run on a coroutine with the watchdog armed\n");
    catnip_rt *rt = fresh();
    catnip_sched *sched = catnip_sched_new(rt, NULL, NULL, NULL);
    catnip_render_set_dispatch(rt, catnip_sched_dispatch, sched);
    lua_State *L = catnip_rt_lua(rt);

    run(rt, "FIRED = 0\n"
            "ui.screen{ ui.button{ id = 'go', text = 'Go',\n"
            "  on_click = function() FIRED = FIRED + 1 end } }\n");
    (void)pass(rt);
    catnip_render_post(rt, obj_handle("go"), "click", CATNIP_INDEX_NONE);
    CHECK(catnip_render_drain(rt) == 1, "the handler is delivered");
    lua_getglobal(L, "FIRED");
    CHECK(lua_tointeger(L, -1) == 1, "and the app's on_click ran");
    lua_pop(L, 1);

    /* The reason handlers do not run inside the input event: on the main state
     * there is no count hook, so this would freeze the device for good. */
    g_log[0] = '\0';
    run(rt, "ui.screen{ ui.button{ id = 'spin', text = 'S',\n"
            "  on_click = function() while true do end end } }\n");
    (void)pass(rt);
    catnip_render_post(rt, obj_handle("spin"), "click", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(strstr(g_log, "ran too long") != NULL, "a runaway handler is stopped");

    /* Yielding is the documented limitation, and the message has to name its
     * own cause rather than Lua's ("attempt to yield from outside a coroutine",
     * which names nothing the author wrote). */
    g_log[0] = '\0';
    run(rt, "ui.screen{ ui.button{ id = 'nap', text = 'N',\n"
            "  on_click = function() sys.sleep(10) end } }\n");
    (void)pass(rt);
    catnip_render_post(rt, obj_handle("nap"), "click", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(strstr(g_log, "cannot yield yet") != NULL, "a handler that yields says why");

    /* The shape #46 takes, and the re-entrancy the queue exists to dissolve: a
     * handler asks for another screen, and the objects that answers are created
     * and destroyed by the next pass, not underneath the input dispatch. */
    run(rt, "ui.screen{ id = 'main', ui.button{ id = 'open', text = 'Erase',\n"
            "  on_click = function()\n"
            "    ui.push{ id = 'warn', ui.button{ id = 'yes', text = 'Sure?' } }\n"
            "  end } }\n");
    (void)pass(rt);
    catnip_render_post(rt, obj_handle("open"), "click", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    (void)pass(rt);
    CHECK_OPS("[+warn@root[1]+yes@warn[0]!warn]",
              "a handler may push a screen, and the next pass builds it");

    run(rt, "ui.pop()");
    (void)pass(rt);
    CHECK_OPS("[-yes-warn!main]", "and popping it destroys only what it built");

    catnip_sched_free(sched);
    catnip_rt_free(rt);
}

/* ---- what a handler is handed, and what it may answer -------------------- */

static void test_handler_arguments(void)
{
    printf("a handler is handed its node and the row an event names\n");
    catnip_rt *rt = fresh();
    catnip_sched *sched = catnip_sched_new(rt, NULL, NULL, NULL);
    catnip_render_set_dispatch(rt, catnip_sched_dispatch, sched);
    lua_State *L = catnip_rt_lua(rt);

    run(rt,
        "SELF, IDX = nil, 'unset'\n"
        "ui.screen{ ui.list{ id = 'rows',\n"
        "  on_click = function(self, index) SELF, IDX = self, index end,\n"
        "  ui.label{ id = 'r1', text = 'a' }, ui.label{ id = 'r2', text = 'b' } } }\n");
    (void)pass(rt);

    /* The joystick names no row: the sentinel has to reach Lua as nil, not as
     * -1, or every handler would have to know what C spells "no row" as. */
    catnip_render_post(rt, obj_handle("rows"), "click", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    run(rt, "SAME = (SELF == ui.get('rows'))");
    lua_getglobal(L, "SAME");
    CHECK(lua_toboolean(L, -1), "the handler's first argument is the node it fired on");
    lua_pop(L, 1);
    lua_getglobal(L, "IDX");
    CHECK(lua_isnil(L, -1), "and an event that names no row passes nil");
    lua_pop(L, 1);

    /* A tap names one, and it arrives one-based, because every other index an
     * app touches in Lua is. */
    catnip_render_post(rt, obj_handle("rows"), "click", 1);
    (void)catnip_render_drain(rt);
    lua_getglobal(L, "IDX");
    CHECK(lua_tointeger(L, -1) == 2, "a tap on the second row arrives as 2, not 1");
    lua_pop(L, 1);

    catnip_sched_free(sched);
    catnip_rt_free(rt);
}

static void test_back_claim(void)
{
    printf("a handler's return value is the claim the platform reads\n");
    catnip_rt *rt = fresh();
    catnip_sched *sched = catnip_sched_new(rt, NULL, NULL, NULL);
    catnip_render_set_dispatch(rt, catnip_sched_dispatch, sched);

    /* No on_back at all: the ordinary single-screen app. Nothing is claimed, so
     * the platform's default - leave the app - is what happens, and the app did
     * not have to write a line for that to be true. */
    run(rt, "ui.screen{ id = 'plain', ui.label{ text = 'hi' } }");
    (void)pass(rt);
    catnip_handle screen = catnip_render_visible_screen(rt);
    CHECK(screen != CATNIP_HANDLE_NONE, "the visible screen is addressable");
    catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(catnip_render_take_claim(rt) == 0, "an app with no on_back claims nothing");

    /* At the root the File Browser declines, and the platform is meant to act. */
    run(rt, "AT_ROOT = true\n"
            "ui.screen{ id = 'browse',\n"
            "  on_back = function() if AT_ROOT then return false end return true end,\n"
            "  ui.label{ text = 'SD:/' } }\n");
    (void)pass(rt);
    screen = catnip_render_visible_screen(rt);
    catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(catnip_render_take_claim(rt) == 0, "a falsy on_back declines it too");

    /* Above the root it climbs, and says so. */
    run(rt, "AT_ROOT = false");
    catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(catnip_render_take_claim(rt) == 1, "a truthy on_back claims it");
    CHECK(catnip_render_take_claim(rt) == 0, "and the claim is cleared as it is read");

    /* A handler that faults claims nothing. The alternative - treating an error
     * as "handled" - would strand the user in an app whose on_back is broken,
     * which is exactly when they most want out. */
    g_log[0] = '\0';
    run(rt, "ui.screen{ id = 'bad', on_back = function() error('nope') end }");
    (void)pass(rt);
    catnip_render_post_claimable(rt, catnip_render_visible_screen(rt), "back",
                                 CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(catnip_render_take_claim(rt) == 0, "a faulting on_back claims nothing");

    /* An ordinary handler that happens to return true is not answering the
     * platform's question. Left unguarded it would leave a claim standing for
     * the next B to find, and that B would do nothing at all. */
    run(rt, "ui.screen{ id = 'chatty', ui.button{ id = 'go', text = 'Go',\n"
            "  on_click = function() return true end } }\n");
    (void)pass(rt);
    catnip_render_post(rt, obj_handle("go"), "click", CATNIP_INDEX_NONE);
    (void)catnip_render_drain(rt);
    CHECK(catnip_render_take_claim(rt) == 0, "a handler nobody asked answers nothing");

    catnip_sched_free(sched);
    catnip_rt_free(rt);
}

static void test_icons(void)
{
    printf("an icon is a named category, and an unknown name costs only the icon\n");
    catnip_rt *rt = fresh();

    run(rt, "ui.screen{ ui.label{ id = 'a', text = 'docs', icon = 'folder' },\n"
            "           ui.label{ id = 'b', text = 'note.txt', icon = 'file' },\n"
            "           ui.label{ id = 'c', text = 'plain' },\n"
            "           ui.label{ id = 'd', text = 'later', icon = 'hologram' } }\n");
    (void)pass(rt);
    CHECK(strstr(g_ops, "+a") != NULL, "a row with an icon renders");

    /* Changing only the icon is a change: without it in the comparison the row
     * would keep the glyph of whatever it used to be, which is exactly what a
     * file browser does to every row when it changes directory. */
    run(rt, "ui.get('a').icon = 'file'");
    CHECK(pass(rt) == 1, "changing an icon alone is one update");
    CHECK_OPS("[~a='docs':file]", "and it carries the new glyph");

    run(rt, "ui.get('d').icon = 'unicorn'");
    CHECK(pass(rt) == 0, "one unknown name for another changes nothing");

    run(rt, "ui.get('c').icon = 'trash'");
    CHECK(pass(rt) == 1, "adding an icon to a plain row is an update");

    catnip_rt_free(rt);
}

static void test_counter(void)
{
    printf("the frame's counter is derived, and blank when there is nothing to count\n");
    catnip_rt *rt = fresh();
    int n = 0, total = 0;

    /* No list on the screen: blank, and emphatically not 0/0. A zero would read
     * as a list that is empty rather than as a screen that has no list. */
    run(rt, "ui.screen{ ui.label{ id = 'just_text', text = 'hello' } }");
    (void)pass(rt);
    CHECK(catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total) == 0,
          "a screen with no list counts nothing");

    run(rt, "ui.screen{ ui.list{ id = 'rows', selected = 2,\n"
            "  ui.label{ id = 'r1' }, ui.label{ id = 'r2' }, ui.label{ id = 'r3' },\n"
            "  ui.label{ id = 'r4' } } }\n");
    (void)pass(rt);
    CHECK(catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total) == 1,
          "the sole list is the one counted, with nothing focused");
    CHECK(n == 2 && total == 4, "and it reads 2/4, one-based for display");

    /* Focus on something that is not a list keeps the count on the list: the
     * user has not left it, they are still looking at it. */
    CHECK(catnip_render_counter(rt, obj_handle("r1"), &n, &total) == 1 && n == 2,
          "a focus that is not a list does not blank the counter");

    run(rt, "ui.get('rows').selected = 4");
    (void)pass(rt);
    (void)catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total);
    CHECK(n == 4 && total == 4, "moving the selection moves the counter");

    /* An empty directory: the list is there and has nothing selected. */
    run(rt, "local l = ui.get('rows') l:set_children({}) l.selected = 0");
    (void)pass(rt);
    CHECK(catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total) == 0,
          "an empty list counts nothing rather than 0/0");

    /* A grid counts pages. Eight cells is two pages, and the counter says which
     * of the two you are on - `8/14` over a wall of pictures answers a question
     * nobody asked of it. */
    run(rt, "local g = ui.list{ id = 'g', layout = 'grid', selected = 1 }\n"
            "local cells = {}\n"
            "for i = 1, 8 do cells[i] = ui.label{ id = 'g' .. i } end\n"
            "ui.screen{ g }\n"
            "g:set_children(cells)\n");
    (void)pass(rt);
    CHECK(catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total) == 1 && n == 1 &&
              total == 2,
          "eight cells in a grid read 1/2 - pages, not cells");
    run(rt, "ui.get('g').selected = 6");
    (void)pass(rt);
    (void)catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total);
    CHECK(n == 1 && total == 2, "the sixth cell is still the first page");
    run(rt, "ui.get('g').selected = 7");
    (void)pass(rt);
    (void)catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total);
    CHECK(n == 2 && total == 2, "and the seventh turns it");

    /* Two lists and no focus is genuinely ambiguous, and a guess would put a
     * number in the bar answering a question nobody asked. */
    run(rt, "ui.screen{ ui.list{ id = 'a', selected = 1, ui.label{ id = 'a1' } },\n"
            "           ui.list{ id = 'b', selected = 1, ui.label{ id = 'b1' } } }\n");
    (void)pass(rt);
    CHECK(catnip_render_counter(rt, CATNIP_HANDLE_NONE, &n, &total) == 0,
          "two lists and nothing focused counts nothing");
    CHECK(catnip_render_counter(rt, obj_handle("b"), &n, &total) == 1 && total == 1,
          "but the focused one settles it");

    catnip_rt_free(rt);
}

static void test_teardown(void)
{
    printf("an app that ends leaves nothing behind\n");
    catnip_rt *rt = fresh();
    lua_State *L = catnip_rt_lua(rt);

    /* The baseline is taken with the renderer's own state already allocated,
     * because that is allocated once and kept; what must come back is the
     * app's tree. */
    (void)pass(rt);
    lua_gc(L, LUA_GCCOLLECT, 0);
    size_t base = 0, built = 0, after = 0;
    catnip_rt_mem(rt, &base, NULL);

    catnip_sched *sched = catnip_sched_new(rt, NULL, NULL, NULL);
    catnip_sched_start(sched,
                       "local rows = {}\n"
                       "for i = 1, 40 do\n"
                       "  rows[i] = ui.label{ id = 'r' .. i, text = 'row ' .. i }\n"
                       "end\n"
                       "ui.screen{ ui.list(rows) }\n"
                       "error('the app faulted')\n",
                       "=app");
    CHECK(catnip_sched_step(sched) == CATNIP_ERROR, "the app faults");
    CHECK(pass(rt) == 42, "its screen was built and drawn all the same");
    lua_gc(L, LUA_GCCOLLECT, 0);
    catnip_rt_mem(rt, &built, NULL);

    g_ops[0] = '\0';
    catnip_render_reset(rt, &BE);
    CHECK(count_ch(g_ops, '-') == 42, "the reset destroys every node exactly once");
    CHECK(catnip_render_focus_order(rt, NULL, 0) == 0, "and keeps no focus order");
    CHECK(pass(rt) == CATNIP_RENDER_NO_SCREEN, "there is no screen left to draw");
    CHECK_OPS("[]", "and nothing left to destroy");

    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_gc(L, LUA_GCCOLLECT, 0);
    catnip_rt_mem(rt, &after, NULL);
    size_t grew = built - base;
    size_t left = after > base ? after - base : 0;
    /* The tree has to be worth measuring, or "it all came back" would be true
     * of a test that never allocated anything. */
    CHECK(grew > 4000, "the app's screen was several kilobytes of accounted heap");
    CHECK(left * 8 < grew, "and the accounted heap came back to its baseline");

    catnip_sched_free(sched);
    catnip_rt_free(rt);
}

static void test_ids_and_focus(void)
{
    printf("ids are names, and the focus order\n");
    catnip_rt *rt = fresh();

    /* The claim identity rests on: an absent id and a duplicated one change
     * nothing, because neither is ever used as a key. */
    run(rt, "ui.screen{ ui.label{ text = 'x' },\n"
            "           ui.label{ id = 'dup', text = '1' },\n"
            "           ui.label{ id = 'dup', text = '2' } }\n");
    CHECK(pass(rt) == 4, "an absent id and a duplicated one still render");
    run(rt, "ui.root().children[2].text = 'z'");
    (void)pass(rt);
    CHECK(count_ch(g_ops, '~') == 1,
          "and changing one of two nodes sharing an id updates only that one");

    run(rt, "ui.screen{ ui.label{ id = 'l', text = 'l' },\n"
            "  ui.button{ id = 'b1', text = '1' },\n"
            "  ui.list{ id = 'm', on_click = function() end,\n"
            "    ui.button{ id = 'b2', text = '2' },\n"
            "    ui.button{ id = 'b3', text = '3', disabled = true } } }\n");
    snprintf(g_watch, sizeof(g_watch), "b3");
    (void)pass(rt);
    CHECK(g_seen_flags == CATNIP_NODE_DISABLED,
          "a disabled button is disabled and not focusable");
    catnip_handle order[8];
    int n = catnip_render_focus_order(rt, order, 8);
    CHECK(n == 3, "labels and disabled buttons are not in the focus order");
    CHECK(n == 3 && order[0] == obj_handle("b1") && order[1] == obj_handle("m") &&
              order[2] == obj_handle("b2"),
          "and what is left is in tree order");

    /* A list nothing is listening to is text in a column, and the ring does not
     * stop on it: the device info page is a page of facts with two buttons
     * under them, and a stop on the facts would offer an interaction that does
     * not exist. The buttons inside it still answer for themselves. */
    run(rt, "ui.screen{ ui.list{ id = 'facts',\n"
            "    ui.label{ text = 'a' }, ui.label{ text = 'b' } },\n"
            "  ui.list{ id = 'keys', layout = 'row',\n"
            "    ui.button{ id = 'k1', text = '1' },\n"
            "    ui.button{ id = 'k2', text = '2' } } }\n");
    (void)pass(rt);
    n = catnip_render_focus_order(rt, order, 8);
    CHECK(n == 2 && order[0] == obj_handle("k1") && order[1] == obj_handle("k2"),
          "a list with no handlers is not a focus stop, and its buttons still are");

    catnip_rt_free(rt);
}

/* ---- string lifetime ---------------------------------------------------- */

/* A Lua allocator that turns "the renderer handed out a string Lua had already
 * collected" from undefined behaviour into something a test can assert on: every
 * block Lua frees is first overwritten with a byte that appears in no expected
 * text. The block is then deliberately not returned to the C allocator, because
 * a later allocation reusing it would overwrite the poison before the assertion
 * could read it. The process is short-lived, so the leak is the cheap half. */
static void *poison_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (nsize == 0) {
        /* Per the Lua contract osize is only a byte count when ptr != NULL. */
        if (ptr) memset(ptr, 0xA5, osize);
        return NULL;
    }
    return realloc(ptr, nsize);
}

/* A backend that collects between the renderer reading a node's strings and the
 * backend using them, which is exactly the window those strings must survive. */
typedef struct {
    lua_State *L;
    int calls;
    int id_ok;
    int text_ok;
} gc_probe;

static int probe_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                        const catnip_node_desc *d)
{
    gc_probe *p = (gc_probe *)ud;
    (void)h;
    (void)parent;
    (void)index;
    if (d->kind != CATNIP_NODE_LABEL) return 0;
    p->calls++;
    lua_gc(p->L, LUA_GCCOLLECT, 0);
    /* Bounded compares: had these strings died they would point into poisoned
     * memory with no terminator, and an unbounded read would run off the block. */
    p->id_ok = strncmp(d->id, "12345", sizeof("12345") - 1) == 0;
    p->text_ok = strncmp(d->text, "987654321", sizeof("987654321") - 1) == 0;
    return 0;
}

static void test_string_lifetime(void)
{
    printf("the strings a backend borrows\n");
    /* Numbers are what makes the lifetime visible: lua_tostring converts the
     * value in place, so the string it produces lives only in the stack slot the
     * renderer is holding - the widget tree keeps the number, not the text. Let
     * go of that slot too early and the backend is reading freed memory. */
    catnip_rt *rt = catnip_rt_new_alloc(poison_alloc, NULL);
    catnip_ui_open(rt);
    gc_probe probe = {catnip_rt_lua(rt), 0, 0, 0};
    catnip_rt_dostring(rt, "ui.screen{ ui.label{ id = 12345, text = 987654321 } }",
                       "=gc");

    catnip_render_backend gbe;
    memset(&gbe, 0, sizeof(gbe));
    gbe.ud = &probe;
    gbe.create = probe_create;
    CHECK(catnip_render(rt, &gbe) == 2, "the gc probe renders the screen and its label");
    CHECK(probe.calls == 1, "the gc probe backend was called");
    CHECK(probe.id_ok, "id survives a collection during the backend call");
    CHECK(probe.text_ok, "text survives a collection during the backend call");
    catnip_rt_free(rt);
}

int main(void)
{
    test_traversal();
    test_identity();
    test_destroy_order();
    test_handles();
    test_create_failure();
    test_screen_stack();
    test_events();
    test_dispatch_on_coroutine();
    test_handler_arguments();
    test_back_claim();
    test_icons();
    test_counter();
    test_teardown();
    test_ids_and_focus();
    test_string_lifetime();
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
