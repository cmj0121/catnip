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
     * THE HANDEDNESS IS NOT CONFIRMED. Rotation 3 admits a second mapping -
     * screen_x = panel_y and screen_y = PANEL_W - 1 - panel_x - which is this
     * one turned through 180 degrees, and every reading taken from the device
     * so far is consistent with both. The measurements were (212, 273),
     * (63, 158) and (118, 205), all of them well inside the panel and none of
     * them near an edge, so nothing in them says which way either axis runs.
     * The form above is derived from the library's own rotation convention,
     * which is the best reason available for preferring it, but the touch
     * digitiser is a separate part bonded to the glass and nothing guarantees
     * it was fitted the same way up as the display it sits on.
     *
     * What would settle it: touch each corner of the screen in turn and record
     * the raw panel coordinate for each. If the top-left corner of what the
     * user sees reports a panel Y near 319 and a panel X near 0, this mapping
     * is right. If it reports a panel Y near 0 and a panel X near 239, the
     * other one is, and the fix is to swap the two subtractions here. A capture
     * of those four corners is being taken; until it is in, treat a tap that
     * lands in the diagonally opposite corner as this comment's fault and not
     * the caller's. */
    *screen_x = (uint16_t)(CATNIP_LCD_PANEL_H - 1 - panel_y);
    *screen_y = panel_x;
    return true;
}
