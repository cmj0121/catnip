/* ble_stack.cpp - see ble_stack.h. */
#include "ble_stack.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {

/* The name the device answers to when it is a peripheral.
 *
 * A central that is only listening never transmits one, so the Scanner pays
 * nothing for this; it is here because init() is where NimBLE takes the name
 * and the stack is shared, and a peripheral that comes up later should not have
 * to restart the stack to be called something. Air Mouse narrows it to the
 * specific thing it is advertising as - see ble_hid.cpp. */
const char *kName = "MeowKit";

/* How many surfaces are holding the stack up. Not a bool: the whole point is
 * that the second holder stops the first one's release from being a teardown. */
int g_users;

} /* namespace */

bool catnip_ble_stack_acquire(void)
{
    if (g_users == 0) {
        NimBLEDevice::init(kName);
        /* init() returns void and swallows its own failure, so the only honest
         * check is to ask afterwards. A stack that did not come up must not be
         * counted, or the release that follows would decrement a user that
         * never existed. */
        if (!NimBLEDevice::getInitialized()) {
            Serial.println("[catnip] ble: stack would not start");
            return false;
        }
        Serial.println("[catnip] ble: stack up");
    }
    g_users++;
    return true;
}

void catnip_ble_stack_release(void)
{
    /* A release with nothing held is a bug in the caller, not something to
     * correct by taking the stack down - that would be tearing down a radio on
     * behalf of somebody who was not using it. */
    if (g_users == 0) return;
    if (--g_users > 0) return;

    /* clearAll: the services, characteristics and the scan object all belong to
     * the stack that is going away, and a surface that comes back gets new ones
     * from the new stack rather than a pointer into the old one. */
    NimBLEDevice::deinit(true);
    Serial.println("[catnip] ble: stack down");
}

bool catnip_ble_stack_is_up(void)
{
    return g_users > 0;
}
