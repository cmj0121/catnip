/*
 * input.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #31: read the joystick / A-B buttons and the FT6336 touchscreen and
 * deliver activations to ui.fire so app on_* callbacks run.
 */
#ifdef CATNIP_DEVICE_WIP
#include <Arduino.h>

extern "C" {
#include "../catnip_runtime.h"
}
#include "lua.h"

// Call each loop(): poll inputs, update focus, and on activate call
// ui.fire(focused_id, 'click'). Joystick up/down move the selection; the
// center/A button activates; B goes back. Touch maps a tap to the hit widget.
void catnip_input_poll(catnip_rt *rt)
{
    // TODO: read QMI8658A/joystick GPIOs and FT6336 over I2C.
    // Example dispatch:
    // lua_State *L = catnip_rt_lua(rt);
    // lua_getglobal(L, "ui"); lua_getfield(L, -1, "fire");
    // lua_pushstring(L, focused_id); lua_pushstring(L, "click");
    // lua_pcall(L, 2, 0, 0);
    (void)rt;
}
#endif /* CATNIP_DEVICE_WIP */
