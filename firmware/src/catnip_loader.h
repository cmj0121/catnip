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
} catnip_app_entry;

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
