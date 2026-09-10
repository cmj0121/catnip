/*
 * net_time.h - the clock, from the network (#84).
 *
 * Split the way the RTC is (see rtc_time.h): the arithmetic and the wire format
 * have no chip and no radio in them and live here in plain C, host-tested; the
 * driver that opens a socket and writes the registers is net_time.cpp.
 *
 * What comes off the wire is SNTP - the small half of NTP, one request out and
 * one reply back, no discipline loop - which is all a device setting a clock
 * from cold needs. What it hands back is UTC. What the RTC holds is local wall
 * time, because that is what the owner set and what the face shows, so the
 * offset is added here on the way from one to the other. A clock that stored
 * UTC and converted on every read would be tidier in the abstract and wrong
 * here: it would change the meaning of registers that already have one.
 */
#ifndef CATNIP_NET_TIME_H
#define CATNIP_NET_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An SNTP request packet: 48 bytes, all zero but the first, which says version
 * 4 and mode 3 (client). Fills `out[0..48)`. Here rather than in the driver
 * because it is a constant of the protocol, not of the socket. */
#define CATNIP_NTP_PACKET_LEN 48
void catnip_ntp_request(uint8_t out[CATNIP_NTP_PACKET_LEN]);

/* The UTC epoch carried by a 48-byte SNTP reply, or 0 for one that cannot be
 * believed.
 *
 * The time is the transmit timestamp, the server's own idea of now, at offset
 * 40: four bytes of seconds since 1900, which this turns into seconds since
 * 1970 by subtracting the 70 years between the two epochs. Zero is returned for
 * a reply too short, for a leap-indicator of 3 (the server says its own clock
 * is unsynchronised), for a stratum of 0 (a "kiss-o'-death", not a time), and
 * for a timestamp of zero - each of which is the wire saying "do not use this",
 * and every one is the same fact to a caller: this sync did not happen, and the
 * RTC must not move. */
uint32_t catnip_ntp_parse(const uint8_t *reply, int len);

/* UTC plus the offset, as the local epoch to write to the RTC. Separate and
 * tiny so the one place that turns "what the server said" into "what the clock
 * shows" is named and testable, rather than an inline `+ off * 60` that the
 * next reader has to recognise. A zero UTC stays zero: "not known" is not a
 * time and adding a timezone to it would invent one. */
uint32_t catnip_ntp_to_local(uint32_t utc_epoch, int tz_offset_min);

/* ---- the driver: the socket and the registers -------------------------- */
/* Implemented in net_time.cpp against the radio and the RTC. The host build
 * links only net_time_wire.c above, so these are declared but never called
 * there. */

typedef enum {
    CATNIP_SYNC_IDLE = 0, /* nothing in flight */
    CATNIP_SYNC_JOINING,  /* waiting for the radio to reach the network */
    CATNIP_SYNC_QUERYING, /* on the network, waiting for the server to answer */
    CATNIP_SYNC_DONE,     /* the RTC was written from a reply */
    CATNIP_SYNC_FAILED,   /* no network, no reply, or a reply not to be trusted */
} catnip_sync_state;

/* Begin a sync: bring the radio up on the card's network and, once it is there,
 * ask an NTP server for the time and write it to the RTC as local wall time
 * using `tz_offset_min`. Non-blocking - it starts the work and returns, and
 * catnip_net_time_poll() carries it forward. `ssid`/`psk` are the network; an
 * empty ssid fails at once, because a sync with no network to reach cannot
 * happen and saying so is better than a timeout. */
void catnip_net_time_sync(const char *ssid, const char *psk, int tz_offset_min);

/* Carry the sync forward and report where it is. Writes the RTC exactly once,
 * on the pass it reaches DONE. When the sync ends - DONE or FAILED - it drops
 * the radio, because nothing else on this device wanted it up. */
catnip_sync_state catnip_net_time_poll(void);

/* Whether a sync is in flight, for the momentary icon the bar shows while it
 * is - JOINING or QUERYING, and nothing once it has settled. */
bool catnip_net_time_busy(void);

/* The local epoch of the last successful sync, or 0 for "never synced" - the
 * honest answer to the device page's "has this actually worked", which is a
 * different question from "what time is it". */
uint32_t catnip_net_time_last(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_NET_TIME_H */
