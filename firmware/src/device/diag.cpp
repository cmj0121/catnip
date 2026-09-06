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
#include "input.h"
#include "touch.h"

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

/* Text rows, chosen around the box block that diag_layout.c lays out between
 * y=40 and y=183. */
/* The quadrant is the headline and gets the big font; the coordinates behind
 * it are the fine print underneath. That is the wrong way round for a page of
 * numbers and the right way round for this one - the question being asked is
 * "is the marker where my finger is", and the quadrant answers it in words,
 * while the coordinates are only how it was arrived at. */
const int kRowQuad = 2;    /* which quarter of the screen the marker is in */
const int kRowTouch = 22;  /* the raw and mapped coordinates behind it */
const int kRowKey = 186;   /* the colour key and the two path colours */
const int kRowEvent = 202; /* the last event and the path that delivered it */
const int kRowNote = 222;

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
    char event[48];
};

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
 * This is the page's reason for existing. catnip_touch_panel_to_screen()
 * encodes one of the two handednesses that rotation 3 admits and cannot yet
 * tell them apart from the readings taken so far - all three landed well
 * inside the panel, where the two mappings agree closely enough to be
 * indistinguishable. Near the edges they do not: the wrong one puts the
 * marker through a 180 degree turn about the centre of the screen. So the
 * marker under a finger dragged to a corner settles it by eye, in one touch,
 * with no capture to read back and no arithmetic to do afterwards.
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
        snprintf(line, sizeof(line), "RAW %3u,%3u   MAP %3u,%3u", s->px, s->py, s->sx,
                 s->sy);
    } else if (s->have_panel) {
        /* The controller reported a point the rotation refused. Showing the
         * raw pair anyway is the whole reason it is kept when the map fails. */
        snprintf(line, sizeof(line), "RAW %3u,%3u   MAP rejected", s->px, s->py);
    } else {
        snprintf(line, sizeof(line), "RAW ---,---   MAP ---,---");
    }
    g_fb.setTextColor(kColFaint);
    g_fb.drawString(line, 8, kRowTouch);

    g_fb.setFont(&fonts::Font2);
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

    g_active = true;
    memset(&g_shown, 0, sizeof(g_shown));
    g_shown.touch_box = -1;
    memcpy(&g_now, &g_shown, sizeof(g_now));
    strcpy(g_now.event, g_event);
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

    if (!g_active) return;

    catnip_input_poll();
    catnip_touch_poll();

    memset(&g_now, 0, sizeof(g_now));
    g_now.touch_down = catnip_touch_down();
    g_now.have_screen = catnip_touch_position(&g_now.sx, &g_now.sy);
    g_now.have_panel = catnip_touch_panel_position(&g_now.px, &g_now.py);

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
