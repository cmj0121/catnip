/* Native test for Batch 1's HID arm gate: service.usb.* over a mock HAL.
 *
 * The gate is the whole safety of the composite keyboard - the interface is
 * always present, and what keeps it from typing is that hid_tap sends nothing
 * unless HID is enabled. That refusal lives in the shared layer (catnip_api.c)
 * as well as in the device driver, precisely so a host test can hold it in
 * place: the mock below records every key the API actually forwards, and the
 * assertions are that a disabled keyboard forwards none. */
#include <stdio.h>
#include <string.h>

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lua.h"

typedef struct {
    int armed;
    int keys; /* how many taps reached the HAL */
    unsigned char last_mods, last_usage;
    int flashed; /* how many times flash_mode reached the HAL */
} usb_mock;

static void m_usb_enable(void *ud, int on)
{
    ((usb_mock *)ud)->armed = on ? 1 : 0;
}
static int m_usb_enabled(void *ud)
{
    return ((usb_mock *)ud)->armed;
}
static void m_usb_key(void *ud, unsigned char mods, unsigned char usage)
{
    usb_mock *m = ud;
    m->keys++;
    m->last_mods = mods;
    m->last_usage = usage;
}
/* On the device this reboots and never returns; the mock just records that the
 * API forwarded the call, so the test can hold that wiring in place. */
static void m_usb_flash(void *ud)
{
    ((usb_mock *)ud)->flashed++;
}

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

static int run(catnip_rt *rt, const char *lua)
{
    return catnip_rt_dostring(rt, lua, "=usb");
}

static int boolean(lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    int v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

int main(void)
{
    usb_mock mk;
    memset(&mk, 0, sizeof(mk));
    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.ud = &mk;
    hal.usb_hid_enable = m_usb_enable;
    hal.usb_hid_enabled = m_usb_enabled;
    hal.usb_hid_key = m_usb_key;
    hal.usb_flash_mode = m_usb_flash;

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    CHECK(catnip_api_open(rt, &hal) == 0, "api installs with the usb surface");
    lua_State *L = catnip_rt_lua(rt);

    /* Disabled at boot. */
    CHECK(run(rt, "E = service.usb.hid_enabled()") == 0 && boolean(L, "E") == 0,
          "HID is disabled by default");

    /* A tap while disabled sends nothing - the keyboard is present but inert. */
    CHECK(run(rt, "S = service.usb.hid_tap(0x04, 0)") == 0 && boolean(L, "S") == 0 &&
              mk.keys == 0,
          "a tap while disabled is refused and reaches no key");

    /* Enable, and now a tap goes through carrying its usage and modifiers. */
    CHECK(run(rt, "service.usb.hid_enable(true); E = service.usb.hid_enabled()") == 0 &&
              boolean(L, "E") == 1 && mk.armed == 1,
          "hid_enable(true) arms it, and the HAL was told");
    CHECK(run(rt, "S = service.usb.hid_tap(0x04, 0x02)") == 0 && boolean(L, "S") == 1 &&
              mk.keys == 1 && mk.last_usage == 0x04 && mk.last_mods == 0x02,
          "an armed tap carries usage and modifiers to the HAL");

    /* Disable again, and the gate closes: no further key is sent. */
    CHECK(run(rt, "service.usb.hid_enable(false); S = service.usb.hid_tap(0x28, 0)") ==
                  0 &&
              boolean(L, "S") == 0 && mk.keys == 1,
          "disabling it again makes taps inert, with no key sent");

    /* The parser is reachable from Lua and returns the event list the BadUSB app
     * walks. The keymap itself is proven in test_ducky.c; here it is only that
     * the binding marshals usage/mods for a keystroke and delay for a pause. */
    CHECK(run(rt, "local e = service.usb.ducky_parse('STRING Hi\\nDELAY 5\\n')\n"
                  "N = #e\n"
                  "U1 = e[1].usage\n"
                  "M1 = e[1].mods\n"
                  "D3 = e[3].delay\n") == 0,
          "ducky_parse runs from Lua");
    {
        lua_getglobal(L, "N");
        lua_getglobal(L, "U1");
        lua_getglobal(L, "M1");
        lua_getglobal(L, "D3");
        /* 'H' (shift, 0x0B), 'i' (0x0C), then DELAY 5. */
        CHECK(lua_tointeger(L, -4) == 3 && lua_tointeger(L, -3) == 0x0B &&
                  lua_tointeger(L, -2) == 0x02 && lua_tointeger(L, -1) == 5,
              "and marshals key events and a delay into a Lua array");
        lua_pop(L, 4);
    }

    /* flash_mode forwards to the HAL. It "returns" false here only because the
     * mock returns at all - on the device it reboots and nothing after it runs -
     * but the wiring under test is that the call reached the hook. */
    CHECK(run(rt, "R = service.usb.flash_mode()") == 0 && boolean(L, "R") == 0 &&
              mk.flashed == 1,
          "flash_mode reaches the HAL and reports false to Lua");

    /* With no such hook - the host default, and every non-composite build - the
     * call is a safe no-op and stays false, which is how the app greys it. */
    hal.usb_flash_mode = NULL;
    CHECK(run(rt, "R = service.usb.flash_mode()") == 0 && boolean(L, "R") == 0 &&
              mk.flashed == 1,
          "flash_mode with no hook is a safe no-op and forwards nothing");

    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
