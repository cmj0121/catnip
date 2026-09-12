/* catnip_menu.c - see catnip_menu.h. */
#include "catnip_menu.h"

#include "catnip_pins.h"
#include "device/rtc_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* The menu screen, built in Lua so it is the same kind of tree an app builds
 * and the renderer cannot tell the two apart. `__catnip_menu_build` takes the
 * app names and installs the screen; the list owns its selection the same way
 * the File Browser's does, moving it on prev/next, so the input layer that
 * drives an app drives the menu with no special case. Activating a row does not
 * launch here - it hands the one-based index to __catnip_menu_pick, a C bridge
 * that only latches it, because launching from inside this handler would tear
 * down the very tree the handler is running on. */
static const char CATNIP_MENU_LUA[] =
    "function __catnip_menu_build(names, ids, ready, gtext, gstyle, open)\n"
    /* Home is the cat, and the apps sit either side of it: one carousel, one
     * `selected`, stepped left and right. The mascot is the first position
     * rather than a screen of its own, so arriving at it and leaving it cost
     * the same one gesture as anything else on the plane. */
    "  local rows = { ui.label{ id = 'menu_home', text = '', icon = 'mascot' } }\n"
    /* Kept so the glance can be rewritten without rebuilding the tree - a clock
     * cell that only changed when the menu happened to be rebuilt would be
     * wrong for most of every minute. */
    "  __catnip_menu_faces = {}\n"
    "  for i, name in ipairs(names) do\n"
    /* `image` names the app, and the platform resolves it to that app's own
     * icon.png. An app that shipped none resolves to nothing and falls back to
     * the placeholder glyph, which is what that glyph exists for. */
    /* `disabled` is the platform's word for "here but not usable"; the backend
     * dims it and the handler below refuses to launch it. */
    /* A cell that shows a value is not a picture and not a word, so the cell is
     * the whole region and is laid out as a face - one centrepiece, and nothing
     * else. The date and the weekday used to sit on the line under it, and they
     * are gone: this is one position on a ring the user is stepping past to
     * find something else, and what it owes them at that speed is the one fact
     * they came for. The rest is worth reading, so it is on the app's own face
     * where there is room to read it. */
    /* The face a value cell draws in is the glance type's, not one face for
     * all of them: the clock's `14:32` is the `display` role - digits, a colon
     * and nothing else, the readout that owns the screen - but the battery's
     * `89%` cannot use it, because `display` has no `%` and the sign would
     * simply not appear. So a battery cell asks for a prose role, which carries
     * the whole of ASCII; `catnip_glance_style` in C decides which, per type,
     * and a canvas draws either at the size its role means. */
    "    if gtext[i] ~= '' then\n"
    "      local face = ui.list{ id = 'menu_app' .. i, layout = 'canvas',\n"
    "                            disabled = not ready[i] }\n"
    "      __catnip_menu_faces[i] =\n"
    "        ui.label{ id = 'menu_time' .. i, text = gtext[i], style = gstyle[i] }\n"
    "      face:set_children({ __catnip_menu_faces[i] })\n"
    "      rows[i + 1] = face\n"
    "    else\n"
    "      rows[i + 1] = ui.label{ id = 'menu_app' .. i, text = name,\n"
    "                              image = ids[i], icon = 'placeholder',\n"
    "                              disabled = not ready[i] }\n"
    "    end\n"
    "  end\n"
    "  local list\n"
    "  local sel = open\n"
    "  list = ui.list{ id = 'menu_list', layout = 'carousel',\n"
    /* A carousel is a ring, not a column: stepping past either end comes round
     * rather than stopping. A list clamps, and that difference is the shape
     * itself - a column has a top and a bottom you can see, and a ring does
     * not, so stopping dead in one reads as an end and in the other as a
     * device that stopped listening. */
    "    on_prev = function()\n"
    "      sel = sel - 1\n"
    "      if sel < 1 then sel = #rows end\n"
    "      list.selected = sel\n"
    "      __catnip_menu_focus(sel)\n"
    "    end,\n"
    "    on_next = function()\n"
    "      sel = sel + 1\n"
    "      if sel > #rows then sel = 1 end\n"
    "      list.selected = sel\n"
    "      __catnip_menu_focus(sel)\n"
    "    end,\n"
    /* A tap names the position it landed on, exactly as in any other list. */
    "    on_click = function(self, i)\n"
    "      if i then sel = i; list.selected = sel end\n"
    /* Position 1 is home; there is nothing to launch there. */
    "      if sel > 1 and ready[sel - 1] then __catnip_menu_pick(sel - 1) end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    "  ui.screen{ list }\n"
    "end\n"
    /* One cell, by its ring position, and one property write. The tree is not
     * rebuilt, which is what the renderer being retained is for; and it is one
     * cell rather than all of them, because a glance is per app now and two
     * cells on the ring can be showing two different things. */
    "function __catnip_menu_glance(i, text)\n"
    "  local f = __catnip_menu_faces[i]\n"
    "  if f then f.text = text end\n"
    "end\n";

/* Mirrors CATNIP_SHELL_MAX_APPS in catnip_shell.h, which is where the app list
 * this menu is fed from is capped. Kept local rather than pulling the whole
 * shell header in for one number; if the shell's cap moves, move this too. */
#define MENU_MAX_APPS 32

struct catnip_menu {
    catnip_rt *rt;
    char ids[MENU_MAX_APPS][64];
    char names[MENU_MAX_APPS][64]; /* kept so the header can name the focus */
    /* Which cells draw a value rather than a picture, what value each one draws
     * (the glance type off the manifest, "" for a picture cell), and which apps
     * could run if they were picked. All answered while the ring is being
     * built, in ring order, so the Lua tables below are a straight copy. */
    bool has_glance[MENU_MAX_APPS];
    char glance_type[MENU_MAX_APPS][16];
    bool ready[MENU_MAX_APPS];
    /* What each value cell was last told, so a pass that changes nothing costs
     * nothing. Per cell rather than one string, because two glance cells can be
     * showing two unrelated values - a clock and a charge - and a shared
     * last-shown would make each of them look changed on the other's cadence. */
    char shown[MENU_MAX_APPS][16];
    int n;
    int pick;  /* zero-based index of a latched pick, or -1 for none */
    int focus; /* one-based carousel position; 1 is home */
    /* Which app the next rebuild opens on, by id rather than by position: a
     * card can be taken out between leaving an app and coming back, and an
     * index into a list that has changed underneath it names a different app.
     * "" is home. */
    char resume[64];
};

/* ---- glances ------------------------------------------------------------ */

bool catnip_glance_text(const char *type, uint32_t epoch, int battery, char *out,
                        size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!type) return false;

    if (strcmp(type, "time") == 0) {
        /* The hour and the minute, and nothing else: the ring's cell shows the
         * time alone, and a zero epoch is the clock not knowing rather than a
         * confident midnight. */
        if (!epoch) {
            snprintf(out, cap, "--:--");
        } else {
            uint32_t h, mi;
            catnip_rtc_split(epoch, NULL, NULL, NULL, &h, &mi, NULL);
            snprintf(out, cap, "%02u:%02u", (unsigned)h, (unsigned)mi);
        }
        return true;
    }
    if (strcmp(type, "battery") == 0) {
        /* -1 is "cannot be read", drawn as unknown rather than as flat - the
         * same distinction device.battery() and the header's gauge both make. */
        if (battery < 0) snprintf(out, cap, "--%%");
        else snprintf(out, cap, "%d%%", battery);
        return true;
    }
    return false;
}

/* Whether `type` is a glance this firmware knows how to fill. */
static bool glance_known(const char *type)
{
    char scratch[16];
    return catnip_glance_text(type, 0, -1, scratch, sizeof(scratch));
}

/* The render style role a glance of `type` draws in. `display` for a readout
 * that is digits and a colon, the way the clock's face uses it; a prose role
 * for anything carrying a sign, because `display` has no `%` and one asked of
 * it would not appear. `body` is the size the Battery app draws its own percent
 * at, so the cell and the app agree. */
static const char *glance_style(const char *type)
{
    if (type && strcmp(type, "battery") == 0) return "body";
    return "display";
}

/* The bridge the menu's on_click calls. It only latches the one-based index the
 * row carries; the map from index to id, and the launch itself, are C's. */
static int menu_pick_cb(lua_State *L)
{
    catnip_menu *m = (catnip_menu *)lua_touserdata(L, lua_upvalueindex(1));
    int idx = (int)luaL_checkinteger(L, 1);

    if (m && idx >= 1 && idx <= m->n) {
        m->pick = idx - 1;
        /* Remembered now, because by the time the launch happens this menu's
         * tree is gone and nothing else knows where the user was. */
        snprintf(m->resume, sizeof(m->resume), "%s", m->ids[idx - 1]);
    }
    return 0;
}

/* Which position the carousel is showing, one-based, so the frame's header can
 * name it. It is latched rather than asked for, because the answer lives in the
 * Lua closure that moves the selection and nothing in C is in a position to
 * read it. */
static int menu_focus_cb(lua_State *L)
{
    catnip_menu *m = (catnip_menu *)lua_touserdata(L, lua_upvalueindex(1));
    int i = (int)luaL_checkinteger(L, 1);
    if (m) m->focus = i;
    return 0;
}

catnip_menu *catnip_menu_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_menu *m;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;

    m = (catnip_menu *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->rt = rt;
    m->pick = -1;
    m->focus = 1;

    if (luaL_dostring(L, CATNIP_MENU_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(m);
        return NULL;
    }

    /* The bridge carries the menu pointer as an upvalue rather than reaching a
     * file static, so a second menu on a second runtime would not collide. */
    lua_pushlightuserdata(L, m);
    lua_pushcclosure(L, menu_pick_cb, 1);
    lua_setglobal(L, "__catnip_menu_pick");
    lua_pushlightuserdata(L, m);
    lua_pushcclosure(L, menu_focus_cb, 1);
    lua_setglobal(L, "__catnip_menu_focus");
    return m;
}

void catnip_menu_show(catnip_menu *m, const catnip_app_entry *apps, int n, bool fs_ready,
                      const char *unpinned)
{
    lua_State *L;
    int i;
    /* The apps to show on the ring: every one whose id is not in the unpinned
     * set. Filtered into a local list first, so everything below - the ordering,
     * the id and name arrays, the focus - works over the pinned apps and knows
     * nothing about the ones left in the grid. */
    const catnip_app_entry *pinned[MENU_MAX_APPS];
    int np = 0;

    if (!m) return;
    L = catnip_rt_lua(m->rt);
    if (!L) return;

    if (n < 0) n = 0;
    if (n > MENU_MAX_APPS) n = MENU_MAX_APPS;
    for (i = 0; i < n; i++)
        if (!catnip_pins_contains(unpinned ? unpinned : "", apps[i].id))
            pinned[np++] = &apps[i];
    apps = NULL; /* everything below uses the filtered list; guard against a slip */
    n = np;
    m->n = n;
    m->pick = -1;
    m->focus = 1;

    /* Apps that show a value go last, which on a ring is one step *left* of the
     * cat - the shortest glance there is. That is the whole reason such a cell
     * exists: a clock you have to walk to is a clock you look at your phone
     * instead of. Everything else keeps the order it was discovered in.
     *
     * Tied to the feature rather than to a position an app could ask for: an
     * app cannot say "put me next to home", it can only say "my cell is a
     * value", and the platform decides that a value belongs where it can be
     * seen at a glance. */
    int order[MENU_MAX_APPS]; /* scratch: everything below is stored in ring
                               * order, so nothing after this needs it */
    int k = 0;
    /* A glance this firmware cannot fill is not a glance here: it sorts and
     * draws as an ordinary icon cell, which is the manifest's own promise that
     * an unknown glance degrades to "no glance" on an older firmware. */
    for (i = 0; i < n; i++)
        if (!glance_known(pinned[i]->glance)) order[k++] = i;
    for (i = 0; i < n; i++)
        if (glance_known(pinned[i]->glance)) order[k++] = i;

    for (i = 0; i < n; i++) {
        const catnip_app_entry *a = pinned[order[i]];
        snprintf(m->ids[i], sizeof(m->ids[i]), "%s", a->id);
        snprintf(m->names[i], sizeof(m->names[i]), "%s", a->name);
        m->has_glance[i] = glance_known(a->glance);
        snprintf(m->glance_type[i], sizeof(m->glance_type[i]), "%s",
                 m->has_glance[i] ? a->glance : "");
        m->ready[i] = a->compatible && (fs_ready || !a->needs_fs);
        /* The new cell holds a placeholder, so what was last written is no
         * longer on screen and the next update has to write again. Without it,
         * coming back from an app left the clock reading `--:--` until the
         * minute changed. */
        m->shown[i][0] = '\0';
    }

    /* Where it was left, which is where leaving an app puts you back - found by
     * id, so an app that is no longer there quietly means home. */
    if (m->resume[0]) {
        for (i = 0; i < n; i++) {
            if (strcmp(m->ids[i], m->resume) == 0) {
                m->focus = i + 2; /* past home */
                break;
            }
        }
    }

    lua_getglobal(L, "__catnip_menu_build");
    lua_newtable(L); /* names, for the header */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, m->names[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* ids, which are what an app's own icon is found by */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, m->ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* whether each one can actually run right now */
    for (i = 0; i < n; i++) {
        lua_pushboolean(L, m->ready[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* the placeholder each value cell opens on, "" otherwise */
    for (i = 0; i < n; i++) {
        char text[16] = "";
        if (m->has_glance[i])
            catnip_glance_text(m->glance_type[i], 0, -1, text, sizeof(text));
        lua_pushstring(L, text);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* the face each value cell draws in, by its glance type */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, m->has_glance[i] ? glance_style(m->glance_type[i]) : "");
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, m->focus);
    if (lua_pcall(L, 6, 0, 0) != LUA_OK) catnip_rt_report_error(m->rt, L);
}

void catnip_menu_update_glances(catnip_menu *m, uint32_t epoch, int battery)
{
    lua_State *L;
    int i;

    if (!m) return;
    L = catnip_rt_lua(m->rt);
    if (!L) return;

    for (i = 0; i < m->n; i++) {
        char text[16];

        if (!m->has_glance[i]) continue;
        catnip_glance_text(m->glance_type[i], epoch, battery, text, sizeof(text));
        /* Nothing at all on the passes where this cell's value has not moved,
         * which is nearly all of them: a glance changes once a minute or
         * slower. */
        if (strcmp(m->shown[i], text) == 0) continue;
        snprintf(m->shown[i], sizeof(m->shown[i]), "%s", text);

        lua_getglobal(L, "__catnip_menu_glance");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            return;
        }
        lua_pushinteger(L, i + 1);
        lua_pushstring(L, text);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) catnip_rt_report_error(m->rt, L);
    }
}

const char *catnip_menu_focus_name(const catnip_menu *m)
{
    int i;

    if (!m || m->focus <= 1) return ""; /* home is the cat, and has no name */
    i = m->focus - 2;                   /* past home, into the app list */
    if (i < 0 || i >= m->n) return "";
    return m->names[i];
}

const char *catnip_menu_take_pick(catnip_menu *m)
{
    int i;

    if (!m || m->pick < 0) return NULL;
    i = m->pick;
    m->pick = -1;
    return m->ids[i];
}

void catnip_menu_home(catnip_menu *m)
{
    if (m) m->resume[0] = '\0';
}

void catnip_menu_free(catnip_menu *m)
{
    free(m);
}
