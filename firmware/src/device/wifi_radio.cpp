/* wifi_radio.cpp - see wifi_radio.h. */
#include "wifi_radio.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <string.h>

#include "wifi.h"

namespace {

/* How many borrowers hold the radio in monitor use. Not a bool: the second
 * borrower stops the first one's release from handing the radio back to the
 * station - the same counted single-owner the BLE stack uses. */
int g_users;

/* Whether promiscuous capture is currently on. Separate from the borrow count:
 * a borrower may hold the radio to transmit without capturing, so monitor_end
 * is not a release. */
bool g_monitor;

uint8_t g_channel = 1;

/* The ring the Wi-Fi task fills and the app loop drains. Single producer (the
 * promiscuous callback, in the Wi-Fi task, core 0) and single consumer (poll, on
 * the Arduino loop, core 1): the producer owns `head`, the consumer owns `tail`.
 * The two cores do not share a store buffer, so a plain volatile index is not
 * enough - the payload memcpy must be ordered BEFORE the head store, and the
 * consumer must see that order. The index is published with a release store and
 * read with an acquire load, which pairs the payload writes to the slot with the
 * head advance that reveals it; the full/empty check reads the other index the
 * same way. A frame that does not fit, or that arrives with the ring full, is
 * dropped and counted rather than blocking the callback.
 *
 * Sized for management frames, which is what a monitor is mostly here to see:
 * 512 bytes is past every beacon and probe, and eight of them is a loop's worth
 * of burst without the ring becoming the biggest thing on the heap. */
const size_t kFrameMax = 512;
const unsigned kRing = 8;

struct rx_slot {
    uint8_t buf[kFrameMax];
    uint16_t len;
    uint8_t channel;
    int8_t rssi;
};

rx_slot g_ring[kRing];
unsigned g_head; /* written only by the Wi-Fi task; published release/acquire */
unsigned g_tail; /* written only by the app loop; published release/acquire */
volatile uint32_t g_drops;

catnip_wifi_rx_cb g_rx_cb;
void *g_rx_ud;

void promisc_rx(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)type;
    const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
    if (!p) return;

    /* sig_len counts the whole frame including its FCS. A zero-length or
     * over-cap frame is dropped whole rather than truncated: a monitor that
     * hands up half a frame is worse than one that admits it missed it. */
    uint16_t len = (uint16_t)p->rx_ctrl.sig_len;
    if (len == 0 || len > kFrameMax) {
        g_drops++;
        return;
    }

    unsigned next = (g_head + 1) % kRing;
    /* Acquire the consumer's tail so a slot it has not yet freed is seen as
     * occupied. */
    if (next == __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE)) {
        g_drops++;
        return;
    }

    rx_slot *s = &g_ring[g_head];
    memcpy(s->buf, p->payload, len);
    s->len = len;
    s->channel = (uint8_t)p->rx_ctrl.channel;
    s->rssi = (int8_t)p->rx_ctrl.rssi;
    /* Release the slot: the memcpy and the fields above are ordered before this
     * store, so a consumer that acquires the new head sees the whole slot, never
     * a half-written one. */
    __atomic_store_n(&g_head, next, __ATOMIC_RELEASE);
}

} /* namespace */

bool catnip_wifi_radio_acquire(void)
{
    if (g_users == 0) {
        /* Park the station link first, through the one call that owns the park,
         * so the sweep's grace timer cannot rejoin underneath us while we hold
         * the radio. hold(true) runs park_link() -> WiFi.disconnect(false),
         * which drops the association but keeps the radio in STA mode. */
        catnip_wifi_hold(true);

        /* Take the radio for monitor use. Mode stays STA (started, no
         * association): promiscuous is a flag on a started radio, esp_wifi_80211_tx
         * injects on WIFI_IF_STA, and esp_wifi_set_channel then owns the channel
         * the station is no longer parked on.
         *
         * If bring-up fails, nothing is counted and the radio is handed back to
         * the station - the caller's if(!acquire()) is then real and there is
         * nothing to release. Undo the hold we took the same way release does:
         * restore STA, then clear the hold so the station's rejoiner runs. */
        if (!WiFi.mode(WIFI_STA)) {
            Serial.println("[catnip] wifi_radio: STA mode would not start");
            catnip_wifi_hold(false);
            return false;
        }
        esp_err_t e = esp_wifi_set_channel(g_channel, WIFI_SECOND_CHAN_NONE);
        if (e != ESP_OK) {
            Serial.printf("[catnip] wifi_radio: channel set failed (%d)\n", (int)e);
            WiFi.mode(WIFI_STA);
            catnip_wifi_hold(false);
            return false;
        }
        Serial.println("[catnip] wifi_radio: radio borrowed for monitor");
    }
    g_users++;
    return true;
}

void catnip_wifi_radio_release(void)
{
    /* A release with nothing held is a caller bug, not a reason to hand the
     * radio back to a station that never lost it. */
    if (g_users == 0) return;
    if (--g_users > 0) return;

    /* Make sure capture is off before the radio goes back to the station. */
    catnip_wifi_monitor_end();

    /* Restore STA mode BEFORE clearing the hold. This ordering is the race fix:
     * the sole rejoiner - unpark_link() in catnip_wifi_poll() - runs only once
     * the hold is clear, so it must find the radio already in STA mode. Clearing
     * the hold first would let the rejoiner run against promiscuous mode. We do
     * NOT rejoin here; the grace timer does, now into a radio back in STA. */
    WiFi.mode(WIFI_STA);
    catnip_wifi_hold(false);
    Serial.println("[catnip] wifi_radio: radio returned to the station");
}

bool catnip_wifi_radio_is_up(void)
{
    return g_users > 0;
}

bool catnip_wifi_monitor_begin(catnip_wifi_rx_cb cb, void *ud, uint8_t channel)
{
    if (g_users == 0 || !cb) return false;

    g_rx_cb = cb;
    g_rx_ud = ud;
    /* Empty the ring so a new session never drains the last one's frames. Safe
     * here: capture is not yet on, so the Wi-Fi task is not touching head. */
    g_head = 0;
    g_tail = 0;
    g_drops = 0;

    /* Install the cb BEFORE enabling promiscuous, so no frame can arrive in a
     * window where capture is on but there is no cb to enqueue it. If any step
     * fails, capture is not actually on: undo and report false rather than
     * claiming a session the radio never started. */
    esp_err_t e = esp_wifi_set_promiscuous_rx_cb(&promisc_rx);
    if (e != ESP_OK) {
        Serial.printf("[catnip] wifi_radio: rx cb install failed (%d)\n", (int)e);
        g_rx_cb = NULL;
        g_rx_ud = NULL;
        return false;
    }
    e = esp_wifi_set_promiscuous(true);
    if (e != ESP_OK) {
        Serial.printf("[catnip] wifi_radio: promiscuous enable failed (%d)\n", (int)e);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        g_rx_cb = NULL;
        g_rx_ud = NULL;
        return false;
    }
    e = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (e != ESP_OK) {
        Serial.printf("[catnip] wifi_radio: channel set failed (%d)\n", (int)e);
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(NULL);
        g_rx_cb = NULL;
        g_rx_ud = NULL;
        return false;
    }
    g_channel = channel;
    g_monitor = true;
    Serial.printf("[catnip] wifi_radio: monitoring on channel %u\n", channel);
    return true;
}

void catnip_wifi_monitor_set_channel(uint8_t channel)
{
    if (!g_monitor) return;
    g_channel = channel;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void catnip_wifi_monitor_end(void)
{
    if (!g_monitor) return;
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_set_promiscuous(false);
    g_monitor = false;
    g_rx_cb = NULL;
    g_rx_ud = NULL;
    Serial.println("[catnip] wifi_radio: monitoring off");
}

void catnip_wifi_radio_poll(void)
{
    /* Snapshot the cb: monitor_end may null it between frames, and a frame still
     * in the ring from a session that just ended is simply dropped. */
    catnip_wifi_rx_cb cb = g_rx_cb;
    void *ud = g_rx_ud;

    /* Acquire the producer's head so every slot up to it is fully visible. */
    unsigned head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    while (g_tail != head) {
        rx_slot *s = &g_ring[g_tail];
        if (cb) cb(ud, s->buf, s->len, s->channel, s->rssi);
        /* Release the slot only after the cb has read it: the producer waits to
         * see this store before it reuses the slot. */
        __atomic_store_n(&g_tail, (g_tail + 1) % kRing, __ATOMIC_RELEASE);
    }
}

bool catnip_wifi_tx(const uint8_t *frame, size_t len)
{
    if (g_users == 0 || !frame || len == 0) return false;
    /* en_sys_seq false: send the frame as given, without the hardware rewriting
     * its sequence-control field - raw injection means raw. */
    esp_err_t e = esp_wifi_80211_tx(WIFI_IF_STA, frame, (int)len, false);
    return e == ESP_OK;
}
