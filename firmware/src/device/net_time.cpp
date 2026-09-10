/* net_time.cpp - the driver half of net_time.h: the socket and the registers.
 *
 * A small non-blocking state machine, because the radio takes seconds to join
 * and the main loop must not stop for it. It owns the sync from "bring the
 * radio up" to "write the RTC", and drops the radio at the end - a clock sync
 * is the whole reason the radio came up, so nothing keeps it after. */
#include "net_time.h"

#include <Arduino.h>
#include <WiFiUdp.h>

#include "rtc.h"
#include "wifi.h"

namespace {

catnip_sync_state g_state = CATNIP_SYNC_IDLE;
int g_tz;
uint32_t g_last;       /* local epoch of the last good sync, 0 for never */
unsigned long g_since; /* when the query began, for its own timeout */
WiFiUDP g_udp;
bool g_sent;

/* pool.ntp.org is the conventional default and resolves to a nearby server. A
 * device on a network with its own NTP server would want that instead, but a
 * name to override it is a second config key for later; the pool is the answer
 * that works from anywhere with a route to the internet. */
const char kServer[] = "pool.ntp.org";
const uint16_t kNtpPort = 123;
const unsigned long kQueryTimeoutMs = 5000;

void finish(catnip_sync_state how)
{
    g_state = how;
    g_udp.stop();
    g_sent = false;
    /* The radio is left as it was found. A sync no longer owns it: a network
     * named on the card is meant to stay joined - the header shows it, and the
     * next thing that wants the link (an app) finds it up. A join that failed
     * has already powered the radio down in catnip_wifi_poll(); a join that
     * succeeded stays connected. */
}

} // namespace

void catnip_net_time_sync(const char *ssid, const char *psk, int tz_offset_min)
{
    g_tz = tz_offset_min;
    g_sent = false;
    if (!ssid || !ssid[0]) {
        Serial.println("[catnip] ntp: no network configured, cannot sync");
        g_state = CATNIP_SYNC_FAILED;
        return;
    }
    Serial.println("[catnip] ntp: sync requested");
    catnip_wifi_begin(ssid, psk);
    g_state = CATNIP_SYNC_JOINING;
}

catnip_sync_state catnip_net_time_poll(void)
{
    if (g_state != CATNIP_SYNC_JOINING && g_state != CATNIP_SYNC_QUERYING) return g_state;

    if (g_state == CATNIP_SYNC_JOINING) {
        catnip_wifi_state w = catnip_wifi_poll();
        if (w == CATNIP_WIFI_FAILED) {
            Serial.println("[catnip] ntp: no network, sync abandoned");
            finish(CATNIP_SYNC_FAILED);
            return g_state;
        }
        if (w != CATNIP_WIFI_CONNECTED) return g_state; /* still joining */

        /* On the network. Open the socket and fire the request. */
        uint8_t req[CATNIP_NTP_PACKET_LEN];
        catnip_ntp_request(req);
        g_udp.begin(0); /* any local port */
        if (g_udp.beginPacket(kServer, kNtpPort) != 1) {
            Serial.println("[catnip] ntp: could not resolve the server");
            finish(CATNIP_SYNC_FAILED);
            return g_state;
        }
        g_udp.write(req, sizeof(req));
        g_udp.endPacket();
        g_sent = true;
        g_since = millis();
        g_state = CATNIP_SYNC_QUERYING;
        Serial.println("[catnip] ntp: request sent");
        return g_state;
    }

    /* QUERYING: wait for the reply, or time out. */
    if (g_udp.parsePacket() >= CATNIP_NTP_PACKET_LEN) {
        uint8_t reply[CATNIP_NTP_PACKET_LEN];
        int n = g_udp.read(reply, sizeof(reply));
        uint32_t utc = catnip_ntp_parse(reply, n);
        uint32_t local = catnip_ntp_to_local(utc, g_tz);
        if (local && catnip_rtc_set(local)) {
            g_last = local;
            Serial.printf("[catnip] ntp: clock set, utc %lu, local %lu (%+d min)\n",
                          (unsigned long)utc, (unsigned long)local, g_tz);
            finish(CATNIP_SYNC_DONE);
        } else {
            /* A reply the wire said not to trust, or one the RTC would not take
             * (a year its registers cannot hold). Either way the clock is left
             * exactly as it was - a sync that did not happen changes nothing. */
            Serial.println("[catnip] ntp: reply not usable, clock left as it was");
            finish(CATNIP_SYNC_FAILED);
        }
        return g_state;
    }
    if (millis() - g_since > kQueryTimeoutMs) {
        Serial.println("[catnip] ntp: no reply, sync abandoned");
        finish(CATNIP_SYNC_FAILED);
    }
    return g_state;
}

bool catnip_net_time_busy(void)
{
    return g_state == CATNIP_SYNC_JOINING || g_state == CATNIP_SYNC_QUERYING;
}

uint32_t catnip_net_time_last(void)
{
    return g_last;
}
