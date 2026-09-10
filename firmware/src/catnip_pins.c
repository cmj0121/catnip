/* catnip_pins.c - see catnip_pins.h. */
#include "catnip_pins.h"

#include <string.h>

/* Find `id` as a whole comma-separated field in `csv`. Returns a pointer to the
 * start of the field, or NULL. Whole-field so a prefix does not match: the
 * field runs to a comma or the end, and its length must equal id's. */
static char *find_field(const char *csv, const char *id)
{
    size_t idlen = strlen(id);
    const char *p = csv;

    if (!id[0]) return NULL;
    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == idlen && strncmp(p, id, idlen) == 0) return (char *)p;
        if (!end) break;
        p = end + 1;
    }
    return NULL;
}

bool catnip_pins_contains(const char *csv, const char *id)
{
    if (!csv || !id) return false;
    return find_field(csv, id) != NULL;
}

bool catnip_pins_toggle(char *csv, size_t cap, const char *id)
{
    char *at;
    size_t idlen, cur;

    if (!csv || !id || !id[0] || cap == 0) return false;
    idlen = strlen(id);
    at = find_field(csv, id);

    if (at) {
        /* Remove the field and one adjacent comma, so no empty field is left
         * behind. Prefer the comma after; if this was the last field, take the
         * one before instead. */
        char *end = strchr(at, ',');
        if (end) {
            memmove(at, end + 1, strlen(end + 1) + 1);
        } else if (at != csv) {
            *(at - 1) = '\0'; /* drop the trailing comma and this field */
        } else {
            csv[0] = '\0'; /* the only field */
        }
        return true;
    }

    /* Append, with a separator if the list is not empty. Dropped whole if it
     * would not fit. */
    cur = strlen(csv);
    if (cur == 0) {
        if (idlen + 1 > cap) return false;
        memcpy(csv, id, idlen + 1);
    } else {
        if (cur + 1 + idlen + 1 > cap) return false;
        csv[cur] = ',';
        memcpy(csv + cur + 1, id, idlen + 1);
    }
    return true;
}
