/* ducky.c - see ducky.h. A keymap and a line parser, and nothing that types. */
#include "ducky.h"

int ducky_char_key(unsigned char c, uint8_t *usage, uint8_t *mods)
{
    uint8_t u = 0, m = 0;

    if (c >= 'a' && c <= 'z') {
        u = (uint8_t)(0x04 + (c - 'a'));
    } else if (c >= 'A' && c <= 'Z') {
        u = (uint8_t)(0x04 + (c - 'A'));
        m = DUCKY_MOD_SHIFT;
    } else if (c >= '1' && c <= '9') {
        u = (uint8_t)(0x1E + (c - '1'));
    } else {
        switch (c) {
        /* The digit row. 0 is the odd one out - its usage sits after 9, not
         * before 1 - and its shifted twin ')' is the one shift on the row that
         * is not the digit above the symbol. */
        case '0': u = 0x27; break;
        case '!':
            u = 0x1E;
            m = DUCKY_MOD_SHIFT;
            break;
        case '@':
            u = 0x1F;
            m = DUCKY_MOD_SHIFT;
            break;
        case '#':
            u = 0x20;
            m = DUCKY_MOD_SHIFT;
            break;
        case '$':
            u = 0x21;
            m = DUCKY_MOD_SHIFT;
            break;
        case '%':
            u = 0x22;
            m = DUCKY_MOD_SHIFT;
            break;
        case '^':
            u = 0x23;
            m = DUCKY_MOD_SHIFT;
            break;
        case '&':
            u = 0x24;
            m = DUCKY_MOD_SHIFT;
            break;
        case '*':
            u = 0x25;
            m = DUCKY_MOD_SHIFT;
            break;
        case '(':
            u = 0x26;
            m = DUCKY_MOD_SHIFT;
            break;
        case ')':
            u = 0x27;
            m = DUCKY_MOD_SHIFT;
            break;
        /* Whitespace a STRING can carry. A newline inside a STRING is not a
         * thing a normal script writes, but if one arrives it is Enter, not a
         * dropped byte. */
        case ' ': u = 0x2C; break;
        case '\t': u = 0x2B; break;
        case '\n': u = 0x28; break;
        /* The punctuation keys, each with the symbol shift puts on it. */
        case '-': u = 0x2D; break;
        case '_':
            u = 0x2D;
            m = DUCKY_MOD_SHIFT;
            break;
        case '=': u = 0x2E; break;
        case '+':
            u = 0x2E;
            m = DUCKY_MOD_SHIFT;
            break;
        case '[': u = 0x2F; break;
        case '{':
            u = 0x2F;
            m = DUCKY_MOD_SHIFT;
            break;
        case ']': u = 0x30; break;
        case '}':
            u = 0x30;
            m = DUCKY_MOD_SHIFT;
            break;
        case '\\': u = 0x31; break;
        case '|':
            u = 0x31;
            m = DUCKY_MOD_SHIFT;
            break;
        case ';': u = 0x33; break;
        case ':':
            u = 0x33;
            m = DUCKY_MOD_SHIFT;
            break;
        case '\'': u = 0x34; break;
        case '"':
            u = 0x34;
            m = DUCKY_MOD_SHIFT;
            break;
        case '`': u = 0x35; break;
        case '~':
            u = 0x35;
            m = DUCKY_MOD_SHIFT;
            break;
        case ',': u = 0x36; break;
        case '<':
            u = 0x36;
            m = DUCKY_MOD_SHIFT;
            break;
        case '.': u = 0x37; break;
        case '>':
            u = 0x37;
            m = DUCKY_MOD_SHIFT;
            break;
        case '/': u = 0x38; break;
        case '?':
            u = 0x38;
            m = DUCKY_MOD_SHIFT;
            break;
        default: return 0;
        }
    }

    if (usage) *usage = u;
    if (mods) *mods = m;
    return 1;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Uppercase-fold compare of the token [s, e) against an ASCII keyword. */
static int tok_eq(const char *s, const char *e, const char *kw)
{
    for (; s < e && *kw; s++, kw++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != *kw) return 0;
    }
    return s == e && *kw == '\0';
}

/* A modifier word, into its bit. */
static int mod_of_token(const char *s, const char *e, uint8_t *m)
{
    if (tok_eq(s, e, "CTRL") || tok_eq(s, e, "CONTROL")) {
        *m = DUCKY_MOD_CTRL;
        return 1;
    }
    if (tok_eq(s, e, "SHIFT")) {
        *m = DUCKY_MOD_SHIFT;
        return 1;
    }
    if (tok_eq(s, e, "ALT")) {
        *m = DUCKY_MOD_ALT;
        return 1;
    }
    if (tok_eq(s, e, "GUI") || tok_eq(s, e, "WINDOWS") || tok_eq(s, e, "WIN") ||
        tok_eq(s, e, "COMMAND")) {
        *m = DUCKY_MOD_GUI;
        return 1;
    }
    return 0;
}

/* A named key word, into its HID usage. */
static int named_key(const char *s, const char *e, uint8_t *u)
{
    static const struct {
        const char *k;
        uint8_t u;
    } tbl[] = {
        {"ENTER", 0x28},      {"RETURN", 0x28},    {"ESC", 0x29},
        {"ESCAPE", 0x29},     {"BACKSPACE", 0x2A}, {"TAB", 0x2B},
        {"SPACE", 0x2C},      {"DELETE", 0x4C},    {"DEL", 0x4C},
        {"INSERT", 0x49},     {"HOME", 0x4A},      {"END", 0x4D},
        {"PAGEUP", 0x4B},     {"PAGEDOWN", 0x4E},  {"UP", 0x52},
        {"UPARROW", 0x52},    {"DOWN", 0x51},      {"DOWNARROW", 0x51},
        {"LEFT", 0x50},       {"LEFTARROW", 0x50}, {"RIGHT", 0x4F},
        {"RIGHTARROW", 0x4F}, {"CAPSLOCK", 0x39},  {"F1", 0x3A},
        {"F2", 0x3B},         {"F3", 0x3C},        {"F4", 0x3D},
        {"F5", 0x3E},         {"F6", 0x3F},        {"F7", 0x40},
        {"F8", 0x41},         {"F9", 0x42},        {"F10", 0x43},
        {"F11", 0x44},        {"F12", 0x45},
    };
    size_t i;
    for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (tok_eq(s, e, tbl[i].k)) {
            *u = tbl[i].u;
            return 1;
        }
    return 0;
}

/* A key token after a modifier: a named key, or a single character. A shifted
 * character folds its own shift into `extra` so `GUI +` is GUI+Shift+=. */
static int key_token(const char *s, const char *e, uint8_t *u, uint8_t *extra)
{
    uint8_t nu = 0;
    if (named_key(s, e, &nu)) {
        *u = nu;
        return 1;
    }
    if (e - s == 1) {
        uint8_t ku = 0, km = 0;
        if (ducky_char_key((unsigned char)s[0], &ku, &km)) {
            *u = ku;
            *extra = (uint8_t)(*extra | km);
            return 1;
        }
    }
    return 0;
}

static int push(ducky_event *out, int max, int *n, uint8_t kind, uint8_t mods,
                uint8_t usage, uint16_t delay_ms)
{
    if (*n >= max) return 0;
    out[*n].kind = kind;
    out[*n].mods = mods;
    out[*n].usage = usage;
    out[*n].delay_ms = delay_ms;
    (*n)++;
    return 1;
}

int ducky_parse(const char *text, size_t len, ducky_event *out, int max)
{
    int n = 0;
    int overflow = 0;
    size_t i = 0;

    while (i < len) {
        size_t ls = i, le, p;
        const char *tok_s, *tok_e;

        while (i < len && text[i] != '\n')
            i++;
        le = i;
        if (i < len) i++; /* step over the newline */
        while (le > ls && text[le - 1] == '\r')
            le--;

        p = ls;
        while (p < le && (text[p] == ' ' || text[p] == '\t'))
            p++;
        if (p >= le) continue; /* blank line */

        {
            size_t ts = p;
            while (p < le && !is_space(text[p]))
                p++;
            tok_s = text + ts;
            tok_e = text + p;
        }

        if (tok_eq(tok_s, tok_e, "REM")) continue; /* a comment */

        if (tok_eq(tok_s, tok_e, "STRING") || tok_eq(tok_s, tok_e, "STRINGLN")) {
            int ln = tok_eq(tok_s, tok_e, "STRINGLN");
            size_t k, sp = p;
            if (sp < le && text[sp] == ' ') sp++; /* one separating space */
            for (k = sp; k < le; k++) {
                uint8_t u = 0, m = 0;
                if (ducky_char_key((unsigned char)text[k], &u, &m))
                    if (!push(out, max, &n, DUCKY_KEY, m, u, 0)) {
                        overflow = 1;
                        goto done;
                    }
            }
            if (ln && !push(out, max, &n, DUCKY_KEY, 0, 0x28, 0)) {
                overflow = 1;
                goto done;
            }
            continue;
        }

        if (tok_eq(tok_s, tok_e, "DELAY")) {
            long v = 0;
            int seen = 0;
            size_t k = p;
            while (k < le && is_space(text[k]))
                k++;
            while (k < le && text[k] >= '0' && text[k] <= '9') {
                v = v * 10 + (text[k] - '0');
                if (v > 65535)
                    v = 65535; /* the field's ceiling; a longer pause is clamped */
                seen = 1;
                k++;
            }
            if (seen && !push(out, max, &n, DUCKY_DELAY, 0, 0, (uint16_t)v)) {
                overflow = 1;
                goto done;
            }
            continue;
        }

        {
            uint8_t mbit = 0;
            if (mod_of_token(tok_s, tok_e, &mbit)) {
                /* A chord: one or more modifiers, then at most one key. */
                uint8_t mods = mbit, usage = 0, extra = 0;
                int have_key = 0;
                while (p < le) {
                    size_t ns;
                    const char *ks, *ke;
                    uint8_t mb = 0;
                    while (p < le && is_space(text[p]))
                        p++;
                    if (p >= le) break;
                    ns = p;
                    while (p < le && !is_space(text[p]))
                        p++;
                    ks = text + ns;
                    ke = text + p;
                    if (mod_of_token(ks, ke, &mb)) {
                        mods = (uint8_t)(mods | mb);
                        continue;
                    }
                    if (key_token(ks, ke, &usage, &extra)) have_key = 1;
                    break; /* the first non-modifier is the key; ignore the rest */
                }
                mods = (uint8_t)(mods | extra);
                if (!push(out, max, &n, DUCKY_KEY, mods, have_key ? usage : 0, 0)) {
                    overflow = 1;
                    goto done;
                }
                continue;
            }
        }

        {
            /* A standalone named key: ENTER, TAB, an arrow, DELETE, F5. An
             * unknown word is dropped. */
            uint8_t u = 0, extra = 0;
            if (key_token(tok_s, tok_e, &u, &extra))
                if (!push(out, max, &n, DUCKY_KEY, extra, u, 0)) {
                    overflow = 1;
                    goto done;
                }
        }
    }

done:
    return overflow ? -1 : n;
}
