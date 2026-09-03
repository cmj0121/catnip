/*
 * display_st7789.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #29: bring up the ST7789 320x240 panel and LVGL.
 *
 * Enable with -DCATNIP_DEVICE_WIP once LVGL is added to platformio.ini.
 */
#ifdef CATNIP_DEVICE_WIP
#include <Arduino.h>
#include <lvgl.h>
// #include a ST7789 driver (e.g. LovyanGFX / TFT_eSPI) here.

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1; // allocate in PSRAM (heap_caps_malloc)

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    // TODO: push the pixel block to the ST7789 over SPI, then:
    lv_disp_flush_ready(drv);
}

void catnip_display_begin()
{
    lv_init();
    // TODO: init the panel + backlight.
    // s_buf1 = (lv_color_t*)heap_caps_malloc(320*40*sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, 320 * 40);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = 320;
    drv.ver_res = 240;
    drv.flush_cb = flush_cb;
    drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&drv);
}

// Wire this into main.cpp's host_pump so the UI stays live while apps wait.
void catnip_display_pump() { lv_timer_handler(); }
#endif /* CATNIP_DEVICE_WIP */
