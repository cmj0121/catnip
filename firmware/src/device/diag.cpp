/* diag.cpp - see diag.h. */
#include <Arduino.h>
#include <SD_MMC.h>

#include <esp_heap_caps.h>
#include <lvgl.h>

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "diag.h"

#include "catnip_font.h"
#include "diag_layout.h"
#include "display.h"
#include "imu.h"
#include "input.h"
#include "lvgl_port.h"
#include "touch.h"
#include "touch_debug.h" /* the raw pair, shown beside the mapped one */

/*
 * HOW THIS DRAWS.
 *
 * In LVGL objects, one per thing on the screen, through the display that
 * lvgl_port.cpp bound to the panel. It did not always: this page was written
 * before LVGL was in the firmware and composed its own frame in an
 * LGFX_Sprite, which it handed to catnip_display_blit(). What follows is why
 * that was right then and why this is right now, because the two answers are
 * different and the difference is worth keeping.
 *
 * display.h offers a raw blit and backlight control and says in its own
 * comment that anything richer belongs to the renderer. A D-pad needs
 * rectangles and text, and with no renderer in the firmware yet the choice was
 * to widen display.h with primitives or to compose a frame here. Widening it
 * was the worse of the two: rectangles alone do not draw a D-pad, the labels
 * need text, text needs a font and a size and a colour and an alignment, and
 * by the time display.h could draw "OK" centred in a box it would have been a
 * renderer with a different name. So the page composed.
 *
 * The renderer has now arrived. LVGL is the thing that owns fonts and
 * alignment and knows which rectangles changed, display.h's contract survived
 * unwidened - lvgl_port.cpp's flush callback hands whole screens to the same
 * catnip_display_blit() this page used to call - and the page's second drawing
 * path, accepted at the time as the price of independence, is no longer being
 * paid. What the page draws is now the same thing every other screen in this
 * firmware will draw with.
 *
 * WHAT THAT COSTS, AND WHAT WAS PUT BACK. The page's whole claim was that it
 * had nothing in the way: that is how it caught its own colours being
 * byte-swapped and how it settled the touch rotation. Built out of LVGL
 * objects it can no longer answer "is the display path itself working",
 * because a broken LVGL and a dead panel look the same from a chair - both are
 * a screen with nothing on it. panel_check() below is what answers that
 * instead, and it is deliberately the smallest thing that can: a loop, a
 * buffer, and catnip_display_blit().
 *
 * The guard against this page growing into a rival UI is unchanged: it stays
 * feature-free.
 *
 * WHY THE GEOMETRY IS STILL diag_layout.c's. Every box is positioned from
 * catnip_diag_box(), the same table that catnip_diag_box_at() answers taps
 * from and that test_diag_layout.c pins. Re-expressing the layout in LVGL's
 * alignment and flex would give the page two descriptions of where the boxes
 * are, and the one that decides which box a finger is in would be the one
 * nobody was looking at.
 */

namespace {

/* Green for a switch, cyan for the glass. The status line names the path in
 * words as well; the colour is what makes a press and a tap on the same box
 * distinguishable at a glance, from across a desk.
 *
 * These are 24-bit values because that is what LVGL takes, and each one is the
 * 24-bit form that converts back to exactly the RGB565 this page drew before -
 * the RGB565 is in the comment so the two can be checked against each other.
 * LVGL does the conversion at render time and lands on the same sixteen bits
 * that used to be written here by hand. */
const uint32_t kColBg = 0x000000;     /* 0x0000 black */
const uint32_t kColEdge = 0xFFFFFF;   /* 0xFFFF white */
const uint32_t kColDark = 0x212421;   /* 0x2124 an unlit box */
const uint32_t kColButton = 0x00FF00; /* 0x07E0 lit by a switch */
const uint32_t kColTouch = 0x00FFFF;  /* 0x07FF lit by a finger */
const uint32_t kColText = 0xFFFFFF;   /* 0xFFFF the text rows */
const uint32_t kColFaint = 0x7B7D7B;  /* 0x7BEF the footnotes */
const uint32_t kColMarker = 0xFF0000; /* 0xF800 the marker: red on all of the above */
const uint32_t kColUp = 0xFFFF00;     /* 0xFFE0 the up arrow: yellow, its own */

/* Text rows, chosen around the box block that diag_layout.c lays out between
 * y=40 and y=183.
 *
 * The page answers two questions, and each gets a headline in words and its
 * numbers in fine print. The words are the big font in both cases. That is the
 * wrong way round for a page of numbers and the right way round for this one:
 * what is being asked is "is the marker where my finger is" and "does the
 * arrow point at the ceiling", and a phrase answers each of those outright
 * while the coordinates and the axes are only how it was arrived at.
 *
 * The touch coordinates gave up a row of their own to make space for the
 * second question, and now sit to the right of their own headline. Nothing was
 * lost: they were already the fine print by this file's own account. */
const int kRowQuad = 2;    /* which quarter of the screen the marker is in */
const int kRowTouch = 6;   /* the raw and mapped coordinates behind it */
const int kRowUp = 22;     /* which screen edge the IMU says is up */
const int kRowKey = 186;   /* the colour key and the two path colours */
const int kRowEvent = 202; /* the last event and the path that delivered it */
const int kRowNote = 222;
const int kRowImu = 231; /* the identity and the axes behind the up arrow */

/* Where the touch coordinates start, to the right of the MARKER headline. Far
 * enough that the longest quadrant name does not run into them, and near
 * enough that the longest coordinate line still ends on the screen. */
const int kColTouchX = 170;

/* The colour key's swatches. */
const int kSwatchW = 14;
const int kSwatchH = 12;

/* The two sizes the page has always had, in LVGL's fonts. Montserrat 14 stands
 * in for LovyanGFX's 16-pixel Font2 and Montserrat 10 for its 8-pixel Font0.
 * They are not the same typeface - no LVGL font is - but the page never
 * depended on the shapes, only on there being an answer in a size that reads
 * from a chair and its working in a size that does not compete with it. */
#define DIAG_FONT_BIG   &catnip_font_16
#define DIAG_FONT_SMALL &catnip_font_10

lv_obj_t *g_screen;
lv_obj_t *g_box[CATNIP_BTN_COUNT];
lv_obj_t *g_box_label[CATNIP_BTN_COUNT];
lv_obj_t *g_lbl_quad;
lv_obj_t *g_lbl_touch;
lv_obj_t *g_lbl_up;
lv_obj_t *g_lbl_event;
lv_obj_t *g_lbl_imu;
/* The marker and the arrow, which are not widgets - see build_overlay(). */
lv_obj_t *g_overlay;

bool g_active = false;

/* What the page last put on screen, so a pass that changed nothing costs
 * nothing. LVGL only repaints what has been invalidated, but setting a label's
 * text or a box's colour invalidates it whether or not the value differs, and
 * the display renders whole screens (see lvgl_port.cpp), so a page that told
 * LVGL about every poll would push a 150 KB frame twenty times a second. That
 * would show up as buttons lighting late, and would be blamed on the very
 * switches this page exists to vindicate. */
struct Shown {
    uint8_t down_mask;
    int touch_box;
    bool touch_down;
    bool have_screen;
    bool have_panel;
    uint16_t sx, sy, px, py;
    /* The acceleration, rounded to kMgStep before it is stored. A resting
     * accelerometer's last few milli-g never stop moving, so keeping the raw
     * value here would make every comparison below differ. The rounding is
     * done once, here, so that what is compared and what is printed are the
     * same numbers and cannot disagree. */
    int32_t mg[3];
    bool have_accel;
    /* What catnip_imu_begin() found, carried here so that the footer says the
     * same thing the arrow does and both come from one snapshot. `imu_ready`
     * is fixed for the run - the driver reports nothing for the rest of it
     * once identification has failed - but it is compared with everything
     * else rather than read from a global, so that the update has exactly one
     * source of truth to draw from. */
    bool imu_ready;
    bool imu_have_who;
    uint8_t imu_who;
    /* INTERNAL_STATUS after the configuration upload, and whether it was read
     * at all. A BMI270 that never reaches init_ok answers its identity
     * register perfectly and produces no data, so without this the page could
     * only say "it refused its setup" about a part whose bus transactions were
     * all acknowledged - which would send the owner looking at the wiring. */
    bool imu_have_status;
    uint8_t imu_status;
    /* A row in imu_map.c's table, or null when no edge is named. Comparing the
     * pointer compares the attitude: the rows are static and there is exactly
     * one of them per half-axis. */
    const catnip_imu_up *up;
    char event[48];
};

/* The one snapshot everything on screen is drawn from. It is a file-scope
 * variable rather than an argument because LVGL renders later than it is told
 * to: apply() runs in the main loop and the overlay's draw callback runs
 * inside lv_timer_handler(), so a pointer handed to apply() would be no use to
 * the callback. Both read this, and it is updated before either. */
Shown g_shown;
Shown g_now;

/* Whether the IMU's INTERNAL_STATUS says its configuration image was accepted.
 * Only the low nibble is the message; the bits above it are separate error
 * flags, and comparing the whole byte would call a working part broken the
 * moment one of them was set. */
bool imu_init_ok(const Shown *s)
{
    return s->imu_have_status &&
           (s->imu_status & CATNIP_IMU_INIT_MSG_MASK) == CATNIP_IMU_INIT_OK;
}

/* The step the acceleration is rounded to, in milli-g. 50 mg is far coarser
 * than the part's noise at rest, so a still device holds a still number, and
 * far finer than the 500 mg that decides which edge is up, so nothing the
 * answer depends on is lost. */
const int32_t kMgStep = 50;

/* Rounded to the nearest step, away from zero on a tie, so that the two signs
 * are treated alike. C's integer division truncates toward zero, which would
 * otherwise round -0.99g and +0.99g to different magnitudes and make an axis
 * appear to change size when the device is turned end for end. */
int32_t round_mg(int32_t mg)
{
    int32_t half = kMgStep / 2;

    return ((mg + (mg < 0 ? -half : half)) / kMgStep) * kMgStep;
}

/* Whether catnip_imu_begin() identified the part. Fixed for the run: the
 * driver reports nothing for the rest of it once identification has failed,
 * exactly as the touch driver does. */
bool g_imu_ready = false;

/* The most recent event, in the page's own words: which switch, and which of
 * the two paths delivered it. */
char g_event[48] = "waiting for a press or a tap";

void note(const char *what, const char *path, bool down)
{
    snprintf(g_event, sizeof(g_event), "%s (%s) %s", what, path, down ? "down" : "up");
}

/* An LVGL object stripped back to a plain rectangle at exact coordinates.
 *
 * lv_obj_create() gives its object the default theme's card: rounded corners,
 * padding, a border, a background and a scrollbar. Every one of those would
 * move a box away from the pixel diag_layout.c put it on, or draw something
 * over the edge of it, and the page's whole job is that a box is where the
 * layout says it is. */
lv_obj_t *plain_rect(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* A text row at a fixed top-left, in one of the page's two sizes.
 *
 * The width is clipped to the rest of the screen rather than left to grow with
 * the text. An LVGL label sized to its content wraps when it runs out of room,
 * and a row that wrapped would push itself into the row below and rearrange
 * the page - which on a diagnostic reads as the fault rather than as the
 * report of it. Clipping loses the end of an over-long line and keeps every
 * other row where it belongs. */
lv_obj_t *make_label(int x, int y, const lv_font_t *font, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(g_screen);

    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, CATNIP_SCREEN_W - x);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, "");
    return l;
}

void build_boxes(void)
{
    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        const catnip_diag_rect *r = catnip_diag_box((catnip_button)b);

        g_box[b] = plain_rect(g_screen, r->x, r->y, r->w, r->h);
        lv_obj_set_style_border_color(g_box[b], lv_color_hex(kColEdge), 0);
        lv_obj_set_style_border_width(g_box[b], 1, 0);

        g_box_label[b] = lv_label_create(g_box[b]);
        lv_obj_remove_style_all(g_box_label[b]);
        lv_obj_set_style_text_font(g_box_label[b], DIAG_FONT_BIG, 0);
        lv_label_set_text(g_box_label[b], catnip_diag_label((catnip_button)b));
        lv_obj_center(g_box_label[b]);
    }
}

/* Three named swatches, so that a colour fault is legible on the screen
 * instead of being deduced from someone describing a crosshair over a cable.
 *
 * The page ran with every colour byte-swapped and nobody could say so directly:
 * what was reported was "the red crosshair looks blue", and the byte order had
 * to be worked back out from the arithmetic. A swatch labelled "red" that is
 * not red says it in one glance, and it says it for whatever the next colour
 * fault turns out to be as well. Making the invisible visible is this page's
 * entire job and it could not do it for its own pixels.
 *
 * It earns its place again on LVGL, which has its own idea of colour depth and
 * byte order and therefore its own way to get this wrong - see the comment in
 * src/lv_conf.h. The swatches are built from the same three constants as
 * before, so they are testing the new path with the old question.
 *
 * The horizontal spacing is still six pixels a character, which was the old
 * font's fixed cell. Montserrat is proportional and narrower than that on
 * average, so the names sit where they used to with a little more air after
 * them; the swatches themselves are unmoved. */
void build_colour_key(void)
{
    static const struct {
        uint32_t colour;
        const char *name;
    } kKey[] = {
        {0xFF0000, "red"},
        {0x00FF00, "green"},
        {0x0000FF, "blue"},
    };
    int x = 8;

    for (size_t i = 0; i < sizeof(kKey) / sizeof(kKey[0]); i++) {
        lv_obj_t *swatch = plain_rect(g_screen, x, kRowKey, kSwatchW, kSwatchH);
        lv_obj_t *name;

        lv_obj_set_style_bg_color(swatch, lv_color_hex(kKey[i].colour), 0);
        lv_obj_set_style_border_color(swatch, lv_color_hex(kColEdge), 0);
        lv_obj_set_style_border_width(swatch, 1, 0);

        name = make_label(x + kSwatchW + 4, kRowKey + 2, DIAG_FONT_SMALL, kColText);
        lv_label_set_text(name, kKey[i].name);
        x += kSwatchW + 4 + (int)strlen(kKey[i].name) * 6 + 10;
    }

    /* What the box colours mean, on the same row, since it is the same
     * question: which colour is that, and what is it telling me. */
    lv_label_set_text(make_label(164, kRowKey + 2, DIAG_FONT_SMALL, kColFaint),
                      "switch=green  touch=cyan");
}

/* The crosshair at the mapped touch position, drawn wherever the finger is and
 * not only inside a box.
 *
 * This is the page's reason for existing, and it has already paid for itself.
 * Rotation 3 admits two mappings, and near the middle of the panel - where
 * every reading available when catnip_touch_panel_to_screen() was written
 * happened to land - they agree closely enough to be indistinguishable. Near
 * the edges they do not: the wrong one puts the marker through a 180 degree
 * turn about the centre of the screen. A finger dragged into the top-left
 * corner put the marker under the fingertip and made the page say
 * "MARKER: top-left", which settled it by eye in one touch, with no capture to
 * read back and no arithmetic afterwards. The same drag re-checks it on
 * another unit.
 *
 * The line from the middle of the screen out to the crosshair is not
 * decoration. The crosshair alone asks the owner to compare two things - where
 * the marker is and where their finger is - and asked that question twice they
 * answered about its colour twice, which says the comparison was not being
 * made. A line from a fixed point turns it into one thing to look at: it
 * points away from the fingertip when the rotation is wrong, and the quadrant
 * printed at the top of the screen says the same in words. */
void draw_marker(lv_layer_t *layer, int x, int y)
{
    const int arm = 12;
    lv_draw_line_dsc_t dsc;
    lv_draw_arc_dsc_t ring;

    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(kColMarker);
    dsc.width = 1;

    dsc.p1.x = CATNIP_SCREEN_W / 2;
    dsc.p1.y = CATNIP_SCREEN_H / 2;
    dsc.p2.x = x;
    dsc.p2.y = y;
    lv_draw_line(layer, &dsc);

    dsc.p1.x = x - arm;
    dsc.p1.y = y;
    dsc.p2.x = x + arm;
    dsc.p2.y = y;
    lv_draw_line(layer, &dsc);

    dsc.p1.x = x;
    dsc.p1.y = y - arm;
    dsc.p2.x = x;
    dsc.p2.y = y + arm;
    lv_draw_line(layer, &dsc);

    lv_draw_arc_dsc_init(&ring);
    ring.color = lv_color_hex(kColMarker);
    ring.width = 1;
    ring.radius = 6;
    ring.center.x = x;
    ring.center.y = y;
    ring.start_angle = 0;
    ring.end_angle = 360;
    lv_draw_arc(layer, &ring);
}

/* The arrow, from the centre of the screen out toward whichever edge the
 * firmware believes is up.
 *
 * This is how the axis mapping was settled, and it is the same move that
 * settled the touch rotation in one drag after two attempts at inferring it
 * from serial output had failed. Held with each screen edge in turn toward the
 * ceiling, the arrow pointed at the ceiling all four times, which is what makes
 * imu_map.c's four horizontal rows a measurement rather than a prediction - and
 * it was visible without reading a number, reporting a value, or anyone
 * interpreting a log line over a link that drops lines. The same four turns
 * re-check the table on any other unit.
 *
 * It cannot settle the two flat attitudes, and that is the limit of the method
 * rather than an oversight: they have no edge up, so this function is never
 * called for them. Those are read off the headline instead.
 *
 * The direction comes out of the same table row as the edge's name, so the
 * arrow and the word cannot disagree. Both components are never zero here: the
 * two flat attitudes have no edge up, and the caller names them in words
 * instead of pointing the arrow somewhere arbitrary. */
void draw_up_arrow(lv_layer_t *layer, int dx, int dy)
{
    const int len = 60;
    const int head = 18;
    const int cx = CATNIP_SCREEN_W / 2;
    const int cy = CATNIP_SCREEN_H / 2;
    int tx = cx + dx * len;
    int ty = cy + dy * len;
    /* The perpendicular, which serves twice: it thickens the shaft, and it is
     * what the head's two base corners are offset along. */
    int px = -dy;
    int py = dx;
    lv_draw_line_dsc_t shaft;
    lv_draw_triangle_dsc_t point;

    lv_draw_line_dsc_init(&shaft);
    shaft.color = lv_color_hex(kColUp);
    shaft.width = 3;
    shaft.p1.x = cx;
    shaft.p1.y = cy;
    shaft.p2.x = tx;
    shaft.p2.y = ty;
    lv_draw_line(layer, &shaft);

    /* A filled head rather than two strokes. The direction has to survive
     * being looked at from across a desk while the device is turning, and a
     * solid triangle does that where a pair of thin lines does not. */
    lv_draw_triangle_dsc_init(&point);
    point.bg_color = lv_color_hex(kColUp);
    point.bg_opa = LV_OPA_COVER;
    point.p[0].x = tx;
    point.p[0].y = ty;
    point.p[1].x = tx - dx * head + px * head / 2;
    point.p[1].y = ty - dy * head + py * head / 2;
    point.p[2].x = tx - dx * head - px * head / 2;
    point.p[2].y = ty - dy * head - py * head / 2;
    lv_draw_triangle(layer, &point);
}

/* The two figures that are not widgets, drawn into the layer LVGL is already
 * rendering into.
 *
 * A crosshair and an arrowhead have no widget to be: LVGL draws rectangles,
 * labels and arcs, and a filled triangle is only reachable through the draw
 * API this callback is handed. Assembling the arrow out of four nested objects
 * with transforms would be a longer way to say the same six calls, and it
 * would put the geometry somewhere a reader of draw_up_arrow() could not see
 * it. So this is one empty, transparent object the size of the screen, created
 * last so that it sits above the boxes, whose whole content is the two
 * functions above.
 *
 * The arrow goes on before the marker, so that a finger on the glass is never
 * hidden behind it, and both go over the boxes: a signal hidden behind the
 * thing it is meant to be compared against would answer nothing. */
void overlay_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const Shown *s = &g_shown;

    if (s->up != NULL && (s->up->dx != 0 || s->up->dy != 0))
        draw_up_arrow(layer, s->up->dx, s->up->dy);
    if (s->have_screen) draw_marker(layer, s->sx, s->sy);
}

void build_overlay(void)
{
    g_overlay = lv_obj_create(g_screen);
    lv_obj_remove_style_all(g_overlay);
    lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(g_overlay, 0, 0);
    lv_obj_set_size(g_overlay, CATNIP_SCREEN_W, CATNIP_SCREEN_H);
    lv_obj_add_event_cb(g_overlay, overlay_draw, LV_EVENT_DRAW_MAIN, nullptr);
}

void build_page(void)
{
    g_screen = lv_screen_active();
    lv_obj_remove_style_all(g_screen);
    lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    /* The headline: where the page believes the finger is, named rather than
     * drawn. A marker that has gone through the wrong half-turn reads
     * "bottom-right" under a finger at the top left, and that sentence needs
     * no interpreting the way a crosshair on a screen apparently did. */
    g_lbl_quad = make_label(8, kRowQuad, DIAG_FONT_BIG, kColMarker);

    /* Raw beside mapped, so a tap that lands in the wrong place can be
     * diagnosed from the screen alone: raw that does not move with the finger
     * is the controller or the bus, and raw that moves while mapped goes the
     * other way is the rotation in touch_map.c. Without the raw pair the page
     * could only say that something was wrong. */
    g_lbl_touch = make_label(kColTouchX, kRowTouch, DIAG_FONT_SMALL, kColFaint);

    /* The second headline: which screen edge the firmware believes is up. */
    g_lbl_up = make_label(8, kRowUp, DIAG_FONT_BIG, kColUp);

    build_boxes();

    g_lbl_event = make_label(8, kRowEvent, DIAG_FONT_BIG, kColText);

    build_colour_key();

    /* The joystick centre is on GPIO5 and its contact never closes on this
     * unit, so the OK box only ever lights cyan. That is written on the screen
     * rather than special-cased away: a diagnostic that hides a broken input
     * is worse than no diagnostic, and a box staying dark under a press is
     * precisely the sort of thing this page is for. */
    lv_label_set_text(make_label(8, kRowNote, DIAG_FONT_SMALL, kColFaint),
                      "OK is GPIO5: contact never closes, so touch only");

    g_lbl_imu = make_label(8, kRowImu, DIAG_FONT_SMALL, kColFaint);

    build_overlay();
}

/* Milli-g as a signed decimal, without printf's float support. Whether %f
 * prints anything at all depends on how newlib was configured for this build,
 * and a row that silently comes out useless is exactly the failure this page
 * exists to prevent. Integer arithmetic cannot have that problem. */
void format_g(int32_t mg, char *buf, size_t n)
{
    int32_t mag = mg < 0 ? -mg : mg;

    snprintf(buf, n, "%c%ld.%02ld", mg < 0 ? '-' : '+', (long)(mag / 1000),
             (long)((mag % 1000) / 10));
}

/* Put g_shown on the page. Callers update g_shown first and then call this;
 * nothing here reads the drivers, so what the screen says and what the last
 * poll found cannot drift apart. */
void apply(void)
{
    const Shown *s = &g_shown;
    char line[64];

    snprintf(line, sizeof(line), "MARKER: %s",
             s->have_screen ? catnip_diag_quadrant(s->sx, s->sy) : "no touch yet");
    lv_label_set_text(g_lbl_quad, line);

    if (s->have_panel && s->have_screen) {
        snprintf(line, sizeof(line), "RAW %3u,%3u MAP %3u,%3u", s->px, s->py, s->sx,
                 s->sy);
    } else if (s->have_panel) {
        /* The controller reported a point the rotation refused. Showing the
         * raw pair anyway is the whole reason it is kept when the map fails. */
        snprintf(line, sizeof(line), "RAW %3u,%3u MAP rejected", s->px, s->py);
    } else {
        snprintf(line, sizeof(line), "RAW ---,--- MAP ---,---");
    }
    lv_label_set_text(g_lbl_touch, line);

    /* Which screen edge the firmware believes is up. It is a sentence rather
     * than three numbers for the same reason the quadrant above it is - asked
     * twice whether a marker followed their finger, the owner answered about
     * its colour both times - and it is set whether or not there is an arrow
     * to go with it, because the cases where there is no arrow are the ones
     * that most need saying out loud.
     *
     * There are six such cases and they are not the same failure: nothing
     * answering at 0x68 at all, something answering that is not a BMI270, a
     * BMI270 whose configuration image never took, a BMI270 that came up and
     * then refused its accelerometer settings, the part configured but not yet
     * read, and the part working perfectly while lying flat, where no edge is
     * up. A page that quietly drew nothing in all six would be
     * indistinguishable from a page whose arrow is broken, and telling them
     * apart is what this row and the footer are for.
     *
     * The third of them is the one most worth separating out. The part
     * identifies itself before anything is written to it, so a failed upload
     * leaves a chip that answers every read, acknowledges every write and
     * produces no data whatsoever - which under the old wording would have
     * been reported as a refused setup and sent whoever read it looking at the
     * bus.
     *
     * The last of them is not a failure at all, which is why it is worded like
     * an attitude rather than an apology. */
    if (!s->imu_have_who) {
        snprintf(line, sizeof(line), "UP: nothing answered at 0x68");
    } else if (s->imu_who != CATNIP_IMU_CHIP_ID_BMI270) {
        snprintf(line, sizeof(line), "UP: 0x68 is not a BMI270");
    } else if (!s->imu_ready && s->imu_have_status && !imu_init_ok(s)) {
        /* It is a BMI270 and its firmware image did not take. Named for what
         * it is rather than folded into the line below, because the two want
         * different things done about them. The status is tested by value and
         * not merely by having been read: it is read on the way to success
         * too, and a part that reached init_ok and then refused a range write
         * belongs on the next line, not this one. */
        snprintf(line, sizeof(line), "UP: the IMU never finished its upload");
    } else if (!s->imu_ready) {
        /* The part is the expected one and still would not come up before the
         * upload was ever reached: a write it did not acknowledge, or a range
         * field that read back as something this driver has no scale for.
         * Saying "not a BMI270" here would contradict the identity printed in
         * the footer two rows down. */
        snprintf(line, sizeof(line), "UP: the IMU refused its setup");
    } else if (!s->have_accel) {
        snprintf(line, sizeof(line), "UP: no reading yet");
    } else if (s->up == NULL) {
        snprintf(line, sizeof(line), "UP: unclear - hold it still");
    } else {
        snprintf(line, sizeof(line), "UP: %s", s->up->name);
    }
    lv_label_set_text(g_lbl_up, line);

    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        bool by_button = (s->down_mask & (1u << b)) != 0;
        bool by_touch = s->touch_box == b;
        uint32_t fill = by_button ? kColButton : (by_touch ? kColTouch : kColDark);

        lv_obj_set_style_bg_color(g_box[b], lv_color_hex(fill), 0);
        lv_obj_set_style_text_color(
            g_box_label[b], lv_color_hex(fill == kColDark ? kColText : kColBg), 0);
    }

    lv_label_set_text(g_lbl_event, s->event);

    /* The identity, and the axes behind the arrow.
     *
     * The raw CHIP_ID is here because it is the one number that decides
     * whether anything else about the arrow means anything: an axis mapping
     * read out of the wrong chip is not a mapping that needs correcting, it is
     * a number that means nothing, and the two look identical from the arrow
     * alone. Printing it beside what a BMI270 is supposed to report settles
     * the identity in the same glance as the mapping.
     *
     * INTERNAL_STATUS joins it when the upload failed, because on a BMI270 the
     * identity alone no longer settles anything. The part reports 0x24 whether
     * or not it has firmware in it, and without firmware it reports nothing
     * else at all - so the raw status byte is what distinguishes a chip that
     * did not come up from one that is not the right chip, and its value says
     * which of the part's error codes came back.
     *
     * The three axes are here for the same reason the raw touch pair is: they
     * are what makes a wrong edge name correctable without another run. Read
     * which axis is carrying gravity, find its row in imu_map.c, and the edit
     * is that row. */
    if (!s->imu_have_who) {
        snprintf(line, sizeof(line), "IMU 0x68: no answer - absent or unpowered");
    } else if (s->imu_who != CATNIP_IMU_CHIP_ID_BMI270) {
        snprintf(line, sizeof(line), "IMU 0x68: who=0x%02X, not a BMI270's 0x%02X",
                 s->imu_who, CATNIP_IMU_CHIP_ID_BMI270);
    } else if (!s->imu_ready && s->imu_have_status && !imu_init_ok(s)) {
        snprintf(line, sizeof(line), "IMU 0x68: BMI270, status=0x%02X not init_ok 0x%02X",
                 s->imu_status, CATNIP_IMU_INIT_OK);
    } else if (!s->imu_ready) {
        snprintf(line, sizeof(line), "IMU 0x68: who=0x%02X, but it refused its setup",
                 s->imu_who);
    } else if (!s->have_accel) {
        snprintf(line, sizeof(line), "IMU 0x68: who=0x%02X ok, no reading yet",
                 s->imu_who);
    } else {
        char ax[10], ay[10], az[10];

        format_g(s->mg[0], ax, sizeof(ax));
        format_g(s->mg[1], ay, sizeof(ay));
        format_g(s->mg[2], az, sizeof(az));
        snprintf(line, sizeof(line), "IMU 0x68: who=0x%02X  ax %s  ay %s  az %s g",
                 s->imu_who, ax, ay, az);
    }
    lv_label_set_text(g_lbl_imu, line);

    /* The marker and the arrow are drawn from g_shown rather than set from it,
     * so nothing above has told LVGL that they moved. */
    lv_obj_invalidate(g_overlay);
}

/* THE WAY TO PUT SOMETHING KNOWN ON THE PANEL WITH NOTHING IN THE WAY.
 *
 * This page used to be that thing. Its value was that it went straight to
 * catnip_display_blit(): that is how its own colours were caught byte-swapped,
 * and it is why a dragged finger could settle the touch rotation. Built out of
 * LVGL objects it cannot answer that question about itself any more. A screen
 * with nothing on it is now four faults at once - a dead backlight, a panel
 * that never came out of reset, an LVGL that failed to start, and a page whose
 * objects were never built - and from a chair they are the same picture.
 *
 * So this is the smallest thing that separates the bottom two from the top
 * two: a loop that writes four colour bars into a buffer, and one call to
 * catnip_display_blit(). No LVGL, no LovyanGFX sprite, no font, no layout. If
 * the bars appear then the panel, the SPI bus, the expander's chip-select and
 * the backlight are all working and the fault is above them; if they do not,
 * nothing above them is worth looking at yet.
 *
 * They are red, green, blue and white from left to right, which is the colour
 * key's question asked without the key: bars in the wrong order, or blue where
 * red belongs, is the byte order again. The values are written as raw RGB565
 * in the CPU's own order, which is what catnip_display_blit() reads, so
 * nothing converts them on the way.
 *
 * The backlight is raised too, because a dark panel is one of the faults being
 * ruled out and leaving it where it was would hide the answer.
 *
 * The buffer is allocated and freed rather than kept: this runs when someone
 * asks for it, and 150 KB held for the rest of the run to save one allocation
 * on a path nobody is timing would be the wrong trade. */
void panel_check(void)
{
    static const uint16_t kBars[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    const int bar_count = (int)(sizeof(kBars) / sizeof(kBars[0]));
    const size_t bytes = (size_t)CATNIP_SCREEN_W * CATNIP_SCREEN_H * 2;
    uint16_t *fb = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);

    if (!fb) {
        Serial.println("[catnip] panel: no PSRAM for the test frame");
        return;
    }
    for (int y = 0; y < CATNIP_SCREEN_H; y++) {
        for (int x = 0; x < CATNIP_SCREEN_W; x++) {
            int bar = x * bar_count / CATNIP_SCREEN_W;

            fb[y * CATNIP_SCREEN_W + x] = kBars[bar];
        }
    }
    catnip_display_blit(fb);
    catnip_display_backlight(255);
    heap_caps_free(fb);
    Serial.println("[catnip] panel: red, green, blue, white bars, left to right, "
                   "straight through catnip_display_blit()");
}

} /* namespace */

bool catnip_diag_marker_present(void)
{
    return SD_MMC.exists(CATNIP_DIAG_MARKER_PATH);
}

bool catnip_diag_serial_request(void)
{
    static char line[16];
    static size_t len = 0;
    bool asked = false;

    while (Serial.available()) {
        int c = Serial.read();

        if (c == '\r' || c == '\n') {
            line[len] = '\0';
            if (!strcmp(line, "diag")) asked = true;
            /* Acted on here rather than reported to the caller, because it is
             * not a request for this page and there is no state for the caller
             * to change: the bars go on the panel and the page, when it has
             * one, comes back at the next thing that moves. */
            if (!strcmp(line, "panel")) panel_check();
            len = 0;
        } else if (len + 1 < sizeof(line)) {
            line[len++] = (char)c;
        }
        /* A line longer than the buffer is simply not "diag": the overflow is
         * dropped and the comparison above fails, which is the right answer
         * for anything that long. */
    }
    return asked;
}

bool catnip_diag_begin(void)
{
    if (g_active) return true;

    /* LVGL comes up here rather than at boot, because this is its first
     * client. An LVGL display with nothing loaded on it is a black screen, and
     * starting it during setup() would have put that black screen in a fight
     * with the boot animation, which goes straight through the blit. */
    if (!catnip_lvgl_begin()) {
        Serial.println("[catnip] diag: LVGL would not start, so there is no page");
        return false;
    }

    /* Before the page is built, not after: the overlay's draw callback reads
     * g_shown, and LVGL could render as soon as the objects exist. */
    memset(&g_shown, 0, sizeof(g_shown));
    g_shown.touch_box = -1;
    strcpy(g_shown.event, g_event);
    build_page();

    catnip_input_begin();
    if (!catnip_touch_begin()) {
        /* The switches are still worth showing without the panel, and the
         * touch driver reports nothing for the rest of the run, so the page
         * simply never lights a box cyan. Saying so here is what stops that
         * being read as a rotation that maps everything off screen. */
        Serial.println("[catnip] diag: no touch controller, showing the switches only");
    }

    /* The IMU comes up here, with the other two, rather than being left to
     * whoever wires sensor.imu up later. This page is what settles the axis
     * mapping, so it has to own the bring-up of the part whose axes it is
     * mapping; and a failure to identify is not a reason to abandon the page,
     * only a reason for it to say so. The driver reports nothing for the rest
     * of the run, and the headline and the footer both name which failure it
     * was. */
    g_imu_ready = catnip_imu_begin();
    if (!g_imu_ready) {
        Serial.println("[catnip] diag: no IMU, so the page draws no arrow and says why");
    }

    g_active = true;
    /* The first frame is set before any poll, so the identity it shows has to
     * be filled in here. Without this the page opens by reporting that nothing
     * answered at 0x68 - which would be a lie for the length of one frame, on
     * the row whose whole job is to be believed. */
    g_shown.imu_ready = g_imu_ready;
    g_shown.imu_have_who = catnip_imu_who_am_i(&g_shown.imu_who);
    g_shown.imu_have_status = catnip_imu_internal_status(&g_shown.imu_status);
    apply();
    catnip_display_backlight(255);
    Serial.println("[catnip] diag: the input page has the screen");
    return true;
}

void catnip_diag_end(void)
{
    if (!g_active) return;

    /* The page built itself on the screen the backend was already showing -
     * lv_screen_active(), which is the blank one - and stripped its styles on
     * the way in. So leaving is: take the widgets off it, put the ground back,
     * and let whoever comes next load a screen of their own over it.
     *
     * Not lv_obj_delete(g_screen): that screen is not the page's, it was
     * borrowed, and deleting it would leave the display with nothing active
     * for as long as it took the shell to build its first tree. */
    lv_obj_clean(g_screen);
    lv_obj_set_style_bg_color(g_screen, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

    /* Every pointer the page held is now dangling, and apply() runs off these.
     * Cleared together rather than as they are used, because "the page is
     * gone" is one fact and half of it is worse than none. */
    g_overlay = nullptr;
    g_lbl_quad = nullptr;
    g_lbl_touch = nullptr;
    g_lbl_up = nullptr;
    g_lbl_event = nullptr;
    g_lbl_imu = nullptr;
    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        g_box[b] = nullptr;
        g_box_label[b] = nullptr;
    }
    g_screen = nullptr;
    g_active = false;
    Serial.println("[catnip] diag: leaving, the screen goes back to the shell");
}

bool catnip_diag_active(void)
{
    return g_active;
}

void catnip_diag_redraw(void)
{
    /* Marking the whole page dirty is all this has to do: LVGL still holds
     * every object, so the next lv_timer_handler() renders them again and
     * flushes a whole screen. What it is for is a panel that was painted over
     * from outside LVGL - the power button blanking it, or panel_check()
     * putting its bars there - which LVGL has no way to know about. */
    if (g_active) lv_obj_invalidate(g_screen);
}

void catnip_diag_step(void)
{
    int box = -1;
    int32_t mg[3];

    if (!g_active) return;

    /* Keep reading the console while the page has the screen. loop() stops
     * calling this once the page is up, and "panel" has to stay reachable
     * exactly then: the page itself is what will look broken. Whether "diag"
     * was typed is not worth reporting - it already is what is on screen. */
    (void)catnip_diag_serial_request();

    catnip_input_poll();
    catnip_touch_poll();
    catnip_imu_poll();

    memset(&g_now, 0, sizeof(g_now));
    g_now.touch_down = catnip_touch_down();
    g_now.have_screen = catnip_touch_position(&g_now.sx, &g_now.sy);
    g_now.have_panel = catnip_touch_panel_position(&g_now.px, &g_now.py);

    /* The whole of what the page knows about the IMU, taken in one place so
     * that the arrow, the headline and the footer are all describing the same
     * instant. The acceleration is rounded here and nowhere else, so what the
     * comparison below sees is exactly what the footer prints - otherwise a
     * still device would repaint on every poll. */
    g_now.imu_ready = g_imu_ready;
    g_now.imu_have_who = catnip_imu_who_am_i(&g_now.imu_who);
    g_now.imu_have_status = catnip_imu_internal_status(&g_now.imu_status);
    if (catnip_imu_acceleration(mg)) {
        for (int i = 0; i < 3; i++)
            g_now.mg[i] = round_mg(mg[i]);
        g_now.have_accel = true;
    }
    /* The edge comes from the driver's own unrounded reading rather than from
     * the rounded copy above. The driver is what other callers will see, and a
     * page that named an edge the driver would not name would be diagnosing
     * itself instead of the device. */
    g_now.up = catnip_imu_orientation();

    /* The box under the last known position, whether or not a finger is on the
     * glass right now. It is what lights a box while the finger is down, and
     * it is also what names the box in the release event - by then the finger
     * is gone but its last position is still there, which is exactly what
     * catnip_touch_position() outliving the touch is for. */
    if (g_now.have_screen) box = catnip_diag_box_at(g_now.sx, g_now.sy);
    g_now.touch_box = g_now.touch_down ? box : -1;

    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        catnip_button btn = (catnip_button)b;

        if (catnip_input_down(btn)) g_now.down_mask |= (uint8_t)(1u << b);
        if (catnip_input_pressed(btn)) note(catnip_diag_label(btn), "button", true);
        if (catnip_input_released(btn)) note(catnip_diag_label(btn), "button", false);
    }

    if (catnip_touch_tapped())
        note(box < 0 ? "--" : catnip_diag_label((catnip_button)box), "touch", true);
    if (catnip_touch_lifted())
        note(box < 0 ? "--" : catnip_diag_label((catnip_button)box), "touch", false);

    strncpy(g_now.event, g_event, sizeof(g_now.event) - 1);

    if (memcmp(&g_now, &g_shown, sizeof(g_now)) != 0) {
        memcpy(&g_shown, &g_now, sizeof(g_shown));
        apply();
    }
}
