/*
 * catnip_icon_map.h - catnip_icon names, and the old U+E000 glyph ids.
 *
 * The twelve shapes are colour images (`catnip_icon_img_14`) now, not a
 * fallback font. The U+E000 table remains so a host test can still name each
 * id without linking LVGL; the device backend does not put those codepoints
 * in a label.
 */
#ifndef CATNIP_ICON_MAP_H
#define CATNIP_ICON_MAP_H

#include "catnip_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CATNIP_ICON_CP_BASE 0xE000

/* UTF-8 of U+E000+(icon-1), or NULL for NONE / unknown. */
const char *catnip_icon_glyph(catnip_icon icon);

/* The name an app writes for this icon ("folder"), or "none". The set is small,
 * bounded and named in exactly one place: a second copy of it - in the renderer
 * resolving Lua's string, or in a test spelling a transcript - is a table that
 * silently disagrees the day the set grows. */
const char *catnip_icon_name(catnip_icon icon);

/* The icon an app named, or CATNIP_ICON_NONE for a name this firmware does not
 * know. Degrading rather than raising is the same promise style roles make: an
 * app written against a later firmware loses an icon on an older one instead of
 * failing to open. */
catnip_icon catnip_icon_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_ICON_MAP_H */
