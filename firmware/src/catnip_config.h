/*
 * catnip_config.h - the settings the owner can change, and their defaults.
 *
 * The settings live in a JSON file on the SD card. Everything here has a
 * default that works, because the card is optional: no card, no file, or a
 * file with a typo in it must all leave a device that still boots and still
 * looks like itself.
 *
 * Parsing is separate from reading the file so it can be tested on the host,
 * where there is no card and no display.
 */
#ifndef CATNIP_CONFIG_H
#define CATNIP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Long enough for a path on the card with room to spare; a longer one is
 * rejected rather than silently cut in half. */
#define CATNIP_CONFIG_PATH_MAX 128

typedef struct {
    /* Status LED. Peak of the breathing cycle, 0-255. A WS2812 is far brighter
     * at a given value than people expect, which is why the default is low. */
    uint8_t led_brightness;
    /* Breaths per second. 0.4 is one breath every two and a half seconds. */
    float led_breaths_per_second;

    /* Where the boot animation's frames are, and how long each is shown.
     * An empty directory means the built-in mascot. */
    char boot_frames_dir[CATNIP_CONFIG_PATH_MAX];
    uint16_t boot_frame_ms;
} catnip_config;

/* Fill in the built-in settings. Always succeeds. */
void catnip_config_defaults(catnip_config *cfg);

/* Apply the settings named in `json` on top of whatever `cfg` already holds.
 *
 * Returns false only when the text is not JSON at all. A field that is missing,
 * of the wrong type, or out of range leaves that one setting alone and does not
 * discard the rest: one bad line in a config file should cost the owner that
 * line, not the whole file. Out-of-range numbers are clamped rather than
 * rejected, since the intent is never in doubt.
 */
bool catnip_config_parse(catnip_config *cfg, const char *json, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_CONFIG_H */
