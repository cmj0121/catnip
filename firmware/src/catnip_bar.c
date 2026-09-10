/* catnip_bar.c - see catnip_bar.h. */
#include "catnip_bar.h"

#include <string.h>

void catnip_bar_open(catnip_bar *b, const catnip_action *items, int n,
                     catnip_handle owner, int index)
{
    int i;

    if (!b) return;
    if (!items || n <= 0) {
        catnip_bar_close(b);
        return;
    }
    if (n > CATNIP_BAR_MAX) n = CATNIP_BAR_MAX;
    for (i = 0; i < n; i++)
        b->items[i] = items[i];
    b->n = n;
    /* The first, which in the two-icon shape is also the one A is bound to -
     * one rule, and the shapes agree about where A points. */
    b->focus = 0;
    b->owner = owner;
    b->index = index;
    b->up = true;
}

void catnip_bar_close(catnip_bar *b)
{
    if (!b) return;
    b->up = false;
    b->n = 0;
    b->focus = 0;
    b->owner = CATNIP_HANDLE_NONE;
    b->index = CATNIP_INDEX_NONE;
}

bool catnip_bar_up(const catnip_bar *b)
{
    return b && b->up && b->n > 0;
}

bool catnip_bar_modal(const catnip_bar *b)
{
    if (!catnip_bar_up(b)) return false;
    if (b->n >= 3) return true;
    /* Two, with the second one destructive: B will not carry it, so nothing
     * would reach it and the bar steps instead. */
    return b->n == 2 && b->items[1].destructive;
}

void catnip_bar_step(catnip_bar *b, int dir)
{
    if (!catnip_bar_modal(b)) return;
    b->focus += dir;
    if (b->focus < 0) b->focus = 0;
    if (b->focus >= b->n) b->focus = b->n - 1;
}

const char *catnip_bar_activate(const catnip_bar *b)
{
    if (!catnip_bar_up(b)) return NULL;
    return b->items[catnip_bar_modal(b) ? b->focus : 0].id;
}

const char *catnip_bar_cancel(const catnip_bar *b)
{
    if (!catnip_bar_up(b)) return NULL;
    /* A modal's B is how you leave it, and leaving is not one of the three
     * things on offer - it is the fourth, and it is the one that must never be
     * a thing an app can spend. */
    if (catnip_bar_modal(b) || b->n < 2) return NULL;
    if (b->items[1].destructive) return NULL;
    return b->items[1].id;
}

const char *catnip_bar_tap(const catnip_bar *b, int cell)
{
    if (!catnip_bar_up(b) || cell < 0 || cell >= b->n) return NULL;
    /* A finger runs what it landed on, destructive or not: it did not land
     * there by reflex the way B is pressed by reflex, and the guard on B is
     * about the reflex rather than about the action. */
    return b->items[cell].id;
}

int catnip_bar_offer(catnip_bar *b, catnip_rt *rt, const catnip_action *catalogue,
                     int n_catalogue, catnip_handle owner, int index)
{
    char ids[CATNIP_ACTIONS_MAX][CATNIP_ACTION_ID_MAX];
    catnip_action items[CATNIP_BAR_MAX];
    int n, i, k = 0;

    if (!b) return 0;
    n = catnip_render_take_actions(rt, ids, CATNIP_ACTIONS_MAX);
    if (n <= 0) return 0;
    for (i = 0; i < n && k < CATNIP_BAR_MAX; i++) {
        const catnip_action *a = catnip_bar_find(catalogue, n_catalogue, ids[i]);
        if (a) items[k++] = *a;
    }
    catnip_bar_open(b, items, k, owner, index);
    return k;
}

const catnip_action *catnip_bar_find(const catnip_action *catalogue, int n,
                                     const char *id)
{
    int i;

    if (!catalogue || !id) return NULL;
    for (i = 0; i < n; i++)
        if (strcmp(catalogue[i].id, id) == 0) return &catalogue[i];
    return NULL;
}
