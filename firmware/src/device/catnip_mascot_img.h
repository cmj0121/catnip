/*
 * catnip_mascot_img.h - the mascot, as something LVGL can draw.
 *
 * The splash is a 320x240 RGB565 array compiled into flash, and until now it
 * only ever reached the panel through catnip_display_blit() - straight past
 * LVGL, which is right for a boot animation that runs before LVGL exists and
 * wrong for a home screen that has to sit under the frame's bar and beside a
 * carousel. Wrapping the same bytes in an image descriptor costs no flash and
 * lets the mascot be a widget like anything else.
 */
#ifndef CATNIP_MASCOT_IMG_H
#define CATNIP_MASCOT_IMG_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t catnip_mascot_img;

/* The idle wave, as the frames an lv_animimg cycles. The order is the boot
 * animation's own - f00, f01, f02, f01 - so the cat breathes the same way on
 * the landing page as it does before LVGL exists, and one cycle is one breath.
 *
 * The boot path blits these straight to the panel and stops the moment the
 * renderer takes over, because two owners repainting the same panel is a fight
 * nobody wins. Once LVGL owns it, the same three pictures have to arrive as
 * widgets, which is what this is. */
#define CATNIP_MASCOT_FRAMES   4
#define CATNIP_MASCOT_FRAME_MS 625 /* a 4-frame cycle is one 2.5 s breath */

extern const void *catnip_mascot_anim[CATNIP_MASCOT_FRAMES];

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_MASCOT_IMG_H */
