/*
 * catnip_pages.h - the home section as a state machine, with no board in it
 * (#81).
 *
 * The launcher, the device page and the preference page are three screens the
 * platform owns, and moving between them is a handful of rules: down from the
 * ring is the device page, its two buttons are the preference page and the
 * diagnostic, A keeps and B puts back, long B is always the cat. Those rules
 * lived in main.cpp, inside `loop()`, behind `#include <Arduino.h>`.
 *
 * Which meant the only way to check any of them was to flash a device and look
 * at it. Every navigation bug on this branch was found that way - a preference
 * page that returned to the wrong screen, a device page nothing could reach, a
 * long B that went somewhere different from a short one - and each cost a
 * build, a flash, and a person describing what they saw.
 *
 * So they live here, in plain C, and a host test presses buttons at them.
 *
 * WHAT IT CANNOT DO ITSELF. Four things, and they are all the board: the facts
 * on the device page (what a chip is called is not this file's business), the
 * app icons (they are read off the card), the clock (an I2C part), and the
 * diagnostic (it takes the panel). They arrive through catnip_pages_env, which
 * a host fills with recorders - and that is what lets a test assert the thing
 * no device ever could: that leaving the preference page with B wrote nothing.
 */
#ifndef CATNIP_PAGES_H
#define CATNIP_PAGES_H

#include <stdbool.h>
#include <stdint.h>

#include "catnip_config.h"
#include "catnip_device_info.h"
#include "catnip_menu.h"
#include "catnip_runtime.h"
#include "catnip_settings.h"
#include "catnip_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which of the platform's own screens is up. An app running is none of them:
 * the shell owns the screen then, and this reports HOME because home is where
 * the app was launched from and where it will return to. */
typedef enum {
    CATNIP_PAGE_HOME = 0, /* the carousel */
    CATNIP_PAGE_INFO,     /* what this device is, and the two ways down */
    CATNIP_PAGE_PREF,     /* the preference page */
} catnip_page;

/* The board, as seen from here. Every callback may be NULL; a run with none of
 * them still moves between pages, and only the parts that need a device are
 * missing - which is exactly the arrangement a host test wants. */
typedef struct {
    /* Fill `rows` with what this device is - version, chip, battery, what is
     * answering on the bus - and return how many were written. Asked afresh
     * every time the page opens rather than kept, because half of it moves and
     * a page of facts that were true a while ago is worse than no page. */
    int (*info_rows)(void *ud, char (*rows)[CATNIP_INFO_ROW_MAX], int max);
    /* Apply a setting now, so a brightness can be seen while it is chosen. */
    void (*apply)(void *ud, const catnip_config *cfg);
    /* Write it down. Called once, on the way out, and never on the way that
     * puts everything back - which is the promise a test can hold this to. */
    void (*save)(void *ud, const catnip_config *cfg);
    /* The frame's header. */
    void (*title)(void *ud, const char *title);
    /* Read the icons for these apps off the card, before the tree names them. */
    void (*icons_load)(void *ud, const catnip_app_entry *apps, int n);
    /* Whether a card is in the slot, so an app that needs one greys out. */
    bool (*card_present)(void *ud);
    /* Seconds since the epoch, or 0 for "the clock does not know" - which the
     * ring shows as `--:--` rather than as a confident midnight. */
    uint32_t (*now_epoch)(void *ud);
    /* Hand the screen to the input diagnostic. It does not come back on its
     * own; the caller's own long-B handling gives it back. */
    void (*enter_diag)(void *ud);
    void *ud;
} catnip_pages_env;

typedef struct catnip_pages catnip_pages;

/* Build the three screens on `rt`. `cfg` is the live configuration: this reads
 * it, steps it, and hands it to apply/save - it does not own it, because the
 * board applied it at boot and will go on reading it. */
catnip_pages *catnip_pages_new(catnip_rt *rt, catnip_shell *shell, catnip_config *cfg,
                               const catnip_pages_env *env);
void catnip_pages_free(catnip_pages *p);

/* Draw the launcher and make it the visible screen, and *be* on it: drawing the
 * ring and believing some other page is up is the same bug twice - the way back
 * from the diagnostic did exactly that, leaving presses routed to a page nobody
 * could see. Called at boot, every time an app returns, and on the way out of
 * anywhere below. */
void catnip_pages_rebuild(catnip_pages *p);

/* One pass: act on what the input step asked for, and on whatever the pages
 * themselves have latched since the last call. `gesture` is a
 * CATNIP_UI_GESTURE_* value.
 *
 * Returns true when this pass belonged to one of these pages - it entered one,
 * left one, or is sitting on one - which is what tells the caller not to also
 * hand the gesture to a running app.
 *
 * A back arriving on the preference page is checked against the render claim
 * here, and only here: the claim is cleared as it is read, and a running app's
 * own answer to on_back is the shell's to read. */
bool catnip_pages_step(catnip_pages *p, int gesture);

/* The launcher's live cells (#75): the clock the ring shows without opening it.
 * Separate from the step because it runs on its own slow cadence - the gauge
 * and the clock do not move at frame rate, and asking the I2C bus thirty times
 * a second for a number that changes once a minute is a bus and a battery spent
 * on nothing. */
void catnip_pages_glance(catnip_pages *p);

catnip_page catnip_pages_current(const catnip_pages *p);

/* Which app the launcher wants started, or NULL. The caller launches it,
 * because launching is the shell's and what happens when it fails - put the
 * menu back rather than leave a blank screen - is the caller's. */
const char *catnip_pages_take_launch(catnip_pages *p);

/* The launcher itself, for the two things the board still says to it directly:
 * that home is where the ring should open, and the app name in the bar. */
catnip_menu *catnip_pages_menu(catnip_pages *p);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PAGES_H */
