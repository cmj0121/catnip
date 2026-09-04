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

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DISPLAY_H */
