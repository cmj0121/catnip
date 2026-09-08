/*
 * app_icon.h - an app's own icon, as something LVGL can draw (#34).
 *
 * A category glyph is the platform's and lives in flash as raw pixels; an app's
 * identity icon is the app's own, arrives as an encoded PNG, and has to be
 * decoded before it is a picture. Keeping the two apart is the whole point: a
 * row costs no decode, and only the launcher pays for one.
 *
 * WHEN IT IS DECODED. Never in a render pass. The bytes are read once when the
 * app list is built and handed to LVGL as an image source; LVGL's own decoder
 * cache turns them into pixels on first draw and keeps them. Reading a file
 * from the card inside catnip_render() would put an SD transaction inside the
 * frame that draws it.
 *
 * WHERE THE BYTES COME FROM. Two places and one shape: a built-in app's icon is
 * already in flash, and a card app's is read into PSRAM. Both end up as the
 * same lv_image_dsc_t holding encoded bytes, so nothing downstream knows or
 * cares which kind of app it is looking at.
 */
#ifndef CATNIP_APP_ICON_H
#define CATNIP_APP_ICON_H

#include "../catnip_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Take the icons for `n` apps. Replaces whatever was held before, so it is
 * called once per app-list refresh and never per frame. An app with no icon, or
 * one whose file cannot be read, simply has none - the launcher falls back to
 * the placeholder glyph, which is what that glyph is for. */
void catnip_app_icons_load(const catnip_app_entry *apps, int n);

/* The image source for `id`, or NULL. The returned pointer is owned here and
 * stays valid until the next load; it is what `image` on a node names. */
const void *catnip_app_icon_find(const char *id);

/* Release every icon and every decoded copy LVGL made of them. */
void catnip_app_icons_free(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_APP_ICON_H */
