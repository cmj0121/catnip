/* catnip_config.c - see catnip_config.h. */
#include "catnip_config.h"

#include <string.h>

#include "cJSON.h"

/* The built-in boot experience, and the floor and ceiling for anything the
 * owner sets. The frame rate is bounded because the panel cannot be redrawn
 * faster than about 20 ms, and a frame that lasts a minute is indistinguishable
 * from a device that has frozen. */
#define DEFAULT_LED_BRIGHTNESS      8
#define DEFAULT_LED_BREATHS_PER_SEC 0.4f
#define DEFAULT_BOOT_FRAME_MS       400

#define MIN_BREATHS_PER_SEC 0.05f  /* one breath every 20 s */
#define MAX_BREATHS_PER_SEC 10.0f  /* faster than this is a flicker, not a breath */
#define MIN_FRAME_MS        20
#define MAX_FRAME_MS        10000

void catnip_config_defaults(catnip_config *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
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
static void read_number(const cJSON *parent, const char *name,
                        double lo, double hi, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (cJSON_IsNumber(item)) *out = clamp(item->valuedouble, lo, hi);
}

static void read_path(const cJSON *parent, const char *name,
                      char *out, size_t out_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!cJSON_IsString(item) || !item->valuestring) return;
    /* A path that does not fit is dropped rather than truncated: half a path
     * would point somewhere real-looking and wrong. */
    if (strlen(item->valuestring) >= out_size) return;
    strcpy(out, item->valuestring);
}

bool catnip_config_parse(catnip_config *cfg, const char *json, size_t len)
{
    if (!cfg || !json) return false;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    const cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (cJSON_IsObject(led)) {
        double brightness = cfg->led_brightness;
        double breaths = cfg->led_breaths_per_second;
        read_number(led, "brightness", 0, 255, &brightness);
        read_number(led, "breaths_per_second", MIN_BREATHS_PER_SEC, MAX_BREATHS_PER_SEC, &breaths);
        cfg->led_brightness = (uint8_t)brightness;
        cfg->led_breaths_per_second = (float)breaths;
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
