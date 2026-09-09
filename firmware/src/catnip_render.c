/* catnip_render.c - see catnip_render.h.
 *
 * The renderer owns one array of slots. A slot is the association between a Lua
 * node table and whatever the backend drew for it, and the array is the *sole*
 * owner of that association: destruction is driven by the structural walk and
 * never by garbage collection. Two registry tables index the array from Lua -
 * [handle] = node, strong, so a rendered node stays alive even if the app drops
 * it, and [node] = handle, weak-keyed, which is the O(1) index that makes a
 * keyed diff free. If the two ever disagree, the array wins.
 *
 * Both tables (and the state itself) hang off the registry under the address of
 * a static char rather than a string key, so a script cannot name them even by
 * accident, and so that catnip_render_post() - which may run inside an input
 * callback - reaches the state without interning a string.
 */
#include "catnip_icon_map.h"
#include "catnip_render.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

enum {
    /* Slots are one fixed array covering every screen on the stack. 128 of them
     * measured 7616 bytes on the accounted Lua heap, allocated once and kept for
     * the life of the runtime; that is the renderer's whole working set, and it
     * is stated here because everything on that heap is charged to the app. A
     * fifty-row list on a stack of four screens fits. */
    SLOTS_MAX = 128,
    /* Mirrors the cap in ui.push (catnip_ui.c). Two numbers, one rule: a
     * handler in a loop must not be able to grow the stack until the heap gives
     * out. If one moves, move the other. */
    SCREENS_MAX = 4,
    /* A malformed tree - a node that is its own descendant - is something a
     * script can build by accident, and it would otherwise overflow the C
     * stack. Sixteen is deeper than any screen this device can usefully show. */
    DEPTH_MAX = 16,
    QUEUE_MAX = 16,
    EVENT_MAX = 12,
};

typedef struct {
    uint16_t gen;
    uint8_t in_use;
    uint8_t seen; /* visited by the pass currently reconciling this screen */
    catnip_handle parent;
    catnip_handle screen; /* the screen this slot belongs to; a screen's is its own */
    int index;            /* the child position last sent to the backend */

    /* The descriptor last sent for this node, in the fields that can be held
     * without keeping a pointer into Lua alive. This is what decides whether an
     * update is emitted - `dirty` only decides whether it is worth looking.
     * A hash of the text would be a kilobyte cheaper and would make a collision
     * into a label that silently stops updating forever, which is the same
     * class of bug as a stale handle resolving to the wrong node. The kilobyte
     * is the right price.
     *
     * `id` is deliberately not compared. It is a name for logs, it is a raw
     * field on the node table so writing it does not even set `dirty`, and a
     * backend that acts on it is already using it as a key. */
    catnip_node_kind kind;
    catnip_style_role style;
    catnip_icon icon;
    catnip_node_layout layout;
    char *image; /* owned copy of the last image name sent, or NULL */
    size_t image_cap;
    int value;
    int steps;
    char *value_text; /* owned copy, like `text`, and compared the same way */
    size_t value_text_cap;
    int selected;
    unsigned flags;
    unsigned events;
    char *text;      /* owned copy of the last text sent, or NULL */
    size_t text_cap; /* bytes handed to the allocator, so they can be given back */
} slot;

typedef struct {
    catnip_handle h;
    char event[EVENT_MAX];
    int index;     /* the child this is about, or CATNIP_INDEX_NONE */
    int claimable; /* the platform is waiting on this handler's answer */
} qentry;

typedef struct {
    slot slots[SLOTS_MAX];
    catnip_handle screens[SCREENS_MAX]; /* the renderer's mirror of ui's stack */
    int depth;
    catnip_handle shown; /* the screen the backend was last told to show */

    qentry q[QUEUE_MAX];
    int qhead, qcount;
    int qdropped; /* posts dropped since the last drain */

    catnip_render_dispatch_fn dispatch;
    void *dispatch_ud;
    int claim; /* a handler returned truthy; read and cleared by take_claim */

    catnip_handle order[SLOTS_MAX]; /* focusable handles, in tree order */
    int n_order;

    int deep_reported; /* the depth limit has already been logged once */
} render_state;

/* Registry keys: the address is the key, so nothing in Lua can name them. */
static const char RK_STATE = 0;
static const char RK_NODES = 0;
static const char RK_SLOTS = 0;
static const char RK_FAILED = 0;
#define RKEY(x) ((void *)&(x))

/* Everything one pass needs, so the three registry tables are looked up once
 * and then addressed by their absolute stack index. */
typedef struct {
    lua_State *L;
    catnip_rt *rt;
    render_state *st;
    const catnip_render_backend *be;
    int nodes;  /* stack index of [handle] = node */
    int slots;  /* stack index of [node] = handle */
    int failed; /* stack index of the set of nodes whose create failed */
    int calls;  /* create + update + move + destroy so far */
    int failure;
    int record_order; /* only the visible screen contributes to the focus order */
} ctx;

/* ---- logging ------------------------------------------------------------ */

static void render_log(catnip_rt *rt, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    catnip_rt_log(rt, buf);
}

/* ---- handles ------------------------------------------------------------ */

static catnip_handle handle_make(int index, uint16_t gen)
{
    return (catnip_handle)(((uint32_t)(gen & 0x7FFFu) << 16) |
                           (uint32_t)(index & 0xFFFF));
}

static int handle_index(catnip_handle h)
{
    return (int)(h & 0xFFFF);
}

static uint16_t handle_gen(catnip_handle h)
{
    return (uint16_t)(((uint32_t)h >> 16) & 0x7FFFu);
}

/* The one place a handle from outside becomes a slot. Every entry point goes
 * through it, which is what makes a stale handle resolve to nothing. */
static slot *slot_of(render_state *st, catnip_handle h)
{
    if (h < 0) return NULL;
    int i = handle_index(h);
    if (i >= SLOTS_MAX) return NULL;
    slot *s = &st->slots[i];
    if (!s->in_use || s->gen != handle_gen(h)) return NULL;
    return s;
}

static catnip_handle handle_at(render_state *st, int i)
{
    return handle_make(i, st->slots[i].gen);
}

/* ---- the cached text ---------------------------------------------------- */

/* The cache lives on the Lua allocator rather than on malloc, because that is
 * the heap this runtime accounts, and a renderer that spent memory the app's
 * budget could not see would be lying about the budget. */
static void text_free(lua_State *L, slot *s)
{
    void *ud = NULL;
    lua_Alloc f = lua_getallocf(L, &ud);
    if (s->text) {
        f(ud, s->text, s->text_cap, 0);
        s->text = NULL;
        s->text_cap = 0;
    }
    if (s->image) {
        f(ud, s->image, s->image_cap, 0);
        s->image = NULL;
        s->image_cap = 0;
    }
    if (s->value_text) {
        f(ud, s->value_text, s->value_text_cap, 0);
        s->value_text = NULL;
        s->value_text_cap = 0;
    }
}

static void str_store(lua_State *L, char **dst, size_t *cap, const char *t)
{
    size_t n = strlen(t) + 1;
    if (*cap < n) {
        void *ud = NULL;
        lua_Alloc f = lua_getallocf(L, &ud);
        char *p = (char *)f(ud, *dst, *cap, n);
        if (!p) { /* keep the old copy; the next pass will try again */
            return;
        }
        *dst = p;
        *cap = n;
    }
    memcpy(*dst, t, n);
}

static void text_store(lua_State *L, slot *s, const char *t)
{
    str_store(L, &s->text, &s->text_cap, t);
}

static void image_store(lua_State *L, slot *s, const char *t)
{
    str_store(L, &s->image, &s->image_cap, t);
}

static void value_text_store(lua_State *L, slot *s, const char *t)
{
    str_store(L, &s->value_text, &s->value_text_cap, t);
}

/* ---- the node tables ---------------------------------------------------- */

static catnip_handle handle_of(ctx *c, int node)
{
    lua_pushvalue(c->L, node);
    lua_rawget(c->L, c->slots);
    catnip_handle h = lua_isnumber(c->L, -1) ? (catnip_handle)lua_tointeger(c->L, -1)
                                             : CATNIP_HANDLE_NONE;
    lua_pop(c->L, 1);
    return h;
}

static catnip_handle slot_alloc(ctx *c, int node)
{
    for (int i = 0; i < SLOTS_MAX; i++) {
        slot *s = &c->st->slots[i];
        if (s->in_use) continue;
        uint16_t gen = s->gen; /* survives the wipe: it is what a stale handle fails on */
        memset(s, 0, sizeof(*s));
        s->gen = gen;
        s->in_use = 1;
        s->parent = CATNIP_HANDLE_NONE;
        s->screen = CATNIP_HANDLE_NONE;
        s->selected = -1;
        catnip_handle h = handle_at(c->st, i);

        lua_pushvalue(c->L, node);
        lua_rawseti(c->L, c->nodes, (lua_Integer)h);
        lua_pushvalue(c->L, node);
        lua_pushinteger(c->L, (lua_Integer)h);
        lua_rawset(c->L, c->slots);
        return h;
    }
    return CATNIP_HANDLE_NONE;
}

static void slot_free(ctx *c, catnip_handle h)
{
    slot *s = slot_of(c->st, h);
    if (!s) return;
    lua_State *L = c->L;

    /* Drop [node] = handle only if it still names *this* handle. A node that
     * was recreated in the same pass - it moved to a different parent - already
     * points at its new slot, and clearing it here would unindex the live one. */
    lua_rawgeti(L, c->nodes, (lua_Integer)h);
    if (!lua_isnil(L, -1)) {
        if (handle_of(c, lua_gettop(L)) == h) {
            lua_pushvalue(L, -1);
            lua_pushnil(L);
            lua_rawset(L, c->slots);
        }
    }
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_rawseti(L, c->nodes, (lua_Integer)h);

    text_free(L, s);
    s->in_use = 0;
    s->gen = (uint16_t)((s->gen + 1) & 0x7FFF);
}

static int node_failed(ctx *c, int node)
{
    lua_pushvalue(c->L, node);
    lua_rawget(c->L, c->failed);
    int hit = lua_toboolean(c->L, -1);
    lua_pop(c->L, 1);
    return hit;
}

static void node_mark_failed(ctx *c, int node)
{
    lua_pushvalue(c->L, node);
    lua_pushboolean(c->L, 1);
    lua_rawset(c->L, c->failed);
}

static void failed_clear(ctx *c)
{
    lua_newtable(c->L);
    lua_newtable(c->L); /* weak keys: a node the app dropped must not be pinned */
    lua_pushstring(c->L, "k");
    lua_setfield(c->L, -2, "__mode");
    lua_setmetatable(c->L, -2);
    lua_pushvalue(c->L, -1);
    lua_rawsetp(c->L, LUA_REGISTRYINDEX, RKEY(RK_FAILED));
    lua_replace(c->L, c->failed);
}

/* ---- reading a node ----------------------------------------------------- */

/* Raw string field `key` of the table at `idx` (or NULL if it is not a string).
 *
 * The value is left on the stack on purpose, and that slot is the only thing
 * keeping the returned pointer alive: nothing else in the tree need own the
 * string, because lua_tostring converts a number in place and the result exists
 * nowhere but here. The caller therefore reads the string first and unwinds
 * afterwards, with the lua_settop that ends the node's frame - never a lua_pop
 * in this helper. */
static const char *push_raw_str(lua_State *L, int idx, const char *key)
{
    idx = lua_absindex(L, idx); /* the push below would shift a relative index */
    lua_pushstring(L, key);
    lua_rawget(L, idx);
    return lua_tostring(L, -1);
}

static catnip_node_kind kind_of(const char *s)
{
    if (s) {
        if (strcmp(s, "screen") == 0) return CATNIP_NODE_SCREEN;
        if (strcmp(s, "button") == 0) return CATNIP_NODE_BUTTON;
        if (strcmp(s, "list") == 0) return CATNIP_NODE_LIST;
    }
    return CATNIP_NODE_LABEL;
}

/* An unknown style name is body rather than an error, so an app written against
 * a later firmware degrades on an older one instead of failing to open. */
static catnip_style_role style_of(const char *s)
{
    if (s) {
        if (strcmp(s, "title") == 0) return CATNIP_STYLE_TITLE;
        if (strcmp(s, "caption") == 0) return CATNIP_STYLE_CAPTION;
        if (strcmp(s, "primary") == 0) return CATNIP_STYLE_PRIMARY;
        if (strcmp(s, "danger") == 0) return CATNIP_STYLE_DANGER;
        if (strcmp(s, "display") == 0) return CATNIP_STYLE_DISPLAY;
    }
    return CATNIP_STYLE_BODY;
}

/* What this node is listening to. The `handlers` table is a raw field, put
 * there by ui.make() from every key beginning `on_`, so this is one lookup and
 * five comparisons rather than a scan.
 *
 * Reported rather than acted on here, because two different questions are
 * downstream of it and they must not each grow their own copy: whether the
 * focus ring stops on this node, and whether a direction does anything from
 * it - which is what the control hint on the screen is drawn from. */
static unsigned node_events(lua_State *L, int node)
{
    static const struct {
        const char *key;
        unsigned bit;
    } kMap[] = {
        {"on_click", CATNIP_EV_CLICK}, {"on_prev", CATNIP_EV_PREV},
        {"on_next", CATNIP_EV_NEXT},   {"on_options", CATNIP_EV_OPTIONS},
        {"on_back", CATNIP_EV_BACK},
    };
    unsigned events = 0;
    size_t i;

    lua_pushstring(L, "handlers");
    lua_rawget(L, node);
    if (lua_istable(L, -1)) {
        for (i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
            lua_pushstring(L, kMap[i].key);
            lua_rawget(L, -2);
            if (!lua_isnil(L, -1)) events |= kMap[i].bit;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return events;
}

/* Fill *d from `node`, leaving on the stack the Lua values d->id and d->text
 * point into. The caller unwinds with lua_settop only after the backend call
 * that borrowed them has returned.
 *
 * Every field is read with rawget, and that is not fastidiousness: kind, props,
 * children, handlers, id and dirty are raw fields on the node table, so the
 * widget metatable never fires for them, and a prop named `kind` would never
 * reach props. Reading node.text instead of node.props.text would work today
 * and break the day someone adds a field. */
static void desc_build(ctx *c, int node, catnip_node_desc *d)
{
    lua_State *L = c->L;
    node = lua_absindex(L, node);

    memset(d, 0, sizeof(*d));
    d->selected = -1;
    d->value = -1; /* "this is not a quantity", which is nearly every node */
    d->id = "";
    d->text = "";
    d->image = "";
    d->value_text = "";

    lua_pushstring(L, "kind");
    lua_rawget(L, node);
    d->kind = kind_of(lua_tostring(L, -1));
    lua_pop(L, 1);

    const char *id = push_raw_str(L, node, "id"); /* stays for the call */
    if (id) d->id = id;

    unsigned flags = 0;
    lua_pushstring(L, "props");
    lua_rawget(L, node); /* stays: it owns the text below */
    if (lua_istable(L, -1)) {
        int props = lua_gettop(L);
        const char *text = push_raw_str(L, props, "text"); /* stays for the call */
        if (text) d->text = text;

        lua_pushstring(L, "style");
        lua_rawget(L, props);
        d->style = style_of(lua_tostring(L, -1));
        lua_pop(L, 1);

        lua_pushstring(L, "icon");
        lua_rawget(L, props);
        d->icon = catnip_icon_from_name(lua_tostring(L, -1));
        lua_pop(L, 1);

        const char *image = push_raw_str(L, props, "image"); /* stays for the call */
        if (image) d->image = image;

        /* Out of range is clamped rather than refused: an app that computed 105
         * from a division meant "full", and a bar drawn past its own top is a
         * rendering bug looking for somewhere to happen. */
        lua_pushstring(L, "value");
        lua_rawget(L, props);
        if (lua_isnumber(L, -1)) {
            int v = (int)lua_tointeger(L, -1);
            d->value = v < 0 ? 0 : (v > 100 ? 100 : v);
        }
        lua_pop(L, 1);

        const char *vt = push_raw_str(L, props, "value_text"); /* stays for the call */
        if (vt) d->value_text = vt;

        lua_pushstring(L, "steps");
        lua_rawget(L, props);
        if (lua_isnumber(L, -1)) {
            int v = (int)lua_tointeger(L, -1);
            d->steps = (v > 0) ? v : 0;
        }
        lua_pop(L, 1);

        if (d->kind == CATNIP_NODE_LIST) {
            lua_pushstring(L, "layout");
            lua_rawget(L, props);
            const char *lay = lua_tostring(L, -1);
            /* Unknown names fall back to rows, the same promise style roles and
             * icon names make: a layout a later firmware knows costs an older
             * one its arrangement, not the app. */
            d->layout = CATNIP_LAYOUT_ROWS;
            if (lay && strcmp(lay, "carousel") == 0) d->layout = CATNIP_LAYOUT_CAROUSEL;
            else if (lay && strcmp(lay, "mixer") == 0) d->layout = CATNIP_LAYOUT_MIXER;
            else if (lay && strcmp(lay, "row") == 0) d->layout = CATNIP_LAYOUT_ROW;
            else if (lay && strcmp(lay, "canvas") == 0) d->layout = CATNIP_LAYOUT_CANVAS;
            lua_pop(L, 1);
        }

        if (d->kind == CATNIP_NODE_LIST) {
            lua_pushstring(L, "selected");
            lua_rawget(L, props);
            if (lua_isnumber(L, -1)) {
                int v = (int)lua_tointeger(L, -1);
                /* Lua's index is one-based; the vtable's is zero-based, like
                 * every other index it carries. Out of range is "none". */
                d->selected = (v >= 1) ? v - 1 : -1;
            }
            lua_pop(L, 1);
        }

        lua_pushstring(L, "hidden");
        lua_rawget(L, props);
        if (lua_toboolean(L, -1)) flags |= CATNIP_NODE_HIDDEN;
        lua_pop(L, 1);

        lua_pushstring(L, "disabled");
        lua_rawget(L, props);
        if (lua_toboolean(L, -1)) flags |= CATNIP_NODE_DISABLED;
        lua_pop(L, 1);
    }

    d->events = node_events(L, node);

    /* A strip is a line of labels, not a set of choices: `selected` means
     * nothing on one, there is nothing for prev/next to move and nothing for a
     * click to activate. So it does not take focus, and the focus cursor does
     * not stop on decoration - nor on a column that is decoration for the
     * other reason, that nothing is listening to it. A button is focusable by
     * its kind: it is a promise to the eye, and one that answered nothing
     * would be a bug in the app rather than a page of facts. */
    if ((d->kind == CATNIP_NODE_BUTTON ||
         (d->kind == CATNIP_NODE_LIST && d->layout != CATNIP_LAYOUT_ROW &&
          (d->events & (CATNIP_EV_CLICK | CATNIP_EV_PREV | CATNIP_EV_NEXT)))) &&
        !(flags & (CATNIP_NODE_HIDDEN | CATNIP_NODE_DISABLED)))
        flags |= CATNIP_NODE_FOCUSABLE;
    d->flags = flags;
}

static int desc_same(const slot *s, const catnip_node_desc *d)
{
    return s->kind == d->kind && s->style == d->style && s->icon == d->icon &&
           s->layout == d->layout && s->selected == d->selected && s->flags == d->flags &&
           s->events == d->events && s->value == d->value && s->steps == d->steps &&
           s->text != NULL && strcmp(s->text, d->text) == 0 && s->image != NULL &&
           strcmp(s->image, d->image) == 0 && s->value_text != NULL &&
           strcmp(s->value_text, d->value_text) == 0;
}

static void desc_store(lua_State *L, slot *s, const catnip_node_desc *d)
{
    s->kind = d->kind;
    s->style = d->style;
    s->icon = d->icon;
    s->layout = d->layout;
    s->selected = d->selected;
    s->flags = d->flags;
    s->events = d->events;
    s->value = d->value;
    s->steps = d->steps;
    text_store(L, s, d->text);
    image_store(L, s, d->image);
    value_text_store(L, s, d->value_text);
}

static int node_dirty(lua_State *L, int node)
{
    lua_pushstring(L, "dirty");
    lua_rawget(L, lua_absindex(L, node));
    int dirty = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return dirty;
}

/* Nobody in catnip_ui.c ever clears `dirty`; the renderer is the only reader,
 * so it is the only thing that can. rawset, because the metatable's __newindex
 * would push it back into props. */
static void node_clean(lua_State *L, int node)
{
    node = lua_absindex(L, node);
    lua_pushstring(L, "dirty");
    lua_pushboolean(L, 0);
    lua_rawset(L, node);
}

/* ---- destruction -------------------------------------------------------- */

/* Children first, one call per node, and no reliance on the backend's toolkit
 * cascading anything. See the destroy() comment in the header for why the
 * cheaper alternative is wrong in a way the host tests cannot see - which is
 * exactly why the order and the count are asserted there instead. */
static void destroy_subtree(ctx *c, catnip_handle h)
{
    if (!slot_of(c->st, h)) return;
    for (int i = 0; i < SLOTS_MAX; i++) {
        slot *k = &c->st->slots[i];
        if (k->in_use && k->parent == h) destroy_subtree(c, handle_at(c->st, i));
    }
    if (c->be && c->be->destroy) c->be->destroy(c->be->ud, h);
    c->calls++;
    slot_free(c, h);
}

static void screen_clear_seen(render_state *st, catnip_handle screen)
{
    for (int i = 0; i < SLOTS_MAX; i++)
        if (st->slots[i].in_use && st->slots[i].screen == screen) st->slots[i].seen = 0;
}

/* Destroy everything this screen still holds that the pass did not visit. Only
 * the roots of the unvisited set are started, because an unvisited node's
 * children are unvisited too and destroy_subtree already walks them. */
static void screen_sweep(ctx *c, catnip_handle screen)
{
    for (int i = 0; i < SLOTS_MAX; i++) {
        slot *s = &c->st->slots[i];
        if (!s->in_use || s->screen != screen || s->seen) continue;
        slot *p = slot_of(c->st, s->parent);
        if (p && !p->seen && p->screen == screen) continue; /* its parent goes too */
        destroy_subtree(c, handle_at(c->st, i));
    }
}

/* ---- the walk ----------------------------------------------------------- */

static void walk(ctx *c, int node, catnip_handle parent, catnip_handle screen, int index,
                 int depth)
{
    lua_State *L = c->L;

    if (depth > DEPTH_MAX) {
        if (!c->st->deep_reported) {
            c->st->deep_reported = 1;
            render_log(c->rt,
                       "catnip: the widget tree is deeper than %d and was cut "
                       "off - is a node its own child?",
                       DEPTH_MAX);
        }
        return;
    }
    /* Each level holds a node, its children table and the values a descriptor
     * borrows. LUA_MINSTACK is only twenty, and sixteen levels is well past it. */
    if (!lua_checkstack(L, 8)) return;

    node = lua_absindex(L, node);

    /* A node whose create failed is not retried until the tree changes at this
     * position or the screen is replaced. Retrying every pass would hammer a
     * failing allocator at frame rate and write one log line per frame, which
     * is how a memory problem turns into a hang. */
    if (node_failed(c, node)) return;

    catnip_handle h = handle_of(c, node);
    slot *s = slot_of(c->st, h);
    if (s && s->parent != parent) {
        /* Reparented. Rare enough not to earn a verb: the new object is created
         * here and the old one is destroyed by this screen's sweep. */
        s = NULL;
        h = CATNIP_HANDLE_NONE;
    }

    int frame = lua_gettop(L);
    if (!s) {
        catnip_node_desc d;
        desc_build(c, node, &d);
        h = slot_alloc(c, node);
        if (h == CATNIP_HANDLE_NONE) {
            c->failure = 1;
            render_log(c->rt, "catnip: out of widget slots (%d); '%s' was not drawn",
                       SLOTS_MAX, d.id);
            lua_settop(L, frame);
            return;
        }
        s = slot_of(c->st, h);
        s->parent = parent;
        s->screen = (screen == CATNIP_HANDLE_NONE) ? h : screen;
        s->index = index;
        int rc = c->be->create ? c->be->create(c->be->ud, h, parent, index, &d) : 0;
        c->calls++;
        if (rc != 0) {
            render_log(c->rt,
                       "catnip: the backend could not create '%s'; the screen "
                       "is incomplete",
                       d.id);
            slot_free(c, h);
            node_mark_failed(c, node);
            c->failure = 1;
            lua_settop(L, frame);
            return;
        }
        desc_store(L, s, &d);
        node_clean(L, node);
        lua_settop(L, frame);
    } else {
        if (s->index != index) {
            if (c->be->move) c->be->move(c->be->ud, h, index);
            s->index = index;
            c->calls++;
        }
        /* `dirty` is the fast path, not the answer: it says the descriptor is
         * worth building, and the cache says whether anything actually differs.
         * That is what makes `status.text = "passing"` on an already-passing
         * label cost nothing, which for a polling app is most writes. */
        if (node_dirty(L, node)) {
            catnip_node_desc d;
            desc_build(c, node, &d);
            if (!desc_same(s, &d)) {
                if (c->be->update) c->be->update(c->be->ud, h, &d);
                c->calls++;
                desc_store(L, s, &d);
            }
            node_clean(L, node);
            lua_settop(L, frame);
        }
    }

    s->seen = 1;
    if (c->record_order && (s->flags & CATNIP_NODE_FOCUSABLE) &&
        c->st->n_order < SLOTS_MAX)
        c->st->order[c->st->n_order++] = h;

    catnip_handle own_screen = s->screen;
    lua_pushstring(L, "children");
    lua_rawget(L, node);
    if (lua_istable(L, -1)) {
        int kids = lua_gettop(L);
        int n = (int)lua_rawlen(L, kids);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, kids, i);
            if (lua_istable(L, -1))
                walk(c, lua_gettop(L), h, own_screen, i - 1, depth + 1);
            lua_settop(L, kids);
        }
    }
    lua_settop(L, frame);
}

/* ---- state -------------------------------------------------------------- */

static int state_gc(lua_State *L)
{
    render_state *st = (render_state *)lua_touserdata(L, 1);
    if (!st) return 0;
    /* The cached texts are ours, not Lua's: without this they would outlive the
     * lua_State that paid for them. */
    for (int i = 0; i < SLOTS_MAX; i++)
        text_free(L, &st->slots[i]);
    return 0;
}

static void push_weak_table(lua_State *L)
{
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
}

static render_state *state_get(lua_State *L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, RKEY(RK_STATE));
    if (lua_isuserdata(L, -1)) {
        render_state *st = (render_state *)lua_touserdata(L, -1);
        lua_pop(L, 1);
        return st;
    }
    lua_pop(L, 1);

    render_state *st = (render_state *)lua_newuserdatauv(L, sizeof(*st), 0);
    memset(st, 0, sizeof(*st));
    st->shown = CATNIP_HANDLE_NONE;
    for (int i = 0; i < SCREENS_MAX; i++)
        st->screens[i] = CATNIP_HANDLE_NONE;

    lua_newtable(L);
    lua_pushcfunction(L, state_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_STATE));

    lua_newtable(L); /* [handle] = node, strong: the renderer keeps it alive */
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_NODES));
    push_weak_table(L); /* [node] = handle: the keyed diff's index */
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_SLOTS));
    push_weak_table(L); /* the nodes whose create failed */
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_FAILED));
    return st;
}

/* Look the state up without creating it. Used by the paths that can only be
 * reached with a handle the renderer already handed out. */
static render_state *state_peek(lua_State *L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, RKEY(RK_STATE));
    render_state *st = (render_state *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return st;
}

static void ctx_push_tables(ctx *c)
{
    lua_rawgetp(c->L, LUA_REGISTRYINDEX, RKEY(RK_NODES));
    c->nodes = lua_gettop(c->L);
    lua_rawgetp(c->L, LUA_REGISTRYINDEX, RKEY(RK_SLOTS));
    c->slots = lua_gettop(c->L);
    lua_rawgetp(c->L, LUA_REGISTRYINDEX, RKEY(RK_FAILED));
    c->failed = lua_gettop(c->L);
}

/* Push ui.root(n) - the n-th screen from the bottom of the stack. Returns 1 with
 * the table on top, else 0 (there is no such screen) and a balanced stack. */
static int push_screen(lua_State *L, int n)
{
    lua_getglobal(L, "ui");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_getfield(L, -1, "root");
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushinteger(L, n);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

/* ---- the pass ----------------------------------------------------------- */

int catnip_render(catnip_rt *rt, const catnip_render_backend *be)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L || !be) return -1;
    render_state *st = state_get(L);
    if (!st) return -1;

    int base = lua_gettop(L);
    ctx c;
    memset(&c, 0, sizeof(c));
    c.L = L;
    c.rt = rt;
    c.st = st;
    c.be = be;
    ctx_push_tables(&c);

    /* The visible stack, bottom first. Reading it whole is what tells a push
     * (one more screen above the same ones) from a replacement (a different
     * screen at the same depth) without either side having to declare which. */
    int screen_at[SCREENS_MAX];
    int n_lua = 0;
    while (n_lua < SCREENS_MAX && push_screen(L, n_lua + 1)) {
        screen_at[n_lua] = lua_gettop(L);
        n_lua++;
    }

    if (be->begin_pass) be->begin_pass(be->ud);

    if (n_lua == 0) {
        /* ui.reset() or a pop past the bottom: there is nothing to show, and
         * whatever is still drawn has to go. */
        for (int i = st->depth - 1; i >= 0; i--)
            destroy_subtree(&c, st->screens[i]);
        for (int i = 0; i < SCREENS_MAX; i++)
            st->screens[i] = CATNIP_HANDLE_NONE;
        st->depth = 0;
        st->shown = CATNIP_HANDLE_NONE;
        st->n_order = 0;
        failed_clear(&c);
        if (be->end_pass) be->end_pass(be->ud);
        lua_settop(L, base);
        /* The destroys are not reported as a count: what the caller needs to
         * know is that there is nothing to draw, and that is the whole answer. */
        return CATNIP_RENDER_NO_SCREEN;
    }

    /* How much of the mirror the visible stack still agrees with. Everything
     * above it was popped or replaced and its slots are gone; everything from
     * it up is built. */
    int common = 0;
    while (common < n_lua && common < st->depth &&
           handle_of(&c, screen_at[common]) == st->screens[common] &&
           st->screens[common] != CATNIP_HANDLE_NONE)
        common++;

    if (common < st->depth) {
        int torn = 0;
        for (int i = st->depth - 1; i >= common; i--) {
            if (st->screens[i] != CATNIP_HANDLE_NONE) {
                destroy_subtree(&c, st->screens[i]);
                torn = 1;
            }
            st->screens[i] = CATNIP_HANDLE_NONE;
        }
        st->depth = common;
        /* A screen that is gone takes its create failures with it, so a node
         * that failed once gets a fresh chance on the screen that replaces it.
         * Only when a real screen was torn down, though: clearing the set for a
         * screen that never got built is how "do not retry" turns back into
         * retrying at frame rate. */
        if (torn) failed_clear(&c);
    }

    /* Only the visible screen is reconciled. One underneath keeps its dirty
     * flags and its stale cache and catches up in a single pass when it is
     * shown again, which is both cheaper and more correct than walking screens
     * nobody can see. */
    int start = (common == n_lua) ? n_lua - 1 : common;
    for (int i = start; i < n_lua; i++) {
        catnip_handle sh = (i < st->depth) ? st->screens[i] : CATNIP_HANDLE_NONE;
        if (sh != CATNIP_HANDLE_NONE) screen_clear_seen(st, sh);
        c.record_order = (i == n_lua - 1);
        if (c.record_order) st->n_order = 0;
        walk(&c, screen_at[i], CATNIP_HANDLE_NONE, CATNIP_HANDLE_NONE, i, 0);
        c.record_order = 0;
        sh = handle_of(&c, screen_at[i]);
        /* The screen itself could not be built. Leave the mirror short rather
         * than recording a screen that is not there, so the next pass finds the
         * same prefix, walks into the same refusal, and stays quiet. */
        if (sh == CATNIP_HANDLE_NONE) break;
        st->screens[i] = sh;
        if (i >= st->depth) st->depth = i + 1;
        screen_sweep(&c, sh);
    }

    catnip_handle top = st->depth > 0 ? st->screens[st->depth - 1] : CATNIP_HANDLE_NONE;
    if (top != CATNIP_HANDLE_NONE && top != st->shown) {
        if (be->show_screen) be->show_screen(be->ud, top);
        st->shown = top;
    }

    if (be->end_pass) be->end_pass(be->ud);
    lua_settop(L, base);
    return c.failure ? CATNIP_RENDER_FAILED : c.calls;
}

void catnip_render_reset(catnip_rt *rt, const catnip_render_backend *be)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return;
    render_state *st = state_peek(L);
    if (!st) return;

    int base = lua_gettop(L);
    ctx c;
    memset(&c, 0, sizeof(c));
    c.L = L;
    c.rt = rt;
    c.st = st;
    c.be = be;
    ctx_push_tables(&c);

    if (be && be->begin_pass) be->begin_pass(be->ud);
    for (int i = st->depth - 1; i >= 0; i--)
        destroy_subtree(&c, st->screens[i]);
    /* Anything left is a slot whose screen was already forgotten; the array is
     * the owner of record, so it has the last word on what is still alive. */
    for (int i = 0; i < SLOTS_MAX; i++)
        if (st->slots[i].in_use) destroy_subtree(&c, handle_at(st, i));
    if (be && be->end_pass) be->end_pass(be->ud);

    for (int i = 0; i < SCREENS_MAX; i++)
        st->screens[i] = CATNIP_HANDLE_NONE;
    st->depth = 0;
    st->shown = CATNIP_HANDLE_NONE;
    st->qhead = st->qcount = st->qdropped = 0;
    /* An unread claim belongs to the app that just went away. Left standing it
     * would answer the *next* app's first B. */
    st->claim = 0;
    st->n_order = 0;
    st->deep_reported = 0;
    failed_clear(&c);
    lua_settop(L, base);

    /* Fresh tables rather than emptied ones, so nothing an app could still be
     * holding keeps an entry alive. */
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_NODES));
    push_weak_table(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, RKEY(RK_SLOTS));

    /* The renderer's release achieves nothing while the ui module still roots
     * the tree in state.root and state.by_id, so the last word belongs to it. */
    lua_getglobal(L, "ui");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "reset");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

int catnip_render_focus_order(catnip_rt *rt, catnip_handle *out, int max)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return 0;
    render_state *st = state_peek(L);
    if (!st) return 0;
    for (int i = 0; i < st->n_order && i < max; i++)
        out[i] = st->order[i];
    return st->n_order;
}

/* ---- input -------------------------------------------------------------- */

void catnip_render_set_dispatch(catnip_rt *rt, catnip_render_dispatch_fn fn, void *ud)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return;
    render_state *st = state_get(L);
    if (!st) return;
    st->dispatch = fn;
    st->dispatch_ud = ud;
}

catnip_handle catnip_render_visible_screen(catnip_rt *rt)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return CATNIP_HANDLE_NONE;
    render_state *st = state_peek(L);
    if (!st) return CATNIP_HANDLE_NONE;
    /* `shown` and not screens[depth-1]: what the backend was told to show is
     * what the user is looking at, and the two differ for exactly one pass
     * after a push, which is a pass in which a press has nowhere sensible to
     * go anyway. */
    return slot_of(st, st->shown) ? st->shown : CATNIP_HANDLE_NONE;
}

int catnip_render_selected(catnip_rt *rt, catnip_handle h)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return CATNIP_INDEX_NONE;
    render_state *st = state_peek(L);
    if (!st) return CATNIP_INDEX_NONE;
    slot *sl = slot_of(st, h);
    if (!sl || sl->kind != CATNIP_NODE_LIST) return CATNIP_INDEX_NONE;
    return (sl->selected < 0) ? CATNIP_INDEX_NONE : sl->selected;
}

int catnip_render_counter(catnip_rt *rt, catnip_handle focus, int *n, int *total)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return 0;
    render_state *st = state_peek(L);
    if (!st || st->shown == CATNIP_HANDLE_NONE) return 0;

    /* The focused list wins; otherwise the visible screen's sole list. Two
     * lists and nothing focused is genuinely ambiguous, and guessing would put
     * a number in the bar that answers a question the user did not ask. */
    slot *list = slot_of(st, focus);
    if (list && list->kind != CATNIP_NODE_LIST) list = NULL;
    if (!list) {
        int found = 0;
        for (int i = 0; i < SLOTS_MAX; i++) {
            slot *s = &st->slots[i];
            if (!s->in_use || s->kind != CATNIP_NODE_LIST) continue;
            if (s->screen != st->shown) continue;
            list = s;
            if (++found > 1) return 0;
        }
        if (!found) return 0;
    }
    if (list->screen != st->shown) return 0;
    if (list->selected < 0) return 0; /* an empty directory counts nothing */

    catnip_handle h = handle_make((int)(list - st->slots), list->gen);
    int rows = 0;
    for (int i = 0; i < SLOTS_MAX; i++)
        if (st->slots[i].in_use && st->slots[i].parent == h) rows++;
    if (rows <= 0) return 0;

    if (n) *n = list->selected + 1; /* one-based for display, converted once */
    if (total) *total = rows;
    return 1;
}

unsigned catnip_render_events(catnip_rt *rt, catnip_handle h)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return 0;
    render_state *st = state_peek(L);
    if (!st) return 0;
    slot *sl = slot_of(st, h);
    return sl ? sl->events : 0u;
}

catnip_node_layout catnip_render_layout(catnip_rt *rt, catnip_handle h)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return CATNIP_LAYOUT_ROWS;
    render_state *st = state_peek(L);
    if (!st) return CATNIP_LAYOUT_ROWS;
    slot *sl = slot_of(st, h);
    if (!sl || sl->kind != CATNIP_NODE_LIST) return CATNIP_LAYOUT_ROWS;
    return sl->layout;
}

int catnip_render_take_claim(catnip_rt *rt)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return 0;
    render_state *st = state_peek(L);
    if (!st) return 0;
    int c = st->claim;
    st->claim = 0;
    return c;
}

static int post(catnip_rt *rt, catnip_handle h, const char *event, int index,
                int claimable)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L || !event) return -1;
    render_state *st = state_peek(L);
    if (!st) return -1;
    if (!slot_of(st, h)) return -1; /* the node is gone; the press goes nowhere */

    if (st->qcount >= QUEUE_MAX) {
        st->qdropped++;
        return -2;
    }
    qentry *e = &st->q[(st->qhead + st->qcount) % QUEUE_MAX];
    e->h = h;
    snprintf(e->event, sizeof(e->event), "%s", event);
    e->index = index;
    e->claimable = claimable;
    st->qcount++;
    return 0;
}

int catnip_render_post(catnip_rt *rt, catnip_handle h, const char *event, int index)
{
    return post(rt, h, event, index, 0);
}

int catnip_render_post_claimable(catnip_rt *rt, catnip_handle h, const char *event,
                                 int index)
{
    return post(rt, h, event, index, 1);
}

int catnip_render_drain(catnip_rt *rt)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return 0;
    render_state *st = state_peek(L);
    if (!st) return 0;

    if (st->qdropped) {
        render_log(rt, "catnip: %d input events were dropped, the queue was full",
                   st->qdropped);
        st->qdropped = 0;
    }

    /* Only what was queued on entry: a handler that posts - a button that opens
     * a screen with a button under the finger - is delivered on the next drain
     * rather than extending this one. */
    int pending = st->qcount;
    int delivered = 0;
    for (int n = 0; n < pending; n++) {
        qentry e = st->q[st->qhead];
        st->qhead = (st->qhead + 1) % QUEUE_MAX;
        st->qcount--;

        /* Checked again here, because the node may have been destroyed between
         * the press and now. This is the generation earning its keep twice. */
        if (!slot_of(st, e.h)) continue;

        lua_rawgetp(L, LUA_REGISTRYINDEX, RKEY(RK_NODES));
        lua_rawgeti(L, -1, (lua_Integer)e.h);
        lua_remove(L, -2);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        int ref = luaL_ref(L, LUA_REGISTRYINDEX); /* the dispatcher owns this */
        if (st->dispatch) {
            /* The claim is latched rather than returned, because the caller
             * that cares about it - the shell, deciding whether B was handled -
             * is not the caller that drains. Only a post that asked to be
             * answered can set it. */
            int r = st->dispatch(st->dispatch_ud, rt, ref, e.event, e.index);
            if (e.claimable && r > 0) st->claim = 1;
            delivered++;
        } else {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }
    return delivered;
}
