/* catnip_settings.c - see catnip_settings.h. */
#include "catnip_settings.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* The page, built in Lua so it is the same kind of tree an app builds.
 *
 * The ladders arrive from C as two parallel tables per column - what to fill
 * the bar to, and what to print above it - and Lua owns which rung each column
 * is on. That division is deliberate: the step is a property write on a
 * retained node, and the code that writes properties is the code that holds the
 * nodes. C is told the new rung afterwards, and turns it back into a setting.
 *
 * Left and right move between columns; short A, or a tap on a column, is the
 * next rung and wraps. There is no second mode and no confirm step - the whole
 * argument for this shape is that a brightness you cannot see while setting it
 * is a brightness you set twice. */
static const char CATNIP_SETTINGS_LUA[] =
    "function __catnip_settings_build(names, fills, blocks, labels, pos, ranges)\n"
    "  local rows = {}\n"
    "  local list\n"
    /* Which column is under the ring (0 = none, which is where the page opens)
     * and whether that column is the one being edited. Two states, because
     * choosing a setting and changing it are two acts here: arriving on a page
     * of five things and having one of them already live under the joystick is
     * how a brightness gets changed by someone who only wanted to look. */
    "  local sel = 0\n"
    "  local active = false\n"
    /* A column is either a ladder of named rungs or a continuous range, and
     * `pos[i]` holds whichever that column deals in - the rung, or the value.
     * These three turn one into the other so nothing below has to ask twice. */
    "  local function fill_of(i)\n"
    "    local r = ranges[i]\n"
    "    if not r then return fills[i][pos[i]] end\n"
    "    return math.floor((pos[i] - r[1]) * 100 / (r[2] - r[1]) + 0.5)\n"
    "  end\n"
    "  local function text_of(i)\n"
    "    if not ranges[i] then return labels[i][pos[i]] end\n"
    "    return pos[i] .. '%'\n"
    "  end\n"
    "  local function clamp(i, v)\n"
    "    local r = ranges[i]\n"
    "    local lo = r and r[1] or 1\n"
    "    local hi = r and r[2] or #fills[i]\n"
    "    if v < lo then return lo elseif v > hi then return hi end\n"
    "    return v\n"
    "  end\n"
    "  local function paint()\n"
    "    list.selected = sel\n"
    "    for i, row in ipairs(rows) do\n"
    /* `primary` is how the backend is told which column is live - the style
     * roles are already the vocabulary for "this one matters more than its
     * neighbours". */
    "      row.style = (active and i == sel) and 'primary' or 'body'\n"
    "    end\n"
    "  end\n"
    /* Put a column where it is being asked to go, and tell C. One place, so
     * every way of changing a value - a button, a swipe, a finger dragged up
     * the blocks - ends in the same three lines. */
    "  local function put(i, v)\n"
    "    v = clamp(i, v)\n"
    "    if v == pos[i] then return end\n"
    "    pos[i] = v\n"
    "    rows[i].value = fill_of(i)\n"
    "    rows[i].value_text = text_of(i)\n"
    "    __catnip_settings_set(i, v)\n"
    "  end\n"
    "  for i, name in ipairs(names) do\n"
    "    rows[i] = ui.label{ id = 'set' .. i, text = name,\n"
    "                        value = fill_of(i), value_text = text_of(i),\n"
    /* The rung count, so a ladder is drawn as that many blocks - this value can
     * only be one of five things and a solid bar would promise otherwise. A
     * range passes 0 and is drawn as a filled track, because for it the bar's
     * promise is true. */
    "                        steps = ranges[i] and 0 or blocks[i],\n"
    /* A finger dragged over this column, carrying how far up the blocks it is.
     * The nearest rung, or the nearest multiple of the range's increment: the
     * finger is pointing at a value, not filling a tank. */
    "                        on_drag = function(self, pct)\n"
    "                          sel = i\n"
    "                          active = true\n"
    "                          local r = ranges[i]\n"
    "                          if r then\n"
    "                            local v = r[1] + pct * (r[2] - r[1]) / 100\n"
    "                            put(i, math.floor(v / r[3] + 0.5) * r[3])\n"
    "                          else\n"
    "                            local n = #fills[i]\n"
    "                            put(i, math.floor(pct * (n - 1) / 100 + 0.5) + 1)\n"
    "                          end\n"
    "                          paint()\n"
    "                        end }\n"
    "  end\n"
    "  list = ui.list{ id = 'settings_list', layout = 'mixer',\n"
    /* Left and right choose the column, and stop at the ends rather than
     * wrapping: this shape has a visible first and last, and coming round in
     * one you can see the whole of reads as the ring jumping. From nothing
     * selected, either direction lands on the first. */
    "    on_prev = function()\n"
    "      if sel <= 1 then sel = 1 else sel = sel - 1 end\n"
    "      paint()\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel < 1 then sel = 1 elseif sel < #rows then sel = sel + 1 end\n"
    "      paint()\n"
    "    end,\n"
    /* Short A, or a tap: choose this column to edit. A tap names its own
     * column, where the joystick can only have moved to one. */
    "    on_click = function(self, i)\n"
    "      if i then sel = i end\n"
    "      if sel >= 1 then active = true end\n"
    "      paint()\n"
    "    end,\n"
    /* Up and down are the value, and only once a column is live. Before that
     * they do nothing, which is the point of the second state. */
    "    on_raise = function()\n"
    "      if not (active and sel >= 1) then return end\n"
    "      put(sel, pos[sel] + (ranges[sel] and ranges[sel][3] or 1))\n"
    "    end,\n"
    "    on_lower = function()\n"
    "      if not (active and sel >= 1) then return end\n"
    "      put(sel, pos[sel] - (ranges[sel] and ranges[sel][3] or 1))\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  paint()\n"
    /* B lets go of the column first and leaves the page second - one press per
     * level, the same climb every other screen offers. Claimed only while
     * something is live, so a B with nothing selected falls through to the
     * platform and leaves. */
    "  ui.screen{ list, id = 'settings_screen',\n"
    "    on_back = function()\n"
    "      if not active then return false end\n"
    "      active = false\n"
    /* And the ring goes with it. "Unselected" has to look like nothing is
     * selected: a column still wearing a border after B would say the page is
     * waiting on it, and the next up or down - which now does nothing - would
     * read as the device having stopped listening. */
    "      sel = 0\n"
    "      paint()\n"
    "      return true\n"
    "    end }\n"
    "end\n";

/* Room for the longest ladder below, with a little to spare. */
#define STEPS_MAX 6

/* One column: the values it can take, in that setting's own unit, and what each
 * one is called. Two arrays rather than a formatter, because the readings are
 * not all the same kind of number - a percentage, a duration, and the word for
 * "off" - and a formatter that covered all three would be a small language. */
typedef struct {
    const char *name;
    /* Rungs, or 0 when the setting is a continuous range and `lo`/`hi`/`inc`
     * are what describe it instead.
     *
     * Both kinds exist because the settings are not all the same kind of thing.
     * A brightness genuinely is a quantity - any value between the ends is a
     * real brightness, and a finger dragged up it should land where it was put.
     * A breathing period is not: there are four rhythms worth having, and
     * offering the eight hundred numbers between them would be offering a
     * precision the setting does not have. */
    int n;
    int step[STEPS_MAX];
    const char *label[STEPS_MAX];
    int lo, hi, inc;
} setting_def;

/* The columns, left to right.
 *
 * The boot animation's frame time is not among them, though catnip_config still
 * carries it and config.json still sets it. It was the only column describing
 * how the device *starts* rather than how it *is*, its name said so to nobody,
 * and anyone who wants to retime a boot animation is already editing the file
 * that names the animation's frames. Four columns are wider than five.
 *
 * Volume is not here either: the ES8311 has no driver yet
 * (#68), and a control that cannot do anything is worse than a missing one -
 * this device already uses dimming to mean "you can fix this", which a volume
 * nobody can enable would quietly turn into a lie. It goes on the right-hand
 * end when it arrives, so nothing an owner has learned the position of moves.
 *
 * No icons either. The name is under every column and legible, and the icon
 * family is drawn rather than improvised; five settings glyphs are a job for
 * whoever draws the set, not for whoever wires the page. */
enum {
    SET_SCREEN = 0,
    SET_IDLE,
    SET_LED,
    SET_BREATH,
    SET_COUNT,
};

static const setting_def kDefs[SET_COUNT] = {
    /* Continuous, because a brightness is a real quantity: every value between
     * the ends is a brightness somebody might want, and a finger dragged up it
     * should land where it was put rather than on the nearest of five.
     *
     * The bottom is CATNIP_SCREEN_MIN_PCT and not 0. A screen turned all the
     * way off cannot show the page that turned it off, and the power button
     * only toggles the panel back to the brightness that is now zero - so the
     * device would be running, listening, and unusable until it was reflashed
     * or its config file edited on a computer. That is not a setting, it is a
     * trap; the dimmest rung is still one that can be read in the dark. */
    {"Screen", 0, {0}, {NULL}, CATNIP_SCREEN_MIN_PCT, 100, 5},
    {"Idle-off",
     5,
     {0, 15, 30, 60, 300},
     {"Never", "15 s", "30 s", "1 m", "5 m"},
     0,
     0,
     0},
    /* The owner may turn the LED off; an app may not. The light is the sign
     * that the firmware is still running when the screen cannot say so, and an
     * app that could switch it off could make a frozen device look well. Nobody
     * is fooled by their own decision, so the owner's ladder starts at Off. */
    /* The rungs climb steeply because the part does: a WS2812 at 100% is a
     * torch, and the useful range for a status light all lives in the bottom
     * fifth. An evenly spaced ladder would spend four of its five rungs on
     * brightnesses nobody would pick. */
    {"LED", 5, {0, 5, 15, 40, 100}, {"Off", "5%", "15%", "40%", "100%"}, 0, 0, 0},
    /* Milli-breaths per second, so the ladder is integers; the reading is the
     * period, because that is the thing an owner is actually looking at.
     *
     * The rungs run from the fastest to the slowest, which is backwards as
     * rates and forwards as the thing on screen: the bar and the number beside
     * it have to move together, or the column says two things at once. A rate
     * climbing while its period falls is exactly that, and it read as the
     * control being wired backwards. */
    {"Breath", 4, {1600, 800, 400, 200}, {"0.6 s", "1.2 s", "2.5 s", "5.0 s"}, 0, 0, 0},
};

struct catnip_settings {
    catnip_rt *rt;
    catnip_config cfg;   /* the settings as they now stand */
    int step[SET_COUNT]; /* the rung each column is on, zero-based */
    bool dirty;
};

/* The rung whose value is closest to `v`. Distance rather than a match, because
 * the value may have come from a hand-edited file or from a firmware whose
 * ladder had different rungs on it, and neither is a reason to refuse. */
static int nearest(const setting_def *d, int v)
{
    int best = 0;
    int best_d = -1;
    int i;

    if (d->n == 0) {
        /* A range holds the value itself rather than a position on a ladder,
         * clamped to its own ends and snapped to its own increment - so a
         * hand-edited 37 comes back as 35 rather than as something the buttons
         * could never have produced and cannot step away from evenly. */
        if (v < d->lo) v = d->lo;
        if (v > d->hi) v = d->hi;
        return d->lo + ((v - d->lo) + d->inc / 2) / d->inc * d->inc;
    }
    for (i = 0; i < d->n; i++) {
        int dist = v - d->step[i];
        if (dist < 0) dist = -dist;
        if (best_d < 0 || dist < best_d) {
            best_d = dist;
            best = i;
        }
    }
    return best;
}

/* The column values, back into the config they came from. One place, so a rung
 * and the setting it stands for cannot drift apart. */
static void steps_to_config(catnip_settings *s)
{
    /* A range column holds its value directly; the others hold a rung. */
    s->cfg.screen_brightness = (uint8_t)s->step[SET_SCREEN];
    s->cfg.idle_off_s = (uint16_t)kDefs[SET_IDLE].step[s->step[SET_IDLE]];
    s->cfg.led_brightness = (uint8_t)kDefs[SET_LED].step[s->step[SET_LED]];
    s->cfg.led_breaths_per_second =
        (float)kDefs[SET_BREATH].step[s->step[SET_BREATH]] / 1000.0f;
    /* boot_frame_ms is deliberately not touched. It is not on this page - see
     * the note above kDefs - so whatever was loaded stays loaded, and a save
     * from here carries it through unchanged rather than resetting it to
     * whatever a column that no longer exists would have said. */
}

/* The bridge Lua calls after it has moved a column: the one-based column and
 * its new one-based rung. */
static int settings_set_cb(lua_State *L)
{
    catnip_settings *s = (catnip_settings *)lua_touserdata(L, lua_upvalueindex(1));
    int col = (int)luaL_checkinteger(L, 1);
    int step = (int)luaL_checkinteger(L, 2);

    if (!s || col < 1 || col > SET_COUNT) return 0;
    if (kDefs[col - 1].n == 0) {
        if (step < kDefs[col - 1].lo || step > kDefs[col - 1].hi) return 0;
        s->step[col - 1] = step;
    } else {
        if (step < 1 || step > kDefs[col - 1].n) return 0;
        s->step[col - 1] = step - 1;
    }
    steps_to_config(s);
    s->dirty = true;
    return 0;
}

catnip_settings *catnip_settings_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_settings *s;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;

    s = (catnip_settings *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->rt = rt;
    catnip_config_defaults(&s->cfg);

    if (luaL_dostring(L, CATNIP_SETTINGS_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(s);
        return NULL;
    }

    /* The page pointer travels as an upvalue rather than through a file static,
     * so a second page on a second runtime would not collide. */
    lua_pushlightuserdata(L, s);
    lua_pushcclosure(L, settings_set_cb, 1);
    lua_setglobal(L, "__catnip_settings_set");
    return s;
}

void catnip_settings_show(catnip_settings *s, const catnip_config *cfg)
{
    lua_State *L;
    int i;
    int j;

    if (!s) return;
    L = catnip_rt_lua(s->rt);
    if (!L) return;

    if (cfg) s->cfg = *cfg;
    s->dirty = false;
    s->step[SET_SCREEN] = nearest(&kDefs[SET_SCREEN], s->cfg.screen_brightness);
    /* nearest() gives a rung for a ladder and the value itself for a range, and
     * `step[]` holds whichever of the two that column deals in. */
    s->step[SET_IDLE] = nearest(&kDefs[SET_IDLE], s->cfg.idle_off_s);
    s->step[SET_LED] = nearest(&kDefs[SET_LED], s->cfg.led_brightness);
    s->step[SET_BREATH] = nearest(&kDefs[SET_BREATH],
                                  (int)(s->cfg.led_breaths_per_second * 1000.0f + 0.5f));
    /* What is shown is what is held: a column that snapped to the nearest rung
     * has changed the setting, and leaving the two out of step would mean the
     * page reported one thing and the device did another. */
    steps_to_config(s);

    lua_getglobal(L, "__catnip_settings_build");

    lua_newtable(L); /* names */
    for (i = 0; i < SET_COUNT; i++) {
        lua_pushstring(L, kDefs[i].name);
        lua_rawseti(L, -2, i + 1);
    }

    /* A range column has no rungs, so its `fills` and `labels` are empty tables
     * that Lua never indexes - it takes the `ranges` entry instead. They are
     * still pushed so every table has an entry per column and nothing below has
     * to reason about holes. */
    lua_newtable(L); /* fills: how far up the bar each rung is, 0-100 */
    for (i = 0; i < SET_COUNT; i++) {
        int n = kDefs[i].n;
        int zero = n > 0 && kDefs[i].step[0] == 0;
        lua_newtable(L);
        for (j = 0; j < n; j++) {
            /* Position on the ladder, not the value: the rungs are not evenly
             * spaced (15 s to 5 m), and a bar drawn from the value would put
             * four of the five columns in the bottom tenth. What the height is
             * telling you is how far along you are.
             *
             * A ladder whose bottom rung is a genuine nothing - Off, Never -
             * starts at an empty bar, and one whose bottom rung is still a
             * quantity starts at one block. Off has to look like off; the
             * fastest breath is not off. */
            lua_Integer fill;
            if (n <= 1) fill = 100;
            else if (zero) fill = (lua_Integer)(j * 100 / (n - 1));
            else fill = (lua_Integer)((j + 1) * 100 / n);
            lua_pushinteger(L, fill);
            lua_rawseti(L, -2, j + 1);
        }
        lua_rawseti(L, -2, i + 1);
    }

    lua_newtable(L); /* how many blocks to draw - one fewer than the rungs when
                      * the bottom rung is an empty bar */
    for (i = 0; i < SET_COUNT; i++) {
        int n = kDefs[i].n;
        int zero = n > 0 && kDefs[i].step[0] == 0;
        lua_pushinteger(L, n ? (zero ? n - 1 : n) : 0);
        lua_rawseti(L, -2, i + 1);
    }

    lua_newtable(L); /* labels */
    for (i = 0; i < SET_COUNT; i++) {
        lua_newtable(L);
        for (j = 0; j < kDefs[i].n; j++) {
            lua_pushstring(L, kDefs[i].label[j]);
            lua_rawseti(L, -2, j + 1);
        }
        lua_rawseti(L, -2, i + 1);
    }

    lua_newtable(L); /* where each column opens: a rung, or a range's value */
    for (i = 0; i < SET_COUNT; i++) {
        lua_pushinteger(L, kDefs[i].n ? s->step[i] + 1 : s->step[i]);
        lua_rawseti(L, -2, i + 1);
    }

    lua_newtable(L); /* {lo, hi, inc} for a continuous column, absent for a ladder */
    for (i = 0; i < SET_COUNT; i++) {
        if (kDefs[i].n) continue;
        lua_newtable(L);
        lua_pushinteger(L, kDefs[i].lo);
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, kDefs[i].hi);
        lua_rawseti(L, -2, 2);
        lua_pushinteger(L, kDefs[i].inc);
        lua_rawseti(L, -2, 3);
        lua_rawseti(L, -2, i + 1);
    }

    if (lua_pcall(L, 6, 0, 0) != LUA_OK) catnip_rt_report_error(s->rt, L);
}

const catnip_config *catnip_settings_config(const catnip_settings *s)
{
    return s ? &s->cfg : NULL;
}

bool catnip_settings_take_dirty(catnip_settings *s)
{
    bool d;

    if (!s) return false;
    d = s->dirty;
    s->dirty = false;
    return d;
}

void catnip_settings_free(catnip_settings *s)
{
    free(s);
}
