/* prefs.cpp - see prefs.h. */
#include "prefs.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>

#include "cJSON.h"
#include "sd_mount.h"

#ifndef CATNIP_CONFIG_PATH
#define CATNIP_CONFIG_PATH "/sd/catnip/config.json"
#endif

namespace {

/* The NVS namespace. Not the stock firmware's "mk_cfg": the keys mean different
 * things - its LED brightness is a percentage of a different ladder and it has
 * settings this firmware does not - and sharing the namespace would mean each
 * one silently reading the other's numbers as its own. */
const char kNamespace[] = "catnip";

/* Bumped whenever a key below is added, renamed, or changes meaning. A stored
 * blob from a version this firmware does not know is ignored: reading a key
 * whose meaning has moved is worse than falling back to the defaults, because
 * the defaults are at least right about themselves. */
const uint32_t kSchema = 1;

const char kKeyVersion[] = "ver";
const char kKeyScreen[] = "screen";
const char kKeyIdleOff[] = "idle_off";
const char kKeyLed[] = "led";
const char kKeyBreaths[] = "breaths";
const char kKeyBootMs[] = "boot_ms";

/* The directory the config file lives in, so a first save on a fresh card does
 * not fail for want of it. */
const char kConfigDir[] = "/catnip";

/* SD_MMC is mounted at /sd, and the paths its own API takes are relative to
 * that mount - so the file this writes is the same one main.cpp reads through
 * the "/sd/..." path the loader uses. */
const char kConfigFile[] = "/catnip/config.json";

void load_nvs(catnip_config *cfg)
{
    Preferences p;

    if (!p.begin(kNamespace, true /* read only */)) return;
    uint32_t ver = p.getUInt(kKeyVersion, 0);
    if (ver != kSchema) {
        /* Nothing saved yet, or saved by a firmware whose keys meant something
         * else. Either way the defaults already in `cfg` are the answer. */
        p.end();
        if (ver != 0)
            Serial.printf("[catnip] prefs: stored schema %u is not %u, ignoring it\n",
                          (unsigned)ver, (unsigned)kSchema);
        return;
    }
    cfg->screen_brightness = (uint8_t)p.getUChar(kKeyScreen, cfg->screen_brightness);
    cfg->idle_off_s = (uint16_t)p.getUShort(kKeyIdleOff, cfg->idle_off_s);
    cfg->led_brightness = (uint8_t)p.getUChar(kKeyLed, cfg->led_brightness);
    cfg->led_breaths_per_second = p.getFloat(kKeyBreaths, cfg->led_breaths_per_second);
    cfg->boot_frame_ms = (uint16_t)p.getUShort(kKeyBootMs, cfg->boot_frame_ms);
    p.end();
    Serial.println("[catnip] prefs: read from NVS");
}

/* The card's file, on top of whatever NVS said. See prefs.h for why this one
 * wins, and for what it costs. */
void load_card(catnip_config *cfg)
{
    File f = SD_MMC.open(kConfigFile);
    if (!f || f.isDirectory()) {
        Serial.println("[catnip] config: none on the card");
        return;
    }
    size_t len = f.size();
    char *text = (char *)malloc(len + 1);
    if (text && f.readBytes(text, len) == len) {
        text[len] = '\0';
        if (catnip_config_parse(cfg, text, len)) {
            Serial.println("[catnip] config: read from " CATNIP_CONFIG_PATH);
        } else {
            /* Deliberately not fatal and deliberately not a reset: a file that
             * is not JSON tells us nothing about what the owner wanted, so what
             * was already loaded stands. */
            Serial.println("[catnip] config: not valid JSON, keeping what was loaded");
        }
    } else {
        Serial.println("[catnip] config: could not be read, keeping what was loaded");
    }
    free(text);
    f.close();
}

void save_nvs(const catnip_config *cfg)
{
    Preferences p;

    if (!p.begin(kNamespace, false)) {
        Serial.println("[catnip] prefs: NVS would not open, settings not saved");
        return;
    }
    p.putUInt(kKeyVersion, kSchema);
    p.putUChar(kKeyScreen, cfg->screen_brightness);
    p.putUShort(kKeyIdleOff, cfg->idle_off_s);
    p.putUChar(kKeyLed, cfg->led_brightness);
    p.putFloat(kKeyBreaths, cfg->led_breaths_per_second);
    p.putUShort(kKeyBootMs, cfg->boot_frame_ms);
    p.end();
    Serial.println("[catnip] prefs: saved to NVS");
}

/* The same settings as the text an owner can open and edit. Written through
 * cJSON rather than printf so the file this produces is exactly the shape
 * catnip_config_parse reads, and stays that way when a field is added. */
void save_card(const catnip_config *cfg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *screen = root ? cJSON_AddObjectToObject(root, "screen") : NULL;
    cJSON *led = root ? cJSON_AddObjectToObject(root, "led") : NULL;
    cJSON *boot = root ? cJSON_AddObjectToObject(root, "boot") : NULL;
    char *text = NULL;

    if (!root || !screen || !led || !boot) {
        cJSON_Delete(root);
        return;
    }
    cJSON_AddNumberToObject(screen, "brightness", cfg->screen_brightness);
    cJSON_AddNumberToObject(screen, "idle_off_s", cfg->idle_off_s);
    cJSON_AddNumberToObject(led, "brightness", cfg->led_brightness);
    cJSON_AddNumberToObject(led, "breaths_per_second", cfg->led_breaths_per_second);
    cJSON_AddNumberToObject(boot, "frame_ms", cfg->boot_frame_ms);
    /* `boot.frames` is not written. It names a directory the owner chose and
     * this page cannot change it, so writing it back would be this firmware
     * repeating something it was told - and dropping it, the day a field is
     * added and forgotten here, would be this firmware losing it. */

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return;

    /* The directory first: a card that has never held an app has no /catnip on
     * it, and SD_MMC.open() for writing does not make one. */
    if (!SD_MMC.exists(kConfigDir)) SD_MMC.mkdir(kConfigDir);

    File f = SD_MMC.open(kConfigFile, FILE_WRITE);
    if (!f) {
        Serial.println("[catnip] config: could not be written to the card");
    } else {
        f.print(text);
        f.close();
        Serial.println("[catnip] config: written to " CATNIP_CONFIG_PATH);
    }
    cJSON_free(text);
}

} // namespace

void catnip_prefs_load(catnip_config *cfg)
{
    if (!cfg) return;
    catnip_config_defaults(cfg);
    load_nvs(cfg);
    if (catnip_sd_mounted()) load_card(cfg);
}

void catnip_prefs_save(const catnip_config *cfg)
{
    if (!cfg) return;
    save_nvs(cfg);
    /* NVS first and unconditionally: it is the copy that has to be right. The
     * card is a mirror, and a device with none must still keep its settings. */
    if (catnip_sd_mounted()) save_card(cfg);
}
