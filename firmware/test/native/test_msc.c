/* Native test for Batch 2's Mass Storage: the two host-reachable decisions and
 * the service.usb.msc_* surface over a mock HAL.
 *
 * The raw SD sectors and the USB stack are device-only and cannot be reached
 * here, but the two things that keep the surface honest can (msc_core.h): when
 * Mass Storage may be entered, and whether a host's block request lands inside
 * the card. Both are asserted directly, and then again through Lua over a mock
 * HAL that mimics the driver's own rule - the card is handed over only when one
 * is present - the way test_usb.c holds the keyboard's arm gate in place. */
#include <stdio.h>
#include <string.h>

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lua.h"
#include "msc_core.h"

/* The mock card owner: whether a card is present, and whether the host has it.
 * m_msc_enable mimics the driver - enabling takes only when a card is present,
 * disabling always returns the card - so the Lua gate is tested against the same
 * behaviour the device has. */
typedef struct {
    int has_card;
    int active;
} msc_mock;

static void m_msc_enable(void *ud, int on)
{
    msc_mock *m = ud;
    if (on) {
        if (m->has_card) m->active = 1;
    } else {
        m->active = 0;
    }
}
static int m_msc_active(void *ud)
{
    return ((msc_mock *)ud)->active;
}
static int m_has_card(void *ud)
{
    return ((msc_mock *)ud)->has_card;
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
    return catnip_rt_dostring(rt, lua, "=msc");
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
    /* ---- the core, on its own ---------------------------------------------- */

    CHECK(catnip_msc_can_enter(true, false), "may enter with a card and not active");
    CHECK(!catnip_msc_can_enter(false, false), "no card: may not enter");
    CHECK(!catnip_msc_can_enter(true, true), "already active: may not enter again");

    /* A 512-byte card, a million sectors. A whole-sector read inside it is a
     * range; anything ragged or off the end is refused. */
    {
        uint32_t lba, count;
        const uint32_t bs = 512, bc = 1000000;

        CHECK(catnip_msc_io_range(0, 0, 4096, bs, bc, &lba, &count) && lba == 0 &&
                  count == 8,
              "a whole-sector read maps to a sector range");
        CHECK(catnip_msc_io_range(100, 0, 512, bs, bc, &lba, &count) && lba == 100 &&
                  count == 1,
              "a single-sector read at an offset lba");
        CHECK(catnip_msc_io_range(10, 1024, 512, bs, bc, &lba, &count) && lba == 12 &&
                  count == 1,
              "a byte offset advances the start sector");
        CHECK(!catnip_msc_io_range(0, 0, 500, bs, bc, &lba, &count),
              "a partial-sector length is refused");
        CHECK(!catnip_msc_io_range(0, 0, 0, bs, bc, &lba, &count),
              "a zero-length transfer is refused");
        CHECK(!catnip_msc_io_range(bc - 1, 0, 4096, bs, bc, &lba, &count),
              "a read running off the end of the card is refused");
        CHECK(catnip_msc_io_range(bc - 1, 0, 512, bs, bc, &lba, &count) &&
                  lba == bc - 1 && count == 1,
              "the very last sector is still in range");
    }

    /* ---- the surface, over a mock HAL -------------------------------------- */

    msc_mock mk;
    memset(&mk, 0, sizeof(mk));
    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.ud = &mk;
    hal.usb_msc_enable = m_msc_enable;
    hal.usb_msc_active = m_msc_active;
    hal.usb_has_card = m_has_card;

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    CHECK(catnip_api_open(rt, &hal) == 0, "api installs with the msc surface");
    lua_State *L = catnip_rt_lua(rt);

    /* Not active at boot, and no card in this mock yet. */
    CHECK(run(rt, "A = service.usb.msc_active()") == 0 && boolean(L, "A") == 0,
          "mass storage is inactive by default");
    CHECK(run(rt, "C = service.usb.has_card()") == 0 && boolean(L, "C") == 0,
          "has_card is false with no card");

    /* With no card, handing over is refused and nothing became active. */
    CHECK(run(rt, "R = service.usb.msc_enable(true)") == 0 && boolean(L, "R") == 0 &&
              mk.active == 0,
          "enabling with no card is refused");

    /* Insert a card: now has_card is true and the handover takes. */
    mk.has_card = 1;
    CHECK(run(rt, "C = service.usb.has_card()") == 0 && boolean(L, "C") == 1,
          "has_card is true once a card is present");
    CHECK(run(rt, "R = service.usb.msc_enable(true)") == 0 && boolean(L, "R") == 1 &&
              mk.active == 1,
          "enabling with a card hands it to the host");
    CHECK(run(rt, "A = service.usb.msc_active()") == 0 && boolean(L, "A") == 1,
          "and msc_active reports the host owns it");

    /* Enabling again while active is idempotent and still reads back active. */
    CHECK(run(rt, "R = service.usb.msc_enable(true)") == 0 && boolean(L, "R") == 1 &&
              mk.active == 1,
          "enabling again while active is idempotent");

    /* Take it back: inactive again. */
    CHECK(run(rt, "R = service.usb.msc_enable(false)") == 0 && boolean(L, "R") == 0 &&
              mk.active == 0,
          "disabling takes the card back");

    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
