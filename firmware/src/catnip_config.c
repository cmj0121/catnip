/* catnip_config.c - see catnip_config.h. */
#include "catnip_config.h"

#include <string.h>

#include "cJSON.h"

/* The built-in boot experience, and the floor and ceiling for anything the
 * owner sets. The frame rate is bounded because the panel cannot be redrawn
 * faster than about 20 ms, and a frame that lasts a minute is indistinguishable
 * from a device that has frozen. */
#define DEFAULT_SCREEN_BRIGHTNESS   100
#define DEFAULT_IDLE_OFF_S          60
#define DEFAULT_LED_BRIGHTNESS      5
#define DEFAULT_LED_BREATHS_PER_SEC 0.4f
#define DEFAULT_BOOT_FRAME_MS       400

#define MIN_BREATHS_PER_SEC 0.05f /* one breath every 20 s */
#define MAX_BREATHS_PER_SEC 10.0f /* faster than this is a flicker, not a breath */
#define MIN_FRAME_MS        20
#define MAX_FRAME_MS        10000
#define MAX_IDLE_OFF_S      3600 /* an hour; past that "never" is the honest word */

/* Twelve hours each way covers every real zone (UTC-12 to UTC+14, the two ends
 * of the line), with a little past +14 so the exact edge is not on the clamp. */
#define MIN_TZ_OFFSET_MIN (-12 * 60)
#define MAX_TZ_OFFSET_MIN (14 * 60)

void catnip_config_defaults(catnip_config *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->screen_brightness = DEFAULT_SCREEN_BRIGHTNESS;
    cfg->idle_off_s = DEFAULT_IDLE_OFF_S;
    cfg->led_brightness = DEFAULT_LED_BRIGHTNESS;
    cfg->led_breaths_per_second = DEFAULT_LED_BREATHS_PER_SEC;
    cfg->boot_frames_dir[0] = '\0';
    cfg->boot_frame_ms = DEFAULT_BOOT_FRAME_MS;
}

static double clamp(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Read one number from `parent`, clamped. Leaves `*out` alone if the field is
 * absent or is not a number. */
static void read_number(const cJSON *parent, const char *name, double lo, double hi,
                        double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (cJSON_IsNumber(item)) *out = clamp(item->valuedouble, lo, hi);
}

static void read_path(const cJSON *parent, const char *name, char *out, size_t out_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!cJSON_IsString(item) || !item->valuestring) return;
    /* A path that does not fit is dropped rather than truncated: half a path
     * would point somewhere real-looking and wrong. */
    if (strlen(item->valuestring) >= out_size) return;
    strcpy(out, item->valuestring);
}

/* Read a string field, same rule as a path: one too long to fit is dropped
 * rather than truncated, because a passphrase with its tail cut off is a
 * passphrase that silently will not connect. An explicit empty string is
 * written through, because "" is how a card clears a credential NVS is
 * holding. */
static void read_str(const cJSON *parent, const char *name, char *out, size_t out_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!cJSON_IsString(item) || !item->valuestring) return;
    if (strlen(item->valuestring) >= out_size) return;
    strcpy(out, item->valuestring);
}

bool catnip_config_parse(catnip_config *cfg, const char *json, size_t len)
{
    if (!cfg || !json) return false;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
    if (cJSON_IsObject(screen)) {
        double brightness = cfg->screen_brightness;
        double idle = cfg->idle_off_s;
        read_number(screen, "brightness", CATNIP_SCREEN_MIN_PCT, 100, &brightness);
        read_number(screen, "idle_off_s", 0, MAX_IDLE_OFF_S, &idle);
        cfg->screen_brightness = (uint8_t)brightness;
        cfg->idle_off_s = (uint16_t)idle;
    }

    const cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (cJSON_IsObject(led)) {
        double brightness = cfg->led_brightness;
        double breaths = cfg->led_breaths_per_second;
        read_number(led, "brightness", 0, 100, &brightness);
        read_number(led, "breaths_per_second", MIN_BREATHS_PER_SEC, MAX_BREATHS_PER_SEC,
                    &breaths);
        cfg->led_brightness = (uint8_t)brightness;
        cfg->led_breaths_per_second = (float)breaths;
    }

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(wifi)) {
        read_str(wifi, "ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
        read_str(wifi, "psk", cfg->wifi_psk, sizeof(cfg->wifi_psk));
    }

    const cJSON *apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
    if (cJSON_IsObject(apps))
        read_str(apps, "unpinned", cfg->unpinned, sizeof(cfg->unpinned));

    const cJSON *clock = cJSON_GetObjectItemCaseSensitive(root, "clock");
    if (cJSON_IsObject(clock)) {
        double tz = cfg->tz_offset_min;
        read_number(clock, "tz_offset_min", MIN_TZ_OFFSET_MIN, MAX_TZ_OFFSET_MIN, &tz);
        cfg->tz_offset_min = (int16_t)tz;
    }

    const cJSON *boot = cJSON_GetObjectItemCaseSensitive(root, "boot");
    if (cJSON_IsObject(boot)) {
        double frame_ms = cfg->boot_frame_ms;
        read_number(boot, "frame_ms", MIN_FRAME_MS, MAX_FRAME_MS, &frame_ms);
        cfg->boot_frame_ms = (uint16_t)frame_ms;
        read_path(boot, "frames", cfg->boot_frames_dir, sizeof(cfg->boot_frames_dir));
    }

    cJSON_Delete(root);
    return true;
}
