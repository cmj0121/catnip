/* input.cpp - see input.h. */
#include <Arduino.h>

#include "board.h"
#include "input.h"
#include "input_debounce.h"

/* Same order as catnip_button. */
static const uint8_t kPins[CATNIP_BTN_COUNT] = {
    CATNIP_PIN_BTN_UP,     CATNIP_PIN_BTN_DOWN, CATNIP_PIN_BTN_LEFT, CATNIP_PIN_BTN_RIGHT,
    CATNIP_PIN_BTN_CENTRE, CATNIP_PIN_BTN_A,    CATNIP_PIN_BTN_B,
};

static catnip_debounce g_state[CATNIP_BTN_COUNT];

void catnip_input_begin(void)
{
    uint32_t now = millis();

    for (int i = 0; i < CATNIP_BTN_COUNT; i++) {
        /* The board already pulls every one of these pins up, so plain INPUT
         * would read correctly. The internal pull-up is asked for anyway: it
         * pulls the same way, so it changes nothing on this board, and it
         * keeps the read correct on a unit where the external part is missing
         * or has been lifted - which is exactly the sort of board someone
         * would be holding when they came to read this. */
        pinMode(kPins[i], INPUT_PULLUP);
        catnip_debounce_init(&g_state[i], now);
    }
}

void catnip_input_poll(void)
{
    uint32_t now = millis();

    for (int i = 0; i < CATNIP_BTN_COUNT; i++) {
        /* Active-low: the switch shorts the pin to ground when it is held. */
        bool down = digitalRead(kPins[i]) == LOW;

        catnip_debounce_update(&g_state[i], down, now);
    }
}

bool catnip_input_down(catnip_button button)
{
    return catnip_debounce_down(&g_state[button]);
}

bool catnip_input_pressed(catnip_button button)
{
    return catnip_debounce_take_pressed(&g_state[button]);
}

bool catnip_input_released(catnip_button button)
{
    return catnip_debounce_take_released(&g_state[button]);
}
