/*
 * ducky.h - a Ducky Script reader, with no keyboard in it (Batch 1, BadUSB).
 *
 * The split this file draws is the same one the radio apps draw: what can be
 * tested on a host without the hardware lives here, and the hardware lives
 * behind the HAL. A Ducky script is text; turning it into a sequence of key
 * events is arithmetic - a US-QWERTY keymap and a line parser - and none of it
 * touches USB. So it is plain C, compiled into the host tests, and the HID
 * keyboard it eventually types on is reached through service.usb.* (catnip_hal.h).
 *
 * What is understood is a deliberate subset: STRING, STRINGLN, ENTER, DELAY,
 * the modifier words GUI/WINDOWS, CTRL, ALT, SHIFT (alone or leading a chord),
 * the arrow keys, TAB, ESC, DELETE/BACKSPACE, the function keys, and REM
 * comments. An unknown line is dropped rather than guessed at - a payload that
 * quietly types the wrong thing is worse than one that skips a line it did not
 * understand.
 */
#ifndef CATNIP_DUCKY_H
#define CATNIP_DUCKY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HID keyboard modifier bits, as they sit in the first byte of a boot-keyboard
 * report. Only the left-hand modifiers are used; a script cannot tell the two
 * sides apart and no host cares which it is told. */
#define DUCKY_MOD_CTRL  0x01
#define DUCKY_MOD_SHIFT 0x02
#define DUCKY_MOD_ALT   0x04
#define DUCKY_MOD_GUI   0x08

/* One step of a run: a key to tap, or a pause to hold. */
typedef enum {
    DUCKY_KEY = 0,   /* hold `mods`, press `usage`, release - one keystroke */
    DUCKY_DELAY = 1, /* wait `delay_ms` before the next step */
} ducky_kind;

typedef struct {
    uint8_t kind;      /* ducky_kind */
    uint8_t mods;      /* modifier bitmask, for DUCKY_KEY */
    uint8_t usage;     /* HID usage id, for DUCKY_KEY (0 = a modifier-only tap) */
    uint16_t delay_ms; /* for DUCKY_DELAY */
} ducky_event;

/* US-QWERTY: which HID usage types the byte `c`, and which modifiers are held
 * to do it - shift for an uppercase letter or a symbol on the top row, nothing
 * for the rest. Returns 1 when the byte can be typed and fills `usage`/`mods`,
 * or 0 for a byte with no key on a US keyboard (which the parser then drops
 * rather than type as something else). Either out-pointer may be NULL. */
int ducky_char_key(unsigned char c, uint8_t *usage, uint8_t *mods);

/* Parse a whole script into a flat event sequence written to `out` (room for
 * `max`). Returns the number of events, or -1 when the script needs more than
 * `max` of them - and then nothing partial is meant to run, because a payload
 * typed halfway is worse than one refused outright. Lines are separated by '\n'
 * ('\r' is tolerated); a blank line and a REM line produce nothing. */
int ducky_parse(const char *text, size_t len, ducky_event *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DUCKY_H */
