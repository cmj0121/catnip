/*
 * display.h - the ST7789 panel, brought up and ready to take pixels.
 *
 * The panel is write-only (MISO is not wired; its CS and RST hang off the I/O
 * expander, see ioexp.h), so nothing here reads back from it: what we push is
 * what is on screen. Callers get a raw blit and backlight control; anything
 * richer belongs to the renderer.
 */
#ifndef CATNIP_DISPLAY_H
#define CATNIP_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the panel up with the backlight off, so the first frame drawn is the
 * first thing seen rather than a flash of uninitialised memory. Returns true
 * on success. */
bool catnip_display_begin(void);

/* Fade the backlight to `level` (0-255). */
void catnip_display_backlight(uint8_t level);

/* Push a full-screen RGB565 image. `data` is CATNIP_SCREEN_W * CATNIP_SCREEN_H
 * pixels, little-endian, which is the ESP32's native uint16_t layout - the
 * driver handles whatever byte order the panel wants on the wire. */
void catnip_display_blit(const void *data);

/* Paint the whole screen one RGB565 colour. */
void catnip_display_fill(uint16_t rgb565);

/* Load an animation's frames from a directory on the card, in filename order:
 * .jpg, .jpeg, .png and .qoi are all understood. Each frame is decoded once
 * into PSRAM, so playing them afterwards costs no decoding.
 *
 * Returns how many frames are ready to show, which is zero when the directory
 * is missing, holds no images, or holds nothing that could be decoded - all of
 * which mean "use the built-in mascot" rather than "fail". */
int catnip_display_load_frames(const char *dir);

/* Show a frame loaded by catnip_display_load_frames. */
void catnip_display_show_frame(int index);

/* One filled circle, straight onto the panel.
 *
 * The busy ring at boot, and nothing else. LVGL is not up until an app draws
 * its first widget, so the one thing that has to be animated before then cannot
 * be a widget - and a full-frame blit per step, which is what the four-frame
 * paw cycle cost, is 153,600 bytes to move eight dots. */
void catnip_display_dot(int cx, int cy, int r, uint16_t rgb565);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DISPLAY_H */
