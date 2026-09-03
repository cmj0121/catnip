/*
 * catnip_manifest.h - parse and validate an app's manifest.json (issue #4).
 *
 * Every catnip app ships a manifest.json declaring its identity, entry point,
 * and the platform API it needs. The `catnip_api` field is the stability
 * contract: the loader refuses an app that needs a newer platform than this one.
 */
#ifndef CATNIP_MANIFEST_H
#define CATNIP_MANIFEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform API version this firmware implements. */
#define CATNIP_API_MAJOR 1
#define CATNIP_API_MINOR 0

#define CATNIP_MAX_PERMISSIONS 16

typedef struct {
    char id[64];
    char name[64];
    char version[32];
    char author[64];
    char entry[64]; /* defaults to "main.lua" */
    char icon[64];
    int api_major;
    int api_minor;
    char permissions[CATNIP_MAX_PERMISSIONS][32];
    int n_permissions;
} catnip_manifest;

/* Parse manifest JSON text into `out`. Required fields: id, name, catnip_api
 * ("MAJOR.MINOR"). Returns 0 on success; on error returns non-zero and writes a
 * short message into errbuf. */
int catnip_manifest_parse(const char *json, catnip_manifest *out, char *errbuf,
                          size_t errlen);

/* 1 if the manifest's catnip_api is satisfiable by this platform (same major,
 * minor <= platform minor), else 0. */
int catnip_manifest_compatible(const catnip_manifest *m);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_MANIFEST_H */
