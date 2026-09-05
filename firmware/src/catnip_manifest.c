/* catnip_manifest.c - see catnip_manifest.h. */
#include "catnip_manifest.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static void copy_str(char *dst, size_t cap, const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, cap, "%s", item->valuestring);
    }
}

static int fail(char *errbuf, size_t errlen, const char *msg)
{
    if (errbuf && errlen) snprintf(errbuf, errlen, "%s", msg);
    return -1;
}

/* Parse "MAJOR.MINOR" into two ints. Returns 0 on success. */
static int parse_api(const char *s, int *major, int *minor)
{
    if (!s) return -1;
    char extra = 0;
    if (sscanf(s, "%d.%d%c", major, minor, &extra) != 2) return -1;
    if (*major < 0 || *minor < 0) return -1;
    return 0;
}

int catnip_manifest_parse(const char *json, catnip_manifest *out, char *errbuf,
                          size_t errlen)
{
    if (!json || !out) return fail(errbuf, errlen, "no input");
    memset(out, 0, sizeof(*out));
    snprintf(out->entry, sizeof(out->entry), "%s", "main.lua");

    cJSON *root = cJSON_Parse(json);
    if (!root) return fail(errbuf, errlen, "invalid JSON");

    int rc = 0;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *api = cJSON_GetObjectItemCaseSensitive(root, "catnip_api");

    if (!cJSON_IsString(id) || !id->valuestring[0]) {
        rc = fail(errbuf, errlen, "missing 'id'");
        goto done;
    }
    if (!cJSON_IsString(name) || !name->valuestring[0]) {
        rc = fail(errbuf, errlen, "missing 'name'");
        goto done;
    }
    if (!cJSON_IsString(api) ||
        parse_api(api->valuestring, &out->api_major, &out->api_minor) != 0) {
        rc = fail(errbuf, errlen, "missing or malformed 'catnip_api'");
        goto done;
    }

    copy_str(out->id, sizeof(out->id), id);
    copy_str(out->name, sizeof(out->name), name);
    copy_str(out->version, sizeof(out->version),
             cJSON_GetObjectItemCaseSensitive(root, "version"));
    copy_str(out->author, sizeof(out->author),
             cJSON_GetObjectItemCaseSensitive(root, "author"));
    copy_str(out->icon, sizeof(out->icon),
             cJSON_GetObjectItemCaseSensitive(root, "icon"));

    const cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, "entry");
    if (cJSON_IsString(entry) && entry->valuestring[0]) {
        copy_str(out->entry, sizeof(out->entry), entry);
    }

    const cJSON *perms = cJSON_GetObjectItemCaseSensitive(root, "permissions");
    if (cJSON_IsArray(perms)) {
        const cJSON *p = NULL;
        cJSON_ArrayForEach(p, perms)
        {
            if (out->n_permissions >= CATNIP_MAX_PERMISSIONS) break;
            if (cJSON_IsString(p) && p->valuestring) {
                snprintf(out->permissions[out->n_permissions],
                         sizeof(out->permissions[0]), "%s", p->valuestring);
                out->n_permissions++;
            }
        }
    }

done:
    cJSON_Delete(root);
    return rc;
}

int catnip_manifest_compatible(const catnip_manifest *m)
{
    if (!m) return 0;
    if (m->api_major != CATNIP_API_MAJOR) return 0;
    if (m->api_minor > CATNIP_API_MINOR) return 0;
    return 1;
}
