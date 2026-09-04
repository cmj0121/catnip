/* led.cpp - see led.h. */
#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "led.h"

namespace {
const float TWO_PI_F = 6.28318531f;
const unsigned long PERIOD_MS = 2600; /* a calm breath, not a blink */

/* Catnip green is #BEE700, but that cannot be used literally here. On screen
 * its red channel reads as part of a lime, surrounded by other colours; on a
 * bare emitter with nothing to compare against, R190 G231 simply looks yellow.
 * Pulling the red down keeps the family and reads as the green it is meant
 * to be. */
const uint8_t BRAND_R = 40;
const uint8_t BRAND_G = 255;
const uint8_t BRAND_B = 0;

uint8_t g_level = 0;
} /* namespace */

void catnip_led_begin(void)
{
    catnip_led_level(0);
}

void catnip_led_level(uint8_t brightness)
{
    g_level = brightness;
    /* Scale the brand colour rather than fading to white: a WS2812 has three
     * separate emitters, and dimming each in proportion keeps the hue. */
    neopixelWrite(CATNIP_PIN_LED,
                  (uint8_t)((BRAND_R * brightness) / 255),
                  (uint8_t)((BRAND_G * brightness) / 255),
                  (uint8_t)((BRAND_B * brightness) / 255));
}

void catnip_led_breathe(void)
{
    float t = (float)(millis() % PERIOD_MS) / (float)PERIOD_MS;
    float wave = (1.0f - cosf(t * TWO_PI_F)) * 0.5f; /* 0 -> 1 -> 0 */
    /* Squaring holds it longer at the dim end, which is what makes it read as
     * breathing rather than as a triangle-wave fade. */
    uint8_t level = (uint8_t)(wave * wave * 255.0f);
    if (level != g_level) catnip_led_level(level);
}
