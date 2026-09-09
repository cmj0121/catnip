/* test_config.c - the settings file: defaults, overrides, and bad input.
 *
 * The point of most of these is what does NOT happen. A config file is edited
 * by hand on a card, so it will contain mistakes, and a mistake must cost the
 * owner the line it is on rather than the boot.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "catnip_config.h"

static int checks;
static void ok(int cond, const char *what)
{
    printf("  %-4s - %s\n", cond ? "ok" : "FAIL", what);
    checks += cond ? 0 : 1;
    assert(cond);
}

static bool nearly(float a, float b)
{
    return (a - b) < 0.001f && (b - a) < 0.001f;
}

static catnip_config parse(const char *json, bool expect_ok)
{
    catnip_config cfg;
    catnip_config_defaults(&cfg);
    bool got = catnip_config_parse(&cfg, json, strlen(json));
    ok(got == expect_ok, expect_ok ? "parsed" : "rejected");
    return cfg;
}

int main(void)
{
    {
        catnip_config cfg;
        catnip_config_defaults(&cfg);
        ok(cfg.led_brightness == 5, "default brightness is the calm one");
        ok(cfg.screen_brightness == 100, "the screen starts at full");
        ok(cfg.idle_off_s == 60, "and goes dark after a minute of nothing");
        ok(nearly(cfg.led_breaths_per_second, 0.4f), "default is a breath every 2.5s");
        ok(cfg.boot_frame_ms == 400, "default frame is 400ms");
        ok(cfg.boot_frames_dir[0] == '\0',
           "no frame directory means the built-in mascot");
    }

    {
        catnip_config cfg =
            parse("{\"led\":{\"brightness\":64,\"breaths_per_second\":1.5},"
                  " \"boot\":{\"frames\":\"/sd/catnip/boot\",\"frame_ms\":120}}",
                  true);
        ok(cfg.led_brightness == 64, "brightness is taken from the file");
        ok(nearly(cfg.led_breaths_per_second, 1.5f), "rhythm is taken from the file");
        ok(strcmp(cfg.boot_frames_dir, "/sd/catnip/boot") == 0,
           "frame directory is taken from the file");
        ok(cfg.boot_frame_ms == 120, "frame time is taken from the file");
    }

    {
        catnip_config cfg = parse("{\"led\":{\"brightness\":40}}", true);
        ok(cfg.led_brightness == 40, "a setting given is applied");
        ok(nearly(cfg.led_breaths_per_second, 0.4f),
           "a setting left out keeps its default");
        ok(cfg.boot_frame_ms == 400, "a whole section left out keeps its defaults");
    }

    {
        catnip_config cfg =
            parse("{\"led\":{\"brightness\":9000,\"breaths_per_second\":-3}}", true);
        ok(cfg.led_brightness == 100, "too bright is clamped, not rejected");
        ok(nearly(cfg.led_breaths_per_second, 0.05f),
           "a negative rhythm is clamped to the slowest");
    }

    {
        catnip_config cfg = parse(
            "{\"led\":{\"brightness\":\"very\"},\"boot\":{\"frame_ms\":null}}", true);
        ok(cfg.led_brightness == 5, "a setting of the wrong type keeps its default");
        ok(cfg.boot_frame_ms == 400, "so does a null one");
    }

    {
        catnip_config cfg =
            parse("{\"screen\":{\"brightness\":0,\"idle_off_s\":99999}}", true);
        ok(cfg.screen_brightness == CATNIP_SCREEN_MIN_PCT,
           "the screen cannot be turned off from the file either");
        ok(cfg.idle_off_s == 3600, "and an absurd idle time is clamped to an hour");
    }

    {
        /* A value that is not the default, so "changed nothing" is a claim with
         * something behind it. */
        catnip_config cfg = parse("{\"led\": {\"brightness\": 75,", false);
        ok(cfg.led_brightness == 5, "a truncated file changes nothing at all");
    }

    {
        char json[256];
        char path[CATNIP_CONFIG_PATH_MAX + 16];
        memset(path, 'x', sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        snprintf(json, sizeof(json), "{\"boot\":{\"frames\":\"%s\"}}", path);
        catnip_config cfg;
        catnip_config_defaults(&cfg);
        catnip_config_parse(&cfg, json, strlen(json));
        ok(cfg.boot_frames_dir[0] == '\0',
           "a path too long to hold is dropped, not cut in half");
    }

    {
        catnip_config cfg;
        catnip_config_defaults(&cfg);
        ok(!catnip_config_parse(NULL, "{}", 2), "no config to write into is refused");
        ok(!catnip_config_parse(&cfg, NULL, 0), "no text to read is refused");
    }

    printf("%s (%d failures)\n", checks ? "FAILED" : "PASSED", checks);
    return checks ? 1 : 0;
}
