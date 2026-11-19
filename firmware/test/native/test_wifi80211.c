/* Native test for the 802.11 frame module (#99, U2).
 *
 * A radio is the one thing the host cannot drive, so what is pinned here is
 * everything up to it: that build_beacon and build_deauth lay down the exact
 * IEEE 802.11 bytes - frame control, duration, the three addresses, sequence,
 * the beacon's fixed params and its SSID + DS tags, the deauth's reason code -
 * and that parse reads type/subtype and the addresses back out of a captured
 * buffer, refusing one that is too short. The layout is checked against bytes
 * written out here by hand, not against a second copy of the builder. */
#include <stdio.h>
#include <string.h>

#include "device/wifi80211.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

static int memeq(const uint8_t *got, const uint8_t *want, size_t n)
{
    return memcmp(got, want, n) == 0;
}

/* A locally administered BSSID and a station, both distinct in every byte so a
 * swapped address field cannot pass. */
static const uint8_t BSSID[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t STATION[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

int main(void)
{
    uint8_t buf[WIFI80211_BEACON_MAX];
    wifi80211_view v;
    size_t n;

    printf("== wifi 802.11 frames ==\n");

    /* ---- beacon ---------------------------------------------------------- */
    {
        static const char ssid[] = "catnip"; /* 6 bytes */
        const size_t ssid_len = 6;
        n = wifi80211_build_beacon(BSSID, ssid, ssid_len, 100, 6, buf);

        CHECK(n == 24 + 12 + (2 + ssid_len) + 3, "a beacon is header+fixed+SSID+DS long");

        CHECK(buf[0] == 0x80, "frame control: version 0, type mgmt, subtype beacon");
        CHECK(buf[1] == 0x00, "frame control flags are zero");
        CHECK(buf[2] == 0x00 && buf[3] == 0x00, "duration is zero");
        CHECK(memeq(buf + 4, BROADCAST, 6), "addr1 is the broadcast receiver");
        CHECK(memeq(buf + 10, BSSID, 6), "addr2 is the transmitting bssid");
        CHECK(memeq(buf + 16, BSSID, 6), "addr3 is the bssid");
        CHECK(buf[22] == 0x00 && buf[23] == 0x00, "sequence control is zero");

        {
            static const uint8_t zero8[8] = {0};
            CHECK(memeq(buf + 24, zero8, 8), "the 8-byte timestamp is zeroed");
        }
        CHECK(buf[32] == 0x64 && buf[33] == 0x00,
              "beacon interval 100 TU, little-endian");
        CHECK(buf[34] == 0x01 && buf[35] == 0x00, "capability info is the ESS bit");

        CHECK(buf[36] == 0x00 && buf[37] == 0x06, "SSID tag 0, length 6");
        CHECK(memeq(buf + 38, (const uint8_t *)ssid, 6), "the SSID bytes follow");
        CHECK(buf[44] == 0x03 && buf[45] == 0x01, "DS Parameter Set tag 3, length 1");
        CHECK(buf[46] == 0x06, "and the channel closes it");
    }

    /* A hidden SSID (length 0) is a valid beacon: the tag stays, empty. */
    {
        n = wifi80211_build_beacon(BSSID, NULL, 0, 100, 1, buf);
        CHECK(n == 24 + 12 + 2 + 3, "a length-0 SSID still carries the empty tag");
        CHECK(buf[36] == 0x00 && buf[37] == 0x00, "SSID tag 0, length 0");
        CHECK(buf[38] == 0x03 && buf[39] == 0x01 && buf[40] == 0x01,
              "DS tag follows directly");
    }

    /* An over-long SSID is refused rather than overrunning the buffer. */
    CHECK(wifi80211_build_beacon(BSSID, "x", 33, 100, 1, buf) == 0,
          "an SSID longer than 32 bytes is rejected");

    /* ---- deauth ---------------------------------------------------------- */
    {
        n = wifi80211_build_deauth(BSSID, STATION, 7, buf);
        CHECK(n == WIFI80211_DEAUTH_LEN, "a deauth is 26 bytes");
        CHECK(buf[0] == 0xC0, "frame control: type mgmt, subtype deauth");
        CHECK(buf[1] == 0x00, "frame control flags are zero");
        CHECK(buf[2] == 0x00 && buf[3] == 0x00, "duration is zero");
        CHECK(memeq(buf + 4, STATION, 6), "addr1 is the receiving station");
        CHECK(memeq(buf + 10, BSSID, 6), "addr2 is the transmitting bssid");
        CHECK(memeq(buf + 16, BSSID, 6), "addr3 is the bssid");
        CHECK(buf[22] == 0x00 && buf[23] == 0x00, "sequence control is zero");
        CHECK(buf[24] == 0x07 && buf[25] == 0x00, "the reason code is little-endian");
    }

    /* ---- parse ----------------------------------------------------------- */
    {
        n = wifi80211_build_beacon(BSSID, "catnip", 6, 100, 6, buf);
        int rc = wifi80211_parse(buf, n, &v);
        CHECK(rc == 24, "parsing a beacon consumes the 24-byte header");
        CHECK(v.ok == 1, "the parse is marked ok");
        CHECK(v.type == WIFI80211_TYPE_MGMT, "type is management");
        CHECK(v.subtype == WIFI80211_SUBTYPE_BEACON, "subtype is beacon");
        CHECK(memeq(v.addr1, BROADCAST, 6), "addr1 reads back as broadcast");
        CHECK(memeq(v.addr2, BSSID, 6) && memeq(v.addr3, BSSID, 6),
              "addr2/addr3 are the bssid");
    }

    {
        n = wifi80211_build_deauth(BSSID, STATION, 7, buf);
        int rc = wifi80211_parse(buf, n, &v);
        CHECK(rc == 24 && v.ok == 1, "a deauth parses");
        CHECK(v.type == WIFI80211_TYPE_MGMT && v.subtype == WIFI80211_SUBTYPE_DEAUTH,
              "type mgmt, subtype deauth");
        CHECK(memeq(v.addr1, STATION, 6), "addr1 is the station");
        CHECK(memeq(v.addr2, BSSID, 6) && memeq(v.addr3, BSSID, 6),
              "addr2/addr3 are the bssid");
    }

    /* A captured frame with three distinct addresses, to prove each offset is
     * read from where it belongs and not aliased to its neighbour. */
    {
        static const uint8_t frame[24] = {
            0x40, 0x00,                         /* probe request: type mgmt, subtype 4 */
            0x00, 0x00,                         /* duration */
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, /* addr1 */
            0x22, 0x22, 0x22, 0x22, 0x22, 0x22, /* addr2 */
            0x33, 0x33, 0x33, 0x33, 0x33, 0x33, /* addr3 */
            0x00, 0x00,                         /* sequence */
        };
        static const uint8_t a1[6] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
        static const uint8_t a2[6] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
        static const uint8_t a3[6] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
        wifi80211_parse(frame, sizeof(frame), &v);
        CHECK(v.ok == 1 && v.type == WIFI80211_TYPE_MGMT && v.subtype == 0x04,
              "an arbitrary captured frame yields its type and subtype");
        CHECK(memeq(v.addr1, a1, 6) && memeq(v.addr2, a2, 6) && memeq(v.addr3, a3, 6),
              "each address is read from its own offset");
    }

    /* A buffer one byte short of the header is refused, and says nothing. */
    {
        uint8_t seed[24];
        memset(seed, 0xAB, sizeof(seed));
        int rc = wifi80211_parse(seed, 23, &v);
        static const uint8_t zero6[6] = {0};
        CHECK(rc == 0, "a too-short buffer returns 0");
        CHECK(v.ok == 0, "and marks the view not ok");
        CHECK(memeq(v.addr1, zero6, 6) && memeq(v.addr2, zero6, 6) &&
                  memeq(v.addr3, zero6, 6),
              "with the addresses left zeroed");
    }
    CHECK(wifi80211_parse(buf, 0, &v) == 0 && v.ok == 0, "an empty buffer is too short");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
