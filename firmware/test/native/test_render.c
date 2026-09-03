/* Native test for issue #30: the ui tree renderer core. A recording backend
 * captures what the traversal would draw; the LVGL backend (device) is a shim
 * over the same vtable. */
#include <stdio.h>
#include <string.h>

#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"

static char g_ops[512];
static void rec(const char *s)
{
    strncat(g_ops, s, sizeof(g_ops) - strlen(g_ops) - 1);
}
static void b_begin(void *ud) { (void)ud; rec("[begin]"); }
static void b_label(void *ud, const char *id, const char *text)
{
    (void)ud;
    char line[128];
    snprintf(line, sizeof(line), "label(%s='%s')", id, text);
    rec(line);
}
static void b_button(void *ud, const char *id, const char *text)
{
    (void)ud;
    char line[128];
    snprintf(line, sizeof(line), "button(%s='%s')", id, text);
    rec(line);
}
static void b_end(void *ud) { (void)ud; rec("[end]"); }

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);

    catnip_render_backend be = {NULL, b_begin, b_label, b_button, b_end};

    /* No screen yet -> nothing to render. */
    CHECK(catnip_render(rt, &be) == -1, "no screen renders nothing");

    catnip_rt_dostring(rt,
                       "ui.screen{\n"
                       "  ui.label{ id = 'title', text = 'Hello' },\n"
                       "  ui.button{ id = 'go', text = 'Go' },\n"
                       "}\n",
                       "=scr");

    g_ops[0] = '\0';
    int n = catnip_render(rt, &be);
    CHECK(n == 2, "renders both widgets");
    CHECK(strcmp(g_ops, "[begin]label(title='Hello')button(go='Go')[end]") == 0,
          "traversal drives the backend in order");

    /* A property change is reflected on the next render. */
    catnip_rt_dostring(rt, "ui.get('title').text = 'Bye'", "=upd");
    g_ops[0] = '\0';
    catnip_render(rt, &be);
    CHECK(strstr(g_ops, "label(title='Bye')") != NULL,
          "renderer sees updated properties");

    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
