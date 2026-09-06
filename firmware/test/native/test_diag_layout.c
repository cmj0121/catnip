/* Native test for issue #42: the diagnostic page's box layout.
 *
 * The page itself cannot be tested here - it is LovyanGFX calls against a
 * panel - but the question it gets wrong most easily is not a drawing
 * question. It is "which box did that tap land in", and that is arithmetic
 * over a fixed table. On the device a bad answer looks exactly like a bad
 * touch rotation: the wrong box lights. Pinning the geometry down here means
 * that when the page is used to settle the rotation, the layout is not a
 * second suspect.
 */
#include <stdio.h>
#include <string.h>

#include "device/board.h"
#include "device/diag_layout.h"

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

static void expect_box(int32_t x, int32_t y, int want, const char *name)
{
    int got = catnip_diag_box_at(x, y);

    if (got == want) {
        printf("  ok   - %s\n", name);
    } else {
        printf("  FAIL - %s: (%d, %d) gave %d, wanted %d\n", name, (int)x, (int)y, got,
               want);
        failures++;
    }
}

/* Do two boxes share a pixel? */
static int overlaps(const catnip_diag_rect *a, const catnip_diag_rect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h &&
           b->y < a->y + a->h;
}

int main(void)
{
    static const char *const names[CATNIP_BTN_COUNT] = {
        "UP", "DOWN", "LEFT", "RIGHT", "CENTRE", "A", "B",
    };
    int i, j;

    printf("test_diag_layout\n");

    /* Every switch has a box, the broken joystick centre included. A
     * diagnostic that quietly omits an input it believes is dead cannot
     * report that it is dead. */
    for (i = 0; i < CATNIP_BTN_COUNT; i++) {
        const catnip_diag_rect *r = catnip_diag_box((catnip_button)i);
        int on_screen = r->w > 0 && r->h > 0 && r->x >= 0 && r->y >= 0 &&
                        r->x + r->w <= CATNIP_SCREEN_W && r->y + r->h <= CATNIP_SCREEN_H;

        printf("  -- %s at (%d, %d) %dx%d\n", names[i], r->x, r->y, r->w, r->h);
        CHECK(on_screen, "the box is a real rectangle on the screen");
        CHECK(catnip_diag_label((catnip_button)i)[0] != '\0', "the box has a label");
    }

    /* Boxes that overlapped would make catnip_diag_box_at's answer depend on
     * the order it happens to walk the table in. */
    for (i = 0; i < CATNIP_BTN_COUNT; i++) {
        for (j = i + 1; j < CATNIP_BTN_COUNT; j++) {
            CHECK(!overlaps(catnip_diag_box((catnip_button)i),
                            catnip_diag_box((catnip_button)j)),
                  "no two boxes overlap");
        }
    }

    /* The middle of each box belongs to that box. */
    for (i = 0; i < CATNIP_BTN_COUNT; i++) {
        const catnip_diag_rect *r = catnip_diag_box((catnip_button)i);

        expect_box(r->x + r->w / 2, r->y + r->h / 2, i, "the centre of the box hits it");
    }

    /* The edges, both ways round. A box owns its top-left pixel and the pixel
     * one short of its far edge, and neither the pixel before it nor the one
     * past it. Off-by-one here is the difference between a tap on a border
     * lighting the box the finger is on and the box next to it. */
    for (i = 0; i < CATNIP_BTN_COUNT; i++) {
        const catnip_diag_rect *r = catnip_diag_box((catnip_button)i);

        expect_box(r->x, r->y, i, "the top-left pixel is inside");
        expect_box(r->x + r->w - 1, r->y + r->h - 1, i,
                   "the bottom-right pixel is inside");
        CHECK(catnip_diag_box_at(r->x - 1, r->y) != i, "one pixel left of it is not");
        CHECK(catnip_diag_box_at(r->x, r->y - 1) != i, "one pixel above it is not");
        CHECK(catnip_diag_box_at(r->x + r->w, r->y) != i, "one pixel right of it is not");
        CHECK(catnip_diag_box_at(r->x, r->y + r->h) != i, "one pixel below it is not");
    }

    /* The corners of the D-pad's bounding block are the gaps between its arms,
     * and a tap there must light nothing rather than the nearest arm. */
    {
        const catnip_diag_rect *up = catnip_diag_box(CATNIP_BTN_UP);
        const catnip_diag_rect *left = catnip_diag_box(CATNIP_BTN_LEFT);
        const catnip_diag_rect *right = catnip_diag_box(CATNIP_BTN_RIGHT);
        const catnip_diag_rect *down = catnip_diag_box(CATNIP_BTN_DOWN);

        expect_box(left->x, up->y, -1, "above LEFT is a gap");
        expect_box(right->x, up->y, -1, "above RIGHT is a gap");
        expect_box(left->x, down->y, -1, "below LEFT is a gap");
        expect_box(right->x, down->y, -1, "below RIGHT is a gap");
    }

    /* The text rows the page draws in are not boxes either. */
    expect_box(0, 0, -1, "the top-left of the screen is not a box");
    expect_box(CATNIP_SCREEN_W / 2, CATNIP_SCREEN_H - 1, -1,
               "the status line is not a box");

    /* A position that is not on the screen at all is an ordinary "no box"
     * rather than something the caller has to guard against - the marker
     * follows the finger wherever the mapping puts it, and a wrong mapping is
     * exactly what could put it off the edge. */
    expect_box(-1, -1, -1, "a point off the top-left is no box");
    expect_box(CATNIP_SCREEN_W, CATNIP_SCREEN_H, -1,
               "a point off the bottom-right is no box");
    expect_box(-10000, 10000, -1, "a point far outside is no box");

    /* The quadrant the page prints beside the coordinates. It is words on a
     * screen and the whole reason it was added is that a picture was being
     * misread, so a wrong word here would be worse than no word: it would be
     * a confident answer to the one question the page exists to settle. */
    {
        const int32_t w = CATNIP_SCREEN_W;
        const int32_t h = CATNIP_SCREEN_H;

        CHECK(!strcmp(catnip_diag_quadrant(0, 0), "top-left"),
              "the top-left pixel is top-left");
        CHECK(!strcmp(catnip_diag_quadrant(w - 1, 0), "top-right"),
              "the top-right pixel is top-right");
        CHECK(!strcmp(catnip_diag_quadrant(0, h - 1), "bottom-left"),
              "the bottom-left pixel is bottom-left");
        CHECK(!strcmp(catnip_diag_quadrant(w - 1, h - 1), "bottom-right"),
              "the bottom-right pixel is bottom-right");

        /* The two mappings rotation 3 admits differ by a half turn about the
         * centre, so the wrong one names the opposite quadrant - which is
         * exactly the reading that tells the owner which one is in the
         * driver. */
        CHECK(!strcmp(catnip_diag_quadrant(w - 1 - 40, h - 1 - 30), "bottom-right"),
              "a point near the bottom right is bottom-right");
        CHECK(!strcmp(catnip_diag_quadrant(40, 30), "top-left"),
              "its half-turn partner is top-left");

        /* Off the screen it still answers, because a wrong rotation is what
         * would put the mapped position out there. */
        CHECK(!strcmp(catnip_diag_quadrant(-50, -50), "top-left"),
              "a point off the top-left is still named");
        CHECK(!strcmp(catnip_diag_quadrant(w + 50, h + 50), "bottom-right"),
              "a point off the bottom-right is still named");
    }

    if (failures) {
        printf("FAILED: %d\n", failures);
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
