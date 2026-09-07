/*
 * catnip_shell.h - the shell that owns the device experience (issue #5).
 *
 * The shell lists the apps found under the apps root, launches one, drives its
 * lifecycle cooperatively, and returns to the menu when it finishes or faults.
 * It ties together the loader (#4), the runtime (#1) and the scheduler (#9).
 *
 * This is the controller. Rendering the menu and a status bar (battery/wifi/
 * time) on the LVGL display is device work and comes with the UI renderer.
 */
#ifndef CATNIP_SHELL_H
#define CATNIP_SHELL_H

#include <stddef.h>

#include "catnip_loader.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CATNIP_SHELL_MENU = 0,   /* showing the app list */
    CATNIP_SHELL_RUNNING = 1 /* an app is running */
};

#define CATNIP_SHELL_MAX_APPS 32

typedef struct catnip_shell catnip_shell;

/* Create a shell over `rt`, discovering apps under `apps_root`. `now`/`pump`/`ud`
 * are the scheduler's host hooks. Opens the ui module. Returns NULL on failure. */
catnip_shell *catnip_shell_new(catnip_rt *rt, const char *apps_root, catnip_now_fn now,
                               catnip_pump_fn pump, void *ud);

/* The backend the running app's screens are drawn through. The shell keeps it
 * only so that it can tear a finished app's widgets down through the same
 * vtable that built them; it does not run render passes itself, because the
 * pass belongs in the main loop next to the display's own step. `be` must
 * outlive the shell. */
void catnip_shell_set_backend(catnip_shell *s, const catnip_render_backend *be);

/* Re-scan the apps root. Returns the number of apps found. */
int catnip_shell_refresh(catnip_shell *s);

int catnip_shell_count(const catnip_shell *s);
const catnip_app_entry *catnip_shell_app(const catnip_shell *s, int index);
int catnip_shell_state(const catnip_shell *s);

/* Launch the app at `index` (or by `id`). Returns 0 and enters RUNNING; on error
 * stays in MENU, returns non-zero, and writes a message to errbuf. */
int catnip_shell_launch(catnip_shell *s, int index, char *errbuf, size_t errlen);
int catnip_shell_launch_id(catnip_shell *s, const char *id, char *errbuf, size_t errlen);

/* Advance the running app by one step; returns to MENU when it finishes or
 * faults. Returns the shell state. */
int catnip_shell_step(catnip_shell *s);

/* Stop the running app and return to the menu. */
void catnip_shell_exit(catnip_shell *s);

void catnip_shell_free(catnip_shell *s);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SHELL_H */
