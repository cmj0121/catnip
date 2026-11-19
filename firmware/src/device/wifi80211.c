/* wifi80211.c - see wifi80211.h. The byte layout, and nothing that transmits. */
#include "wifi80211.h"

#include <string.h>

/* The common management/data header carries three addresses in 24 bytes:
 *   [0..1]  frame control   [2..3] duration   [4..9] addr1
 *   [10..15] addr2          [16..21] addr3    [22..23] sequence control
 * Beacon and deauth, the two frames built here, both use this full header. */
#define WIFI80211_HDR_LEN 24

/* Beacon capability info: the ESS bit, which marks the sender as an
 * infrastructure access point - what a beacon frame is. Nothing else is
 * claimed. Little-endian on the wire. */
#define WIFI80211_CAP_ESS 0x0001

/* Longest SSID the standard allows in the tag. */
#define WIFI80211_SSID_MAX 32

static uint8_t *put_u16le(uint8_t *p, uint16_t v)
{
    *p++ = (uint8_t)(v & 0xFF);
    *p++ = (uint8_t)((v >> 8) & 0xFF);
    return p;
}

/* Write the 24-byte three-address header. frame_control is the first byte
 * (version | type<<2 | subtype<<4); the second is zero for these frames.
 * Duration and sequence control are left at zero. */
static uint8_t *put_header(uint8_t *p, uint8_t fc0, const uint8_t *addr1,
                           const uint8_t *addr2, const uint8_t *addr3)
{
    *p++ = fc0;
    *p++ = 0x00;         /* frame control flags */
    p = put_u16le(p, 0); /* duration */
    memcpy(p, addr1, WIFI80211_ADDR_LEN);
    p += WIFI80211_ADDR_LEN;
    memcpy(p, addr2, WIFI80211_ADDR_LEN);
    p += WIFI80211_ADDR_LEN;
    memcpy(p, addr3, WIFI80211_ADDR_LEN);
    p += WIFI80211_ADDR_LEN;
    p = put_u16le(p, 0); /* sequence control */
    return p;
}

size_t wifi80211_build_beacon(const uint8_t bssid[WIFI80211_ADDR_LEN], const char *ssid,
                              size_t ssid_len, uint16_t interval_tu, uint8_t channel,
                              uint8_t *out)
{
    static const uint8_t broadcast[WIFI80211_ADDR_LEN] = {0xFF, 0xFF, 0xFF,
                                                          0xFF, 0xFF, 0xFF};
    uint8_t fc0 = (uint8_t)(WIFI80211_SUBTYPE_BEACON << 4); /* type MGMT = 0 */
    uint8_t *p = out;

    if (!bssid || !out) return 0;
    if (ssid_len > WIFI80211_SSID_MAX) return 0;
    if (ssid_len > 0 && !ssid) return 0;

    /* addr1 = receiver (broadcast for a beacon), addr2 = transmitter = bssid,
     * addr3 = bssid. */
    p = put_header(p, fc0, broadcast, bssid, bssid);

    /* Fixed parameters: timestamp (zeroed), beacon interval, capability. */
    memset(p, 0, 8);
    p += 8;
    p = put_u16le(p, interval_tu);
    p = put_u16le(p, WIFI80211_CAP_ESS);

    /* Tagged parameters: SSID (tag 0), then DS Parameter Set (tag 3). */
    *p++ = 0x00; /* SSID element id */
    *p++ = (uint8_t)ssid_len;
    if (ssid_len > 0) {
        memcpy(p, ssid, ssid_len);
        p += ssid_len;
    }
    *p++ = 0x03; /* DS Parameter Set element id */
    *p++ = 0x01; /* length: one channel byte */
    *p++ = channel;

    return (size_t)(p - out);
}

size_t wifi80211_build_deauth(const uint8_t bssid[WIFI80211_ADDR_LEN],
                              const uint8_t station[WIFI80211_ADDR_LEN], uint16_t reason,
                              uint8_t *out)
{
    uint8_t fc0 = (uint8_t)(WIFI80211_SUBTYPE_DEAUTH << 4); /* type MGMT = 0 */
    uint8_t *p = out;

    if (!bssid || !station || !out) return 0;

    /* addr1 = receiver = station, addr2 = transmitter = bssid, addr3 = bssid. */
    p = put_header(p, fc0, station, bssid, bssid);

    /* Body: the 2-byte reason code. */
    p = put_u16le(p, reason);

    return (size_t)(p - out);
}

int wifi80211_parse(const uint8_t *buf, size_t len, wifi80211_view *out)
{
    uint8_t fc0;

    if (!out) return 0;
    memset(out, 0, sizeof(*out)); /* ok = 0, addresses zeroed */

    if (!buf || len < WIFI80211_HDR_LEN) return 0;

    fc0 = buf[0];
    out->type = (uint8_t)((fc0 >> 2) & 0x03);
    out->subtype = (uint8_t)((fc0 >> 4) & 0x0F);
    memcpy(out->addr1, buf + 4, WIFI80211_ADDR_LEN);
    memcpy(out->addr2, buf + 10, WIFI80211_ADDR_LEN);
    memcpy(out->addr3, buf + 16, WIFI80211_ADDR_LEN);
    out->ok = 1;
    return WIFI80211_HDR_LEN;
}
