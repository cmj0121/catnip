/* catnip_api.c - see catnip_api.h. Each namespace is a table of C closures that
 * share the hal pointer as an upvalue and call through it. */
#include "catnip_api.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "catnip_fs_path.h"
#include "lauxlib.h"
#include "lua.h"

static const catnip_hal *hal_of(lua_State *L)
{
    return (const catnip_hal *)lua_touserdata(L, lua_upvalueindex(1));
}

/* ---- device.* ---- */

static int l_vibrate(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int ms = (int)luaL_checkinteger(L, 1);
    if (h && h->vibrate) h->vibrate(h->ud, ms);
    return 0;
}

static int l_led(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int r = (int)luaL_checkinteger(L, 1);
    int g = (int)luaL_checkinteger(L, 2);
    int b = (int)luaL_checkinteger(L, 3);
    if (h && h->led) h->led(h->ud, r, g, b);
    return 0;
}

static int l_battery(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    lua_pushinteger(L, (h && h->battery) ? h->battery(h->ud) : -1);
    return 1;
}

static int l_brightness(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int pct = (int)luaL_checkinteger(L, 1);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (h && h->brightness) h->brightness(h->ud, pct);
    return 0;
}

static int l_button(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    lua_pushboolean(L, (h && h->button) ? h->button(h->ud, name) : 0);
    return 1;
}

/* ---- sensor.* ---- */

static int l_imu(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    /* NaN to start with, not zero. An axis the HAL leaves alone is one nothing
     * measured, and it stays NaN through to the loop below, which drops it:
     * the returned table simply has no gx field rather than a gx of 0.0. See
     * catnip_hal.h for why zero cannot be made to mean "absent" here.
     *
     * This is also what a runtime with no imu hook at all now reports - an
     * empty table rather than six confident zeros. */
    float v[6];
    for (int i = 0; i < 6; i++)
        v[i] = NAN;
    if (h && h->imu) h->imu(h->ud, v);
    lua_newtable(L);
    static const char *const keys[6] = {"ax", "ay", "az", "gx", "gy", "gz"};
    for (int i = 0; i < 6; i++) {
        if (isnan(v[i])) continue;
        lua_pushnumber(L, v[i]);
        lua_setfield(L, -2, keys[i]);
    }
    return 1;
}

static int l_rtc(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    lua_pushinteger(L, (h && h->rtc_now) ? (lua_Integer)h->rtc_now(h->ud) : 0);
    return 1;
}

/* sensor.rtc_set(epoch) -> ok. False when this device has no clock to set, so
 * an app can tell "I set it" from "there was nothing to set" rather than
 * writing into the air. */
static int l_rtc_set(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    lua_Integer epoch = luaL_checkinteger(L, 1);
    lua_pushboolean(L, (h && h->rtc_set) ? h->rtc_set(h->ud, (long)epoch) : 0);
    return 1;
}

/* ---- gpio.* ---- */

static int l_gpio_mode(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int pin = (int)luaL_checkinteger(L, 1);
    const char *mode = luaL_checkstring(L, 2);
    if (h && h->gpio_mode) h->gpio_mode(h->ud, pin, mode);
    return 0;
}

static int l_gpio_write(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int pin = (int)luaL_checkinteger(L, 1);
    int val = lua_toboolean(L, 2) || (lua_isnumber(L, 2) && lua_tointeger(L, 2));
    if (h && h->gpio_write) h->gpio_write(h->ud, pin, val ? 1 : 0);
    return 0;
}

static int l_gpio_read(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int pin = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (h && h->gpio_read) ? h->gpio_read(h->ud, pin) : 0);
    return 1;
}

static int l_gpio_adc(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    int pin = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (h && h->gpio_adc) ? h->gpio_adc(h->ud, pin) : 0);
    return 1;
}

/* ---- service.* ---- */

static int l_wifi_status(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    lua_pushboolean(L, (h && h->wifi_status) ? h->wifi_status(h->ud) : 0);
    return 1;
}

static int l_wifi_ssid(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *ssid = (h && h->wifi_ssid) ? h->wifi_ssid(h->ud) : NULL;
    if (ssid) lua_pushstring(L, ssid);
    else lua_pushnil(L);
    return 1;
}

/* Nearby access points, or nil while a scan is still running - which is the
 * signal to ask again, not that the air is empty. Each entry is a table with
 * ssid, rssi and channel; the list is sorted strongest first, as the driver
 * returns it. */
/* When the network last set this clock, as a local epoch, or nil for "not since
 * this boot". nil rather than 0 because "never" is an absence, and a script
 * that forgets to check would otherwise format the epoch and print 1970. */
static int l_ntp_last(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    long t = (h && h->ntp_last) ? h->ntp_last(h->ud) : 0;

    if (t > 0) lua_pushinteger(L, t);
    else lua_pushnil(L);
    return 1;
}

/* service.ble.scan() -> { {name=, addr=, rssi=}, ... } or nil while running.
 *
 * The same contract service.wifi.scan() answers by, deliberately: nil means
 * "still listening", a table means "this is what was heard". An app that has
 * learned one radio should not have to learn the other, and the Scanner is
 * going to poll both.
 *
 * `name` is empty far more often than an ssid is - most advertisers do not
 * carry one - so the address is the identity here and the name is the nicety,
 * which is the other way round from Wi-Fi. */
static int l_ble_scan(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    catnip_ble_dev devs[24];
    int n;

    if (!h || !h->ble_scan) {
        lua_pushnil(L);
        return 1;
    }
    n = h->ble_scan(h->ud, devs, (int)(sizeof(devs) / sizeof(devs[0])));
    if (n < 0) {
        lua_pushnil(L); /* still listening */
        return 1;
    }
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, devs[i].name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, devs[i].addr);
        lua_setfield(L, -2, "addr");
        lua_pushinteger(L, devs[i].rssi);
        lua_setfield(L, -2, "rssi");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* service.ble.rescan() -> ok. Throws the last listen away and starts another,
 * which is what a press of A means on a page already showing a list. */
static int l_ble_rescan(lua_State *L)
{
    const catnip_hal *h = hal_of(L);

    if (h && h->ble_rescan) h->ble_rescan(h->ud);
    lua_pushboolean(L, h && h->ble_rescan ? 1 : 0);
    return 1;
}

/* service.wifi.rescan() -> ok. Throws the last scan away and starts another.
 *
 * The driver rescans on its own behind anything that keeps polling, so this is
 * not how an app gets fresh results - it is how it says it asked for them. What
 * it buys is the busy ring: a page that answers a press with the list it was
 * already showing has not answered it. */
static int l_wifi_rescan(lua_State *L)
{
    const catnip_hal *h = hal_of(L);

    if (h && h->wifi_rescan) h->wifi_rescan(h->ud);
    lua_pushboolean(L, h && h->wifi_rescan ? 1 : 0);
    return 1;
}

static int l_wifi_scan(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    catnip_wifi_ap aps[24];
    int n;

    if (!h || !h->wifi_scan) {
        lua_pushnil(L);
        return 1;
    }
    n = h->wifi_scan(h->ud, aps, (int)(sizeof(aps) / sizeof(aps[0])));
    if (n < 0) {
        lua_pushnil(L); /* still scanning */
        return 1;
    }
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, aps[i].ssid);
        lua_setfield(L, -2, "ssid");
        lua_pushinteger(L, aps[i].rssi);
        lua_setfield(L, -2, "rssi");
        lua_pushinteger(L, aps[i].channel);
        lua_setfield(L, -2, "channel");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int l_http_get(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *url = luaL_checkstring(L, 1);
    if (!h || !h->http_get) {
        lua_pushnil(L);
        lua_pushstring(L, "http not available");
        return 2;
    }
    char buf[2048];
    int n = h->http_get(h->ud, url, buf, sizeof(buf));
    if (n < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "http request failed");
        return 2;
    }
    lua_pushlstring(L, buf, (size_t)n);
    return 1;
}

/* ---- fs.* (flat, confined to hal->fs_base) ---- */

/* The one place a name chosen by an app becomes a path on the device. Every
 * fs.* entry point below goes through it, fs.list() included, so that the rule
 * stated in device/fs_path.h has a single implementation: six copies of it
 * would be five chances to leave one out, and the one left out is the hole.
 *
 * Neither refusal below is a value an app could mistake for an answer. Both
 * raise, because both are the app's own mistake rather than a fact about the
 * card, and a nil here would be read as "no such file". */
static int fs_path(lua_State *L, const catnip_hal *h, const char *name, char *out,
                   size_t cap)
{
    /* No base means no storage - on the MeowKit, no card in the slot. The HAL
     * leaves fs_base NULL rather than naming a mount point that is not there,
     * precisely so that this is an error and not a directory that answers
     * "empty" to every question asked of it. See device/hal_meowkit.cpp. */
    if (!h || !h->fs_base) return luaL_error(L, "fs not available");
    if (!catnip_fs_resolve(h->fs_base, name, out, cap))
        return luaL_error(L, "bad path: %s", name);
    return 0;
}

static int l_fs_write(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    char path[512];
    fs_path(L, h, name, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    lua_pushboolean(L, wrote == len);
    return 1;
}

static int l_fs_read(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    char path[512];
    fs_path(L, h, name, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        luaL_addlstring(&b, chunk, n);
    fclose(f);
    luaL_pushresult(&b);
    return 1;
}

static int l_fs_exists(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    char path[512];
    fs_path(L, h, name, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    lua_pushboolean(L, f != NULL);
    if (f) fclose(f);
    return 1;
}

/* fs.delete(name) -> true if the file was removed. */
static int l_fs_delete(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    char path[512];
    fs_path(L, h, name, path, sizeof(path));
    lua_pushboolean(L, remove(path) == 0);
    return 1;
}

/* fs.reset() -> true if the SD card was reformatted (destructive). */
static int l_fs_reset(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    if (!h || !h->sd_reset) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "sd reset not available");
        return 2;
    }
    lua_pushboolean(L, h->sd_reset(h->ud) == 0);
    return 1;
}

/* fs.list([path]) -> array of { name, is_dir, size }, or nil if not a dir. */
/* A directory entry, collected so the listing can be sorted before it is
 * returned. readdir hands entries back in the filesystem's own order, which is
 * not the same on two machines - an unsorted listing passed the File Browser's
 * test on macOS and failed it on Linux, and would have shown files in an order
 * that changed with the card. Sorting by name gives the stable, expected order
 * a browser wants. */
struct fs_entry {
    char name[256];
    int is_dir;
    long size;
};

static int fs_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const struct fs_entry *)a)->name, ((const struct fs_entry *)b)->name);
}

static int l_fs_list(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    /* No argument means the root, which fs_path() resolves from the empty name.
     * This call built its path and checked it itself until #45, and the two
     * checks had already drifted apart - a rule with two implementations is a
     * rule with two behaviours. */
    const char *sub = luaL_optstring(L, 1, "");
    char dir[512];
    fs_path(L, h, sub, dir, sizeof(dir));

    DIR *d = opendir(dir);
    if (!d) {
        lua_pushnil(L);
        return 1;
    }

    /* Collect every entry, sort by name, then build the table. The array is
     * grown as needed rather than capped: the returned Lua table already holds
     * one row per entry, so the intermediate array is the same order of memory
     * the function was always going to use, and an arbitrary cap here would
     * drop files the caller asked to see. On allocation failure the listing is
     * returned as far as it got, which is the honest partial answer. */
    struct fs_entry *ents = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (n == cap) {
            int ncap = cap ? cap * 2 : 32;
            struct fs_entry *grown =
                (struct fs_entry *)realloc(ents, (size_t)ncap * sizeof(*ents));
            if (!grown) break; /* keep what we have; return the partial listing */
            ents = grown;
            cap = ncap;
        }
        struct fs_entry *ent = &ents[n];
        snprintf(ent->name, sizeof(ent->name), "%s", e->d_name);
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->name);
        struct stat st;
        ent->is_dir = 0;
        ent->size = 0;
        if (stat(full, &st) == 0) {
            ent->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            ent->size = (long)st.st_size;
        }
        n++;
    }
    closedir(d);

    qsort(ents, (size_t)n, sizeof(*ents), fs_entry_cmp);

    lua_newtable(L);
    for (int i = 0; i < n; i++) {
        lua_newtable(L);
        lua_pushstring(L, ents[i].name);
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, ents[i].is_dir);
        lua_setfield(L, -2, "is_dir");
        lua_pushinteger(L, ents[i].size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, i + 1);
    }
    free(ents);
    return 1;
}

/* fs.stat(name) -> { size, is_dir }, or nil if it does not exist. */
static int l_fs_stat(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *name = luaL_checkstring(L, 1);
    char path[512];
    fs_path(L, h, name, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)st.st_size);
    lua_setfield(L, -2, "size");
    lua_pushboolean(L, S_ISDIR(st.st_mode) ? 1 : 0);
    lua_setfield(L, -2, "is_dir");
    return 1;
}

/* Register `funcs` into a new global table `name`, sharing the hal upvalue, and
 * mirror it under catnip.<name>. */
static void install(lua_State *L, const catnip_hal *hal, const char *name,
                    const luaL_Reg *funcs)
{
    lua_newtable(L);
    lua_pushlightuserdata(L, (void *)hal);
    luaL_setfuncs(L, funcs, 1);
    lua_pushvalue(L, -1);
    lua_setglobal(L, name);
    lua_getglobal(L, "catnip");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 2); /* catnip + the namespace table */
}

/* service.kv is a simple in-memory store; persistence is later fs/nvs work. */
static const char KV_LUA[] = "service.kv = (function()\n"
                             "  local store = {}\n"
                             "  return {\n"
                             "    set = function(k, v) store[k] = v end,\n"
                             "    get = function(k) return store[k] end,\n"
                             "    delete = function(k) store[k] = nil end,\n"
                             "  }\n"
                             "end)()\n"
                             "if catnip then catnip.service = service end\n";

int catnip_api_open(catnip_rt *rt, const catnip_hal *hal)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return -1;

    static const luaL_Reg device_funcs[] = {
        {"vibrate", l_vibrate},       {"led", l_led},       {"battery", l_battery},
        {"brightness", l_brightness}, {"button", l_button}, {NULL, NULL}};
    static const luaL_Reg sensor_funcs[] = {
        {"imu", l_imu}, {"rtc", l_rtc}, {"rtc_set", l_rtc_set}, {NULL, NULL}};
    static const luaL_Reg gpio_funcs[] = {{"mode", l_gpio_mode},
                                          {"write", l_gpio_write},
                                          {"read", l_gpio_read},
                                          {"adc", l_gpio_adc},
                                          {NULL, NULL}};
    static const luaL_Reg service_funcs[] = {{"wifi_status", l_wifi_status},
                                             {"wifi_ssid", l_wifi_ssid},
                                             {"wifi_scan", l_wifi_scan},
                                             {"wifi_rescan", l_wifi_rescan},
                                             {"ble_scan", l_ble_scan},
                                             {"ble_rescan", l_ble_rescan},
                                             {"ntp_last", l_ntp_last},
                                             {"http_get", l_http_get},
                                             {NULL, NULL}};
    static const luaL_Reg fs_funcs[] = {{"read", l_fs_read},     {"write", l_fs_write},
                                        {"exists", l_fs_exists}, {"list", l_fs_list},
                                        {"stat", l_fs_stat},     {"delete", l_fs_delete},
                                        {"reset", l_fs_reset},   {NULL, NULL}};

    install(L, hal, "device", device_funcs);
    install(L, hal, "sensor", sensor_funcs);
    install(L, hal, "gpio", gpio_funcs);
    install(L, hal, "service", service_funcs);
    install(L, hal, "fs", fs_funcs);

    /* Reshape service into nested wifi/http tables + kv, over the C funcs. */
    static const char SERVICE_LUA[] =
        "service.wifi = { status = service.wifi_status, ssid = service.wifi_ssid,\n"
        "                 scan = service.wifi_scan, rescan = service.wifi_rescan }\n"
        "service.ble = { scan = service.ble_scan, rescan = service.ble_rescan }\n"
        "service.ntp = { last = service.ntp_last }\n"
        "service.http = { get = service.http_get }\n"
        "service.wifi_status, service.wifi_ssid, service.wifi_scan = nil, nil, nil\n"
        "service.wifi_rescan = nil\n"
        "service.ble_scan, service.ble_rescan = nil, nil\n"
        "service.ntp_last, service.http_get = nil, nil\n";
    if (luaL_dostring(L, SERVICE_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        return -1;
    }
    if (luaL_dostring(L, KV_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        return -1;
    }
    return 0;
}
