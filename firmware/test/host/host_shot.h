/*
 * host_shot.h - write the last rendered frame out as a picture (#81).
 *
 * The third thing testing without a board buys, after the tree and the
 * geometry: being able to look. It is not an assertion and nothing fails
 * because of it - what a page *should* look like is a judgement, and a golden
 * image would turn every deliberate redesign into a failing test. This is for
 * the moment when the geometry all passes and the page is still wrong.
 */
#ifndef CATNIP_HOST_SHOT_H
#define CATNIP_HOST_SHOT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write the last frame LVGL rendered to `path` as a PNG. */
bool catnip_host_shot(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_HOST_SHOT_H */
