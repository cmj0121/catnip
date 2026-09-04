/* led.cpp - see led.h. */
#include <Arduino.h>
#include <math.h>

#include "board.h"
#include "led.h"

namespace {
const float TWO_PI_F = 6.28318531f;

/* A calm breath, not a blink. Both are the owner's to change (see
 * catnip_led_configure); these are where they start. */
unsigned long g_period_ms = 2600;

/* Peak brightness, out of 255. This is the one thing on the device that stays
 * lit whenever the board has power, sitting in peripheral vision for as long
 * as it is on, so it wants to read as a heartbeat rather than a warning.
 *
 * The value was found by looking at the hardware, not by arithmetic: full
 * output glared, a quarter still pulled the eye, a tenth was close, and this
 * is where it stopped being noticeable and started being reassuring. A WS2812
 * is far brighter at a given duty than the LED anyone reaching for this
 * constant will be picturing. */
uint8_t g_peak = 8;

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

void catnip_led_configure(uint8_t peak, float breaths_per_second)
{
    g_peak = peak;
    /* Guard the divide rather than trusting the caller: a zero here would be a
     * division by zero in the breathing cycle, and the config parser's clamp is
     * not this function's to assume. */
    if (breaths_per_second > 0.0f) {
        g_period_ms = (unsigned long)(1000.0f / breaths_per_second);
    }
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
    float t = (float)(millis() % g_period_ms) / (float)g_period_ms;
    float wave = (1.0f - cosf(t * TWO_PI_F)) * 0.5f; /* 0 -> 1 -> 0 */
    /* Squaring holds it longer at the dim end, which is what makes it read as
     * breathing rather than as a triangle-wave fade. */
    uint8_t level = (uint8_t)(wave * wave * (float)g_peak);
    if (level != g_level) catnip_led_level(level);
}
