/*
 * catnip_font.h - the faces the platform draws in.
 *
 * One typeface at five sizes, cut from assets/fonts/MonaspaceNeon-Regular.otf
 * by tools/gen_font.py and committed, the way the icons are - so a build on a
 * bare runner needs neither a font renderer nor this script.
 *
 * **Monospaced, on purpose.** Nearly everything this device shows is a value
 * beside a label: a channel next to a name, a strength next to both, a heap
 * size under a version. In a proportional face those columns line up only by
 * accident - the number after a short name sits somewhere else than the number
 * after a long one - and a page has to be read across instead of down. A
 * monospaced face makes the columns a property of the type rather than of
 * whichever strings happen to be in them.
 *
 * The four prose sizes are the four the style roles name: caption, body, title,
 * and the one a canvas cell uses. Nothing else may ask for a size, which is
 * what the roles are for.
 *
 * `catnip_font_display` takes digits and nothing else - `0-9`, a colon, a full
 * stop, a minus, a space. Ask for it with letters in the string and you get
 * missing-glyph boxes, on purpose: it exists for a quantity that has earned the
 * whole panel, and a face that also worked as "big text" would immediately
 * become that instead, leaving the five prose roles no longer the only way to
 * set words.
 */
#ifndef CATNIP_FONT_H
#define CATNIP_FONT_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t catnip_font_10;
extern const lv_font_t catnip_font_16;
extern const lv_font_t catnip_font_20;
extern const lv_font_t catnip_font_24;
extern const lv_font_t catnip_font_display;

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_FONT_H */
