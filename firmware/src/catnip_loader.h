/*
 * catnip_loader.h - discover and open catnip apps on the filesystem (issue #4).
 *
 * Apps live as folders under an apps root (on the device, /catnip/apps/<id>/),
 * each with a manifest.json and an entry script. Discovery only reads manifests
 * (no VM), so the shell can list apps cheaply; opening an app parses its
 * manifest, checks API compatibility, and reads the entry script for the shell
 * to run.
 */
#ifndef CATNIP_LOADER_H
#define CATNIP_LOADER_H

#include <stddef.h>

#include "catnip_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[64];
    char name[64];
    char icon[64];  /* icon filename from the manifest (for the launcher) */
    char dir[256];  /* full path to the app folder */
    int compatible; /* 1 if its manifest parsed and the API is satisfiable */
    /* 1 if the manifest asks for the filesystem. Compatibility is about the
     * API version and never changes; this is about what the device has in it
     * right now, and a card can be taken out between one launch and the next -
     * so the answer to "can this run" is not stored here, only the question. */
    int needs_fs;
} catnip_app_entry;

/* The apps compiled into the firmware, listed before anything on the card.
 *
 * A built-in app is the same thing as one under /catnip/apps - the same
 * manifest, the same main.lua, the same ui.* - and it is opened through the
 * same catnip_loader_open(). What it cannot do is live on a card, because a
 * card is removable and the File Browser is how you look at one.
 *
 * Their `dir` is "builtin:<id>" rather than a path, which is what
 * catnip_loader_open() recognises. It is not a path anything can open, and that
 * is deliberate: a built-in app has no directory to be found in, so nothing can
 * accidentally read one from the filesystem and get a different answer.
 *
 * Returns how many were written, which is 0 on a host build where nothing was
 * generated. */
int catnip_loader_builtin(catnip_app_entry *out, int max);

/* The icon file's bytes for a built-in app, or NULL. The launcher needs them
 * because a built-in app's icon is not on any filesystem to be read from. */
const unsigned char *catnip_loader_builtin_icon(const char *id, size_t *len);

/* Scan `apps_root` for subfolders containing a manifest.json. Fills up to `max`
 * entries in `out`; returns the count found, or -1 if the root can't be opened. */
int catnip_loader_discover(const char *apps_root, catnip_app_entry *out, int max);

/* Open the app in `dir`: parse manifest.json, require API compatibility, and
 * read the entry script into a freshly malloc'd buffer (caller frees *code).
 * Returns 0 on success; on error returns non-zero and writes a message into
 * errbuf, leaving *code untouched. */
int catnip_loader_open(const char *dir, catnip_manifest *m, char **code, char *errbuf,
                       size_t errlen);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_LOADER_H */
