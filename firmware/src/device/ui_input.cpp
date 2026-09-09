/* ui_input.cpp - see ui_input.h. The decisions are in ui_input_core.c; what is
 * left here is the half that needs a board: read the seven switches, read the
 * panel, feed LVGL's pointer, and answer the two questions that are geometry. */
#include <Arduino.h>
#include <lvgl.h>

#include "input.h"
#include "lvgl_backend.h"
#include "touch.h"
#include "ui_input.h"
#include "ui_input_core.h"

namespace {

/* The LVGL pointer indev fed by the touch panel, created lazily once LVGL is up.
 * A tap on it lands on the button under the finger and fires LV_EVENT_CLICKED,
 * which is the same road a joystick activation takes: on_clicked in the backend
 * posts "click". So touch needs no code of its own here beyond reading the
 * panel into LVGL. */
lv_indev_t *g_touch_indev;

/* Everything one pass has to remember. Held here rather than in the core so the
 * core stays a function of its arguments, which is what a host test drives. */
catnip_ui_input g_in;

void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0;

    (void)indev;
    /* The last known position outlives the touch (see touch.h), so the release
     * still reports where the finger was - which is the point LVGL clicks. */
    if (catnip_touch_position(&x, &y)) {
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
    }
    data->state = catnip_touch_down() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void ensure_touch_indev(void)
{
    if (g_touch_indev) return;
    /* LVGL comes up on the first widget an app draws, not at boot, so the indev
     * cannot be created until then. Until it is, touch does nothing, which is
     * correct: there is nothing on the panel to tap. */
    if (!catnip_lvgl_backend_active()) return;
    g_touch_indev = lv_indev_create();
    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, touch_read);
}

/* The three things the core cannot do without something that draws. */
int env_mixer_at(void *ud, int x, int y, catnip_handle *col, int *pct)
{
    (void)ud;
    return catnip_lvgl_backend_mixer_at(x, y, col, pct) ? 1 : 0;
}

int env_mixer_pct(void *ud, catnip_handle col, int y)
{
    (void)ud;
    return catnip_lvgl_backend_mixer_pct(col, y);
}

/* Told to the indev rather than gated in the backend: this ends the contact as
 * far as LVGL is concerned - it sends PRESS_LOST, forgets the object and emits
 * no click - which covers every clickable object rather than the two whose
 * callbacks anyone remembered to guard. */
void env_cancel_touch(void *ud)
{
    (void)ud;
    if (g_touch_indev) lv_indev_wait_release(g_touch_indev);
}

const catnip_ui_env kEnv = {env_mixer_at, env_mixer_pct, env_cancel_touch, nullptr};

} /* namespace */

void catnip_ui_input_begin(void)
{
    /* The switches are begun by the HAL; only the touch panel is ours to bring
     * up, and it shares the I2C bus that is already up by now. A panel that does
     * not answer leaves every touch read reporting nothing, which the pointer
     * indev renders as a finger that is never down. */
    catnip_touch_begin();
}

int catnip_ui_input_step(catnip_rt *rt)
{
    catnip_ui_sample s = {};
    uint16_t tx = 0, ty = 0;

    /* The edges are still read, because the driver clears them as they are read
     * and this is their sole reader - but nothing acts on them any more: what a
     * switch means is decided from how long it has been held, so what the core
     * is handed is the settled level. */
    for (int b = 0; b < CATNIP_BTN_COUNT; b++) {
        (void)catnip_input_pressed((catnip_button)b);
        s.down[b] = catnip_input_down((catnip_button)b);
    }
    s.now = (unsigned)millis();

    catnip_touch_poll();
    ensure_touch_indev();
    (void)catnip_touch_position(&tx, &ty);
    s.touch_down = catnip_touch_down();
    s.touch_x = (int)tx;
    s.touch_y = (int)ty;

    int gesture = catnip_ui_input_run(&g_in, rt, &s, &kEnv);

    /* The ring goes on whatever is focused now. The backend moves it only when
     * the handle changed and dereferences the object itself, so this hands it a
     * handle and nothing more. */
    catnip_lvgl_backend_focus(catnip_ui_input_focus(&g_in));
    return gesture;
}

catnip_handle catnip_ui_input_focused(void)
{
    return catnip_ui_input_focus(&g_in);
}

void catnip_ui_input_end(void)
{
    /* Every contact in flight and the ring with them, for the reason
     * press_gesture.h gives: a switch still held here must not carry its start
     * time into whatever comes next. */
    catnip_ui_input_reset(&g_in);
    if (!g_touch_indev) return;
    lv_indev_delete(g_touch_indev);
    g_touch_indev = nullptr;
}
