/* ble.cpp - see ble.h. */
#include "ble.h"

#include "ble_stack.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string.h>

namespace {

/* How long one listen lasts, and how long the radio rests between them.
 *
 * Advertisers repeat on their own interval - a beacon every hundred
 * milliseconds, a tracker every second or two, a phone somewhere between - so
 * the only thing a scan window buys is the chance to hear the slow ones. Four
 * seconds catches everything that advertises faster than about once a second,
 * which is nearly everything a person is carrying, and is short enough that a
 * page which opens on it does not feel stuck.
 *
 * The rest is the same second the Wi-Fi sweep takes, and for the same reason:
 * this front end is shared, and a radio that is always listening is a radio
 * nothing else can have. */
const uint32_t kWindowMs = 4000;
const uint32_t kRestMs = 1000;

/* An active scan asks each advertiser for its scan response, which is where a
 * name usually lives - a passive one hears the advertisement only and would
 * leave most of this list unnamed. It costs a transmission per device, which is
 * the price of the page being readable. */
const bool kActive = true;

/* More than this in one room is a room nobody is going to read a list of. The
 * cap is the Lua binding's anyway; this one only has to be no smaller. */
const int kMax = 32;

bool g_up;
NimBLEScan *g_scan;

/* What the last finished window heard, merged by address. */
catnip_ble_dev g_found[kMax];
int g_n;

bool g_running;
bool g_answered;
uint32_t g_started;
uint32_t g_last_ask;
uint32_t g_done_at;

/* The same two ends the Wi-Fi claim has, and for the same reason: the busy ring
 * this drives takes the whole panel, and a claim that takes the whole device
 * must be one that can end without anybody clearing it. The claim itself begins
 * at rescan() and nowhere else - see catnip_wifi_scanning() for why asking must
 * not begin one. */
const uint32_t kAskGraceMs = 3000;
const uint32_t kPatienceMs = 12000;

/* How long the radio stays up with nobody asking before it is taken down.
 *
 * A controller that is initialised is a controller sharing the 2.4 GHz front
 * end with Wi-Fi whether or not it is listening, so it does not get to outlive
 * the page that wanted it. Longer than the grace above, because a page that
 * alternates between two radios stops asking this one for a whole Wi-Fi sweep
 * and has not gone anywhere. */
const uint32_t kIdleDownMs = 20000;

void collect(NimBLEScanResults results)
{
    g_n = 0;
    for (int i = 0; i < results.getCount() && g_n < kMax; i++) {
        NimBLEAdvertisedDevice d = results.getDevice(i);
        catnip_ble_dev *out = &g_found[g_n];

        snprintf(out->addr, sizeof(out->addr), "%s", d.getAddress().toString().c_str());
        /* A name if it gave one, and an empty string if not - which is the
         * common case and is the app's to render, not this driver's to invent.
         * A driver that wrote "(unknown)" here would be putting a word on the
         * screen that no radio ever said. */
        if (d.haveName())
            snprintf(out->name, sizeof(out->name), "%s", d.getName().c_str());
        else out->name[0] = '\0';
        out->rssi = d.getRSSI();
        g_n++;
    }
}

} /* namespace */

bool catnip_ble_begin(void)
{
    if (g_up) return true;
    /* The stack is shared and counted now (see ble_stack.h): this asks for it
     * rather than starting it, because the mouse may already be holding it. */
    if (!catnip_ble_stack_acquire()) return false;
    g_scan = NimBLEDevice::getScan();
    if (!g_scan) {
        catnip_ble_stack_release();
        return false;
    }
    g_scan->setActiveScan(kActive);
    /* Listen for all of every interval rather than a slice of each: there is no
     * connection to keep alive here, so there is nothing to leave room for. */
    g_scan->setInterval(100);
    g_scan->setWindow(99);
    /* NimBLE can merge duplicates itself, and it is asked to: an advertiser is
     * heard many times in one window, and a list that showed it once per
     * advertisement would be a list of one device's heartbeats. */
    g_scan->setDuplicateFilter(true);
    g_up = true;
    Serial.println("[catnip] ble: up");
    return true;
}

void catnip_ble_end(void)
{
    if (!g_up) return;
    /* Put the scan away by hand. It used to be enough to deinit the stack and
     * let everything go with it, but the stack is no longer this driver's to
     * take down - releasing it may leave it up for the mouse, and a scan left
     * running inside it would be this driver still listening after it said it
     * had stopped. */
    g_scan->stop();
    g_scan->clearResults();
    g_up = false;
    g_scan = nullptr;
    g_running = false;
    g_n = 0;
    catnip_ble_stack_release();
    Serial.println("[catnip] ble: scan down");
}

bool catnip_ble_scanning(void)
{
    uint32_t now;

    if (g_answered || !g_started) return false;
    now = millis();
    if ((uint32_t)(now - g_last_ask) > kAskGraceMs) return false;
    if ((uint32_t)(now - g_started) > kPatienceMs) return false;
    return true;
}

int catnip_ble_scan(catnip_ble_dev *out, int max)
{
    uint32_t now = millis();

    if (!catnip_ble_begin()) return -1;

    g_last_ask = now;

    if (g_running) {
        if (g_scan->isScanning()) return -1;
        /* The window closed while nobody was looking. NimBLE holds the results
         * until they are asked for, so this is where they are taken. */
        collect(g_scan->getResults());
        g_scan->clearResults();
        g_running = false;
        g_done_at = now ? now : 1;
        g_answered = true;
    } else if (g_done_at == 0 || (uint32_t)(now - g_done_at) >= kRestMs) {
        /* Non-blocking: `false` for is_continue, and the call returns at once
         * with the window running behind it. */
        g_scan->start(kWindowMs / 1000, nullptr, false);
        g_running = true;
        return -1;
    }

    if (!out || max <= 0) return g_n;
    int n = g_n < max ? g_n : max;
    for (int i = 0; i < n; i++)
        out[i] = g_found[i];
    return n;
}

void catnip_ble_poll(void)
{
    /* Nobody is listening any more. Give the front end back.
     *
     * Here rather than at the end of a scan because the question is not "has
     * the window closed" but "has the page gone", and the only evidence of that
     * is the asking stopping. g_last_ask of zero is a radio nobody has ever
     * asked about, which is not idle - it is not up. */
    if (!g_up || !g_last_ask) return;
    if ((uint32_t)(millis() - g_last_ask) <= kIdleDownMs) return;
    catnip_ble_end();
    g_last_ask = 0;
    g_started = 0;
    g_answered = false;
}

void catnip_ble_rescan(void)
{
    if (!catnip_ble_begin()) return;
    if (g_running) {
        g_scan->stop();
        g_scan->clearResults();
        g_running = false;
    }
    g_n = 0;
    g_done_at = 0;
    g_started = millis() ? millis() : 1;
    g_answered = false;
}
