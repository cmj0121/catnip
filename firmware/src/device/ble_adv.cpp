/* ble_adv.cpp - see ble_adv.h. */
#include "ble_adv.h"

#include "ble_stack.h"
#include "wifi.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>

namespace {

/* The whole of an advertisement is 31 bytes. A payload longer than that is one
 * the app built wrong - ble_gap_adv_set_data would reject it - so it is turned
 * away here rather than passed down to fail as a return code nothing reads. */
const size_t kMaxPayload = 31;

/* The controller counts advertising intervals in 0.625 ms steps. A caller asks
 * in milliseconds because that is the unit a person sets a beacon in; this is
 * where the two meet. */
uint16_t ms_to_units(uint32_t ms)
{
    uint32_t units = (ms * 8) / 5;      /* ms / 0.625 */
    if (units < 0x20) units = 0x20;     /* the spec's floor, 20 ms */
    if (units > 0x4000) units = 0x4000; /* and ceiling, ~10.24 s */
    return (uint16_t)units;
}

bool g_up;

} /* namespace */

bool catnip_ble_adv_begin(const uint8_t *payload, size_t len, uint32_t interval_ms)
{
    if (!payload || len == 0 || len > kMaxPayload) return false;

    /* A re-arm is not a second user of the stack. begin() doubles as the app's
     * "change the id" call, so the stack is acquired and the Wi-Fi held on the
     * first begin only - acquiring again on every change would run the refcount
     * up past the one end() that will ever release it, and the stack would never
     * come down. */
    bool was_up = g_up;
    if (!was_up && !catnip_ble_stack_acquire()) return false;

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    /* Re-arm from a clean slate. begin() doubles as the app's "change the id"
     * call, so a second one lands on an advertising object the first left
     * configured - and reset() is what stops the old payload's fields from
     * surviving under the new raw data. It also stops any advertising in
     * flight, which is what makes re-arming a replacement rather than a race. */
    adv->reset();

    /* Non-connectable and non-scannable: ADV_NONCONN_IND. A beacon has no
     * server to connect to, and turning the scan response off is what keeps the
     * air quiet and leaves all 31 bytes to the payload. */
    adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    adv->setScanResponse(false);

    uint16_t units = ms_to_units(interval_ms);
    adv->setMinInterval(units);
    adv->setMaxInterval(units);

    /* The payload verbatim. setAdvertisementData hands these bytes straight to
     * the controller and marks the data custom, so start() adds nothing of its
     * own - no flags, no name - which is the whole point of assembling the AD
     * structures in the app: what goes on the air is exactly what was built.
     * addData copies into the object's own storage, so `payload` need not
     * outlive this call. */
    NimBLEAdvertisementData data;
    data.addData(std::string(reinterpret_cast<const char *>(payload), len));
    adv->setAdvertisementData(data);

    if (!adv->start()) {
        /* Only give back what this call took. A failed re-arm leaves the beacon
         * that was already up as it was - the stack and the Wi-Fi hold are the
         * first begin's to release, not this one's. */
        if (!was_up) catnip_ble_stack_release();
        return false;
    }

    /* The promise from the header: while this is a beacon, the front end is this
     * radio's. Held on the first begin only, alongside the acquire. */
    if (!was_up) catnip_wifi_hold(true);
    g_up = true;

    Serial.printf("[catnip] ble-adv: advertising %u bytes every %u ms\n", (unsigned)len,
                  (unsigned)interval_ms);
    return true;
}

void catnip_ble_adv_end(void)
{
    if (!g_up) return;
    g_up = false;

    NimBLEDevice::stopAdvertising();

    catnip_ble_stack_release();
    catnip_wifi_hold(false);
    Serial.println("[catnip] ble-adv: beacon down");
}

bool catnip_ble_adv_up(void)
{
    return g_up;
}
