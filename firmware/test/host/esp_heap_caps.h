/*
 * esp_heap_caps.h - the one line of ESP-IDF the host build needs (#81).
 *
 * src/lv_conf.h gives LVGL its 512 KB pool out of PSRAM:
 *
 *     #define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
 *
 * A host build could avoid that by carrying its own lv_conf.h - and then the
 * layout being tested would be the layout of a *different configuration*, which
 * is the one thing these tests must not be. The whole argument for running the
 * device's own backend against the device's own LVGL is that what comes out is
 * what the device does; a second lv_conf.h would quietly undo it.
 *
 * So the configuration stays exactly as the device has it, and the four bytes
 * of ESP-IDF it names are supplied here instead. A host has one kind of memory.
 */
#ifndef CATNIP_HOST_ESP_HEAP_CAPS_H
#define CATNIP_HOST_ESP_HEAP_CAPS_H

#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0

static inline void *heap_caps_malloc(size_t size, unsigned caps)
{
    (void)caps;
    return malloc(size);
}

#endif /* CATNIP_HOST_ESP_HEAP_CAPS_H */
