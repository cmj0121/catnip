/*
 * wifi80211.h - IEEE 802.11 management-frame build and parse, pure C.
 *
 * Kept apart from the radio and free of esp_* / Arduino.h for the same reason
 * input_debounce.h is: the byte layout is the whole of the logic, so it is
 * lifted out where a host test can assemble a beacon or a deauth and read the
 * exact bytes back, with no radio attached. The driver that actually transmits
 * is a thin call over the bytes these functions return.
 *
 * All multi-byte header fields are little-endian, per the standard.
 */
#ifndef CATNIP_WIFI80211_H
#define CATNIP_WIFI80211_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI80211_ADDR_LEN       6
#define WIFI80211_SUBTYPE_BEACON 0x08
#define WIFI80211_SUBTYPE_DEAUTH 0x0C
#define WIFI80211_BEACON_MAX     128
#define WIFI80211_DEAUTH_LEN     26
typedef enum {
    WIFI80211_TYPE_MGMT = 0,
    WIFI80211_TYPE_CTRL = 1,
    WIFI80211_TYPE_DATA = 2
} wifi80211_type;
typedef struct {
    int ok;                            /* 0 if buffer too short for the frame */
    uint8_t type;                      /* 2-bit frame type */
    uint8_t subtype;                   /* 4-bit subtype */
    uint8_t addr1[WIFI80211_ADDR_LEN]; /* receiver/dest */
    uint8_t addr2[WIFI80211_ADDR_LEN]; /* transmitter/src */
    uint8_t addr3[WIFI80211_ADDR_LEN]; /* bssid; read from the header for the
                                            mgmt frames this module handles, and
                                            left zeroed only when parse fails */
} wifi80211_view;
int wifi80211_parse(const uint8_t *buf, size_t len, wifi80211_view *out);

/* Returns the frame length written. `out` must point to at least
 * WIFI80211_BEACON_MAX bytes: the builder bounds the frame only through the
 * ssid_len <= 32 check and does not otherwise guard the destination. Returns 0
 * (writing nothing) on a bad argument or an SSID over 32 bytes. */
size_t wifi80211_build_beacon(const uint8_t bssid[WIFI80211_ADDR_LEN], const char *ssid,
                              size_t ssid_len, uint16_t interval_tu, uint8_t channel,
                              uint8_t *out);

/* Returns the frame length written (always WIFI80211_DEAUTH_LEN on success).
 * `out` must point to at least WIFI80211_DEAUTH_LEN bytes; the builder does not
 * bound-check the destination. Returns 0 (writing nothing) on a bad argument. */
size_t wifi80211_build_deauth(const uint8_t bssid[WIFI80211_ADDR_LEN],
                              const uint8_t station[WIFI80211_ADDR_LEN], uint16_t reason,
                              uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_WIFI80211_H */
