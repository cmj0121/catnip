/*
 * Native test for the catnip icon mapping table and packed colour bits.
 *
 * lvgl_backend.cpp is not on this target, so a wrong name or an empty raster
 * has to fail here. The RGB565A8 arrays are in the header; the SVGs must
 * round-trip to them via gen_icons.py --check.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_icon_bits.h"
#include "catnip_icon_map.h"

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

#define PX        14
#define RGB_BYTES (PX * PX * 2)
#define A_BYTES   (PX * PX)

static const uint8_t *alpha14(int n)
{
    return catnip_icon_rgb565a8_14[n] + RGB_BYTES;
}

static int alpha_nonzero(int n)
{
    const uint8_t *a = alpha14(n);
    int i;

    for (i = 0; i < A_BYTES; i++)
        if (a[i]) return 1;
    return 0;
}

static int alpha_equal(int a, int b)
{
    return memcmp(alpha14(a), alpha14(b), A_BYTES) == 0;
}

static int utf8_eq(const char *s, unsigned a, unsigned b, unsigned c)
{
    return s && (unsigned char)s[0] == a && (unsigned char)s[1] == b &&
           (unsigned char)s[2] == c && s[3] == '\0';
}

int main(void)
{
    int n, a, b, distinct, inked;

    CHECK(CATNIP_ICON_FOLDER == 1 && CATNIP_ICON_CLOSE == 12,
          "enum order folder=1 close=12");
    CHECK(CATNIP_ICON_CP_BASE == 0xE000, "codepoint base is U+E000");
    CHECK(utf8_eq(catnip_icon_glyph(CATNIP_ICON_FOLDER), 0xEE, 0x80, 0x80),
          "folder is U+E000 (EE 80 80)");
    CHECK(utf8_eq(catnip_icon_glyph(CATNIP_ICON_CLOSE), 0xEE, 0x80, 0x8B),
          "close is U+E00B (EE 80 8B)");
    CHECK(catnip_icon_glyph(CATNIP_ICON_NONE) == NULL, "NONE is NULL");
    CHECK(catnip_icon_glyph((catnip_icon)13) == NULL, "unknown is NULL");
    CHECK(strcmp(catnip_icon_name(CATNIP_ICON_FOLDER), "folder") == 0, "folder name");
    CHECK(catnip_icon_from_name("close") == CATNIP_ICON_CLOSE, "close from name");
    CHECK(catnip_icon_from_name("hologram") == CATNIP_ICON_NONE, "unknown name is NONE");

    inked = 1;
    for (n = 0; n < 12; n++)
        if (!alpha_nonzero(n)) inked = 0;
    CHECK(inked, "every 14px raster has some alpha");
    CHECK(!alpha_equal(CATNIP_ICON_OK - 1, CATNIP_ICON_CLOSE - 1),
          "ok alpha != close alpha");

    distinct = 1;
    for (a = 0; a < 12; a++)
        for (b = a + 1; b < 12; b++)
            if (alpha_equal(a, b)) distinct = 0;
    CHECK(distinct, "every pair of 14px masks differs");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
