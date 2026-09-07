/* input_names.c - see input_names.h. */
#include "input_names.h"

#include <stddef.h>

/* Not strcasecmp: that is POSIX rather than C99, and this file is compiled by
 * the host build and by the device toolchain both. Folding the two bytes here
 * is shorter than arranging for a header that declares it in either place. */
static int equal_ignoring_case(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static const struct {
    const char *name;
    catnip_button button;
} kNames[] = {
    {"up", CATNIP_BTN_UP},         {"down", CATNIP_BTN_DOWN},
    {"left", CATNIP_BTN_LEFT},     {"right", CATNIP_BTN_RIGHT},
    {"centre", CATNIP_BTN_CENTRE}, {"center", CATNIP_BTN_CENTRE},
    {"a", CATNIP_BTN_A},           {"b", CATNIP_BTN_B},
};

catnip_button catnip_button_from_name(const char *name)
{
    size_t i;

    if (!name) return CATNIP_BTN_COUNT;
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (equal_ignoring_case(kNames[i].name, name)) return kNames[i].button;
    }
    return CATNIP_BTN_COUNT;
}
