/*
 * wifi.h - the radio, and nothing above it (#82).
 *
 * The HAL has advertised service.wifi.status() and service.wifi.ssid() to every
 * app since it landed, and on this board both were stubs that always said "not
 * connected" - a lie with a return value, as hal_meowkit.cpp admitted in a
 * comment. This is the driver that makes them true.
 *
 * It joins one network, named on the card (catnip_config's wifi_ssid/psk, which
 * arrive there and are cached to NVS). It does not scan (that is #54), it does
 * not fetch (that is separate), and it does not raise an access point. One
 * network, on demand, dropped when nothing needs it - because the radio is the
 * most expensive thing on this board and a handheld that joins a network to sit
 * on a launcher is spending a battery to do nothing.
 */
#ifndef CATNIP_WIFI_H
#define CATNIP_WIFI_H

#include <stdbool.h>

#include "../catnip_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CATNIP_WIFI_OFF = 0,   /* the radio is down; nothing asked for it */
    CATNIP_WIFI_JOINING,   /* associating, or waiting for an address */
    CATNIP_WIFI_CONNECTED, /* associated, with an address */
    CATNIP_WIFI_FAILED,    /* tried and could not - wrong key, or no such network */
} catnip_wifi_state;

/* Ask the radio to join `ssid` with `psk` (which may be empty for an open
 * network). Non-blocking: it starts the association and returns, and the state
 * moves to CONNECTED or FAILED over the following seconds as catnip_wifi_poll()
 * is called. A second begin() with the same credentials while already joining
 * or connected is a no-op, so it is safe to call every time something wants the
 * link. An empty ssid is "no network", and turns the radio off. */
void catnip_wifi_begin(const char *ssid, const char *psk);

/* Advance the state machine and return where it is now. Cheap to call every
 * loop; it only reads the driver's own status. */
catnip_wifi_state catnip_wifi_poll(void);

/* Where it is, without advancing anything - for the parts that only report,
 * like the status strip and service.wifi.status(). */
catnip_wifi_state catnip_wifi_status(void);

/* The SSID currently joined or being joined, or NULL when the radio is off.
 * Owned by the driver; valid until the next begin(). */
const char *catnip_wifi_ssid(void);

/* Drop the link and power the radio down. Called when the last thing that
 * wanted it is done, so "connected" is never the resting state. */
void catnip_wifi_end(void);

/* Scan for nearby access points (#54, for the Wi-Fi prober). Non-blocking: the
 * first call starts an asynchronous scan and returns -1 (still running); once
 * it completes, a call fills up to `max` entries and returns the count, then
 * starts the next scan. So a caller polls it and gets a fresh list every few
 * seconds without ever stopping the loop for the scan.
 *
 * A scan briefly drops an active connection - the radio cannot listen on every
 * channel and hold an association at once - which is the cost the prober pays
 * to see the air, and the reason it is the prober that scans and not the
 * background. */
int catnip_wifi_scan(catnip_wifi_ap *out, int max);

/* Whether a scan is in the air right now. The platform draws the busy ring off
 * this: an app that scans says nothing about waiting, because what waiting
 * looks like is not an app's to decide. */
bool catnip_wifi_scanning(void);

/* Throw away whatever the last scan found and go and look again.
 *
 * The driver already rescans behind a caller that keeps polling, so this is not
 * about getting fresh results - it is about *saying so*. A page that answers a
 * press with the same list it was already showing has not answered it; this is
 * what makes the busy ring come back up, which is the device saying "yes, I
 * heard you, and I am looking". */
void catnip_wifi_rescan(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_WIFI_H */
