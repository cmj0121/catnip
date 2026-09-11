/* ble_hid.cpp - see ble_hid.h. */
#include "ble_hid.h"

#include "ble_stack.h"
#include "wifi.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>

namespace {

/* The report descriptor for a plain three-button wheel mouse.
 *
 * This is the shape every host already has a driver for, which is the entire
 * point of choosing it: a descriptor nobody has seen before is one that some
 * operating system decides to interpret its own way. Three buttons as single
 * bits, five bits of padding to finish the byte, then X, Y and wheel as signed
 * bytes - four bytes of report in total.
 *
 * The values are the HID usage tables' own numbers rather than names because at
 * this level there are no names for them; the comment on each line is the name.
 * Read it as a list of declarations, not as code. */
const uint8_t kReportMap[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop)          */
    0x09, 0x02, /* Usage (Mouse)                         */
    0xA1, 0x01, /* Collection (Application)              */
    0x85, 0x01, /*   Report ID (1)                       */
    0x09, 0x01, /*   Usage (Pointer)                     */
    0xA1, 0x00, /*   Collection (Physical)               */
    0x05, 0x09, /*     Usage Page (Button)               */
    0x19, 0x01, /*     Usage Minimum (button 1)          */
    0x29, 0x03, /*     Usage Maximum (button 3)          */
    0x15, 0x00, /*     Logical Minimum (0)               */
    0x25, 0x01, /*     Logical Maximum (1)               */
    0x75, 0x01, /*     Report Size (1 bit)               */
    0x95, 0x03, /*     Report Count (3)                  */
    0x81, 0x02, /*     Input (data, variable, absolute)  */
    0x75, 0x05, /*     Report Size (5 bits)              */
    0x95, 0x01, /*     Report Count (1)                  */
    0x81, 0x03, /*     Input (constant) - byte padding   */
    0x05, 0x01, /*     Usage Page (Generic Desktop)      */
    0x09, 0x30, /*     Usage (X)                         */
    0x09, 0x31, /*     Usage (Y)                         */
    0x09, 0x38, /*     Usage (Wheel)                     */
    0x15, 0x81, /*     Logical Minimum (-127)            */
    0x25, 0x7F, /*     Logical Maximum (127)             */
    0x75, 0x08, /*     Report Size (8 bits)              */
    0x95, 0x03, /*     Report Count (3)                  */
    0x81, 0x06, /*     Input (data, variable, relative)  */
    0xC0,       /*   End Collection                      */
    0xC0,       /* End Collection                        */
};

/* The report id declared above. One report, so one id, but it is named because
 * inputReport() takes it and a bare 1 there says nothing. */
const uint8_t kReportId = 1;

/* What the descriptor says a step may be. Named for the clamp, so the range and
 * the 0x81/0x7F in the descriptor cannot drift apart silently. */
const int kStepMax = 127;

/* The name this advertises under. Narrower than the stack's "MeowKit": what a
 * host is about to list in its Bluetooth menu should say what it will be, and a
 * user picking a device out of that list is choosing a mouse. */
const char *kMouseName = "MeowKit Mouse";

bool g_up;
/* Written from the NimBLE host task and read from the app loop, so volatile:
 * these are the two sides of the only piece of state that crosses. */
volatile bool g_connected;

NimBLEServer *g_server;
NimBLEHIDDevice *g_hid;
NimBLECharacteristic *g_input;

int clamp_step(int v)
{
    if (v < -kStepMax) return -kStepMax;
    if (v > kStepMax) return kStepMax;
    return v;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *) override
    {
        g_connected = true;
        Serial.println("[catnip] ble-hid: a host connected");
    }

    void onDisconnect(NimBLEServer *) override
    {
        g_connected = false;
        Serial.println("[catnip] ble-hid: the host went away");
        /* Offer ourselves again at once. A mouse that had to be re-opened after
         * the host slept would look broken to somebody who only closed a lid -
         * and the app above has no way to tell that case from any other, so it
         * cannot be the one to decide to re-advertise. Only while the surface
         * is still meant to be up: after end(), this is a teardown, not a
         * disconnection to recover from. */
        if (g_up) NimBLEDevice::startAdvertising();
    }
};

ServerCallbacks g_callbacks;

} /* namespace */

bool catnip_ble_hid_begin(void)
{
    if (g_up) return true;
    if (!catnip_ble_stack_acquire()) return false;

    /* Just Works, and bonded. There is no keypad on the host side of this
     * conversation to type a passkey into, so asking for man-in-the-middle
     * protection would mean asking for a thing neither end can perform. Secure
     * connections (the third argument) is the LE Secure Connections pairing
     * this stack does support, and costs nothing to require. */
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setDeviceName(kMouseName);

    g_server = NimBLEDevice::createServer();
    if (!g_server) {
        catnip_ble_stack_release();
        return false;
    }
    /* `false`: we own g_callbacks, NimBLE does not.
     *
     * setCallbacks takes ownership by default, and ~NimBLEServer() deletes what
     * it was given - so the default would have the stack calling delete on a
     * static object the next time it is torn down, which is heap corruption
     * rather than a leak. The object is static precisely because it has no
     * state worth allocating. */
    g_server->setCallbacks(&g_callbacks, false);

    g_hid = new NimBLEHIDDevice(g_server);
    g_input = g_hid->inputReport(kReportId);
    if (!g_input) {
        delete g_hid;
        g_hid = nullptr;
        g_server = nullptr;
        catnip_ble_stack_release();
        return false;
    }

    g_hid->manufacturer(std::string("catnip"));
    /* Vendor id source 0x02 is "assigned by the USB Implementer's Forum". The
     * numbers themselves are this firmware's own; nothing checks them, but a
     * host logs them, and a device that reported zeros would be one nobody
     * could identify in a log afterwards. */
    g_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
    /* Country 0 is "not localised", which a pointing device is. Flag 0x01 is
     * remote wake: a mouse is a reasonable thing to wake a sleeping host with. */
    g_hid->hidInfo(0x00, 0x01);
    g_hid->reportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
    g_hid->startServices();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    /* The appearance is what puts a mouse icon next to the name in the host's
     * pairing list, and the service uuid is what lets a host filter for input
     * devices before it has connected to anything. */
    adv->setAppearance(HID_MOUSE);
    adv->addServiceUUID(g_hid->hidService()->getUUID());
    adv->setScanResponse(true);

    /* Up before the advertising starts: onDisconnect checks it, and a callback
     * that arrived between start() and the assignment would decide this surface
     * was being torn down and decline to re-advertise. */
    g_up = true;
    g_connected = false;

    if (!adv->start()) {
        g_up = false;
        catnip_ble_hid_end();
        return false;
    }

    /* The promise from the header: while this is a mouse, the front end is
     * this radio's. Held rather than parked-with-a-timer, because the reason
     * has nothing to do with how recently anything was asked. */
    catnip_wifi_hold(true);

    Serial.printf("[catnip] ble-hid: advertising as '%s'\n", kMouseName);
    return true;
}

void catnip_ble_hid_end(void)
{
    if (!g_up && !g_server) return;

    /* Down first, so the disconnect below is understood as a teardown by the
     * callback rather than as a host that wandered off. */
    g_up = false;
    g_connected = false;

    NimBLEDevice::stopAdvertising();
    if (g_server) {
        /* Ask any host to let go before the services go away underneath it.
         * A peer disconnected by the stack vanishing gets no disconnect reason
         * and some of them wait out a timeout before believing it. */
        std::vector<uint16_t> peers = g_server->getPeerDevices();
        for (size_t i = 0; i < peers.size(); i++)
            g_server->disconnect(peers[i]);
    }

    /* The services belong to the server, which the stack's clearAll deletes;
     * the NimBLEHIDDevice wrapper around them is ours and nothing else will
     * free it. Its destructor is empty - it does not touch the services - so
     * deleting it here is a leak fixed rather than a double free created, and
     * this driver is begun and ended every time the app is opened. */
    delete g_hid;
    g_hid = nullptr;
    g_input = nullptr;
    g_server = nullptr;

    catnip_ble_stack_release();
    catnip_wifi_hold(false);
    Serial.println("[catnip] ble-hid: mouse down");
}

bool catnip_ble_hid_up(void)
{
    return g_up;
}

bool catnip_ble_hid_connected(void)
{
    return g_up && g_connected;
}

void catnip_ble_hid_move(int dx, int dy, int buttons, int wheel)
{
    /* Nothing connected is not an error: an app driving a cursor should not
     * have to ask permission before every step, and a report sent into a
     * disconnection has simply gone nowhere. */
    if (!g_up || !g_connected || !g_input) return;

    uint8_t report[4];
    report[0] = (uint8_t)(buttons &
                          (CATNIP_MOUSE_LEFT | CATNIP_MOUSE_RIGHT | CATNIP_MOUSE_MIDDLE));
    report[1] = (uint8_t)(int8_t)clamp_step(dx);
    report[2] = (uint8_t)(int8_t)clamp_step(dy);
    report[3] = (uint8_t)(int8_t)clamp_step(wheel);

    g_input->setValue(report, sizeof(report));
    g_input->notify();
}
