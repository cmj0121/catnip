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
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
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

int catnip_loader_open(const char *dir, catnip_manifest *m, char **code,
                       char *errbuf, size_t errlen)
{
    if (!dir || !m || !code) return fail(errbuf, errlen, "bad arguments");

    char mpath[320];
    snprintf(mpath, sizeof(mpath), "%s/manifest.json", dir);
    char *json = read_file(mpath);
    if (!json) return fail(errbuf, errlen, "cannot read manifest.json");

    int rc = catnip_manifest_parse(json, m, errbuf, errlen);
    free(json);
    if (rc != 0) return rc;

    if (!catnip_manifest_compatible(m)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "app needs catnip_api %d.%d, this platform is %d.%d",
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
