/*
 * ble_hid.h - the device as a BLE mouse (#59, the HID third of #53).
 *
 * The other direction of the radio. ble.h listens for advertisers; this one
 * *is* one - a connectable peripheral carrying the HID service, which a
 * computer or a phone pairs with and then treats as a pointing device. Nothing
 * here is specific to the app on top of it: what this offers is "a mouse the
 * host believes in", and where the movement comes from is Air Mouse's problem.
 *
 * **It takes the air with it.** A mouse is not a thing you do for four seconds;
 * it is held for as long as the app is open, and the 2.4 GHz front end it holds
 * is the one Wi-Fi was using. So begin() parks the Wi-Fi association for the
 * duration and end() lets it go - see catnip_wifi_hold(). That is a promise
 * this driver makes rather than a step an app has to remember, because an app
 * that forgot it would not fail visibly; it would just produce a mouse that
 * stutters, which is the hardest kind of bug to attribute.
 *
 * **Just Works pairing, and bonding.** There is no keyboard on the host side of
 * this conversation to type a passkey into and no screen on ours worth showing
 * one on, so the pairing is unauthenticated - which is what every commodity
 * mouse does, and for the same reason. Bonds are kept, so the second connection
 * costs the user nothing.
 *
 * Reports are *relative*: dx, dy and wheel are deltas in the range HID allows,
 * not screen coordinates. A peripheral has no idea how big the host's screen
 * is, where the cursor currently sits, or how the host will scale what it is
 * sent - so a driver that tried to talk in positions would be inventing a
 * coordinate space the host does not share with it.
 */
#ifndef CATNIP_BLE_HID_H
#define CATNIP_BLE_HID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which buttons a report says are down. A bitmask, because a real mouse can
 * hold one button while clicking another and a report carries the whole state
 * of all three every time rather than the one that changed. */
#define CATNIP_MOUSE_LEFT   0x01
#define CATNIP_MOUSE_RIGHT  0x02
#define CATNIP_MOUSE_MIDDLE 0x04

/* Become a mouse: bring the stack up, publish the HID service, start
 * advertising, and park the Wi-Fi link.
 *
 * False means the stack would not start, in which case nothing was taken and
 * nothing needs giving back. True means the device is now *advertising* - not
 * that anything has connected, which is the host's decision and takes as long
 * as it takes. Ask catnip_ble_hid_connected() for that.
 *
 * Calling it twice is not an error and does not restart anything. */
bool catnip_ble_hid_begin(void);

/* Stop being a mouse: stop advertising, drop any connection, release the stack
 * and give the Wi-Fi link back.
 *
 * The bond survives. A user who leaves the app and comes back should not have
 * to pair again - and a user who wants to be forgotten is asking a different
 * question, which belongs on a page rather than in a teardown. */
void catnip_ble_hid_end(void);

/* Whether the mouse surface is running at all - advertising or connected.
 * Distinct from connected(): a page needs to tell "waiting to be picked up"
 * from "not offering itself at all", and those are different sentences. */
bool catnip_ble_hid_up(void);

/* Whether a host is connected right now.
 *
 * This is the one thing the app actually shows: a mouse nobody has connected to
 * looks exactly like a mouse that is working, and the difference is the whole
 * of whether tilting the device does anything. */
bool catnip_ble_hid_connected(void);

/* Send one movement report.
 *
 * `dx`/`dy`/`wheel` are relative steps and are clamped to the -127..127 the
 * report descriptor declares - a caller that computed something larger gets the
 * edge rather than a wrapped byte, because a wrap would send the cursor the
 * wrong way, which is worse than sending it a short distance the right way.
 * `buttons` is a mask of CATNIP_MOUSE_*, and is the complete state.
 *
 * A no-op when nothing is connected: an app driving the cursor does not have to
 * check first, and a report sent into a disconnection is not an error worth
 * making it handle. */
void catnip_ble_hid_move(int dx, int dy, int buttons, int wheel);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BLE_HID_H */
