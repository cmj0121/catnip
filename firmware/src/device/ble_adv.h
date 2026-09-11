/*
 * ble_adv.h - the device as a BLE beacon (#60, the advertise third of #53).
 *
 * ble.h listens for advertisers and ble_hid.h *is* a connectable one; this is
 * the third shape the same radio takes - an advertiser that carries a payload
 * and never answers. A beacon is not a device a host pairs with. It is a packet
 * repeated on an interval, and whoever is listening reads it and moves on, so
 * there is no server here, no connection, no callbacks, and nothing that a
 * second party's arrival could change.
 *
 * **The payload is bytes, and this driver does not read them.** iBeacon and
 * Eddystone are two ways of laying out the advertising data, and both are
 * assembled in Lua by the app on top - which is where the choice between them
 * lives. What reaches here is the finished advertisement, the AD structures
 * already concatenated, and this sets it on the air verbatim. A driver that
 * knew one format from the other would be a second place the layout is written,
 * and the one on the device would be the one nobody could see to fix.
 *
 * **It takes the air with it, like the mouse does.** A beacon is held for as
 * long as the app is open, not for a four-second window, and the 2.4 GHz front
 * end it holds is the one Wi-Fi was using. So begin() parks the Wi-Fi
 * association for the duration and end() lets it go - see catnip_wifi_hold().
 * The same promise the HID surface makes, for the same reason: an app that
 * forgot it would produce a beacon that stutters while Wi-Fi rejoins underneath
 * it, which nothing on screen would explain.
 *
 * **Non-connectable and silent, by default.** Left alone the advertisement is
 * ADV_NONCONN_IND: a beacon offers nothing to connect to and answers no scan
 * request, which is both what a beacon is and what keeps the whole 31-byte
 * payload for the app's own bytes rather than spending any of it on a name a
 * scanner would ask for. That is the whole of what #60 needs, so it is the
 * default and #60 never asks for anything else.
 *
 * **But the spam surface (#89) needs the other shapes.** Some of the public
 * Momentum-Apps spoof families are only convincing as a connectable ADV_IND -
 * a phone will not raise a pairing popup for something it cannot pair with -
 * and a few carry a name in a scan response. And a catalogue that cycles has to
 * look like many devices, not one, so it rotates the advertiser address every
 * re-arm. Those three - the adv type, a scan response, the address - are what
 * set_type() and set_addr() below add, each applied by the next begin(). They
 * default to the beacon above, so a caller that never touches them (which is
 * #60) is advertising exactly as it always was.
 */
#ifndef CATNIP_BLE_ADV_H
#define CATNIP_BLE_ADV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Become a beacon: bring the stack up, set `payload` as the raw advertisement,
 * start advertising it every `interval_ms`, and park the Wi-Fi link.
 *
 * `payload`/`len` are the finished advertising data - the AD structures the app
 * assembled - and must be no longer than the 31 bytes an advertisement holds.
 * `interval_ms` is how often the packet repeats; the controller works in units
 * of 0.625 ms, so this is rounded to the nearest one.
 *
 * False means the stack would not start or the payload would not set, in which
 * case nothing was taken and nothing needs giving back. True means the beacon
 * is on the air. Calling it again while up re-arms with the new payload and
 * interval rather than stacking a second beacon - which is how the app changes
 * the id or the rate on a running advertisement. */
bool catnip_ble_adv_begin(const uint8_t *payload, size_t len, uint32_t interval_ms);

/* Stop being a beacon: stop advertising, release the stack and give the Wi-Fi
 * link back. A no-op when nothing is up, so the main loop's teardown invariant
 * can call it every pass. */
void catnip_ble_adv_end(void);

/* Whether the beacon surface is on the air. There is no "connected" here as
 * there is for the mouse - a beacon is never connected to - so this one boolean
 * is the whole of its state: off, or advertising. */
bool catnip_ble_adv_up(void);

/* Shape the next begin (#89, the spam surface). Both default - never called -
 * to the beacon above: non-connectable, no scan response, so #60 is untouched.
 * `connectable` true sends a connectable ADV_IND, which is what makes a phone
 * offer to pair; `scan_rsp`/`len` (NULL or 0 for none) is the data answered to
 * a scan request. The choice persists until changed and is applied on the next
 * begin, so an app sets it once and every re-arm carries it. */
void catnip_ble_adv_set_type(bool connectable, const uint8_t *scan_rsp,
                             size_t scan_rsp_len);

/* Pick the advertiser address the next begin advertises under (#89). `addr` is
 * six bytes to set one explicitly, or NULL for a fresh random static address -
 * which is how a cycling catalogue looks like a new device each pass rather
 * than one device shouting. Applied on the next begin, alongside the reset that
 * re-arm already does; NimBLE's own-address type is switched to random to match.
 * Never called, the address is the controller's own and does not change. */
void catnip_ble_adv_set_addr(const uint8_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BLE_ADV_H */
