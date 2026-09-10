/* wifi.cpp - see wifi.h. */
#include "wifi.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string.h>

#include "catnip_config.h"

namespace {

catnip_wifi_state g_state = CATNIP_WIFI_OFF;
char g_ssid[CATNIP_WIFI_SSID_MAX];
char g_psk[CATNIP_WIFI_PSK_MAX];
unsigned long g_since; /* when the current join began, for the timeout */

/* Long enough for a slow router and a DHCP lease, short enough that a wrong
 * password is reported as a failure rather than as a device that hangs. WiFi's
 * own retry sits inside this. */
const unsigned long kJoinTimeoutMs = 15000;

} // namespace

void catnip_wifi_begin(const char *ssid, const char *psk)
{
    if (!ssid) ssid = "";
    if (!psk) psk = "";

    /* No network configured is the radio off, not a failed join: a device that
     * was never told a network has not failed to reach one. */
    if (!ssid[0]) {
        catnip_wifi_end();
        return;
    }

    /* Already on it, or already trying it: nothing to do. Comparing the
     * credentials rather than a flag means a card that changed the network
     * between boots is noticed and rejoined. */
    if ((g_state == CATNIP_WIFI_CONNECTED || g_state == CATNIP_WIFI_JOINING) &&
        strcmp(g_ssid, ssid) == 0 && strcmp(g_psk, psk) == 0) {
        return;
    }

    snprintf(g_ssid, sizeof(g_ssid), "%s", ssid);
    snprintf(g_psk, sizeof(g_psk), "%s", psk);

    Serial.printf("[catnip] wifi: joining '%s'\n", g_ssid);
    WiFi.persistent(false); /* the credentials live in NVS as catnip's, not the SDK's */
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid, g_psk[0] ? g_psk : nullptr);
    g_state = CATNIP_WIFI_JOINING;
    g_since = millis();
}

catnip_wifi_state catnip_wifi_poll(void)
{
    if (g_state != CATNIP_WIFI_JOINING) return g_state;

    if (WiFi.status() == WL_CONNECTED) {
        g_state = CATNIP_WIFI_CONNECTED;
        Serial.printf("[catnip] wifi: connected to '%s', ip %s\n", g_ssid,
                      WiFi.localIP().toString().c_str());
        return g_state;
    }
    if (millis() - g_since > kJoinTimeoutMs) {
        g_state = CATNIP_WIFI_FAILED;
        /* WL_NO_SSID_AVAIL and WL_CONNECT_FAILED are the two the SDK actually
         * distinguishes; everything else that is not "connected" by now is a
         * timeout, and the log says which so a failure to join is diagnosable
         * from the cable rather than by guessing. */
        Serial.printf("[catnip] wifi: '%s' did not connect (status %d)\n", g_ssid,
                      (int)WiFi.status());
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    return g_state;
}

catnip_wifi_state catnip_wifi_status(void)
{
    return g_state;
}

const char *catnip_wifi_ssid(void)
{
    return g_state == CATNIP_WIFI_OFF ? nullptr : g_ssid;
}

int catnip_wifi_scan(catnip_wifi_ap *out, int max)
{
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_FAILED) {
        /* Nothing running and nothing to report: start one. Async, so this
         * returns at once and the answer arrives on a later call. show_hidden
         * false, passive false - an ordinary active scan of the 2.4 GHz band. */
        WiFi.scanNetworks(true);
        return -1;
    }
    if (n == WIFI_SCAN_RUNNING) return -1;

    /* n >= 0: a scan finished. Copy what fits, free it, and start the next so
     * the list stays fresh as long as something keeps asking. */
    int count = n < max ? n : max;
    for (int i = 0; i < count && out; i++) {
        snprintf(out[i].ssid, sizeof(out[i].ssid), "%s", WiFi.SSID(i).c_str());
        out[i].rssi = WiFi.RSSI(i);
        out[i].channel = WiFi.channel(i);
    }
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    return count;
}

void catnip_wifi_end(void)
{
    if (g_state == CATNIP_WIFI_OFF) return;
    Serial.println("[catnip] wifi: radio down");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_state = CATNIP_WIFI_OFF;
    g_ssid[0] = '\0';
    g_psk[0] = '\0';
}
