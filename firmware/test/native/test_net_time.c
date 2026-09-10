/*
 * Native test for the SNTP wire format and the timezone maths (#84).
 *
 * The clock coming off the network goes wrong the same quiet way the clock
 * coming off the chip does: a byte order read backwards, an epoch off by
 * seventy years, a server that is answering but does not know the time itself.
 * None of that needs a socket to reproduce, so none of it is tested with one.
 *
 * The through-line, as in test_rtc_time, is that not knowing survives every
 * path. A reply the wire says not to trust comes back as 0, never as a
 * plausible wrong date - because the whole point of setting a clock from the
 * network is to be right, and a confident wrong answer is worse than none.
 */
#include <stdio.h>
#include <string.h>

#include "device/net_time.h"

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

/* A well-formed reply carrying `ntp_secs` in its transmit timestamp. */
static void reply_with(uint8_t *r, uint8_t li_vn_mode, uint8_t stratum, uint32_t ntp_secs)
{
    memset(r, 0, CATNIP_NTP_PACKET_LEN);
    r[0] = li_vn_mode;
    r[1] = stratum;
    r[40] = (uint8_t)(ntp_secs >> 24);
    r[41] = (uint8_t)(ntp_secs >> 16);
    r[42] = (uint8_t)(ntp_secs >> 8);
    r[43] = (uint8_t)ntp_secs;
}

int main(void)
{
    uint8_t r[CATNIP_NTP_PACKET_LEN];
    uint8_t req[CATNIP_NTP_PACKET_LEN];

    printf("the clock from the network\n");

    /* The request says client, version 4, and nothing else. */
    catnip_ntp_request(req);
    CHECK(req[0] == 0x23, "the request is LI 0, version 4, mode 3 (client)");
    {
        int zeros = 1;
        for (int i = 1; i < CATNIP_NTP_PACKET_LEN; i++)
            if (req[i]) zeros = 0;
        CHECK(zeros, "and carries no time of its own");
    }

    /* 1900-01-01 + 3944678400 s is 2025-01-01 00:00:00 UTC. The parse subtracts
     * the 70-year delta to land on the Unix time. */
    reply_with(r, 0x1C, 2, 3944678400u); /* LI 0, VN 3, mode 4 (server) */
    CHECK(catnip_ntp_parse(r, sizeof(r)) == 1735689600u,
          "a good reply decodes to the Unix epoch it names");

    /* Short is not a reply. */
    CHECK(catnip_ntp_parse(r, 40) == 0, "a reply too short is not believed");
    CHECK(catnip_ntp_parse(NULL, 48) == 0, "and neither is no reply at all");

    /* The server saying it does not know: leap indicator 3. */
    reply_with(r, 0xDC, 2, 3944678400u); /* LI 3 */
    CHECK(catnip_ntp_parse(r, sizeof(r)) == 0,
          "a leap indicator of 3 is the server disowning its own time");

    /* Stratum 0 is a kiss-o'-death: the timestamp bytes are a code, not a time. */
    reply_with(r, 0x1C, 0, 0x4B4F4400u); /* "KOD\0" where the seconds go */
    CHECK(catnip_ntp_parse(r, sizeof(r)) == 0,
          "stratum 0 is a kiss-o'-death, not a timestamp");

    /* A zero timestamp, and one that predates 1970, both underflow the delta. */
    reply_with(r, 0x1C, 2, 0);
    CHECK(catnip_ntp_parse(r, sizeof(r)) == 0, "a zero timestamp is not a time");
    reply_with(r, 0x1C, 2, 1000u);
    CHECK(catnip_ntp_parse(r, sizeof(r)) == 0,
          "and one before 1970 is not allowed to wrap into one");

    /* The offset is added on the way to local, and only to a time that is
     * known. Taipei is +480. */
    CHECK(catnip_ntp_to_local(1735689600u, 480) == 1735689600u + 480 * 60,
          "the timezone offset is added to reach local wall time");
    CHECK(catnip_ntp_to_local(1735689600u, -300) == 1735689600u - 300 * 60,
          "a negative offset is subtracted");
    CHECK(catnip_ntp_to_local(0, 480) == 0,
          "adding a timezone to a time that is not known invents nothing");

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
