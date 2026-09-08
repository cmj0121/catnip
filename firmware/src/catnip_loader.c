/* catnip_loader.c - see catnip_loader.h. */
#include "catnip_loader.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) snprintf(errbuf, errlen, "%s", msg);
    return -1;
}

/* Read a whole file into a malloc'd, NUL-terminated buffer. */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Present only in a device build: tools/gen_apps.py writes the header and
 * defines this, so the host build is not silently changed by whether a
 * generated file happens to be lying around. Its tests drive an app from its
 * own directory, which is the source these were made from. */
#ifdef CATNIP_BUILTIN_APPS
#include "generated/builtin_apps.h"
#else
typedef struct {
    const char *id;
    const char *name;
    const char *icon_name;
    const char *manifest;
    const char *lua;
    const unsigned char *icon;
    size_t icon_len;
} catnip_builtin_app;
static const catnip_builtin_app catnip_builtin_apps[1];
#define CATNIP_BUILTIN_APP_COUNT 0
#endif

#define BUILTIN_PREFIX "builtin:"

/* Whether a manifest asks for storage. Read off the permissions it already
 * declares rather than from a new field: an app that says it reads files has
 * said everything the launcher needs to know about whether it can run without
 * a card. */
static int manifest_needs_fs(const catnip_manifest *m)
{
    for (int i = 0; i < m->n_permissions; i++)
        if (strncmp(m->permissions[i], "fs.", 3) == 0) return 1;
    return 0;
}

static const catnip_builtin_app *builtin_by_id(const char *id)
{
    for (int i = 0; i < CATNIP_BUILTIN_APP_COUNT; i++)
        if (strcmp(catnip_builtin_apps[i].id, id) == 0) return &catnip_builtin_apps[i];
    return NULL;
}

int catnip_loader_builtin(catnip_app_entry *out, int max)
{
    int n = 0;
    for (int i = 0; i < CATNIP_BUILTIN_APP_COUNT && n < max; i++) {
        const catnip_builtin_app *b = &catnip_builtin_apps[i];
        catnip_app_entry *e = &out[n];
        memset(e, 0, sizeof(*e));
        snprintf(e->id, sizeof(e->id), "%s", b->id);
        snprintf(e->name, sizeof(e->name), "%s", b->name);
        snprintf(e->icon, sizeof(e->icon), "%s", b->icon_name);
        snprintf(e->dir, sizeof(e->dir), BUILTIN_PREFIX "%s", b->id);
        catnip_manifest m;
        e->compatible = (catnip_manifest_parse(b->manifest, &m, NULL, 0) == 0) &&
                        catnip_manifest_compatible(&m);
        if (e->compatible) e->needs_fs = manifest_needs_fs(&m);
        n++;
    }
    return n;
}

const unsigned char *catnip_loader_builtin_icon(const char *id, size_t *len)
{
    const catnip_builtin_app *b = id ? builtin_by_id(id) : NULL;
    if (!b || !b->icon) return NULL;
    if (len) *len = b->icon_len;
    return b->icon;
}

int catnip_loader_discover(const char *apps_root, catnip_app_entry *out, int max)
{
    DIR *d = opendir(apps_root);
    if (!d) return -1;

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        if (e->d_name[0] == '.') continue; /* skip . .. and hidden */

        char dir[256];
        snprintf(dir, sizeof(dir), "%s/%s", apps_root, e->d_name);
        if (!is_dir(dir)) continue;

        char mpath[320];
        snprintf(mpath, sizeof(mpath), "%s/manifest.json", dir);
        if (!is_file(mpath)) continue;

        catnip_app_entry *entry = &out[count];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->dir, sizeof(entry->dir), "%s", dir);
        snprintf(entry->id, sizeof(entry->id), "%s", e->d_name);

        char *json = read_file(mpath);
        if (json) {
            catnip_manifest m;
            if (catnip_manifest_parse(json, &m, NULL, 0) == 0) {
                snprintf(entry->id, sizeof(entry->id), "%s", m.id);
                snprintf(entry->name, sizeof(entry->name), "%s", m.name);
                snprintf(entry->icon, sizeof(entry->icon), "%s", m.icon);
                entry->compatible = catnip_manifest_compatible(&m);
                entry->needs_fs = manifest_needs_fs(&m);
            } else {
                snprintf(entry->name, sizeof(entry->name), "%s", "(invalid manifest)");
                entry->compatible = 0;
            }
            free(json);
        }
        count++;
    }
    closedir(d);
    return count;
}

int catnip_loader_open(const char *dir, catnip_manifest *m, char **code, char *errbuf,
                       size_t errlen)
{
    if (!dir || !m || !code) return fail(errbuf, errlen, "bad arguments");

    /* A built-in app has no directory: its manifest and its source are in
     * flash. Handled here rather than at the call site so the shell launches
     * both kinds through one path and cannot treat them differently. */
    if (strncmp(dir, BUILTIN_PREFIX, sizeof(BUILTIN_PREFIX) - 1) == 0) {
        const catnip_builtin_app *b = builtin_by_id(dir + sizeof(BUILTIN_PREFIX) - 1);
        if (!b) return fail(errbuf, errlen, "no such built-in app");
        if (catnip_manifest_parse(b->manifest, m, errbuf, errlen) != 0) return -1;
        if (!catnip_manifest_compatible(m))
            return fail(errbuf, errlen, "the app needs a newer catnip");
        size_t n = strlen(b->lua);
        char *buf = (char *)malloc(n + 1);
        if (!buf) return fail(errbuf, errlen, "out of memory");
        memcpy(buf, b->lua, n + 1);
        *code = buf;
        return 0;
    }

    char mpath[320];
    snprintf(mpath, sizeof(mpath), "%s/manifest.json", dir);
    char *json = read_file(mpath);
    if (!json) return fail(errbuf, errlen, "cannot read manifest.json");

    int rc = catnip_manifest_parse(json, m, errbuf, errlen);
    free(json);
    if (rc != 0) return rc;

    if (!catnip_manifest_compatible(m)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "app needs catnip_api %d.%d, this platform is %d.%d",
                 m->api_major, m->api_minor, CATNIP_API_MAJOR, CATNIP_API_MINOR);
        return fail(errbuf, errlen, msg);
    }

    char epath[384];
    snprintf(epath, sizeof(epath), "%s/%s", dir, m->entry);
    char *entry_code = read_file(epath);
    if (!entry_code) return fail(errbuf, errlen, "cannot read entry script");

    *code = entry_code;
    return 0;
}
