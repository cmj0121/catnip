/* Native test for Batch 1's BadUSB: the Ducky Script keymap and parser.
 *
 * There is no keyboard here and there does not need to be - what this checks is
 * the arithmetic that turns a script into a sequence of key events, which is
 * all the part of BadUSB that has no radio in it. A sample script is parsed and
 * asserted event by event against the US-QWERTY keymap, so a wrong usage or a
 * dropped modifier is caught on the host rather than on a machine being typed
 * at. See ducky.h. */
#include <stdio.h>

#include "ducky.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

static int key_is(unsigned char c, uint8_t usage, uint8_t mods)
{
    uint8_t u = 0, m = 0;
    return ducky_char_key(c, &u, &m) && u == usage && m == mods;
}

int main(void)
{
    /* ---- the keymap ----------------------------------------------------- */
    CHECK(key_is('a', 0x04, 0), "'a' is usage 0x04, no shift");
    CHECK(key_is('A', 0x04, DUCKY_MOD_SHIFT), "'A' is the same key with shift");
    CHECK(key_is('z', 0x1D, 0), "'z' is usage 0x1D");
    CHECK(key_is('1', 0x1E, 0), "'1' is usage 0x1E");
    CHECK(key_is('0', 0x27, 0), "'0' sits after 9, at 0x27");
    CHECK(key_is('!', 0x1E, DUCKY_MOD_SHIFT), "'!' is shift-1");
    CHECK(key_is(' ', 0x2C, 0), "space is 0x2C");
    CHECK(key_is('/', 0x38, 0), "'/' is 0x38");
    CHECK(key_is('?', 0x38, DUCKY_MOD_SHIFT), "'?' is shift-'/'");
    CHECK(key_is('-', 0x2D, 0) && key_is('_', 0x2D, DUCKY_MOD_SHIFT),
          "'-' and '_' share a key");
    {
        uint8_t u = 0xFF, m = 0xFF;
        /* A byte with no key on a US keyboard is refused rather than typed as
         * something else - the parser drops it. */
        CHECK(ducky_char_key(0x80, &u, &m) == 0, "an untypable byte returns 0");
    }

    /* ---- a sample script, event by event -------------------------------- */
    /* A REM to skip, a GUI chord, a pause, a typed word, a named key, a
     * three-key chord, and a STRINGLN that ends in Enter - one of most of the
     * things the subset understands. */
    static const char SCRIPT[] = "REM open a run box and type\n"
                                 "GUI r\n"
                                 "DELAY 500\n"
                                 "STRING notepad\n"
                                 "ENTER\n"
                                 "CTRL ALT DELETE\n"
                                 "STRINGLN Hi!\n";

    static const ducky_event WANT[] = {
        {DUCKY_KEY, DUCKY_MOD_GUI, 0x15, 0},                  /* GUI r */
        {DUCKY_DELAY, 0, 0, 500},                             /* DELAY 500 */
        {DUCKY_KEY, 0, 0x11, 0},                              /* n */
        {DUCKY_KEY, 0, 0x12, 0},                              /* o */
        {DUCKY_KEY, 0, 0x17, 0},                              /* t */
        {DUCKY_KEY, 0, 0x08, 0},                              /* e */
        {DUCKY_KEY, 0, 0x13, 0},                              /* p */
        {DUCKY_KEY, 0, 0x04, 0},                              /* a */
        {DUCKY_KEY, 0, 0x07, 0},                              /* d */
        {DUCKY_KEY, 0, 0x28, 0},                              /* ENTER */
        {DUCKY_KEY, DUCKY_MOD_CTRL | DUCKY_MOD_ALT, 0x4C, 0}, /* CTRL ALT DELETE */
        {DUCKY_KEY, DUCKY_MOD_SHIFT, 0x0B, 0},                /* H */
        {DUCKY_KEY, 0, 0x0C, 0},                              /* i */
        {DUCKY_KEY, DUCKY_MOD_SHIFT, 0x1E, 0},                /* ! */
        {DUCKY_KEY, 0, 0x28, 0},                              /* STRINGLN's Enter */
    };
    const int want_n = (int)(sizeof(WANT) / sizeof(WANT[0]));

    ducky_event got[64];
    int n =
        ducky_parse(SCRIPT, sizeof(SCRIPT) - 1, got, (int)(sizeof(got) / sizeof(got[0])));
    CHECK(n == want_n, "the script parses to the expected number of events");
    if (n == want_n) {
        int all = 1;
        for (int i = 0; i < n; i++) {
            if (got[i].kind != WANT[i].kind || got[i].mods != WANT[i].mods ||
                got[i].usage != WANT[i].usage || got[i].delay_ms != WANT[i].delay_ms) {
                printf("    event %d: got {kind=%u mods=%u usage=0x%02X delay=%u}\n", i,
                       got[i].kind, got[i].mods, got[i].usage, got[i].delay_ms);
                all = 0;
            }
        }
        CHECK(all, "and every event matches the US-QWERTY keymap");
    }

    /* A lone modifier line is a modifier-only tap: mods set, usage 0. */
    {
        ducky_event e[4];
        int m = ducky_parse("GUI\n", 4, e, 4);
        CHECK(m == 1 && e[0].kind == DUCKY_KEY && e[0].mods == DUCKY_MOD_GUI &&
                  e[0].usage == 0,
              "a lone GUI is a modifier tap with no key");
    }

    /* An unknown command line is dropped, not guessed at. */
    {
        ducky_event e[4];
        int m = ducky_parse("WOBBLE the thing\n", 17, e, 4);
        CHECK(m == 0, "an unknown command produces nothing");
    }

    /* Too many events for the buffer refuses the whole run rather than typing a
     * truncated payload. */
    {
        ducky_event e[3];
        int m = ducky_parse("STRING abcdef\n", 14, e, 3);
        CHECK(m == -1, "a script that overflows the buffer is refused, not half-run");
    }

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
