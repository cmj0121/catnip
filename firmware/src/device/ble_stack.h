/*
 * ble_stack.h - the one bring-up two BLE surfaces share (#53).
 *
 * NimBLEDevice::init() and deinit() are global: there is one controller, one
 * host stack, and one call that takes the whole thing down. That was fine while
 * scanning was the only thing built on it - ble.cpp brought the stack up on the
 * first ask and put it away once nobody had asked for a while, and nothing else
 * had an opinion.
 *
 * It stops being fine the moment a second surface exists. A Scanner that idles
 * out and calls deinit() while Air Mouse is holding a HID connection has not
 * stopped scanning; it has dropped the mouse. Neither driver is in a position
 * to know whether the other is finished, so neither of them gets to decide.
 *
 * So the bring-up moves here and is counted. A surface acquires before it
 * touches the stack and releases when it is done, and the stack goes down when
 * the last one lets go. That is the same lazy, pay-for-it-on-first-ask
 * behaviour as before - a device that never opens the Scanner and never becomes
 * a mouse never pays for either - with more than one thing allowed to ask.
 */
#ifndef CATNIP_BLE_STACK_H
#define CATNIP_BLE_STACK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the stack up if it is not, and count one more user of it. False means
 * it would not start, in which case nothing was counted and the caller has
 * nothing to release. */
bool catnip_ble_stack_acquire(void);

/* Let go. The stack comes down when the last user does; until then this only
 * decrements, because somebody else is still using the radio. */
void catnip_ble_stack_release(void);

/* Whether anything currently holds the stack up. For logs and diagnostics - a
 * caller deciding what it may do should hold a reference instead of asking. */
bool catnip_ble_stack_is_up(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BLE_STACK_H */
