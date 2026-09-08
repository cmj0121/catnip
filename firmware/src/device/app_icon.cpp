/* app_icon.cpp - see app_icon.h. */
#include "app_icon.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "../catnip_shell.h"
#include "sd_mount.h"

namespace {

/* One more than the shell's cap would be waste; one fewer would silently drop
 * an app's face. It is the same number for the same reason. */
const int kMaxIcons = CATNIP_SHELL_MAX_APPS;

struct Slot {
    char id[64];
    lv_image_dsc_t dsc;
    uint8_t *owned; /* PSRAM copy for a card app; NULL when it is in flash */
    bool used;
};

Slot g_slots[kMaxIcons];

/* The icon file's bytes, wrapped as an image source LVGL will decode on first
 * draw. LV_COLOR_FORMAT_RAW is how LVGL is told "this is a file's contents, not
 * pixels" - lodepng picks it up from there. */
bool fill(Slot *s, const uint8_t *data, size_t len)
{
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    /* The size is read out of the PNG's own IHDR rather than left at zero.
     * LVGL will not decode this until it first draws it, but it sizes and
     * scales the object *before* that - LV_IMAGE_ALIGN_STRETCH works out its
     * factor from header.w, and a zero there scales the picture to nothing.
     * Which is exactly how an icon that was found and set drew as blank. */
    if (len < 24 || memcmp(data, kSig, sizeof(kSig)) != 0) return false;
    if (memcmp(data + 12, "IHDR", 4) != 0) return false;
    uint32_t w = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
                 ((uint32_t)data[18] << 8) | data[19];
    uint32_t h = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
                 ((uint32_t)data[22] << 8) | data[23];
    if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;

    memset(&s->dsc, 0, sizeof(s->dsc));
    s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->dsc.header.cf = LV_COLOR_FORMAT_RAW;
    s->dsc.header.w = (uint16_t)w;
    s->dsc.header.h = (uint16_t)h;
    s->dsc.data = data;
    s->dsc.data_size = (uint32_t)len;
    return true;
}

/* Read a file off the card into PSRAM. Returns NULL and says nothing on a
 * missing file: an app without a usable icon is an ordinary state, not a fault,
 * and the launcher already knows what to draw instead. */
uint8_t *read_file(const char *path, size_t *len)
{
    if (!catnip_sd_mounted()) return nullptr;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f || f.isDirectory()) return nullptr;
    size_t n = f.size();
    /* A quarter of a megabyte is far past any icon and well short of trouble;
     * a file bigger than this is a mistake, not an icon. */
    if (n == 0 || n > 256u * 1024u) {
        f.close();
        return nullptr;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!buf) {
        f.close();
        return nullptr;
    }
    size_t got = f.read(buf, n);
    f.close();
    if (got != n) {
        free(buf);
        return nullptr;
    }
    *len = n;
    return buf;
}

} /* namespace */

void catnip_app_icons_free(void)
{
    for (int i = 0; i < kMaxIcons; i++) {
        if (!g_slots[i].used) continue;
        /* LVGL caches what it decoded, keyed by the source pointer. Dropping
         * that before the bytes go is the difference between a cache miss next
         * time and a cache hit on freed memory. */
        lv_image_cache_drop(&g_slots[i].dsc);
        if (g_slots[i].owned) free(g_slots[i].owned);
        g_slots[i].owned = nullptr;
        g_slots[i].used = false;
    }
}

void catnip_app_icons_load(const catnip_app_entry *apps, int n)
{
    catnip_app_icons_free();
    if (!apps) return;

    int k = 0, missing = 0;
    for (int i = 0; i < n && k < kMaxIcons; i++) {
        const catnip_app_entry *a = &apps[i];
        if (!a->icon[0]) continue;

        size_t len = 0;
        const uint8_t *data = catnip_loader_builtin_icon(a->id, &len);
        uint8_t *owned = nullptr;
        if (!data) {
            char path[336];
            snprintf(path, sizeof(path), "%s/%s", a->dir, a->icon);
            owned = read_file(path, &len);
            data = owned;
        }
        if (!data) {
            missing++;
            continue;
        }

        Slot *s = &g_slots[k];
        if (!fill(s, data, len)) { /* not a PNG we can size, so not an icon */
            if (owned) free(owned);
            missing++;
            continue;
        }
        k++;
        snprintf(s->id, sizeof(s->id), "%s", a->id);
        s->owned = owned;
        s->used = true;
    }
    /* Once per rebuild, not per frame. An app without a usable icon is an
     * ordinary state, so this is a count rather than a warning per app. */
    Serial.printf("[catnip] icons: %d loaded, %d without one\n", k, missing);
}

const void *catnip_app_icon_find(const char *id)
{
    if (!id || !id[0]) return nullptr;
    for (int i = 0; i < kMaxIcons; i++)
        if (g_slots[i].used && strcmp(g_slots[i].id, id) == 0) return &g_slots[i].dsc;
    return nullptr;
}
