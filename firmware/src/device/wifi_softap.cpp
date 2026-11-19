/* wifi_softap.cpp - softAP + captive DNS/HTTP hook, see wifi_radio.h.
 *
 * This borrows the counted wifi_radio owner for a second, mutually exclusive
 * use: an open access point with a captive front door. Begin acquires the
 * shared radio (which parks the station link), raises the AP, points a DNS at
 * the device for every name, and opens an HTTP server whose handlers hand each
 * request to the caller's cb. End tears those down in reverse and releases the
 * radio - the last release restores STA mode and the station's own grace-timer
 * rejoiner puts Wi-Fi back on a network. This file NEVER calls WiFi.begin().
 *
 * The DNS and HTTP servers are serviced on the app loop, not in an ISR: nothing
 * here runs from a Wi-Fi task. catnip_wifi_softap_poll() drives both, and the
 * WebServer handlers (and therefore the cb) run inside that call.
 */
#include "wifi_radio.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {

/* Whether the AP is up. Separate from the radio's borrow count: it says whether
 * THIS file raised the AP and owns the one release that pairs with its acquire,
 * so a second begin or a stray end cannot double-count the shared radio. */
bool g_ap;

/* Port 53 for the captive DNS, 80 for the portal. The DNS answers every query
 * with the AP's own address, which is what turns a client's name lookup into a
 * redirect to this device; the HTTP server catches every path and calls the
 * hook. Both are constructed once and reused - start()/stop() toggle them. */
DNSServer g_dns;
WebServer g_http(80);

catnip_wifi_http_cb g_http_cb;
void *g_http_ud;

const char *method_name(HTTPMethod m)
{
    switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "?";
    }
}

/* Every request, whatever its path: hand it up and answer with a tiny 200 so a
 * captive-portal check sees a reply from the device rather than the Internet it
 * was expecting, which is the redirect. The body is NOT a portal - it is one
 * line, here only to prove the request reached a handler; the portal is #106.
 *
 * This runs inside g_http.handleClient(), i.e. on the app loop from
 * catnip_wifi_softap_poll(), so the cb is on the app loop as promised. */
void handle_any()
{
    /* WebServer::arg returns a temporary String, so hold it (and the uri) in a
     * local before taking a c_str that outlives the call. */
    String path = g_http.uri();
    String body = g_http.hasArg("plain") ? g_http.arg("plain") : String();

    if (g_http_cb)
        g_http_cb(g_http_ud, method_name(g_http.method()), path.c_str(),
                  body.length() ? body.c_str() : nullptr);

    g_http.send(200, "text/plain", "catnip\n");
}

} /* namespace */

bool catnip_wifi_softap_begin(const char *ssid, uint8_t channel, catnip_wifi_http_cb cb,
                              void *ud)
{
    /* Already up, or no name: nothing to count. */
    if (g_ap) return false;
    if (!ssid || !ssid[0]) return false;

    /* AP and promiscuous are the same radio in incompatible modes. The model is
     * one owner at a time, so a radio already borrowed is the monitor holding
     * it - refuse rather than fight it for the mode. Checked before the acquire
     * so a refusal counts nothing. */
    if (catnip_wifi_radio_is_up()) {
        Serial.println("[catnip] wifi_softap: radio already in use");
        return false;
    }

    /* Take the shared radio. This parks the station link and clears STA to the
     * monitor's starting point; false here means nothing was counted. */
    if (!catnip_wifi_radio_acquire()) return false;

    /* Open AP, no password - this is a demo/captive hook, not a secured
     * network. From here every failure path releases the radio so the acquire
     * above is undone and the caller's false is honest. */
    if (!WiFi.softAP(ssid, nullptr, channel)) {
        Serial.println("[catnip] wifi_softap: softAP would not start");
        catnip_wifi_radio_release();
        return false;
    }

    IPAddress ip = WiFi.softAPIP();

    g_http_cb = cb;
    g_http_ud = ud;

    /* Answer "*" - every name - with the AP's address, so a client resolving
     * anything is pointed back at this device and its captive check lands on the
     * HTTP server below. */
    if (!g_dns.start(53, "*", ip)) {
        Serial.println("[catnip] wifi_softap: captive DNS would not start");
        WiFi.softAPdisconnect(true);
        g_http_cb = NULL;
        g_http_ud = NULL;
        catnip_wifi_radio_release();
        return false;
    }

    /* One handler for every path - onNotFound catches what no route matched,
     * which, with no routes registered, is all of them. */
    g_http.onNotFound(handle_any);
    g_http.begin();

    g_ap = true;
    Serial.printf("[catnip] wifi_softap: AP '%s' up on channel %u, ip %s\n", ssid,
                  channel, ip.toString().c_str());
    return true;
}

void catnip_wifi_softap_end(void)
{
    /* An end with nothing up is a caller bug, not a reason to release a radio
     * this file never acquired - that would decrement the monitor's borrow. */
    if (!g_ap) return;

    /* Tear down in reverse: the servers first, then the AP interface, then hand
     * the radio back. softAPdisconnect(true) drops the AP and its mode bit;
     * radio_release then restores WIFI_STA and clears the hold, in that order,
     * so the station's sole rejoiner runs against a radio already back in STA.
     * We do NOT rejoin here. */
    g_http.stop();
    g_dns.stop();
    WiFi.softAPdisconnect(true);

    g_http_cb = NULL;
    g_http_ud = NULL;
    g_ap = false;
    Serial.println("[catnip] wifi_softap: AP down");

    catnip_wifi_radio_release();
}

void catnip_wifi_softap_poll(void)
{
    if (!g_ap) return;
    /* The app-loop half: service a pending DNS query and run any HTTP client,
     * which is where handle_any() - and so the cb - is called. */
    g_dns.processNextRequest();
    g_http.handleClient();
}
