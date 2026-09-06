/*
 * input_debounce.h - the bounce filter behind the button driver.
 *
 * Kept apart from input.cpp, and free of Arduino.h, for one reason: the clock
 * is an argument rather than a call to millis(), so the whole filter can be
 * driven from a host test with a made-up clock and no hardware. That leaves
 * input.cpp as pin numbers and digitalRead() and nothing else worth testing.
 */
#ifndef CATNIP_INPUT_DEBOUNCE_H
#define CATNIP_INPUT_DEBOUNCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One switch's filtered state. Treat the fields as private; the functions
 * below are the interface. It is a plain struct so the driver can hold an
 * array of them without allocating anything. */
typedef struct {
    bool down;         /* the filtered level: true while the switch is held */
    bool candidate;    /* the raw level as it last read */
    bool pressed;      /* edge seen and not yet read */
    bool released;     /* edge seen and not yet read */
    uint32_t since_ms; /* when the raw level last became `candidate` */
} catnip_debounce;

/* Start from "not pressed" at time `now_ms`. */
void catnip_debounce_init(catnip_debounce *d, uint32_t now_ms);

/* Feed one raw sample. `raw_down` is the switch as the pin reads it right now,
 * already turned the right way up by the caller, and `now_ms` is a monotonic
 * millisecond clock.
 *
 * A change is accepted only once the new raw level has held steady for
 * CATNIP_INPUT_DEBOUNCE_MS; a level that flickers before then restarts the
 * count and is never reported at all. That costs the window in latency on
 * every edge, and the trace in board.h is why it is worth paying: the release
 * measured at t=256792 was followed by a re-close 30 ms later that stayed
 * closed for 160 ms. A filter that took each edge at once and then locked out
 * changes for 50 ms would have accepted the release, then accepted the
 * re-close as well, and reported two presses where the human made one. Waiting
 * for the level to settle swallows the whole excursion, which is what it is.
 *
 * The clock is compared by unsigned subtraction, which stays correct across
 * the wrap of millis() every 49 days. */
void catnip_debounce_update(catnip_debounce *d, bool raw_down, uint32_t now_ms);

/* True while the switch is held, after filtering. */
bool catnip_debounce_down(const catnip_debounce *d);

/* True once per press, and once per release: the edge is cleared as it is
 * read, so it reaches exactly one caller. This is how
 * catnip_pmu_power_key_pressed() already behaves, and it means a caller that
 * polls less often than the driver still sees the edge instead of missing it
 * between two of its own calls. */
bool catnip_debounce_take_pressed(catnip_debounce *d);
bool catnip_debounce_take_released(catnip_debounce *d);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_INPUT_DEBOUNCE_H */
