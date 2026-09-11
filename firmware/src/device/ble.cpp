/* ble.cpp - see ble.h. */
#include "ble.h"

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
 * must be one that can end without anybody clearing it. */
const uint32_t kAskGraceMs = 3000;
const uint32_t kPatienceMs = 12000;

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
    NimBLEDevice::init("");
    g_scan = NimBLEDevice::getScan();
    if (!g_scan) return false;
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
    g_scan->stop();
    NimBLEDevice::deinit(true);
    g_up = false;
    g_scan = nullptr;
    g_running = false;
    g_n = 0;
    Serial.println("[catnip] ble: down");
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

    if (!g_started || (uint32_t)(now - g_last_ask) > kAskGraceMs) {
        g_started = now ? now : 1;
        g_answered = false;
    }
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
    g_started = millis();
    g_answered = false;
}
