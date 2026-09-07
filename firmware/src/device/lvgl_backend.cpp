/* lvgl_backend.cpp - see lvgl_backend.h.
 *
 * HOW THIS IS ARRANGED. The renderer keeps no pointer to anything LVGL made,
 * so the map from handle to object is here and it is the only one. Every verb
 * starts by finding its entry in it, and every object carries its handle back
 * in lv_obj_set_user_data() - which is the road an input event takes to
 * catnip_render_post(), and the reason the map is not keyed the other way.
 *
 * The map is a flat array searched linearly. It could be indexed by the low
 * half of the handle, which catnip_render.h documents as the slot index, but a
 * pass touches a handful of entries at most - the whole point of a retained
 * renderer is that an unchanged tree costs nothing - so the scan is never on a
 * hot path, and not depending on the handle's encoding leaves that encoding
 * free to change.
 *
 * NO GEOMETRY. catnip_render.h deliberately carries no coordinates, so
 * everything here is LVGL's flex column: a screen stacks its children top to
 * bottom, a list does the same and scrolls, and a label or a button is as wide
 * as its parent and as tall as its content. An app cannot ask for a position
 * and this file cannot invent one, which is what keeps the same tree legible
 * on a panel of a different size.
 *
 * WHAT A ROLE LOOKS LIKE. The five style roles are a vocabulary, not a
 * stylesheet: what `title` means in fonts and colours lives here, with the
 * fonts. They are applied as LVGL *local* styles rather than through shared
 * lv_style_t objects, because update() can change a node's role and overwriting
 * a local property is one call where swapping a shared style would be a remove
 * and an add with the old role to remember in between.
 */
#include <lvgl.h>

#include <stdint.h>

#include "lvgl_backend.h"
#include "lvgl_port.h"

namespace {

/* One entry per renderer slot, and the renderer caps those at 128 (SLOTS_MAX
 * in catnip_render.c). Sized to match rather than guessed: if the two ever
 * disagree the renderer is the one that decides, and the create below fails
 * honestly instead of overrunning. */
const int kMaxObjects = 128;

/* A dark palette, because this panel is read in a room and a white screen on it
 * is a lamp. The values are the same ones the diagnostic page settled on, so
 * the two pages look like the same device. */
const uint32_t kColBg = 0x000000;      /* the screen behind everything */
const uint32_t kColPanel = 0x212421;   /* a raised surface: an ordinary button */
const uint32_t kColText = 0xFFFFFF;    /* body and title ink */
const uint32_t kColFaint = 0x7B7D7B;   /* caption ink, and a list's border */
const uint32_t kColPrimary = 0x00B0FF; /* the affirmative action */
const uint32_t kColDanger = 0xFF4B3E;  /* the one that cannot be undone */
const uint32_t kColSelect = 0xFFFFFF;  /* the selected row's fill */

struct Entry {
    catnip_handle h;
    catnip_handle parent;
    lv_obj_t *obj;
    catnip_node_kind kind;
    int selected;   /* list only: the child to highlight, or -1 */
    bool sel_dirty; /* list only: the highlight has to be re-applied */
    bool used;
};

Entry g_map[kMaxObjects];

catnip_rt *g_rt;

/* LVGL is up and this backend has drawn into it. Once true it stays true: see
 * the header for why the panel is not handed back. */
bool g_up;

/* The screen LVGL made for itself in lv_display_create(). It is never in the
 * map and never destroyed, and it is what gets loaded when the screen an app
 * was showing is torn down. That matters more than it looks: lv_obj_delete()
 * on the active screen leaves the display with no active screen at all, and
 * the next lv_timer_handler() dereferences it. */
lv_obj_t *g_blank;

/* ---- the map ------------------------------------------------------------ */

Entry *map_find(catnip_handle h)
{
    if (h == CATNIP_HANDLE_NONE) return nullptr;
    for (int i = 0; i < kMaxObjects; i++)
        if (g_map[i].used && g_map[i].h == h) return &g_map[i];
    return nullptr;
}

Entry *map_alloc(void)
{
    for (int i = 0; i < kMaxObjects; i++) {
        if (g_map[i].used) continue;
        g_map[i] = Entry();
        g_map[i].used = true;
        return &g_map[i];
    }
    return nullptr;
}

void map_release(Entry *e)
{
    *e = Entry();
}

/* A list applies its selection at the end of the pass, so anything that can
 * change which child is where has to say so. */
void mark_list(catnip_handle parent)
{
    Entry *p = map_find(parent);
    if (p && p->kind == CATNIP_NODE_LIST) p->sel_dirty = true;
}

/* ---- style roles -------------------------------------------------------- */

const lv_font_t *role_font(catnip_style_role role)
{
    switch (role) {
    case CATNIP_STYLE_TITLE: return &lv_font_montserrat_16;
    case CATNIP_STYLE_CAPTION: return &lv_font_montserrat_10;
    /* An unknown name from Lua already arrived as BODY - catnip_render.c
     * resolves it - so this is the fallback for the roles that do not change
     * the size, not for a name nobody recognised. */
    default: return &lv_font_montserrat_14;
    }
}

uint32_t role_ink(catnip_style_role role)
{
    switch (role) {
    case CATNIP_STYLE_CAPTION: return kColFaint;
    case CATNIP_STYLE_PRIMARY: return kColPrimary;
    case CATNIP_STYLE_DANGER: return kColDanger;
    default: return kColText;
    }
}

/* A button is a shape rather than a run of text, so its role colours the fill
 * and the ink follows from it. A label's role colours the ink directly. Same
 * five names, and the same five meanings, read off two different surfaces. */
uint32_t role_fill(catnip_style_role role)
{
    switch (role) {
    case CATNIP_STYLE_PRIMARY: return kColPrimary;
    case CATNIP_STYLE_DANGER: return kColDanger;
    default: return kColPanel;
    }
}

/* ---- applying a descriptor ---------------------------------------------- */

/* The label a button shows its text on. It is made in make_button() before
 * anything else, so it is child zero for the life of the button. A button with
 * node children of its own would push it around, and nothing in ui.* is
 * expected to build one - a button's text is a prop, not a child. */
lv_obj_t *button_label(lv_obj_t *button)
{
    return lv_obj_get_child(button, 0);
}

void apply_style(Entry *e, catnip_style_role role)
{
    if (e->kind == CATNIP_NODE_BUTTON) {
        uint32_t fill = role_fill(role);
        lv_obj_t *label = button_label(e->obj);

        lv_obj_set_style_bg_color(e->obj, lv_color_hex(fill), 0);
        lv_obj_set_style_text_font(label, role_font(role), 0);
        /* Black on the two loud fills and white on the quiet one, so the text
         * stays legible whichever role a button is given. */
        lv_obj_set_style_text_color(
            label, lv_color_hex(fill == kColPanel ? kColText : kColBg), 0);
        return;
    }
    lv_obj_set_style_text_font(e->obj, role_font(role), 0);
    lv_obj_set_style_text_color(e->obj, lv_color_hex(role_ink(role)), 0);
}

void apply_text(Entry *e, const char *text)
{
    /* lv_label_set_text() copies, which is why nothing here has to think about
     * the descriptor's strings dying when this call returns. */
    if (e->kind == CATNIP_NODE_LABEL) lv_label_set_text(e->obj, text);
    else if (e->kind == CATNIP_NODE_BUTTON) lv_label_set_text(button_label(e->obj), text);
}

void apply_flags(Entry *e, unsigned flags)
{
    /* LV_OBJ_FLAG_HIDDEN takes the object out of the flex layout as well as out
     * of the picture, so a hidden status line leaves no gap where it was -
     * which is what the File Browser's `status.hidden` is asking for. */
    if (flags & CATNIP_NODE_HIDDEN) lv_obj_add_flag(e->obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(e->obj, LV_OBJ_FLAG_HIDDEN);

    if (flags & CATNIP_NODE_DISABLED) lv_obj_add_state(e->obj, LV_STATE_DISABLED);
    else lv_obj_remove_state(e->obj, LV_STATE_DISABLED);
}

/* The whole descriptor, every time, with no second diff. The renderer only
 * calls update() when something genuinely differs and does not say what, and
 * re-deriving that here would be the same comparison done twice - once against
 * a cache that is right and once against LVGL's state, which is not the same
 * thing and would eventually disagree. */
void apply_desc(Entry *e, const catnip_node_desc *d)
{
    apply_text(e, d->text);
    apply_style(e, d->style);
    apply_flags(e, d->flags);
    if (e->kind == CATNIP_NODE_LIST) {
        e->selected = d->selected;
        e->sel_dirty = true;
    }
}

/* Put the highlight on the selected child and scroll it into view.
 *
 * This is the one thing that moves a list's scroll, and it does nothing when
 * the row is already on screen - which is what keeps the position across an
 * update. The app cannot read the scroll and cannot set it; keeping it is the
 * whole reason the renderer is retained, so nothing here may reset it.
 *
 * The layout is forced first because LVGL lays out at render time and this runs
 * before that: without it a row created in this same pass has no position yet
 * and would be scrolled to the wrong place. */
void apply_selection(Entry *e)
{
    uint32_t n = lv_obj_get_child_count(e->obj);

    e->sel_dirty = false;
    lv_obj_update_layout(e->obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(e->obj, i);

        if ((int)i == e->selected) {
            lv_obj_add_state(child, LV_STATE_CHECKED);
            lv_obj_scroll_to_view(child, LV_ANIM_OFF);
        } else {
            lv_obj_remove_state(child, LV_STATE_CHECKED);
        }
    }
}

/* ---- building objects --------------------------------------------------- */

void on_clicked(lv_event_t *ev)
{
    lv_obj_t *obj = lv_event_get_target_obj(ev);
    catnip_handle h = (catnip_handle)(intptr_t)lv_obj_get_user_data(obj);

    /* Safe from inside an LVGL callback by contract: it validates the handle,
     * copies the name and returns, and runs no Lua. The handler itself runs
     * from catnip_render_drain() in the main loop. */
    if (g_rt) catnip_render_post(g_rt, h, "click");
}

/* A vertical stack with room to breathe. Shared by the screen and the list,
 * because they are the same thing at two scales. */
void make_column(lv_obj_t *obj, int pad)
{
    lv_obj_set_style_pad_all(obj, pad, 0);
    lv_obj_set_style_pad_row(obj, pad, 0);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
}

lv_obj_t *make_screen(void)
{
    lv_obj_t *obj = lv_obj_create(nullptr);

    if (!obj) return nullptr;
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    make_column(obj, 6);
    return obj;
}

lv_obj_t *make_list(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    if (!obj) return nullptr;
    lv_obj_set_width(obj, lv_pct(100));
    /* The list takes whatever height the fixed rows around it leave, rather
     * than growing with its contents. A list that grew would push the buttons
     * under it off the screen the moment a directory got long, and the thing
     * that is supposed to scroll would be the screen instead of the list. */
    lv_obj_set_flex_grow(obj, 1);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(kColFaint), 0);
    make_column(obj, 2);
    return obj;
}

lv_obj_t *make_label(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_label_create(parent);

    if (!obj) return nullptr;
    lv_obj_set_width(obj, lv_pct(100));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    return obj;
}

lv_obj_t *make_button(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_t *label;

    if (!obj) return nullptr;
    label = lv_label_create(obj);
    if (!label) {
        /* Half a button is worse than none: the renderer frees the slot the
         * moment create() refuses, so anything left behind here would never be
         * reachable again. */
        lv_obj_delete(obj);
        return nullptr;
    }
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_center(label);
    lv_obj_add_event_cb(obj, on_clicked, LV_EVENT_CLICKED, nullptr);
    return obj;
}

/* What a row in a list looks like when it is the selected one. The style is
 * given to the child rather than to the list, because it is the child that
 * carries LV_STATE_CHECKED and LVGL resolves a style against the object's own
 * state. */
void style_as_row(lv_obj_t *obj)
{
    lv_obj_set_style_pad_all(obj, 2, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColSelect), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, lv_color_hex(kColBg), LV_STATE_CHECKED);
}

/* ---- the vtable --------------------------------------------------------- */

/* Bring LVGL up on the first widget, not on the first pass. A pass that draws
 * nothing is begin_pass immediately followed by end_pass, and starting LVGL
 * there would put an empty screen on the panel over the boot animation before
 * any app had asked for one. */
bool ensure_up(void)
{
    if (g_up) return true;
    if (!catnip_lvgl_begin()) return false;
    g_blank = lv_screen_active();
    lv_obj_set_style_bg_color(g_blank, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(g_blank, LV_OPA_COVER, 0);
    g_up = true;
    return true;
}

int be_create(void *ud, catnip_handle h, catnip_handle parent, int index,
              const catnip_node_desc *d)
{
    (void)ud;
    lv_obj_t *parent_obj = nullptr;
    Entry *parent_entry = nullptr;
    Entry *e;
    lv_obj_t *obj;

    if (!ensure_up()) return -1;

    if (parent != CATNIP_HANDLE_NONE) {
        parent_entry = map_find(parent);
        if (!parent_entry) return -1;
        parent_obj = parent_entry->obj;
    }

    e = map_alloc();
    if (!e) return -1;

    switch (d->kind) {
    case CATNIP_NODE_SCREEN: obj = make_screen(); break;
    case CATNIP_NODE_LIST: obj = make_list(parent_obj); break;
    case CATNIP_NODE_BUTTON: obj = make_button(parent_obj); break;
    default: obj = make_label(parent_obj); break;
    }
    if (!obj) {
        map_release(e);
        return -1;
    }

    /* The handle on the object, which is how an event finds its way back. */
    lv_obj_set_user_data(obj, (void *)(intptr_t)h);

    e->h = h;
    e->parent = parent;
    e->obj = obj;
    e->kind = d->kind;
    e->selected = -1;

    if (parent_entry && parent_entry->kind == CATNIP_NODE_LIST) {
        style_as_row(obj);
        parent_entry->sel_dirty = true;
    }
    apply_desc(e, d);
    /* LVGL appends, and the renderer says where. They agree for a tree built
     * front to back and disagree the moment a node is inserted in the middle,
     * so the position is asked for rather than assumed.
     *
     * The object's own parent decides, not the handle's: a screen node nested
     * inside another screen has a parent in the tree and none in LVGL, because
     * a screen is always a root there. It draws as nothing, which is the honest
     * rendering of a screen inside a screen. */
    if (lv_obj_get_parent(obj)) lv_obj_move_to_index(obj, index);
    return 0;
}

void be_update(void *ud, catnip_handle h, const catnip_node_desc *d)
{
    Entry *e = map_find(h);

    (void)ud;
    if (e) apply_desc(e, d);
}

void be_move(void *ud, catnip_handle h, int index)
{
    Entry *e = map_find(h);

    (void)ud;
    if (!e) return;
    /* A screen's index is its position in the screen stack, which is the
     * renderer's own bookkeeping and has no LVGL equivalent - LVGL shows one
     * screen and knows nothing about what is underneath it. */
    if (!lv_obj_get_parent(e->obj)) return;
    lv_obj_move_to_index(e->obj, index);
    mark_list(e->parent);
}

void be_destroy(void *ud, catnip_handle h)
{
    Entry *e = map_find(h);

    (void)ud;
    if (!e) return;
    mark_list(e->parent);

    /* Deleting the screen that is on the panel would leave the display with no
     * active screen, and the next lv_timer_handler() would dereference it. The
     * blank screen is loaded first so there is always one. */
    if (!lv_obj_get_parent(e->obj) && lv_screen_active() == e->obj)
        lv_screen_load(g_blank);

    /* Synchronous, and one call per node. Not lv_obj_delete_async(), and not
     * LVGL's parent-deletes-children cascade: the renderer sends a destroy for
     * every child first, and under the cascade this map would still be holding
     * entries for objects that had just been freed underneath it. See the
     * destroy() comment in catnip_render.h - the host tests cannot catch this
     * either way, which is why it is written down in both places. */
    lv_obj_delete(e->obj);
    map_release(e);
}

void be_show(void *ud, catnip_handle h)
{
    Entry *e = map_find(h);

    (void)ud;
    /* The screen named here may have been built many passes ago - that is what
     * happens when a pushed screen is popped and the one underneath comes back
     * - so this looks it up like any other handle rather than remembering the
     * last thing it created. */
    if (e) lv_screen_load(e->obj);
}

/* Selections are applied here rather than where they change, because a list's
 * update() arrives before its children are created: the pass that adds a row
 * and moves the highlight onto it would otherwise be highlighting a child that
 * did not exist yet. By the end of the pass every list has the children it is
 * going to have. */
void be_end_pass(void *ud)
{
    (void)ud;
    for (int i = 0; i < kMaxObjects; i++) {
        Entry *e = &g_map[i];

        if (e->used && e->kind == CATNIP_NODE_LIST && e->sel_dirty) apply_selection(e);
    }
}

/* begin_pass is NULL on purpose. LVGL needs no framing - it batches its own
 * drawing behind lv_obj_invalidate() and paints in lv_timer_handler() - and
 * the one thing that could have gone here, bringing LVGL up, belongs on the
 * first create() instead. See ensure_up().
 *
 * `ud` is NULL and the runtime is a file static instead. Not for want of a
 * better place: on_clicked() is an LVGL callback and is handed no `ud` at all,
 * so the runtime has to be reachable without one, and having it in two places
 * would only give them the chance to disagree. */
const catnip_render_backend kBackend = {
    nullptr, nullptr, be_end_pass, be_create, be_update, be_move, be_destroy, be_show,
};

} /* namespace */

const catnip_render_backend *catnip_lvgl_backend(catnip_rt *rt)
{
    g_rt = rt;
    return &kBackend;
}

bool catnip_lvgl_backend_active(void)
{
    return g_up;
}

void catnip_lvgl_backend_redraw(void)
{
    if (g_up) lv_obj_invalidate(lv_screen_active());
}
