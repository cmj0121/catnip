/* diag_layout.c - see diag_layout.h. */
#include "diag_layout.h"

#include "board.h"

/* One cell of the D-pad. 64x48 is not a style choice: a fingertip on this
 * panel covers something like 40 pixels across, so a box much smaller than
 * this cannot be aimed at, and the page would be reporting the layout's fault
 * as the touch driver's. */
#define CELL_W 64
#define CELL_H 48

/* The D-pad's top-left cell corner. The arms are laid out from here:
 *
 *          +------+
 *          |  UP  |
 *   +------+------+------+
 *   | LEFT |  OK  | RIGHT|
 *   +------+------+------+
 *          | DOWN |
 *          +------+
 *
 * The block spans x 24..215 and y 40..183, which leaves the top two text rows
 * above it, the bottom two below it, and room to the right for A and B. */
#define PAD_X 24
#define PAD_Y 40

/* A and B stand apart from the D-pad, on the right, because they are separate
 * switches under the thumb and drawing them as part of the cross would suggest
 * a geometry the hardware does not have. */
#define AB_X 236

/* Indexed by catnip_button, in that enum's order. */
static const catnip_diag_rect kBoxes[CATNIP_BTN_COUNT] = {
    {PAD_X + CELL_W, PAD_Y, CELL_W, CELL_H},              /* UP */
    {PAD_X + CELL_W, PAD_Y + 2 * CELL_H, CELL_W, CELL_H}, /* DOWN */
    {PAD_X, PAD_Y + CELL_H, CELL_W, CELL_H},              /* LEFT */
    {PAD_X + 2 * CELL_W, PAD_Y + CELL_H, CELL_W, CELL_H}, /* RIGHT */
    {PAD_X + CELL_W, PAD_Y + CELL_H, CELL_W, CELL_H},     /* CENTRE */
    {AB_X, PAD_Y + 24, CELL_W, CELL_H},                   /* A */
    {AB_X, PAD_Y + 24 + CELL_H + 12, CELL_W, CELL_H},     /* B */
};

/* The screen is a fixed size and so is this table, so a box hanging off the
 * edge is a mistake that can be caught while compiling rather than by half a
 * box appearing on the device. The rows below are the rightmost and the two
 * lowest edges the table produces. */
typedef char catnip_diag_layout_fits[(AB_X + CELL_W <= CATNIP_SCREEN_W &&
                                      PAD_Y + 3 * CELL_H <= CATNIP_SCREEN_H &&
                                      PAD_Y + 24 + 2 * CELL_H + 12 <= CATNIP_SCREEN_H)
                                         ? 1
                                         : -1];

/* Short enough to fit the box at the font size the page draws at. "OK" rather
 * than "CENTRE" for the same reason. */
static const char *const kLabels[CATNIP_BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "OK", "A", "B",
};

const catnip_diag_rect *catnip_diag_box(catnip_button button)
{
    return &kBoxes[button];
}

const char *catnip_diag_label(catnip_button button)
{
    return kLabels[button];
}

int catnip_diag_box_at(int32_t screen_x, int32_t screen_y)
{
    for (int i = 0; i < CATNIP_BTN_COUNT; i++) {
        const catnip_diag_rect *r = &kBoxes[i];

        if (screen_x >= r->x && screen_x < r->x + r->w && screen_y >= r->y &&
            screen_y < r->y + r->h) {
            return i;
        }
    }
    return -1;
}

const char *catnip_diag_quadrant(int32_t screen_x, int32_t screen_y)
{
    /* The dividing lines are the screen's own centre. A point exactly on one
     * of them is counted as the far half, which is arbitrary but has to be
     * decided somewhere, and no finger lands on a single pixel anyway. */
    int left = screen_x < CATNIP_SCREEN_W / 2;
    int top = screen_y < CATNIP_SCREEN_H / 2;

    if (top) return left ? "top-left" : "top-right";
    return left ? "bottom-left" : "bottom-right";
}
