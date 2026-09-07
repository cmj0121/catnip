/* touch_map.c - see touch_map.h. */
#include "touch_map.h"

#include "board.h"

/* Rotation 3 turns the 240x320 panel into the 320x240 screen, so the panel's
 * height is the screen's width and the panel's width is the screen's height.
 * The driver mixes the two sets of constants below, and this is what makes
 * that safe: if anyone ever changes one of the four numbers in board.h without
 * the others, this stops compiling instead of quietly drawing in the wrong
 * place. */
typedef char catnip_touch_map_rotation_matches[(CATNIP_LCD_PANEL_H == CATNIP_SCREEN_W &&
                                                CATNIP_LCD_PANEL_W == CATNIP_SCREEN_H)
                                                   ? 1
                                                   : -1];

bool catnip_touch_panel_to_screen(uint16_t panel_x, uint16_t panel_y, uint16_t *screen_x,
                                  uint16_t *screen_y)
{
    if (panel_x >= CATNIP_LCD_PANEL_W || panel_y >= CATNIP_LCD_PANEL_H) {
        return false;
    }

    /* The controller reports where the finger is on the glass in the panel's
     * own orientation, and the panel is mounted sideways: board.h records a
     * 240x320 part driven at CATNIP_LCD_ROTATION 3 so that the user sees a
     * 320x240 landscape screen. Drawing already goes through that rotation, so
     * a touch has to go through the same one or the two disagree.
     *
     * This is the inverse of the rotation the graphics library applies when it
     * draws. At rotation 3 it puts a screen pixel (sx, sy) at the panel
     * position (sy, PANEL_H - 1 - sx); running that backwards gives the two
     * lines below. So a finger at the panel's origin lands at the top right of
     * the screen, and walking down the panel's Y axis walks left across it.
     *
     * THE HANDEDNESS IS CONFIRMED on this unit, and the paragraphs below are
     * the record of how, kept because without them this transform reads as
     * more complicated than it needs to be and invites being "simplified" into
     * the wrong one of two forms that look equally reasonable on paper.
     *
     * Rotation 3 admits a second mapping - screen_x = panel_y and
     * screen_y = PANEL_W - 1 - panel_x - which is this one turned through 180
     * degrees about the centre of the screen. Near the middle of the panel the
     * two agree closely enough to be indistinguishable, and the first three
     * readings taken from the device - (212, 273), (63, 158) and (118, 205) -
     * were all well inside it, so none of them said which way either axis
     * runs. The form above was derived from the graphics library's own
     * rotation convention, which was the best reason available for preferring
     * it and was not a measurement: the touch digitiser is a separate part
     * bonded to the glass, and nothing guarantees it was fitted the same way
     * up as the display underneath it.
     *
     * What settled it: the input diagnostic page (device/diag.cpp) draws a
     * crosshair at the mapped position and prints, in words, which quarter of
     * the screen that position is in. A finger dragged into the top-left
     * corner of the screen the user is looking at made the page report
     * "MARKER: top-left", with its line from screen centre pointing back at
     * the fingertip. The alternative mapping is a half turn, so for that same
     * finger it would have reported "bottom-right"; it is excluded.
     *
     * The two lines below are therefore measured rather than inferred, and
     * swapping the two subtractions - the correction this comment used to hold
     * in reserve - would now be the bug rather than the fix. The check repeats
     * on another unit in one drag: put /sd/catnip/diag on the card or type
     * "diag" over serial, drag a finger into a named corner of the screen, and
     * read the quadrant the page prints. */
    *screen_x = (uint16_t)(CATNIP_LCD_PANEL_H - 1 - panel_y);
    *screen_y = panel_x;
    return true;
}
