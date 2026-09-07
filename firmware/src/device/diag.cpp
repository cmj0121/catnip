/* diag.cpp - see diag.h. */
#include <Arduino.h>
#include <SD_MMC.h>

#include <LovyanGFX.hpp>

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "diag.h"
#include "diag_layout.h"
#include "display.h"
#include "imu.h"
#include "input.h"
#include "touch.h"
#include "touch_debug.h" /* the raw pair, shown beside the mapped one */

/*
 * HOW THIS DRAWS, AND WHY NOT THROUGH display.h.
 *
 * display.h offers a raw blit and backlight control and says in its own
 * comment that anything richer belongs to the renderer. A D-pad needs
 * rectangles and text, so there were two ways to get them: widen display.h
 * with primitives, or compose a frame in memory here and hand the finished
 * pixels to the blit that already exists.
 *
 * This file composes. The reason is not economy of code - a couple of
 * rectangle calls would have been shorter - but that the primitives would not
 * have stayed a couple. Rectangles alone do not draw a D-pad; the labels need
 * text, text needs a font and a size and a colour and an alignment, and by the
 * time display.h can draw "OK" centred in a box it is a renderer with a
 * different name, and issue #30's renderer arrives to find its job already
 * half done by the panel driver. Composing keeps display.h's contract exactly
 * as it is written, and leaves this page's drawing where the page can be
 * deleted with it.
 *
 * The framebuffer is an LGFX_Sprite in PSRAM, which is what
 * catnip_display_load_frames() already does for the boot animation: same
 * memory, same 16-bit depth, same 150 KB per full-screen frame, and its buffer
 * is laid out exactly as catnip_display_blit() expects. It also means the page
 * gets LovyanGFX's font rather than a hand-rolled one - the library is linked
 * either way, and a bitmap font written here to avoid using it would be a
 * second font in the firmware for no gain.
 *
 * This is a second drawing path alongside the renderer that #30 will bring,
 * and that was accepted when this page was planned: independence from the
 * renderer is the entire value of a diagnostic. The guard against it growing
 * into a rival UI is that it stays feature-free.
 */

namespace {

/* Green for a switch, cyan for the glass. The status line names the path in
 * words as well; the colour is what makes a press and a tap on the same box
 * distinguishable at a glance, from across a desk. */
const uint16_t kColBg = 0x0000;     /* black */
const uint16_t kColEdge = 0xFFFF;   /* white */
const uint16_t kColDark = 0x2124;   /* an unlit box */
const uint16_t kColButton = 0x07E0; /* lit by a switch */
const uint16_t kColTouch = 0x07FF;  /* lit by a finger */
const uint16_t kColText = 0xFFFF;   /* the text rows */
const uint16_t kColFaint = 0x7BEF;  /* the footnotes */
const uint16_t kColMarker = 0xF800; /* the touch marker: red on all of the above */
const uint16_t kColUp = 0xFFE0;     /* the up arrow: yellow, claimed by nothing else */

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

lgfx::LGFX_Sprite g_fb;
bool g_active = false;

/* What the last redraw drew, so a pass that changed nothing costs nothing. A
 * full-screen blit is 150 KB over SPI; doing it on every loop would make the
 * page's own redraw the slowest thing between a press and its box lighting. */
struct Shown {
    uint8_t down_mask;
    int touch_box;
    bool touch_down;
    bool have_screen;
    bool have_panel;
    uint16_t sx, sy, px, py;
    /* The acceleration, rounded to kMgStep before it is stored. A resting
     * accelerometer's last few milli-g never stop moving, so keeping the raw
     * value here would make every comparison below differ, and turn a page
     * that redraws on a change into one that pushes a 150 KB frame twenty
     * times a second. That would show up as buttons lighting late, and would
     * be blamed on the very switches this page exists to vindicate. The
     * rounding is done once, here, so that what is compared and what is
     * printed are the same numbers and cannot disagree. */
    int32_t mg[3];
    bool have_accel;
    /* What catnip_imu_begin() found, carried here so that the footer says the
     * same thing the arrow does and both come from one snapshot. `imu_ready`
     * is fixed for the run - the driver reports nothing for the rest of it
     * once identification has failed - but it is compared with everything
     * else rather than read from a global, so that the redraw has exactly one
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

Shown g_shown;
Shown g_now;

/* The most recent event, in the page's own words: which switch, and which of
 * the two paths delivered it. */
char g_event[48] = "waiting for a press or a tap";

void note(const char *what, const char *path, bool down)
{
    snprintf(g_event, sizeof(g_event), "%s (%s) %s", what, path, down ? "down" : "up");
}

void draw_box(catnip_button b, uint8_t down_mask, int touch_box)
{
    const catnip_diag_rect *r = catnip_diag_box(b);
    bool by_button = (down_mask & (1u << b)) != 0;
    bool by_touch = touch_box == (int)b;
    uint16_t fill = by_button ? kColButton : (by_touch ? kColTouch : kColDark);

    g_fb.fillRect(r->x, r->y, r->w, r->h, fill);
    g_fb.drawRect(r->x, r->y, r->w, r->h, kColEdge);
    g_fb.setTextDatum(lgfx::middle_center);
    g_fb.setTextColor(fill == kColDark ? kColText : kColBg);
    g_fb.drawString(catnip_diag_label(b), r->x + r->w / 2, r->y + r->h / 2);
    g_fb.setTextDatum(lgfx::top_left);
}

/* A crosshair at the mapped touch position, drawn wherever the finger is and
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
 * It is drawn last, over the boxes, because a marker hidden behind the thing
 * it is meant to be compared against would answer nothing. */
void draw_marker(uint16_t x, uint16_t y)
{
    const int arm = 12;

    /* A line from the middle of the screen out to the crosshair. The crosshair
     * alone asks the owner to compare two things - where the marker is and
     * where their finger is - and asked that question twice they answered
     * about its colour twice, which says the comparison was not being made. A
     * line from a fixed point turns it into one thing to look at: it points
     * away from the fingertip when the rotation is wrong, and the quadrant
     * printed at the top of the screen says the same in words. */
    g_fb.drawLine(CATNIP_SCREEN_W / 2, CATNIP_SCREEN_H / 2, x, y, kColMarker);
    g_fb.drawFastHLine(x - arm, y, arm * 2 + 1, kColMarker);
    g_fb.drawFastVLine(x, y - arm, arm * 2 + 1, kColMarker);
    g_fb.drawCircle(x, y, 6, kColMarker);
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
void draw_up_arrow(int dx, int dy)
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

    for (int i = -1; i <= 1; i++) {
        g_fb.drawLine(cx + px * i, cy + py * i, tx + px * i, ty + py * i, kColUp);
    }
    /* A filled head rather than two strokes. The direction has to survive
     * being looked at from across a desk while the device is turning, and a
     * solid triangle does that where a pair of thin lines does not. */
    g_fb.fillTriangle(tx, ty, tx - dx * head + px * head / 2,
                      ty - dy * head + py * head / 2, tx - dx * head - px * head / 2,
                      ty - dy * head - py * head / 2, kColUp);
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

/* Three named swatches, so that a colour fault is legible on the screen
 * instead of being deduced from someone describing a crosshair over a cable.
 *
 * The page ran with every colour byte-swapped and nobody could say so directly:
 * what was reported was "the red crosshair looks blue", and the byte order had
 * to be worked back out from the arithmetic. A swatch labelled "red" that is
 * not red says it in one glance, and it says it for whatever the next colour
 * fault turns out to be as well. Making the invisible visible is this page's
 * entire job and it could not do it for its own pixels. */
void draw_colour_key(void)
{
    static const struct {
        uint16_t colour;
        const char *name;
    } kKey[] = {
        {0xF800, "red"},
        {0x07E0, "green"},
        {0x001F, "blue"},
    };
    int x = 8;

    for (size_t i = 0; i < sizeof(kKey) / sizeof(kKey[0]); i++) {
        g_fb.fillRect(x, kRowKey, kSwatchW, kSwatchH, kKey[i].colour);
        g_fb.drawRect(x, kRowKey, kSwatchW, kSwatchH, kColEdge);
        g_fb.setTextColor(kColText);
        g_fb.drawString(kKey[i].name, x + kSwatchW + 4, kRowKey + 2);
        x += kSwatchW + 4 + (int)strlen(kKey[i].name) * 6 + 10;
    }

    /* What the box colours mean, on the same row, since it is the same
     * question: which colour is that, and what is it telling me. */
    g_fb.setTextColor(kColFaint);
    g_fb.drawString("switch=green  touch=cyan", 164, kRowKey + 2);
}

void redraw(const Shown *s)
{
    char line[64];

    g_fb.fillScreen(kColBg);
    g_fb.setFont(&fonts::Font2);

    /* The headline: where the page believes the finger is, named rather than
     * drawn. A marker that has gone through the wrong half-turn reads
     * "bottom-right" under a finger at the top left, and that sentence needs
     * no interpreting the way a crosshair on a screen apparently did. */
    g_fb.setTextColor(kColMarker);
    snprintf(line, sizeof(line), "MARKER: %s",
             s->have_screen ? catnip_diag_quadrant(s->sx, s->sy) : "no touch yet");
    g_fb.drawString(line, 8, kRowQuad);

    /* Raw beside mapped, so a tap that lands in the wrong place can be
     * diagnosed from the screen alone: raw that does not move with the finger
     * is the controller or the bus, and raw that moves while mapped goes the
     * other way is the rotation in touch_map.c. Without the raw pair the page
     * could only say that something was wrong. */
    g_fb.setFont(&fonts::Font0);
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
    g_fb.setTextColor(kColFaint);
    g_fb.drawString(line, kColTouchX, kRowTouch);

    /* The second headline: which screen edge the firmware believes is up. It
     * is a sentence rather than three numbers for the same reason the quadrant
     * above it is - asked twice whether a marker followed their finger, the
     * owner answered about its colour both times - and it is drawn whether or
     * not there is an arrow to go with it, because the cases where there is no
     * arrow are the ones that most need saying out loud.
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
     * The third of them is new with the BMI270 and is the one most worth
     * separating out. The part identifies itself before anything is written to
     * it, so a failed upload leaves a chip that answers every read,
     * acknowledges every write and produces no data whatsoever - which under
     * the old wording would have been reported as a refused setup and sent
     * whoever read it looking at the bus.
     *
     * The last of them is not a failure at all, which is why it is worded like
     * an attitude rather than an apology. */
    g_fb.setFont(&fonts::Font2);
    g_fb.setTextColor(kColUp);
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
    g_fb.drawString(line, 8, kRowUp);

    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        draw_box((catnip_button)b, s->down_mask, s->touch_box);
    }

    g_fb.setTextColor(kColText);
    g_fb.drawString(s->event, 8, kRowEvent);

    /* The joystick centre is on GPIO5 and its contact never closes on this
     * unit, so the OK box only ever lights cyan. That is written on the screen
     * rather than special-cased away: a diagnostic that hides a broken input
     * is worse than no diagnostic, and a box staying dark under a press is
     * precisely the sort of thing this page is for. */
    g_fb.setFont(&fonts::Font0);
    draw_colour_key();
    g_fb.setTextColor(kColFaint);
    g_fb.drawString("OK is GPIO5: contact never closes, so touch only", 8, kRowNote);

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
    g_fb.drawString(line, 8, kRowImu);

    /* The arrow before the marker, so that a finger on the glass is never
     * hidden behind it. Both are drawn over the boxes: a signal hidden behind
     * the thing it is meant to be compared against would answer nothing.
     *
     * No arrow for the two flat attitudes, which have no edge up and say so in
     * the headline instead. Pointing one somewhere anyway would be the page
     * inventing an answer, which is the one thing it must never do. */
    if (s->up != NULL && (s->up->dx != 0 || s->up->dy != 0))
        draw_up_arrow(s->up->dx, s->up->dy);

    if (s->have_screen) draw_marker(s->sx, s->sy);

    catnip_display_blit(g_fb.getBuffer());
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

    g_fb.setPsram(true);
    /* NOT setColorDepth(16), which is rgb565_2Byte: a LovyanGFX sprite at that
     * depth stores its pixels byte-swapped, because that is the order the SPI
     * bus wants them in and pushSprite() hands the buffer straight to the bus.
     * This page does not push the sprite - it cannot, the panel object is
     * private to display_st7789.cpp - it takes getBuffer() and passes it to
     * catnip_display_blit(), which casts to lgfx::rgb565_t, the native
     * little-endian order.
     *
     * Those two orders disagreeing is what put the wrong colours on this page
     * the first time it ran: the crosshair drawn 0xF800 red arrived as 0x00F8
     * and read as blue, and the touch highlight drawn 0x07FF cyan arrived as
     * 0xFF07 and read as yellow. The boot mascot has always been right because
     * it never goes through a sprite at all - tools/png_to_rgb565.py emits
     * native-order rgb565 and catnip_display_blit() reads it as exactly that -
     * so the panel's configuration in board.h is correct and is not what needed
     * changing. Only the composition step disagreed.
     *
     * rgb565_nonswapped is LovyanGFX's own name for "same 16 bits, not
     * swapped", and it is the right one by the library's own declaration
     * rather than by experiment: lgfx::rgb565_t - the very type
     * catnip_display_blit() casts this buffer to - declares
     * `static constexpr color_depth_t depth = rgb565_nonswapped`. Asking the
     * sprite for that depth is asking it for a buffer of exactly those pixels. Fixing it here rather than by pre-swapping the colour
     * constants matters: swapped constants would look right on this page while
     * leaving the next person to draw on this sprite to rediscover the whole
     * thing. */
    g_fb.setColorDepth(lgfx::rgb565_nonswapped);
    if (!g_fb.createSprite(CATNIP_SCREEN_W, CATNIP_SCREEN_H)) {
        Serial.println("[catnip] diag: no memory for the page's framebuffer");
        return false;
    }

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
    memset(&g_shown, 0, sizeof(g_shown));
    g_shown.touch_box = -1;
    memcpy(&g_now, &g_shown, sizeof(g_now));
    strcpy(g_now.event, g_event);
    /* The first frame is drawn before any poll, so the identity it shows has
     * to be filled in here. Without this the page opens by reporting that
     * nothing answered at 0x68 - which would be a lie for the length of one
     * frame, on the row whose whole job is to be believed. */
    g_now.imu_ready = g_imu_ready;
    g_now.imu_have_who = catnip_imu_who_am_i(&g_now.imu_who);
    g_now.imu_have_status = catnip_imu_internal_status(&g_now.imu_status);
    redraw(&g_now);
    memcpy(&g_shown, &g_now, sizeof(g_shown));
    catnip_display_backlight(255);
    Serial.println("[catnip] diag: the input page has the screen");
    return true;
}

bool catnip_diag_active(void)
{
    return g_active;
}

void catnip_diag_redraw(void)
{
    if (g_active) redraw(&g_shown);
}

void catnip_diag_step(void)
{
    int box = -1;
    int32_t mg[3];

    if (!g_active) return;

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
     * still device would redraw a 150 KB frame on every poll. */
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
        redraw(&g_now);
        memcpy(&g_shown, &g_now, sizeof(g_shown));
    }
}
