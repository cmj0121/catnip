/* Native test for issue #30: the ui tree renderer core. A recording backend
 * captures what the traversal would draw; the LVGL backend (device) is a shim
 * over the same vtable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lua.h"

static char g_ops[512];
static void rec(const char *s)
{
    strncat(g_ops, s, sizeof(g_ops) - strlen(g_ops) - 1);
}
static void b_begin(void *ud)
{
    (void)ud;
    rec("[begin]");
}
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
static void b_end(void *ud)
{
    (void)ud;
    rec("[end]");
}

/* A Lua allocator that turns "the renderer handed out a string Lua had already
 * collected" from undefined behaviour into something a test can assert on: every
 * block Lua frees is first overwritten with a byte that appears in no expected
 * text. The block is then deliberately not returned to the C allocator, because
 * a later allocation reusing it would overwrite the poison before the assertion
 * could read it. The process is short-lived, so the leak is the cheap half. */
static void *poison_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (nsize == 0) {
        /* Per the Lua contract osize is only a byte count when ptr != NULL. */
        if (ptr) memset(ptr, 0xA5, osize);
        return NULL;
    }
    return realloc(ptr, nsize);
}

/* A backend that collects between the renderer reading a node's strings and the
 * backend using them, which is exactly the window those strings must survive. */
typedef struct {
    lua_State *L;
    int calls;
    int id_ok;
    int text_ok;
} gc_probe;

static void probe_label(void *ud, const char *id, const char *text)
{
    gc_probe *p = (gc_probe *)ud;
    p->calls++;
    lua_gc(p->L, LUA_GCCOLLECT, 0);
    /* Bounded compares: had these strings died they would point into poisoned
     * memory with no terminator, and an unbounded read would run off the block. */
    p->id_ok = strncmp(id, "12345", sizeof("12345") - 1) == 0;
    p->text_ok = strncmp(text, "987654321", sizeof("987654321") - 1) == 0;
}

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

    /* The strings a backend is handed have to outlive a collection landing in
     * the middle of the pass. Numbers are what makes that lifetime visible:
     * lua_tostring converts the value in place, so the string it produces lives
     * only in the stack slot the renderer is holding - the widget tree keeps the
     * number, not the text. Let go of that slot too early and the backend is
     * reading freed memory. */
    catnip_rt *grt = catnip_rt_new_alloc(poison_alloc, NULL);
    catnip_ui_open(grt);
    gc_probe probe = {catnip_rt_lua(grt), 0, 0, 0};
    catnip_rt_dostring(grt, "ui.screen{ ui.label{ id = 12345, text = 987654321 } }",
                       "=gc");
    catnip_render_backend gbe = {&probe, NULL, probe_label, NULL, NULL};
    CHECK(catnip_render(grt, &gbe) == 1, "gc probe renders the one widget");
    CHECK(probe.calls == 1, "gc probe backend was called");
    CHECK(probe.id_ok, "id survives a collection during the backend call");
    CHECK(probe.text_ok, "text survives a collection during the backend call");
    catnip_rt_free(grt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
