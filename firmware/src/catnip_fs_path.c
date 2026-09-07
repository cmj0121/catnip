/* fs_path.c - see fs_path.h. */
#include "catnip_fs_path.h"

#include <stdio.h>
#include <string.h>

/* True when the segment of `len` bytes starting at `s` is one the rule refuses:
 * empty, "." or "..". Everything else is an ordinary name, dots included. */
static bool segment_is_refused(const char *s, size_t len)
{
    if (len == 0) return true;
    if (len == 1 && s[0] == '.') return true;
    if (len == 2 && s[0] == '.' && s[1] == '.') return true;
    return false;
}

bool catnip_fs_resolve(const char *base, const char *name, char *out, size_t cap)
{
    /* Cleared first, so that every refusal below can simply return and a caller
     * who ignored the answer still finds nothing usable in the buffer. */
    out[0] = '\0';

    /* An empty name is the root itself and has no segments to walk. */
    if (name[0] != '\0') {
        const char *seg = name;
        for (;;) {
            const char *sep = strchr(seg, '/');
            size_t len = sep ? (size_t)(sep - seg) : strlen(seg);

            if (segment_is_refused(seg, len)) return false;
            if (!sep) break;
            seg = sep + 1;
        }
    }

    /* Nothing left to normalise: every segment that could have moved the path
     * was refused above, so the join is the whole of the answer. `base` carries
     * no trailing separator (see sd_mount.h), which is what makes one '/' here
     * correct. */
    int n = name[0] ? snprintf(out, cap, "%s/%s", base, name)
                    : snprintf(out, cap, "%s", base);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return false;
    }
    return true;
}
