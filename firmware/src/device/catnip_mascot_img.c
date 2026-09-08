/* catnip_mascot_img.c - see catnip_mascot_img.h. */
#include "catnip_mascot_img.h"

#include "../generated/anim_f01_rgb565.h"
#include "../generated/anim_f02_rgb565.h"
#include "../generated/splash_rgb565.h"

const lv_image_dsc_t catnip_mascot_img = {
    .header =
        {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = CATNIP_SPLASH_W,
            .h = CATNIP_SPLASH_H,
            .stride = CATNIP_SPLASH_W * 2,
            .reserved_2 = 0,
        },
    .data_size = sizeof(catnip_splash),
    .data = (const uint8_t *)catnip_splash,
    .reserved = NULL,
};

/* The two frames the splash is not. Same shape, same size, same format - only
 * the paw and the arcs differ. */
#define MASCOT_FRAME(px)                                                                 \
    {                                                                                    \
        .header = {.magic = LV_IMAGE_HEADER_MAGIC,                                       \
                   .cf = LV_COLOR_FORMAT_RGB565,                                         \
                   .flags = 0,                                                           \
                   .w = CATNIP_SPLASH_W,                                                 \
                   .h = CATNIP_SPLASH_H,                                                 \
                   .stride = CATNIP_SPLASH_W * 2,                                        \
                   .reserved_2 = 0},                                                     \
        .data_size = sizeof(px),                                                         \
        .data = (const uint8_t *)(px),                                                   \
        .reserved = NULL,                                                                \
    }

static const lv_image_dsc_t mascot_f01 = MASCOT_FRAME(catnip_anim_f01);
static const lv_image_dsc_t mascot_f02 = MASCOT_FRAME(catnip_anim_f02);

const void *catnip_mascot_anim[CATNIP_MASCOT_FRAMES] = {
    &catnip_mascot_img,
    &mascot_f01,
    &mascot_f02,
    &mascot_f01,
};
