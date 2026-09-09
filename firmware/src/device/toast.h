/*
 * toast.h - a line of red at the bottom of the screen when Lua faults.
 *
 * A script that faults used to fail invisibly: the traceback went to the serial
 * log, and the device carried on showing whatever was last drawn. Somebody
 * holding it saw a screen that had stopped responding and nothing that said
 * why - which is the same information a frozen device gives.
 *
 * So the platform says it. On the top layer, like the frame's bar, because it
 * has to appear over whatever is on screen - an app's page, a canvas that asked
 * for the whole panel, the launcher - and none of those may be asked to make
 * room for it. It is not in the node tree at all, so no app can draw one, hide
 * one, or find out that one is up.
 *
 * The message and not the traceback: there is room for one line on this panel,
 * and the stack under it is already in the log, where there is room for all of
 * it and somebody reading a log is looking for exactly that.
 */
#ifndef CATNIP_TOAST_H
#define CATNIP_TOAST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show `msg` for a few seconds. A second fault replaces the first and starts
 * the time again: the newest is the one being looked for, and a queue of them
 * would make the device spend a minute reciting a fault somebody has already
 * fixed.
 *
 * Safe before LVGL is up, when it does nothing - a fault that early has no
 * screen to appear on, and the log still has it. */
void catnip_toast_show(const char *msg);

/* Called once per pass. Takes the toast away when its time is up, and does
 * nothing at all when there is none - it is a clock check, not a redraw. */
void catnip_toast_step(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_TOAST_H */
