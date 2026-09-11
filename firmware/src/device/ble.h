/*
 * ble.h - the other radio, and nothing above it (#53).
 *
 * This board is an ESP32-S3: it has Bluetooth Low Energy and **no Classic
 * Bluetooth at all**. That is not a scope decision, it is the silicon, and it
 * is the first thing anything built on this has to be honest about - a page
 * that offers "Bluetooth" and cannot see a pair of headphones is a page a user
 * concludes is broken. What this finds is advertisers: beacons, trackers,
 * fitness bands, phones with an app broadcasting, and the advertising half of
 * devices that are also Classic.
 *
 * NimBLE rather than the Arduino core's Bluedroid. Bluedroid is the full
 * dual-mode stack at a few hundred kilobytes of flash and a large slice of the
 * internal heap, and there is no Classic here to be dual-mode about.
 *
 * The scan contract is Wi-Fi's, deliberately: -1 while one is running, a count
 * when it finishes. A caller that has learned one radio should not have to
 * learn the other, and the Scanner (#57) is going to poll both.
 *
 * **One radio.** WiFi and BLE share the same 2.4 GHz front end, and the
 * coexistence arbiter gives each one a slice rather than both the whole. A
 * scan of either while the other is also scanning is two bad scans, which is
 * why the Scanner asks for one at a time and why this driver does not start
 * itself.
 */
#ifndef CATNIP_BLE_H
#define CATNIP_BLE_H

#include <stdbool.h>

#include "../catnip_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the stack up. Costs flash and heap, so it is not done at boot: the
 * first ask is what pays for it, and a device that never opens the Scanner
 * never does. */
bool catnip_ble_begin(void);

/* Put it away again, and give the heap back. */
void catnip_ble_end(void);

/* The advertisers heard, or -1 while a scan is still running.
 *
 * Non-blocking, like the Wi-Fi one: the first call starts a scan and answers
 * -1, and a later call answers the count. Duplicates are merged by address and
 * the strongest reading of each is kept - an advertiser is heard many times in
 * one window, and a list that showed it once per advertisement would be a list
 * of one device's heartbeats. */
int catnip_ble_scan(catnip_ble_dev *out, int max);

/* Throw the last scan away and listen again - what a press of A means on a page
 * that is already showing a list. */
void catnip_ble_rescan(void);

/* Called from the main loop. Takes the radio down once nobody has asked about
 * it for a while: an initialised controller shares the front end with Wi-Fi
 * whether or not it is listening, so it does not get to outlive the page that
 * wanted it. Nothing is lost by it - the next ask brings it back up. */
void catnip_ble_poll(void);

/* Whether there is nothing to show yet, which is what raises the busy ring. The
 * same meaning catnip_wifi_scanning() carries, and for the same reason: a
 * caller that keeps polling keeps a scan in the air, so "the radio is busy" is
 * true the whole time a page is open and says nothing. */
bool catnip_ble_scanning(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BLE_H */
