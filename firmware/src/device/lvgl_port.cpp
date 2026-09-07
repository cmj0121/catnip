/* lvgl_port.cpp - see lvgl_port.h. */
#include <Arduino.h>

#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "lvgl_port.h"

namespace {

/* One full-screen draw buffer, in PSRAM, and LVGL rendering whole screens into
 * it.
 *
 * LVGL offers three render modes and the choice is made for us by display.h.
 * PARTIAL renders a strip at a time and calls the flush callback once per
 * strip; DIRECT renders into a screen-sized buffer but flushes only the
 * rectangles that changed. Both hand the callback a sub-rectangle, and
 * catnip_display_blit() takes whole screens - so either one would mean either
 * widening the panel driver's contract with a windowed write, or reaching past
 * it to the LovyanGFX object it keeps private. FULL renders the whole screen
 * and calls the callback exactly once with the whole of it, which is the shape
 * the blit already has.
 *
 * That is not a compromise here. This panel is write-only over SPI at 80 MHz,
 * so a flush is 150 KB on the wire whatever LVGL renders; the modes differ in
 * how much they redraw in memory, and this device redraws a screen that is
 * mostly flat colour. It is also exactly what the diagnostic page already did
 * before it moved onto LVGL - compose a full 320x240 frame in PSRAM, then blit
 * it - so the cost is measured rather than assumed.
 *
 * 320 * 240 * 2 = 153,600 bytes, in PSRAM because that is where the 150 KB
 * frames on this device have always lived (catnip_display_load_frames does the
 * same for the boot animation) and internal RAM has none to spare. One buffer,
 * not two: the second is only worth its 150 KB when the flush is asynchronous
 * and LVGL can render the next frame while the last one is on the wire, and
 * catnip_display_blit() returns when the pixels have gone. */
lv_display_t *g_disp = nullptr;
uint16_t *g_draw_buf = nullptr;

const size_t kDrawBufBytes = (size_t)CATNIP_SCREEN_W * CATNIP_SCREEN_H * 2;

/* LVGL's clock. millis() is already the firmware's idea of the time - the
 * shell, the animation and the debounce filter all read it - so LVGL reads the
 * same one rather than being fed a tick from an interrupt that could drift
 * away from it. */
uint32_t tick_ms(void)
{
    return millis();
}

/* The whole of the path from LVGL to the glass.
 *
 * `area` is the whole screen every time, because the display is in FULL render
 * mode, and `px_map` is the start of the draw buffer above. So the callback is
 * the blit and nothing else: no coordinates to get wrong, no byte order to
 * convert. LV_COLOR_DEPTH 16 makes those pixels native-order RGB565 and
 * catnip_display_blit() reads native-order RGB565 - see the comment in
 * src/lv_conf.h for why that agreement is worth stating twice. */
void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    catnip_display_blit(px_map);
    lv_display_flush_ready(disp);
}

} /* namespace */

bool catnip_lvgl_begin(void)
{
    if (g_disp) return true;

    g_draw_buf = (uint16_t *)heap_caps_malloc(kDrawBufBytes, MALLOC_CAP_SPIRAM);
    if (!g_draw_buf) {
        Serial.println("[catnip] lvgl: no PSRAM for the draw buffer");
        return false;
    }

    lv_init();
    lv_tick_set_cb(tick_ms);

    g_disp = lv_display_create(CATNIP_SCREEN_W, CATNIP_SCREEN_H);
    /* Said out loud rather than left to the default, because the default is
     * what LV_COLOR_DEPTH happens to imply and this is the one setting that
     * decides whether red arrives red. */
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, g_draw_buf, nullptr, kDrawBufBytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_disp, flush);

    Serial.printf("[catnip] lvgl: up, %d bytes of PSRAM for one full-screen buffer\n",
                  (int)kDrawBufBytes);
    return true;
}

void catnip_lvgl_step(void)
{
    if (!g_disp) return;
    lv_timer_handler();
}
