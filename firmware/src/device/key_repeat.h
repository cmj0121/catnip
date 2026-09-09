/*
 * key_repeat.h - a held direction key keeps going, lifted out of the driver.
 *
 * The four directions were read as edges: one press, one step. That is right
 * for a list of eight files and wrong the moment a value has sixty of
 * something in it - setting a clock a minute at a time is fifty-nine presses,
 * and nobody makes fifty-nine presses. They give up, which is what "the UI is
 * hard to modify anything" means.
 *
 * So a held key repeats, on the convention every keyboard has used for forty
 * years: the first step happens the instant it goes down, then nothing for long
 * enough that a deliberate single press cannot become two, then a steady
 * stream.
 *
 * Plain C with no board in it, so the timing can be held still by a host test -
 * the same arrangement press_gesture.c and swipe.c are in, and for the same
 * reason: this is a convention, and a convention is worth pinning.
 */
#ifndef CATNIP_KEY_REPEAT_H
#define CATNIP_KEY_REPEAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Before the first repeat. Long enough that a press meant as one press is one
 * press: the longest deliberate tap is well under this, and a user holding a
 * key this long is holding it on purpose. */
#define CATNIP_REPEAT_DELAY_MS 400u

/* Between repeats afterwards. Nine a second: fast enough to cross sixty minutes
 * without waiting, slow enough that letting go within a step of the wanted
 * value is possible. */
#define CATNIP_REPEAT_EVERY_MS 110u

typedef struct {
    bool down;
    bool repeating; /* the delay has been served, so the stream is running */
    unsigned since; /* when the key went down, or when the last step was */
} catnip_repeat;

/* One sample. Returns true on the pass a step should be taken: once the instant
 * the key goes down, then again every CATNIP_REPEAT_EVERY_MS once it has been
 * held for CATNIP_REPEAT_DELAY_MS.
 *
 * A key that is not down resets, so a tap always costs exactly one step however
 * long the key was held the time before. */
bool catnip_repeat_step(catnip_repeat *r, bool down, unsigned now);

/* Whether the step this pass produced was the *first* of a press rather than
 * one of the repeats that followed.
 *
 * Some shapes want the key and not the repeat: a carousel step hides one cell
 * and shows another, which on a full-screen render mode is the whole panel
 * blitted, so holding left would pin the bus rather than walk a ring of four.
 * Asked rather than configured, because whether repeating is wanted is a
 * property of what the key is moving, and only the caller knows that. */
bool catnip_repeat_first(const catnip_repeat *r);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_KEY_REPEAT_H */
