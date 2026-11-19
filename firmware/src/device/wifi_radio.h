/*
 * wifi_radio.h - the radio as a monitor, counted like the BLE stack (#99).
 *
 * wifi.cpp owns the station link: it joins one network, parks it for a sweep,
 * and - crucially - is the SOLE thing that rejoins, on the grace timer inside
 * catnip_wifi_poll(). This module borrows the front end for something the
 * station cannot do at the same time: promiscuous capture, channel hopping and
 * raw injection. The two are mutually exclusive by construction.
 *
 * So the borrow is counted, exactly like ble_stack: the first acquire parks the
 * link (via catnip_wifi_hold(true)) and takes the radio into monitor use; the
 * last release restores STA mode and clears the hold, and the station's own
 * rejoiner - never this module - puts Wi-Fi back on a network. This module MUST
 * NEVER call WiFi.begin(): that is wifi.cpp's to own, so there is one rejoiner
 * and one place that knows whether a station link is wanted at all.
 *
 * Capture does not touch the app loop from the Wi-Fi task: the promiscuous
 * callback only copies a frame into a small ring, and catnip_wifi_radio_poll()
 * drains the ring to the caller's cb on the app loop - the same enqueue-here,
 * drain-there shape the BLE write callbacks use.
 */
#ifndef CATNIP_WIFI_RADIO_H
#define CATNIP_WIFI_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A captured frame, handed over on the app loop (never from the Wi-Fi task).
 * `buf` is owned by the driver and valid only for the duration of the call. */
typedef void (*catnip_wifi_rx_cb)(void *ud, const uint8_t *buf, size_t len,
                                  uint8_t channel, int8_t rssi);

/* Borrow the radio for monitor use, and count one more borrower. The first
 * borrow parks the station link and takes channel control; a later one only
 * increments. False means it would not start, in which case nothing was counted
 * and the caller has nothing to release. */
bool catnip_wifi_radio_acquire(void);

/* Let go. The radio returns to the station only when the last borrower does:
 * that release restores WIFI_STA mode and THEN clears the hold, so the station's
 * grace-timer rejoiner in catnip_wifi_poll() runs against a radio already back
 * in STA mode. This never rejoins itself. */
void catnip_wifi_radio_release(void);

/* Whether anything currently holds the radio in monitor use. For logs and
 * diagnostics; a caller that needs the radio should hold a reference instead. */
bool catnip_wifi_radio_is_up(void);

/* Turn promiscuous capture on, delivering frames to `cb` with `ud` on the app
 * loop, starting on `channel`. Requires a held acquire; false without one. */
bool catnip_wifi_monitor_begin(catnip_wifi_rx_cb cb, void *ud, uint8_t channel);

/* Hop the capture to another channel. No-op if monitoring is not on. */
void catnip_wifi_monitor_set_channel(uint8_t channel);

/* Turn promiscuous capture off. Leaves the acquire in place - the caller still
 * releases it - so TX can continue without capture, and vice versa. */
void catnip_wifi_monitor_end(void);

/* Drain queued captured frames to the monitor cb. Call every app loop; it only
 * reads the ring the Wi-Fi task fills. */
void catnip_wifi_radio_poll(void);

/* Inject one raw 802.11 frame. Requires a held acquire; false without one, or
 * if the controller rejects the frame. */
bool catnip_wifi_tx(const uint8_t *frame, size_t len);

/* --- softAP + captive hook (wifi_softap.cpp) --------------------------------
 *
 * A second way to borrow the same counted radio: instead of promiscuous
 * capture it raises an open access point and a captive portal's front door - a
 * DNS that points every name at the device and an HTTP server that hands each
 * request up to a callback. It shares catnip_wifi_radio_acquire/release, so it
 * parks the station link and restores it through exactly the same single owner
 * and single rejoiner the monitor uses, and NEVER calls WiFi.begin() itself.
 *
 * AP and promiscuous are the same radio in two incompatible modes, so the two
 * are mutually exclusive: begin fails if the radio is already borrowed (e.g.
 * monitor is up), and a caller runs only one at a time.
 *
 * This is the HAL mechanism only - an open AP, the captive redirect, and
 * request->cb. No portal HTML, no credential capture, no persistence; those are
 * a consumer's job (#106). */

/* A received HTTP request, delivered on the app loop from inside
 * catnip_wifi_softap_poll() (never from a Wi-Fi task). `method` and `path` are
 * always non-NULL and valid only for the call; `body` is the request body or
 * NULL when there is none. */
typedef void (*catnip_wifi_http_cb)(void *ud, const char *method, const char *path,
                                    const char *body);

/* Borrow the radio and raise an open softAP named `ssid` on `channel`, with a
 * captive DNS answering every name with the AP's address and an HTTP server
 * handing requests to `cb`. Acquires the shared radio first; false (nothing
 * counted) if the radio is already in use, the AP is already up, or bring-up
 * fails. */
bool catnip_wifi_softap_begin(const char *ssid, uint8_t channel, catnip_wifi_http_cb cb,
                              void *ud); /* needs acquire */

/* Tear the HTTP server, the DNS and the AP down, then release the radio. The
 * last release restores WIFI_STA and lets the station's rejoiner rejoin - this
 * never rejoins itself. No-op when the AP is not up. */
void catnip_wifi_softap_end(void);

/* Service the captive DNS and HTTP server, and so invoke the request cb. Call
 * every app loop while the AP is up; a no-op otherwise. Like
 * catnip_wifi_radio_poll(), this is the app-loop half - nothing here runs from
 * a Wi-Fi task. */
void catnip_wifi_softap_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_WIFI_RADIO_H */
