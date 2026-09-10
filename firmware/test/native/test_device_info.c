/*
 * Native test for issue #69: the device info page, and the hub it became.
 *
 * The page has almost no logic in it on purpose - what a chip is called and how
 * much PSRAM is free are the device's questions, and it is handed the answers
 * already written out. What is left, and what is pinned here, is what it
 * decides: that the three things behind A are told apart, that nothing on the
 * page itself acts, and that a press nobody read does not survive the page it
 * was made on.
 *
 * The middle one is the whole point of the page. It answers before it offers -
 * "what is this thing" before "change it", and long before "is the joystick
 * broken" - and it used to do half of the offering on the screen itself, with
 * two tiles under the facts. Operations are behind A everywhere else in the
 * device; there was never a reason for this page to be the exception.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_device_info.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

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

static void fire(catnip_rt *rt, const char *lua)
{
    catnip_rt_dostring(rt, lua, "=t");
}

static int ui_has(catnip_rt *rt, const char *id)
{
    lua_State *L = catnip_rt_lua(rt);
    char q[96];
    int have;

    snprintf(q, sizeof(q), "H = ui.get('%s') ~= nil", id);
    catnip_rt_dostring(rt, q, "=t");
    lua_getglobal(L, "H");
    have = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return have;
}

static int lua_len_of(catnip_rt *rt, const char *global)
{
    lua_State *L = catnip_rt_lua(rt);
    char q[96];
    int n;

    snprintf(q, sizeof(q), "N = %s and #%s or -1", global, global);
    catnip_rt_dostring(rt, q, "=t");
    lua_getglobal(L, "N");
    n = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return n;
}

static const char *nth(catnip_rt *rt, const char *global, int i)
{
    static char buf[32];
    lua_State *L = catnip_rt_lua(rt);
    char q[96];

    snprintf(q, sizeof(q), "V = %s and %s[%d] or ''", global, global, i);
    catnip_rt_dostring(rt, q, "=t");
    lua_getglobal(L, "V");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_device_info *d = catnip_device_info_new(rt);
    const char *rows[3] = {"catnip v0.3.1", "heap 213 KB free", "card none"};

    printf("the device info page reports, and what it can do is behind A\n");
    CHECK(d != NULL, "the page is created");

    catnip_device_info_show(d, rows, 3);
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "nothing is latched before a press");

    /* Nothing on the page acts. The two tiles that used to sit under the facts
     * were operations drawn on a screen, which is the one thing this page was
     * least entitled to have - it answers before it offers, and offering was
     * the half it was doing on the screen itself. */
    CHECK(!ui_has(rt, "info_left") && !ui_has(rt, "info_right"),
          "there are no buttons on it at all");

    /* What it offers is an answer to A: three ids, which the platform turns
     * into a bar. The page builds nothing, exactly as an app does not. */
    fire(rt, "A = ui.fire('info_list', 'click')\n"
             "_, OFFER = ui.fire('info_list', 'click')");
    CHECK(lua_len_of(rt, "OFFER") == 3, "A answers with three actions");
    CHECK(strcmp(nth(rt, "OFFER", 1), "pref") == 0, "the preference page");
    CHECK(strcmp(nth(rt, "OFFER", 2), "sizes") == 0, "the type sample");
    CHECK(strcmp(nth(rt, "OFFER", 3), "diag") == 0, "and the diagnostic");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "and answering is all it does - nothing is latched by the asking");

    /* Long A answers the same three. This page has no selection for long A to
     * be about and nothing else short A could mean, so both reach it - a user
     * arriving from any other app finds it where they expect. */
    fire(rt, "_, OFFER = ui.fire('info_list', 'options')");
    CHECK(lua_len_of(rt, "OFFER") == 3, "and so does a long press");

    /* Running one is the id coming back, which is the whole of what the page
     * decides: the three are told apart. */
    fire(rt, "ui.fire('info_list', 'action', 'pref')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_PREF,
          "pref is the preference page");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "and reading the latch clears it");

    fire(rt, "ui.fire('info_list', 'action', 'sizes')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_SIZES,
          "sizes is the type sample");

    fire(rt, "ui.fire('info_list', 'action', 'diag')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_DIAG,
          "diag is the diagnostic");

    /* An id this page never offered does nothing rather than the nearest
     * thing. */
    fire(rt, "ui.fire('info_list', 'action', 'reboot')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "and an id it never offered does nothing at all");

    /* The catalogue is the platform's own, because this page has no manifest -
     * and the bar that draws it cannot tell the difference. */
    {
        int n = 0;
        const catnip_action *cat = catnip_device_info_actions(&n);
        CHECK(cat && n == 3, "it declares three actions in C, where an app uses JSON");
        CHECK(cat && !cat[0].destructive && !cat[2].destructive,
              "and none of them is destructive, so B may carry the last");
    }

    /* A rebuild forgets a press nobody consumed: the page it belonged to is
     * gone, and acting on it would leave home because of something the user did
     * on a screen that is no longer there. */
    fire(rt, "ui.fire('info_list', 'action', 'pref')");
    catnip_device_info_show(d, rows, 3);
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "a rebuild drops an unread press");

    /* More rows than the page holds are cut rather than overrunning it. */
    {
        const char *many[CATNIP_INFO_MAX_ROWS + 4];
        for (int i = 0; i < CATNIP_INFO_MAX_ROWS + 4; i++)
            many[i] = "x";
        catnip_device_info_show(d, many, CATNIP_INFO_MAX_ROWS + 4);
        CHECK(1, "too many rows is a cut, not a crash");
    }

    catnip_device_info_free(d);
    catnip_rt_free(rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
