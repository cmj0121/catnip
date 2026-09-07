/*
 * diag.h - the on-screen input diagnostic: a D-pad that lights under a press.
 *
 * Two jobs, and the second is the one that earns this page a permanent place
 * in the firmware rather than a life as a throwaway sketch.
 *
 * The first is to show that the physical inputs work, on screen, with nothing
 * in the way: no LVGL, no renderer, no ui event model. When a press does not
 * reach an app, this page says whether the switch reached the firmware at all,
 * and that is a different question from whether the layer above dispatched it.
 *
 * The second was to settle the touch rotation, which it did. Rotation 3 admits
 * two mappings a half turn apart, and the readings available when
 * catnip_touch_panel_to_screen() was written all sat well inside the panel,
 * where the two are indistinguishable. This page draws a marker at the mapped
 * position, wherever the finger is, and names the quarter of the screen it
 * believes that position is in; a finger dragged into the top-left corner
 * reported "MARKER: top-left", which excluded the other mapping. That took one
 * drag rather than a corner-by-corner capture read back over serial, and
 * touch_map.c carries the record.
 *
 * It stays in the firmware because that was never the last question of its
 * kind. The same drag re-checks the rotation on a different unit, and the same
 * boxes answer "did the switch reach the firmware at all" for whatever fails
 * next.
 *
 * HOW IT IS ENTERED, AND WHY NOT BY A KEY. A held button at boot is the
 * obvious gesture and it is the wrong one here: the buttons are the thing
 * under test, so an entry that depends on them fails in exactly the case the
 * page is wanted for - and would then be indistinguishable from a page that
 * refuses to start. Two paths that depend on nothing under test:
 *
 *   - a marker file on the card, so a device with no serial attached can be
 *     put into the diagnostic and handed to someone;
 *   - a serial command, so it is still reachable with no card in the slot,
 *     which is also the lower-friction one for an owner already on a USB
 *     cable and holding a keyboard.
 *
 * Neither is a fallback for the other. The card path fails with no card, the
 * serial path fails with no host, and the two failures do not overlap.
 */
#ifndef CATNIP_DIAG_H
#define CATNIP_DIAG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The marker file. Same shape as CATNIP_CONFIG_PATH in main.cpp, and read the
 * same way, so the two agree about where the card is. It is only its presence
 * that matters; the contents are never read. */
#ifndef CATNIP_DIAG_MARKER_PATH
#define CATNIP_DIAG_MARKER_PATH "/sd/catnip/diag"
#endif

/* True when the marker file is on the card. Ask after mounting; with no card
 * this is false, which is what the serial command is for. */
bool catnip_diag_marker_present(void);

/* Consume whatever the host has typed and report whether a line reading
 * "diag" has arrived. Call it from the main loop. It reads the serial input
 * that nothing else in the firmware reads, so a line meant for something else
 * cannot be swallowed here - there is nothing else yet. */
bool catnip_diag_serial_request(void);

/* Take the screen. Returns false when the framebuffer could not be allocated,
 * in which case nothing was drawn and the caller should carry on booting -
 * losing the diagnostic is better than leaving a half-drawn page over a device
 * that is otherwise fine. */
bool catnip_diag_begin(void);

/* True once catnip_diag_begin() has succeeded. The page keeps the screen for
 * the rest of the run: there is no way out that does not involve the buttons,
 * and that is the same reason there is no way in that does. */
bool catnip_diag_active(void);

/* Put the page back on a screen that was cleared underneath it - which the
 * power button does, since a short press blanks the panel and lights it again
 * (see main.cpp). Without this the screen comes back empty and the diagnostic
 * looks like it has died. */
void catnip_diag_redraw(void);

/* Poll the switches and the touch panel, and redraw when something moved.
 * Call it from the main loop in place of whatever else was drawing. */
void catnip_diag_step(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DIAG_H */
