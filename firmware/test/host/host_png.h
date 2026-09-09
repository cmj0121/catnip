/*
 * host_png.h - a screen, as a file you can look at (#81).
 *
 * The last part of testing without a board is not a test at all: it is being
 * able to *see* a page without a build, a flash that fails a third of the time
 * on this unit, and ninety seconds. LVGL renders into memory here, so the
 * picture already exists - all that was missing was somewhere to put it.
 *
 * A PNG rather than a raw dump, so it opens in anything, and written by hand
 * rather than by a library: this is a test harness and a new dependency in it
 * would have to be justified to everyone who builds the project. The
 * compression is deflate's "stored" mode - no compression at all - which for a
 * 320x240 screenshot is 230 KB nobody keeps.
 */
#ifndef CATNIP_HOST_PNG_H
#define CATNIP_HOST_PNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write `w` x `h` RGB565 pixels to `path` as a PNG. Returns false if the file
 * could not be opened. */
bool catnip_host_png_write(const char *path, int w, int h, const uint16_t *px);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_HOST_PNG_H */
