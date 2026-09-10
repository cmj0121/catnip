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

/* Start one.
 *
 * This used to turn modem sleep off first, on the theory that a connected
 * station parks on the channel it is associated to and cannot hear anything
 * else. The theory was wrong and the log said so: with the dwell and the rest
 * below in place, scans come back with seven and eight networks while
 * `WiFi.getSleep()` reads 1 throughout. The machinery went with the theory -
 * a driver-level power change made under a running scan can abort it, and
 * paying that risk for something the data says does nothing is the worst
 * trade there is. */
/* How long the radio listens on each channel, and how long it rests between
 * sweeps.
 *
 * Four hundred milliseconds is longer than the beacon interval every access
 * point in the world uses (about 102 ms), so a channel is listened to for four
 * beacons rather than for one that may or may not fall inside the window - and
 * across the fourteen channels of the 2.4 GHz band that is a sweep of about
 * five and a half seconds. A scan is a thing that takes time; the failure this
 * replaces was one that took less and reported nothing.
 *
 * And a second of quiet between sweeps, because this radio is also holding an
 * association. It was restarting the next sweep in the same breath as
 * finishing the last, so the station never had an uninterrupted moment to be a
 * station in. */
static const uint32_t kDwellMs = 400;
static const uint32_t kRestMs = 1000;
static uint32_t g_scan_done_at;

static void start_scan(void)
{
    /* Async, hidden networks included, active rather than passive, and the
     * dwell said out loud rather than left to the default. A page that is here
     * to show what is on the air is here to show the ones that do not announce
     * themselves as well, and the app already has a word for them. */
    WiFi.scanNetworks(true, true, false, kDwellMs);
    g_scan_done_at = 0;
}

catnip_wifi_state catnip_wifi_poll(void)
{
    /* Modem sleep off for a scan, and back on when nobody is scanning.
     *
     * A connected station with modem sleep on parks itself on the channel it is
     * associated to and wakes only for its own beacons - so a scan started from
     * there never leaves that channel and finishes having heard nothing. The
     * radio reports that as a *completed* scan of zero networks, which is the
     * most misleading answer it could give: the app is right, the driver is
     * right, the plumbing is right, and the page is empty.
     *
     * It costs power, which is why it is not simply left off. It goes off for
     * as long as somebody is asking for scans and comes back the moment they
     * stop - the same "still asking" the busy ring is raised by, so a page that
     * is open is a radio that is awake and a page that was closed is not. */
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

/* Whether the ring should be up, which is not the same as whether the radio is
 * scanning. A caller that keeps polling keeps a scan in the air for as long as
 * it is open, so "the radio is scanning" is true the whole time the prober is
 * on screen - and a spinner that never stops is not telling anybody anything.
 *
 * What the ring means is "there is nothing to show yet". So: up from the first
 * ask until the first answer, and then down, however many scans run behind it.
 *
 * A gap in the asking is a new session - the app was closed and opened again -
 * and the ring comes back for it. Three seconds, which is longer than any
 * caller's own poll interval and shorter than anybody's patience. */
static bool g_scan_asked;
static bool g_scan_answered;
static uint32_t g_scan_last_ask;
static uint32_t g_scan_started;

/* How long after the last ask the claim expires, and how long a single wait may
 * last however hard somebody is asking.
 *
 * Both of these are the same lesson twice. The ring this drives is full-screen
 * and opaque, so "busy" is a claim that takes the whole device - and a claim
 * that takes the whole device must be a claim that can end on its own. It could
 * not: leaving the prober with a scan outstanding left `asked` true and
 * `answered` false with nobody left to change either, and the ring sat over
 * every screen in the device for ever with no press that would clear it. A scan
 * that never completed did the same thing without anyone leaving.
 *
 * So the claim lives only as long as somebody is still asking (a caller that
 * has gone away stops asking), and never longer than a scan can honestly take.
 * A 2.4 GHz sweep is about two seconds; eight is generous and still an end. */
#define SCAN_ASK_GRACE_MS 3000u
#define SCAN_PATIENCE_MS  8000u

bool catnip_wifi_scanning(void)
{
    uint32_t now;

    if (!g_scan_asked || g_scan_answered) return false;
    now = millis();
    if ((uint32_t)(now - g_scan_last_ask) > SCAN_ASK_GRACE_MS) return false;
    if ((uint32_t)(now - g_scan_started) > SCAN_PATIENCE_MS) return false;
    return true;
}

/* Modem sleep off for a scan, and back on when nobody is scanning.
 *
 * A connected station with modem sleep on parks itself on the channel it is
 * associated to and wakes only for its own beacons - so a scan started from
 * there never leaves that channel and finishes having heard nothing. The radio
 * reports that as a completed scan of zero networks, which is the most
 * misleading answer it could give: the app is right, the driver is right, the
 * plumbing is right, and the page is empty.
 *
 * It costs power, which is why it is not simply left off. It goes off for as
 * long as somebody is asking for scans and comes back the moment they stop -
 * the same "still asking" the busy ring is raised by, so a page that is open is
 * a radio that is awake and a page that was closed is not. */
void catnip_wifi_rescan(void)
{
    /* Drop the finished scan so the next ask starts one rather than reading
     * this one out again, and forget that anything was ever answered - which is
     * what puts the ring back up. */
    WiFi.scanDelete();
    g_scan_asked = true;
    g_scan_answered = false;
    g_scan_started = millis();
    start_scan();
}

int catnip_wifi_scan(catnip_wifi_ap *out, int max)
{
    int n = WiFi.scanComplete();
    uint32_t now = millis();

    /* TEMPORARY: what the radio is actually answering. */
    {
        static int last_n = -99;
        static uint32_t last_log;
        if (n != last_n || (uint32_t)(now - last_log) > 2000u) {
            last_n = n;
            last_log = now;
            Serial.printf("[catnip] scan: complete=%d status=%d sleep=%d awake=%d "
                          "asked=%d answered=%d\n",
                          n, (int)WiFi.status(), (int)WiFi.getSleep(), 0,
                          (int)g_scan_asked, (int)g_scan_answered);
        }
    }

    if (!g_scan_asked || (uint32_t)(now - g_scan_last_ask) > SCAN_ASK_GRACE_MS) {
        g_scan_asked = true;
        g_scan_answered = false;
        g_scan_started = now;
    }
    g_scan_last_ask = now;

    if (n == WIFI_SCAN_FAILED) {
        /* Nothing running and nothing to report: start one. Async, so this
         * returns at once and the answer arrives on a later call. show_hidden
         * false, passive false - an ordinary active scan of the 2.4 GHz band. */
        /* Nothing running. Start one, unless the last sweep only just
         * finished - the rest is what keeps this from being a radio that
         * scans continuously and is a station in the gaps. */
        if (g_scan_done_at == 0 || (uint32_t)(now - g_scan_done_at) >= kRestMs)
            start_scan();
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
    /* Answered, and then quiet. The next sweep starts on a later ask, once the
     * rest above has passed: starting it here meant the radio was mid-sweep
     * again before the answer had even been drawn. */
    g_scan_done_at = now ? now : 1;
    g_scan_answered = true;
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
