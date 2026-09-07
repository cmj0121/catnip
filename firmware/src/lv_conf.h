/*
 * lv_conf.h - LVGL's configuration for this board (#29).
 *
 * LVGL gives every one of its options a default in lv_conf_internal.h and only
 * takes what this file overrides, so this file carries the settings the
 * MeowKit's hardware actually constrains and nothing else. Copying the
 * thousand-line template would bury those three decisions among a thousand
 * lines that agree with the default anyway, and every version bump would then
 * mean re-diffing the copy against a new template to find out which of those
 * thousand lines had moved underneath us.
 *
 * It is found through -DLV_CONF_INCLUDE_SIMPLE and the -Isrc that
 * platformio.ini already carries for the runtime headers.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* RGB565, in the CPU's own byte order, which is what the blit path reads.
 *
 * catnip_display_blit() casts its argument to lgfx::rgb565_t, and
 * lgfx::rgb565_t declares `depth = rgb565_nonswapped` - sixteen bits per pixel
 * in the ESP32's native little-endian layout, not the byte-swapped order the
 * SPI bus wants. LovyanGFX does that swap itself on the way out.
 *
 * LV_COLOR_DEPTH 16 renders exactly that: v9 emits native-order RGB565 and
 * leaves swapping to an explicit lv_draw_sw_rgb565_swap() call in the flush
 * callback, which lvgl_port.cpp deliberately does not make. So the pixels LVGL
 * hands to the flush callback are already in the order the blit reads them in,
 * with no conversion step between the two that could be got the wrong way
 * round.
 *
 * Getting this wrong does not fail loudly, it just recolours the screen: this
 * page once ran with every pixel byte-swapped, and what it looked like was red
 * arriving blue and cyan arriving yellow. The colour key at the bottom of the
 * diagnostic page is what makes that legible, and it is why the key survived
 * the move onto LVGL. */
#define LV_COLOR_DEPTH 16

/* LVGL's heap comes out of PSRAM rather than out of a static array in internal
 * RAM, which is what the built-in allocator does by default. The default would
 * put 64 KB - about a fifth of the internal RAM this firmware has - into .bss
 * at link time, spent whether or not anything ever draws. The board has 8 MB of
 * PSRAM and the display buffers are already there (see lvgl_port.cpp and
 * catnip_display_load_frames), so the widget tree belongs there with them.
 *
 * LV_MEM_SIZE is left at LVGL's 64 KB default: this page is a few dozen
 * objects and has never come close to it, and a number invented here would
 * only be a guess wearing a constant's name. */
#define LV_MEM_POOL_INCLUDE     <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)

/* The small font, for the rows of raw numbers under each headline. LVGL builds
 * in Montserrat 14 already and that stands in for the diagnostic page's old
 * 16-pixel headline font; 10 stands in for the 8-pixel one it used for the
 * numbers. Both are needed for the page to keep the two sizes it had, which is
 * what makes an answer and the working behind it tell apart at a glance. */
#define LV_FONT_MONTSERRAT_10 1

/* The large font, for the ui.* renderer's `title` role (#30). The five style
 * roles an app can name are a vocabulary and each one has to be tellable from
 * the others; `title` and `body` set in the same face at the same size are the
 * same thing with a different name, and an app that marked its heading would
 * have nothing to show for it. Three sizes cover the three that differ - 10
 * for `caption`, 14 for `body`, 16 for `title` - and the remaining two roles,
 * `primary` and `danger`, differ in colour rather than in size.
 *
 * This is a UI decision in a file that otherwise carries hardware ones, which
 * is the same exception the line above already is: a font has to be compiled
 * in, so the only place to ask for one is here. */
#define LV_FONT_MONTSERRAT_16 1

#endif /* LV_CONF_H */
