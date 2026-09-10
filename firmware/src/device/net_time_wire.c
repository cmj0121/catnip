/* net_time_wire.c - the SNTP wire format and the timezone maths (#84).
 *
 * The half of net_time.h with no socket in it, so a host test can hold a real
 * server's reply still and check that a good one decodes and every bad one
 * comes back as "not known". See rtc_time.c for the same arrangement and the
 * same reason: a clock that answers confidently and wrongly will be believed.
 */
#include "net_time.h"

/* The 70 years between the NTP epoch (1900) and the Unix epoch (1970), in
 * seconds, leap days counted. Every SNTP timestamp is this much larger than the
 * Unix time it names. */
#define NTP_UNIX_DELTA 2208988800u

void catnip_ntp_request(uint8_t out[CATNIP_NTP_PACKET_LEN])
{
    int i;

    if (!out) return;
    for (i = 0; i < CATNIP_NTP_PACKET_LEN; i++)
        out[i] = 0;
    /* LI = 0, VN = 4, Mode = 3 (client): 00 100 011. The server fills in the
     * rest; a request carries no time of its own that this device could be
     * trusted to know. */
    out[0] = 0x23;
}

uint32_t catnip_ntp_parse(const uint8_t *reply, int len)
{
    uint8_t li, stratum;
    uint32_t secs;

    if (!reply || len < CATNIP_NTP_PACKET_LEN) return 0;

    /* The leap indicator is the top two bits of byte 0. 3 means the server's
     * own clock is not synchronised - it is answering, but with a time it does
     * not stand behind. */
    li = (uint8_t)((reply[0] >> 6) & 0x3);
    if (li == 3) return 0;

    /* Stratum 0 is a kiss-o'-death packet: the four "time" bytes are an ASCII
     * code, not a timestamp, and reading them as one gives a plausible wrong
     * date decades out. */
    stratum = reply[1];
    if (stratum == 0) return 0;

    /* The transmit timestamp's seconds, bytes 40-43, big-endian on the wire. */
    secs = ((uint32_t)reply[40] << 24) | ((uint32_t)reply[41] << 16) |
           ((uint32_t)reply[42] << 8) | (uint32_t)reply[43];
    if (secs == 0) return 0;

    /* Before 1970 is impossible for a real server and means the subtraction
     * would wrap; treated as "not known" rather than allowed to underflow. */
    if (secs < NTP_UNIX_DELTA) return 0;
    return secs - NTP_UNIX_DELTA;
}

uint32_t catnip_ntp_to_local(uint32_t utc_epoch, int tz_offset_min)
{
    long local;

    if (utc_epoch == 0) return 0; /* not known has no timezone */
    local = (long)utc_epoch + (long)tz_offset_min * 60;
    if (local < 0) return 0;
    return (uint32_t)local;
}
