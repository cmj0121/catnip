/*
 * input_names.h - the names a Lua script calls the switches by.
 *
 * device.button() takes a string, because a script should not have to know the
 * order of an enum in a C header. Turning that string back into a catnip_button
 * is the only part of the HAL's input path that is arithmetic rather than I/O,
 * so it lives here where a host test can drive it - the same arrangement as
 * imu_map.h and touch_map.h.
 *
 * It is worth testing rather than obvious because of how it fails. An
 * unrecognised name and a switch that is simply not held are indistinguishable
 * at the Lua end: both are `false`. So a spelling this map does not accept does
 * not raise, it silently reports a button nobody is holding, and the app author
 * has no way to tell the two apart from inside the script.
 */
#ifndef CATNIP_INPUT_NAMES_H
#define CATNIP_INPUT_NAMES_H

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The switch `name` names, or CATNIP_BTN_COUNT when this device has no such
 * switch. NULL is not a switch either.
 *
 * Case is ignored, so device.button('a') and device.button('A') are the same
 * switch. Both spellings of the joystick's centre are accepted: the enum and
 * the rest of this codebase say "centre", and an author who types "center"
 * would otherwise get a button that is never held rather than a mistake. */
catnip_button catnip_button_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_INPUT_NAMES_H */
