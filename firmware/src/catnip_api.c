/* catnip_api.c - see catnip_api.h. Each namespace is a table of C closures that
 * share the hal pointer as an upvalue and call through it. */
#include "catnip_api.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
    float v[6] = {0, 0, 0, 0, 0, 0};
    if (h && h->imu) h->imu(h->ud, v);
    lua_newtable(L);
    static const char *const keys[6] = {"ax", "ay", "az", "gx", "gy", "gz"};
    for (int i = 0; i < 6; i++) {
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

static int fs_path(lua_State *L, const catnip_hal *h, const char *name, char *out,
                   size_t cap)
{
    if (!h || !h->fs_base) return luaL_error(L, "fs not available");
    /* Allow subdirectories, but never escape the base with "..". */
    if (strstr(name, "..")) return luaL_error(L, "bad path");
    snprintf(out, cap, "%s/%s", h->fs_base, name);
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
static int l_fs_list(lua_State *L)
{
    const catnip_hal *h = hal_of(L);
    const char *sub = luaL_optstring(L, 1, "");
    if (!h || !h->fs_base) return luaL_error(L, "fs not available");
    if (strstr(sub, "..")) return luaL_error(L, "bad path");

    char dir[512];
    if (sub[0]) snprintf(dir, sizeof(dir), "%s/%s", h->fs_base, sub);
    else snprintf(dir, sizeof(dir), "%s", h->fs_base);

    DIR *d = opendir(dir);
    if (!d) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    int i = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        int is_dir = 0;
        long size = 0;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            size = (long)st.st_size;
        }
        lua_newtable(L);
        lua_pushstring(L, e->d_name);
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, is_dir);
        lua_setfield(L, -2, "is_dir");
        lua_pushinteger(L, size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, ++i);
    }
    closedir(d);
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
    static const luaL_Reg sensor_funcs[] = {{"imu", l_imu}, {"rtc", l_rtc}, {NULL, NULL}};
    static const luaL_Reg gpio_funcs[] = {{"mode", l_gpio_mode},
                                          {"write", l_gpio_write},
                                          {"read", l_gpio_read},
                                          {"adc", l_gpio_adc},
                                          {NULL, NULL}};
    static const luaL_Reg service_funcs[] = {{"wifi_status", l_wifi_status},
                                             {"wifi_ssid", l_wifi_ssid},
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
        "service.wifi = { status = service.wifi_status, ssid = service.wifi_ssid }\n"
        "service.http = { get = service.http_get }\n"
        "service.wifi_status, service.wifi_ssid, service.http_get = nil, nil, nil\n";
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
