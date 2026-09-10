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

/* The screen is never allowed all the way down. Every setting in here is
 * reachable from the settings page, and a device whose only display has been
 * turned off cannot show the way back - the owner would be left holding a black
 * rectangle that is still running. The stock firmware picked the same floor for
 * the same reason. */
#define CATNIP_SCREEN_MIN_PCT 20

/* Room for a WPA2 network and its passphrase, each with its null. 32 is the
 * SSID limit the standard sets; 63 is the longest a WPA2 pre-shared key can be,
 * and a 64th slot for the terminator. A key given as 64 hex digits fits too. */
#define CATNIP_WIFI_SSID_MAX 33
#define CATNIP_WIFI_PSK_MAX  64

/* Enough for a good many app ids, comma-separated. A device with more unpinned
 * apps than fit simply keeps the first that fit pinned-off; the rest fall back
 * to shown, which is the safe direction. */
#define CATNIP_UNPINNED_MAX 256

typedef struct {
    /* Backlight, 20-100%. See CATNIP_SCREEN_MIN_PCT for the floor. */
    uint8_t screen_brightness;

    /* Seconds of no input before the screen goes dark, or 0 for never. The LED
     * dims with it, so the device still says it is alive without lighting up a
     * pocket. */
    uint16_t idle_off_s;

    /* Status LED. Peak of the breathing cycle, 0-100%. A WS2812 is far brighter
     * at a given value than people expect, which is why the default is low.
     *
     * Percent rather than the 0-255 the part takes, because this is a number an
     * owner reads and sets. The conversion belongs at the edge that talks to the
     * hardware, and having it anywhere else means two scales in the same file
     * and a bug waiting for whoever forgets which one they are holding. */
    uint8_t led_brightness;
    /* Breaths per second. 0.4 is one breath every two and a half seconds. */
    float led_breaths_per_second;

    /* Where the boot animation's frames are, and how long each is shown.
     * An empty directory means the built-in mascot. */
    char boot_frames_dir[CATNIP_CONFIG_PATH_MAX];
    uint16_t boot_frame_ms;

    /* The network to join, and the key to join it with (#82). Empty ssid means
     * "no network configured", which is a device that never brings the radio
     * up - most devices, most of the time. The key is kept beside the settings
     * because it arrives the same way they do, on the card, and is cached to
     * NVS for the same reason: a device with no card in the slot still has to
     * reach the clock server on the next boot. */
    char wifi_ssid[CATNIP_WIFI_SSID_MAX];
    char wifi_psk[CATNIP_WIFI_PSK_MAX];

    /* Minutes to add to UTC to get local wall time (#84). +480 is Taipei. The
     * RTC holds local time - what the owner set and what the face shows - so
     * this is what the NTP sync adds to the UTC it receives before writing the
     * registers. A signed number of minutes rather than a POSIX TZ string,
     * because this device has neither the flash nor the audience for one and
     * Taiwan, where it ships, has no daylight saving to describe. */
    int16_t tz_offset_min;

    /* The apps kept *off* the carousel (#71), as a comma-separated list of ids.
     * Off rather than on, so the default - an empty string - is every app
     * pinned, which is what a fresh device should show: unpinning is the
     * exception a user makes, and the exceptions are what is worth storing. An
     * app whose id is in here appears only in the grid that `up` opens, not on
     * the ring. */
    char unpinned[CATNIP_UNPINNED_MAX];
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
