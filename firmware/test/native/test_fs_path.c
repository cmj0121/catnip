/* Native test for issue #45: the rule that confines a Lua app to fs_base.
 *
 * This is string arithmetic with no hardware in it and one clear failure mode -
 * a path that leaves the card reaches the rest of the device - so it is pinned
 * here rather than discovered on a board. The interesting cases are not the
 * leading "..": they are the ones a check written in a hurry lets through. */
#include <stdio.h>
#include <string.h>

#include "catnip_fs_path.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* The MeowKit's root, so the cases read as they would on the device. */
#define BASE "/sd"

static void allows(const char *name, const char *want, const char *why)
{
    char out[64];
    bool ok = catnip_fs_resolve(BASE, name, out, sizeof(out));
    CHECK(ok && strcmp(out, want) == 0, why);
}

static void refuses(const char *name, const char *why)
{
    char out[64];
    bool ok = catnip_fs_resolve(BASE, name, out, sizeof(out));
    /* The empty buffer is asserted as well as the false. A caller that forgot
     * to check the return value must not find a usable path waiting in it. */
    CHECK(!ok && out[0] == '\0', why);
}

int main(void)
{
    printf("== fs path confinement ==\n");

    /* Ordinary names, including the ones that only look dangerous. */
    allows("", BASE, "the empty name is the root itself");
    allows("a.txt", BASE "/a.txt", "a plain name lands under the root");
    allows("sub/inner.txt", BASE "/sub/inner.txt", "subdirectories are allowed");
    allows("a..b.txt", BASE "/a..b.txt", "dots inside a name are not a climb");
    allows("..hidden", BASE "/..hidden", "a name beginning with dots is still a name");
    allows("...", BASE "/...", "three dots is a name, not a segment that moves");

    /* Climbing out, in each of the shapes it comes in. */
    refuses("..", "\"..\" alone is refused");
    refuses("../etc/passwd", "a climb at the front is refused");
    refuses("a/../../etc", "a climb in the middle is refused too");
    refuses("sub/..", "a path that normalises back to the root is still refused");
    refuses("sub/../sub/inner.txt",
            "a climb that ends up somewhere legal is refused, not repaired");

    /* Segments that mean nothing, which exist to pad a path past a naive check. */
    refuses(".", "\".\" alone is refused");
    refuses("./a.txt", "a leading \".\" is refused");
    refuses("a/./b", "a \".\" in the middle is refused");

    /* Absolute paths. The last one is the case a prefix comparison gets wrong:
     * "/sdcard/x" starts with the whole of "/sd" and is not inside it. */
    refuses("/", "the bare separator is refused");
    refuses("/etc/passwd", "an absolute path is refused");
    refuses("/sd/a.txt", "an absolute path is refused even when it names the root");
    refuses("/sdcard/x", "a sibling that merely starts with the root's text is refused");

    /* Separators that change what a later join builds. */
    refuses("sub/", "a trailing separator is refused");
    refuses("a//b", "a doubled separator is refused");

    /* A different root, so nothing above passes by having "/sd" built in. */
    {
        char out[64];
        bool ok = catnip_fs_resolve("/tmp/catnip", "a.txt", out, sizeof(out));
        CHECK(ok && strcmp(out, "/tmp/catnip/a.txt") == 0,
              "the root is whatever base is");
    }

    /* Too long to fit. Truncating would hand back a real path that is not the
     * one asked for, which for fs.delete() is a different file. */
    {
        char small[8];
        bool ok =
            catnip_fs_resolve(BASE, "a-name-that-does-not-fit.txt", small, sizeof(small));
        CHECK(!ok && small[0] == '\0', "a name that does not fit is refused, not cut");
    }
    {
        /* Three bytes: "/sd" needs four with its terminator. */
        char small[3];
        bool ok = catnip_fs_resolve(BASE, "", small, sizeof(small));
        CHECK(!ok && small[0] == '\0', "a root that does not fit is refused as well");
    }

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
