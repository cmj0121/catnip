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

#include "../catnip_icon_map.h"
#include "app_icon.h"
#include "catnip_icon_img.h"
#include "../generated/splash_rgb565.h"
#include "catnip_mascot_img.h"
#include "frame.h"
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
/* Sampled from the mascot's own background rather than chosen: home is a
 * full-screen picture and the app plane is icons on the ground behind it, and
 * if those two grounds differ then stepping between them flashes. Black was
 * fine while nothing full-screen was drawn on it. */
const uint32_t kColBg = 0x203048;      /* the screen behind everything */
const uint32_t kColPanel = 0x212421;   /* a raised surface: an ordinary button */
const uint32_t kColText = 0xFFFFFF;    /* body and title ink */
const uint32_t kColFaint = 0x7B7D7B;   /* caption ink, and a list's border */
const uint32_t kColPrimary = 0x00B0FF; /* the affirmative action */
const uint32_t kColDanger = 0xFF4B3E;  /* the one that cannot be undone */

/* How far the landing mascot sits below centre, so the bar does not cross it. */
const int kMascotDropPx = 20;

struct Entry {
    catnip_handle h;
    catnip_handle parent;
    lv_obj_t *obj;
    catnip_node_kind kind;
    int selected;              /* list only: the child to highlight, or -1 */
    catnip_node_layout layout; /* list only: how its children are arranged */
    bool sel_dirty;            /* list only: the highlight has to be re-applied */
    bool row;                  /* list child: internal flex row of image + label */
    /* The widgets inside a row, held rather than looked up by child index: a
     * mixer column adds two more and the order on screen is not the order they
     * were made in, so an index here would be a second thing to keep in step
     * with the flex flow. nullptr on anything that is not a row, and `bar` and
     * `val` stay nullptr until a row turns out to carry a value - a directory
     * of four hundred files should not pay for four hundred bars it never
     * shows. */
    lv_obj_t *img;
    lv_obj_t *name;
    lv_obj_t *bar;
    lv_obj_t *val;
    bool used;
};

Entry g_map[kMaxObjects];

catnip_rt *g_rt;

/* The handle whose object wears the focus ring (#31), or CATNIP_HANDLE_NONE. It
 * lives here because the object it names is the backend's to touch, and it is
 * cleared in be_destroy() when that object is the one being freed. */
catnip_handle g_focused = CATNIP_HANDLE_NONE;

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

void apply_list_layout(Entry *e);

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
    lv_obj_t *text = e->row ? e->name : e->obj;
    if (!text) return;
    lv_obj_set_style_text_font(text, role_font(role), 0);
    lv_obj_set_style_text_color(text, lv_color_hex(role_ink(role)), 0);
}

void apply_text(Entry *e, const char *text, catnip_icon icon, const char *image)
{
    lv_obj_t *label = nullptr;
    lv_obj_t *img = nullptr;

    bool big = false;
    bool mixer = false;
    if (e->row) {
        Entry *p = map_find(e->parent);
        big = p && p->layout == CATNIP_LAYOUT_CAROUSEL;
        mixer = p && p->layout == CATNIP_LAYOUT_MIXER;
        img = e->img;
        label = e->name;
    } else if (e->kind == CATNIP_NODE_LABEL) {
        label = e->obj;
    } else if (e->kind == CATNIP_NODE_BUTTON) {
        label = button_label(e->obj);
    }
    if (!label) return;

    /* lv_label_set_text() copies, which is why nothing here has to think about
     * the descriptor's strings dying when this call returns. Colour icons are
     * an lv_image beside the label, built only for list rows; the node model
     * still sees one child. */
    if (e->row) {
        /* A carousel cell is the whole region: the picture over its name,
         * centred. A row is a line: the glyph before its name, ranged left. */
        lv_obj_set_flex_flow(e->obj,
                             (big || mixer) ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(e->obj,
                              (big || mixer) ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        /* A mixer column is as tall as the region and shares the width evenly
         * with its neighbours, which is the whole point of the shape: height is
         * the number, so every column has to have the same height to be read
         * against the others. */
        if (mixer) {
            lv_obj_set_flex_grow(e->obj, 1);
            lv_obj_set_width(e->obj, LV_SIZE_CONTENT);
            lv_obj_set_height(e->obj, LV_PCT(100));
            lv_obj_set_style_pad_ver(e->obj, 4, 0);
            lv_obj_set_style_pad_row(e->obj, 4, 0);
        }
        /* A carousel cell is the region, so it carries no padding of its own;
         * the gap under the picture is there only when there is a name. */
        lv_obj_set_style_pad_all(e->obj, big ? 0 : 2, 0);
        if (!big) {
            lv_obj_set_style_pad_ver(e->obj, 9, 0);
            lv_obj_set_style_pad_left(e->obj, 6, 0);
            lv_obj_set_style_pad_right(e->obj, 6, 0);
        }
        lv_obj_set_style_pad_row(e->obj, 0, 0);
        /* A carousel cell shows the picture and nothing else: what it is called
         * is the header's to say, which is where a name is legible and where it
         * does not steal room from the thing it names. */
        if (big) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_flex_grow(label, (big || mixer) ? 0 : 1);
        lv_obj_set_width(label, (big || mixer) ? LV_SIZE_CONTENT : LV_PCT(100));
        lv_obj_set_style_text_align(
            label, (big || mixer) ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    }
    lv_label_set_text(label, text);
    if (!img) return;
    /* An app's own icon wins over any glyph: a glyph is a category and this is
     * an identity, and an identity is the more specific answer. A name that
     * resolves to nothing falls through to the glyph, which is how an app that
     * shipped no icon.png still gets a picture.
     *
     * Source first, alignment second, and the order is the whole thing:
     * LV_IMAGE_ALIGN_STRETCH works its factor out from the source it can see
     * when it is set, so asking for it before there is one scales the picture
     * by nothing and draws a blank. */
    const void *app_img = catnip_app_icon_find(image);
    if (app_img) {
        lv_anim_delete(img, NULL);
        lv_obj_set_style_translate_y(img, 0, 0);
        /* Drawn at the size it was made, not scaled to a box. An app icon is
         * 70 px and the region has room for it, so scaling buys nothing and
         * costs a transform on every draw - and a transform is the one thing
         * between "the decoder accepted it" and "it is on the glass" that has
         * no way to report that it did nothing. */
        lv_obj_set_size(img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_DEFAULT);
        lv_image_set_scale(img, 256);
        lv_image_set_src(img, app_img);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    } else if (icon == CATNIP_ICON_MASCOT) {
        /* The source is the 320x240 splash and the content region is shorter
         * than that, so it is drawn at half size in a box that fits. At its
         * own size LVGL clips it to the row rather than shrinking it, which
         * is a cat nobody can see. */
        /* The panel's own size: the mascot is the landing page, not a picture
         * on it, and the frame's bar sits over it on the top layer.
         *
         * Nudged down so the cat clears the bar rather than wearing it. It is a
         * translation and not padding, so the image keeps its full size and the
         * layout does not have to make room that is not there; the cost is the
         * same number of pixels off the bottom, where the picture has margin to
         * spare and the top does not. */
        lv_obj_set_size(img, CATNIP_SPLASH_W, CATNIP_SPLASH_H);
        lv_obj_set_style_translate_y(img, kMascotDropPx, 0);
        lv_animimg_set_src(img, catnip_mascot_anim, CATNIP_MASCOT_FRAMES);
        lv_animimg_set_duration(img, CATNIP_MASCOT_FRAME_MS * CATNIP_MASCOT_FRAMES);
        lv_animimg_set_repeat_count(img, LV_ANIM_REPEAT_INFINITE);
        lv_animimg_start(img);
        /* STRETCH rather than a scale factor: lv_image_set_scale() turns about
         * the pivot, which defaults to the middle of the *source*, so halving a
         * 320x240 image inside a 160x120 box threw the result outside the box
         * and it was clipped away entirely. This asks LVGL to fit it instead,
         * and there is no pivot to get wrong. */
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    } else if (icon >= CATNIP_ICON_FOLDER && icon <= CATNIP_ICON_CLOSE) {
        /* The same twelve shapes at whichever size the shape on screen calls
         * for: beside a word in a row, alone in the middle of a carousel. */
        lv_obj_set_size(img, big ? 64 : 14, big ? 64 : 14);
        /* The box is the source's own size, so nothing is scaled; said out
         * loud because the same object may have been the mascot a moment ago -
         * and if it was, its animation is still running and would keep putting
         * the cat back. */
        lv_anim_delete(img, NULL);
        lv_obj_set_style_translate_y(img, 0, 0);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_DEFAULT);
        lv_image_set_src(img, big ? &catnip_icon_img_64[icon - CATNIP_ICON_FOLDER]
                                  : &catnip_icon_img_14[icon - CATNIP_ICON_FOLDER]);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
}

/* The quantity a row stands for: a bar whose height is the number, and the
 * reading printed above it.
 *
 * Both widgets are made the first time a row turns out to have a value and are
 * then kept, which is the same bargain the rest of this backend makes - a
 * widget is expensive to create and cheap to hide. They are moved to the front
 * of the row because the flex flow draws children in order and the reading
 * belongs above the bar, above the icon, above the name.
 *
 * A vertical bar rather than a horizontal one: five of them side by side can be
 * compared at a glance, and the touch target becomes a column the height of the
 * region instead of a line of text. */
/* `active` is the column being edited rather than merely the one under the
 * ring - two states, because on a settings page choosing which setting and
 * changing it are separate acts (see catnip_settings.c). It arrives as the
 * `primary` style role rather than as a field of its own: the roles are already
 * the vocabulary for "this one matters more than its neighbours", and a second
 * way of saying it would be a second thing to keep in step. */
void apply_value(Entry *e, int value, const char *value_text, int steps, bool active)
{
    if (!e->row) return;
    if (value < 0) {
        if (e->bar) lv_obj_add_flag(e->bar, LV_OBJ_FLAG_HIDDEN);
        if (e->val) lv_obj_add_flag(e->val, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!e->bar) {
        e->val = lv_label_create(e->obj);
        e->bar = lv_obj_create(e->obj);
        if (!e->val || !e->bar) {
            /* Half of it is worse than none: a bar with no reading is a
             * quantity nobody can name. Whatever was made is left hidden. */
            if (e->val) lv_obj_add_flag(e->val, LV_OBJ_FLAG_HIDDEN);
            if (e->bar) lv_obj_add_flag(e->bar, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_remove_flag(e->val, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(e->bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(e->bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_to_index(e->val, 0);
        lv_obj_move_to_index(e->bar, 1);

        lv_obj_set_width(e->bar, 26);
        lv_obj_set_flex_grow(e->bar, 1);
        lv_obj_set_style_bg_opa(e->bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(e->bar, 0, 0);
        lv_obj_set_style_pad_all(e->bar, 0, 0);
        lv_obj_set_style_pad_row(e->bar, 3, 0);
        lv_obj_set_style_text_color(e->val, lv_color_hex(kColText), 0);
    }
    lv_obj_remove_flag(e->bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(e->val, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(e->val, value_text);

    /* A quantity with rungs is drawn as that many blocks; one without is drawn
     * as a filled track, because those are two different promises and a finger
     * is about to be dragged up one of them. `steps` is 0 for the continuous
     * kind, which is one block whose height is the value rather than one block
     * per rung. */
    bool ladder = steps > 1;
    /* A quantity grows upward, so the fill is anchored to the bottom.
     *
     * Two mechanisms rather than one, and deliberately not flex-with-a-reversed
     * flow: the blocks are laid out top to bottom and lit from the end, and the
     * single fill of a continuous track is aligned to the bottom with no layout
     * at all. Both are stated in terms of where things end up rather than in
     * terms of which way a flow runs, because "reversed" is a property of the
     * container that has to be read together with the alignment to know what it
     * means - and read wrongly once already, which is how this bar came out
     * upside down. */
    lv_obj_set_layout(e->bar, ladder ? LV_LAYOUT_FLEX : LV_LAYOUT_NONE);
    if (ladder) lv_obj_set_flex_flow(e->bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(e->bar, lv_color_hex(kColFaint), 0);
    lv_obj_set_style_bg_opa(e->bar, ladder ? LV_OPA_TRANSP : LV_OPA_20, 0);
    lv_obj_set_style_radius(e->bar, ladder ? 0 : 6, 0);

    int want = ladder ? steps : 1;
    if ((int)lv_obj_get_child_count(e->bar) != want) {
        lv_obj_clean(e->bar);
        for (int i = 0; i < want; i++) {
            lv_obj_t *blk = lv_obj_create(e->bar);
            if (!blk) break;
            lv_obj_remove_flag(blk, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(blk, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_width(blk, LV_PCT(100));
            lv_obj_set_flex_grow(blk, 1);
            lv_obj_set_style_border_width(blk, 0, 0);
            lv_obj_set_style_radius(blk, 3, 0);
            lv_obj_set_style_bg_opa(blk, LV_OPA_COVER, 0);
        }
    }

    /* The rung the value stands on, counting the bottom one as lit: `value` is
     * the position along the ladder, so rung 0 of five is 0 and lights one
     * block, and rung 4 is 100 and lights all five. Nothing is ever unlit
     * entirely - a column with no blocks would read as a broken widget rather
     * than as the lowest setting. */
    uint32_t n = lv_obj_get_child_count(e->bar);
    uint32_t ink = active ? kColPrimary : kColText;
    if (!ladder) {
        lv_obj_t *blk = lv_obj_get_child(e->bar, 0);
        if (blk) {
            /* Never quite nothing: a track with no fill at all reads as a
             * broken widget rather than as the lowest setting. */
            lv_obj_set_flex_grow(blk, 0);
            lv_obj_set_size(blk, LV_PCT(100), LV_PCT(value < 3 ? 3 : value));
            lv_obj_align(blk, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_radius(blk, 6, 0);
            lv_obj_set_style_bg_color(blk, lv_color_hex(ink), 0);
            lv_obj_set_style_bg_opa(blk, LV_OPA_COVER, 0);
        }
        return;
    }
    /* No forced minimum: a ladder that reaches Off has to be able to look
     * empty, and one that does not never asks for zero in the first place -
     * catnip_settings.c gives its bottom rung a fill of one block instead. */
    int lit = ((int)n * value + 50) / 100;
    if (lit > (int)n) lit = (int)n;
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *blk = lv_obj_get_child(e->bar, (int32_t)i);
        /* Lit from the end, because the children run down the column and the
         * value climbs up it. */
        bool on = ((int)i >= (int)n - lit);
        lv_obj_set_style_bg_color(blk, lv_color_hex(on ? ink : kColFaint), 0);
        lv_obj_set_style_bg_opa(blk, on ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

/* How far up `e`'s blocks the y coordinate is: 0 at the bottom, 100 at the top.
 *
 * Measured against the blocks and not the whole column, because the reading and
 * the name are inside the column too, and a finger on the word "Screen" asking
 * for 0% is not what anyone meant. */
int mixer_pct_of(Entry *e, int y)
{
    lv_area_t bar;
    lv_obj_get_coords(e->bar, &bar);
    int h_px = bar.y2 - bar.y1;
    if (h_px <= 0) return -1;
    int up = bar.y2 - y; /* from the bottom, which is where 0 is */
    if (up < 0) up = 0;
    if (up > h_px) up = h_px;
    return up * 100 / h_px;
}

/* A drawn column of a mixer, or nullptr. */
Entry *mixer_column(catnip_handle h)
{
    Entry *e = map_find(h);
    if (!e || !e->used || !e->row || !e->bar) return nullptr;
    Entry *p = map_find(e->parent);
    if (!p || p->layout != CATNIP_LAYOUT_MIXER) return nullptr;
    if (lv_obj_has_flag(e->obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    return e;
}

} // namespace

int catnip_lvgl_backend_mixer_at(int x, int y, catnip_handle *h, int *pct)
{
    for (int i = 0; i < kMaxObjects; i++) {
        Entry *e = mixer_column(g_map[i].h);
        if (!e) continue;

        lv_area_t col;
        lv_obj_get_coords(e->obj, &col);
        if (x < col.x1 || x > col.x2) continue;

        int p = mixer_pct_of(e, y);
        if (p < 0) continue;
        if (h) *h = e->h;
        if (pct) *pct = p;
        return 1;
    }
    return 0;
}

int catnip_lvgl_backend_mixer_pct(catnip_handle h, int y)
{
    Entry *e = mixer_column(h);
    return e ? mixer_pct_of(e, y) : -1;
}

namespace {

void apply_flags(Entry *e, unsigned flags)
{
    /* LV_OBJ_FLAG_HIDDEN takes the object out of the flex layout as well as out
     * of the picture, so a hidden status line leaves no gap where it was -
     * which is what the File Browser's `status.hidden` is asking for. */
    if (flags & CATNIP_NODE_HIDDEN) lv_obj_add_flag(e->obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(e->obj, LV_OBJ_FLAG_HIDDEN);

    bool off = (flags & CATNIP_NODE_DISABLED) != 0;
    if (off) lv_obj_add_state(e->obj, LV_STATE_DISABLED);
    else lv_obj_remove_state(e->obj, LV_STATE_DISABLED);
    /* LV_STATE_DISABLED changes nothing about an image on its own, and a row
     * whose whole content is a picture would look exactly like a usable one.
     * Said here so every kind dims the same way. */
    lv_obj_set_style_image_opa(e->obj, off ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(e->obj, off ? LV_OPA_50 : LV_OPA_COVER, 0);
}

/* The whole descriptor, every time, with no second diff. The renderer only
 * calls update() when something genuinely differs and does not say what, and
 * re-deriving that here would be the same comparison done twice - once against
 * a cache that is right and once against LVGL's state, which is not the same
 * thing and would eventually disagree. */
void apply_desc(Entry *e, const catnip_node_desc *d)
{
    apply_text(e, d->text, d->icon, d->image);
    apply_value(e, d->value, d->value_text, d->steps, d->style == CATNIP_STYLE_PRIMARY);
    apply_style(e, d->style);
    apply_flags(e, d->flags);
    if (e->kind == CATNIP_NODE_LIST) {
        if (e->layout != d->layout) {
            e->layout = d->layout;
            apply_list_layout(e);
        }
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
/* Bring `child` inside `list`'s content area, moving as little as possible.
 *
 * lv_obj_scroll_to_view() would do this, and it is not used: it refuses on a
 * parent without LV_OBJ_FLAG_SCROLLABLE, and that flag is off precisely so a
 * finger cannot scroll the list. lv_obj_scroll_by() does not consult it, so
 * the scroll offset remains the platform's to set while the finger's drag
 * means something else entirely.
 *
 * Moving as little as possible is what keeps the position: a row already on
 * screen moves nothing, so stepping through the middle of a long list does not
 * jump the view. */
void scroll_into_view(lv_obj_t *list, lv_obj_t *child)
{
    lv_area_t content, row;
    lv_obj_get_content_coords(list, &content);
    lv_obj_get_coords(child, &row);

    int32_t dy = 0;
    if (row.y1 < content.y1) dy = row.y1 - content.y1;
    else if (row.y2 > content.y2) dy = row.y2 - content.y2;
    /* Negated: scrolling by a negative dy moves the content up, which is what
     * brings a row that is below the window into it. */
    if (dy) lv_obj_scroll_by(list, 0, -dy, LV_ANIM_OFF);

    /* And the same along x, because a mixer lays its children out across the
     * region rather than down it. One function for both axes rather than two:
     * "move as little as possible to bring the selection into the window" is
     * the same rule whichever way the list runs, and a second copy of it would
     * be a second place for it to drift. */
    int32_t dx = 0;
    if (row.x1 < content.x1) dx = row.x1 - content.x1;
    else if (row.x2 > content.x2) dx = row.x2 - content.x2;
    if (dx) lv_obj_scroll_by(list, -dx, 0, LV_ANIM_OFF);
}

/* A list's own arrangement. Rows stack and scroll; a carousel centres one child
 * in the whole region and the others are simply not shown - stepping it is a
 * change of `selected` and nothing else, which is why it needs no second node
 * kind and no second event. */
void apply_list_layout(Entry *e)
{
    bool carousel = e->layout == CATNIP_LAYOUT_CAROUSEL;
    bool mixer = e->layout == CATNIP_LAYOUT_MIXER;

    /* Rows and a carousel run down the region; a mixer runs across it. */
    lv_obj_set_flex_flow(e->obj, mixer ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        e->obj, (carousel || mixer) ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(e->obj, (carousel || mixer) ? 0 : 1, 0);
    lv_obj_set_style_pad_all(e->obj, carousel ? 0 : 2, 0);

    /* A carousel takes the whole panel and the bar floats over it, where a
     * column starts below the bar. The screen is the platform's either way, so
     * the layout that knows which shape it is, is the thing that says so:
     * reserving room for the bar and then centring a full-screen mascot in what
     * was left would put the cat low and crop it. */
    Entry *screen = map_find(e->parent);
    if (screen && screen->kind == CATNIP_NODE_SCREEN) {
        lv_obj_set_style_pad_top(screen->obj, carousel ? 0 : CATNIP_FRAME_BAR_H + 4, 0);
        lv_obj_set_style_pad_bottom(screen->obj, carousel ? 0 : 6, 0);
        lv_obj_set_style_pad_left(screen->obj, carousel ? 0 : 6, 0);
        lv_obj_set_style_pad_right(screen->obj, carousel ? 0 : 6, 0);
    }
    e->sel_dirty = true;
}

void apply_selection(Entry *e)
{
    uint32_t n = lv_obj_get_child_count(e->obj);

    bool carousel = e->layout == CATNIP_LAYOUT_CAROUSEL;

    e->sel_dirty = false;
    lv_obj_update_layout(e->obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(e->obj, i);
        bool on = ((int)i == e->selected);

        if (carousel) {
            /* Nothing to contrast with, so nothing is highlighted: the one
             * child that is shown is the selection. */
            if (on) lv_obj_remove_flag(child, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(child, LV_STATE_CHECKED);
            continue;
        }
        if (on) {
            lv_obj_add_state(child, LV_STATE_CHECKED);
            scroll_into_view(e->obj, child);
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
    if (g_rt) catnip_render_post(g_rt, h, "click", CATNIP_INDEX_NONE);
}

/* A tap on a row. The row has no handlers of its own and never gets any - it is
 * a label, and making forty-five of them focusable is what the design refused -
 * so the event is addressed to the row's *list*, carrying which row it was. The
 * list is the thing with a selection and a handler, and the index is the only
 * thing it cannot work out for itself.
 *
 * The row's own handle is not used and does not need to exist: what LVGL
 * carries here is the parent's, stored on the row when it was styled as one, so
 * a tap costs no lookup. */
void on_row_clicked(lv_event_t *ev)
{
    lv_obj_t *row = lv_event_get_target_obj(ev);
    lv_obj_t *list = lv_obj_get_parent(row);
    if (!list || !g_rt) return;
    catnip_handle h = (catnip_handle)(intptr_t)lv_obj_get_user_data(list);
    catnip_render_post(g_rt, h, "click", (int)lv_obj_get_index(row));
}

/* A vertical stack with room to breathe. Shared by the screen and the list,
 * because they are the same thing at two scales. */
void make_column(lv_obj_t *obj, int pad)
{
    lv_obj_set_style_pad_all(obj, pad, 0);
    lv_obj_set_style_pad_row(obj, pad, 0);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    /* Nothing scrolls under a finger. A drag is one of the joystick's four
     * directions (swipe.h), and if LVGL scrolled the viewport as well, the
     * selection would be left off-screen behind it - which breaks the one rule
     * the scrolling model rests on: the selection leads and the view follows.
     * The scroll offset is still ours to set, because lv_obj_scroll_by() does
     * not consult this flag; only the indev does. */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *make_screen(void)
{
    lv_obj_t *obj = lv_obj_create(nullptr);

    if (!obj) return nullptr;
    lv_obj_set_style_bg_color(obj, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    make_column(obj, 6);
    /* Room for the frame's bar, which is drawn on the top layer above every
     * screen. The number is the frame's, so the two cannot drift apart. */
    lv_obj_set_style_pad_top(obj, CATNIP_FRAME_BAR_H + 4, 0);
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

/* A list row is one node in the tree and two widgets on the panel: a colour
 * image and the label. The node model has no horizontal container; this is
 * only how the backend draws `[icon] [name]`. */
lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *img;
    lv_obj_t *label;

    if (!row) return nullptr;
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* An animimg rather than an image: its base class *is* lv_image, so every
     * static icon still goes through lv_image_set_src() and nothing else here
     * changes. Only the landing mascot ever asks it to move. */
    img = lv_animimg_create(row);
    if (!img) {
        lv_obj_delete(row);
        return nullptr;
    }
    lv_obj_set_size(img, 14, 14);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

    label = lv_label_create(row);
    if (!label) {
        lv_obj_delete(row);
        return nullptr;
    }
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return row;
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
    /* Tall enough to hit with a finger rather than tight around the text. It is
     * padding and not a height so that the row grows with whatever font a style
     * role picks and the text stays centred in it either way; a fixed height
     * would pin the text to the top of the box the moment the font changed.
     * The app cannot ask for a row height - this is geometry, and geometry is
     * the platform's - so it is decided once, here.
     *
     * The full width matters as much as the height: a row only as wide as its
     * text leaves most of the line a dead zone that looks tappable. */
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_pad_ver(obj, 9, 0);
    lv_obj_set_style_pad_left(obj, 6, 0);
    lv_obj_set_style_pad_right(obj, 6, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_STATE_CHECKED);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(obj, lv_color_hex(kColPrimary), LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_CHECKED);
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
    default:
        if (parent_entry && parent_entry->kind == CATNIP_NODE_LIST)
            obj = make_row(parent_obj);
        else obj = make_label(parent_obj);
        break;
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
    e->row = parent_entry && parent_entry->kind == CATNIP_NODE_LIST &&
             d->kind == CATNIP_NODE_LABEL;
    if (e->row) {
        /* The two widgets make_row() built, remembered here so nothing later
         * has to know what order they ended up in. */
        e->img = lv_obj_get_child(obj, 0);
        e->name = lv_obj_get_child(obj, 1);
    }

    if (parent_entry && parent_entry->kind == CATNIP_NODE_LIST) {
        style_as_row(obj);
        /* A row is a touch target even though it is not focusable: a finger can
         * name a row directly, where the joystick can only step to it. That
         * asymmetry is the whole reason an event carries an index. */
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, on_row_clicked, LV_EVENT_CLICKED, nullptr);
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

    /* If the ring was on this object, forget it before the object goes, so a
     * later focus move does not try to take the ring off a freed object. */
    if (h == g_focused) g_focused = CATNIP_HANDLE_NONE;

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

void catnip_lvgl_backend_focus(catnip_handle h)
{
    /* Move the ring only when the focused handle actually changes: an unchanged
     * pass returns at once rather than searching the map twice. A destroyed
     * object takes its state with it and be_destroy() clears g_focused when it is
     * the one that goes, so nothing here ever paints a ring onto a reused slot. */
    if (h == g_focused) return;
    Entry *was = map_find(g_focused);
    if (was) lv_obj_remove_state(was->obj, LV_STATE_FOCUSED);
    Entry *now = map_find(h);
    if (now) lv_obj_add_state(now->obj, LV_STATE_FOCUSED);
    g_focused = h;
}

catnip_handle catnip_lvgl_backend_focused(void)
{
    return g_focused;
}
