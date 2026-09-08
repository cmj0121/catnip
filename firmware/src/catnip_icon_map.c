/* catnip_icon_map.c - see catnip_icon_map.h. */
#include "catnip_icon_map.h"

#include <string.h>

/* The one table. Its order is the enum's, so an icon added in the middle of
 * catnip_icon without a name added here fails to compile rather than shifting
 * every name by one. */
static const char *const kNames[] = {
    "folder",
    "file",
    "placeholder",
    "image",
    "audio",
    "settings",
    "edit",
    "trash",
    "refresh",
    "warning",
    "ok",
    "close",
    /* Not a generated glyph - see catnip_icon. Named here so the launcher can
     * ask for it the way anything else asks for an icon. */
    "mascot",
};

/* The table and the enum cannot disagree without failing to build. They did
 * once: a name was left out while the bound check that guards this array was
 * widened, and catnip_icon_name() read one past the end with no symptom at
 * all. */
typedef char catnip_icon_names_match[(sizeof(kNames) / sizeof(kNames[0]) ==
                                      CATNIP_ICON_MASCOT - CATNIP_ICON_FOLDER + 1)
                                         ? 1
                                         : -1];

const char *catnip_icon_name(catnip_icon icon)
{
    if (icon < CATNIP_ICON_FOLDER || icon > CATNIP_ICON_MASCOT) return "none";
    return kNames[icon - CATNIP_ICON_FOLDER];
}

catnip_icon catnip_icon_from_name(const char *name)
{
    if (!name) return CATNIP_ICON_NONE;
    for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
        if (strcmp(name, kNames[i]) == 0) return (catnip_icon)(CATNIP_ICON_FOLDER + i);
    return CATNIP_ICON_NONE;
}

const char *catnip_icon_glyph(catnip_icon icon)
{
    /* U+E000..U+E00B as 3-byte UTF-8: EE 80 80 .. EE 80 8B. */
    static const char kGlyphs[][4] = {
        "\xEE\x80\x80", "\xEE\x80\x81", "\xEE\x80\x82", "\xEE\x80\x83",
        "\xEE\x80\x84", "\xEE\x80\x85", "\xEE\x80\x86", "\xEE\x80\x87",
        "\xEE\x80\x88", "\xEE\x80\x89", "\xEE\x80\x8A", "\xEE\x80\x8B",
    };

    if (icon < CATNIP_ICON_FOLDER || icon > CATNIP_ICON_CLOSE) return NULL;
    return kGlyphs[icon - CATNIP_ICON_FOLDER];
}
