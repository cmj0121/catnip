/*
 * Native test for the action bar: what long A produces, and what the two
 * buttons mean once it is up.
 *
 * The whole value of the bar being the platform's is that every app's Delete
 * looks and behaves identically, so what is pinned here is the behaviour and
 * not the drawing: which action A runs, which one B runs, which one B refuses
 * to run, and where the line between the two shapes falls.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_bar.h"

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

static int is(const char *got, const char *want)
{
    if (!got || !want) return got == want;
    return strcmp(got, want) == 0;
}

static const catnip_action CATALOGUE[] = {
    {"open", "Open", "file", false},
    {"rename", "Rename", "edit", false},
    {"delete", "Delete", "trash", true},
    {"cancel", "Cancel", "close", false},
};
#define CATALOGUE_N ((int)(sizeof(CATALOGUE) / sizeof(CATALOGUE[0])))

static void put(catnip_bar *b, const char *const *ids, int n)
{
    catnip_action items[CATNIP_BAR_MAX];
    int k = 0;

    for (int i = 0; i < n && k < CATNIP_BAR_MAX; i++) {
        const catnip_action *a = catnip_bar_find(CATALOGUE, CATALOGUE_N, ids[i]);
        if (a) items[k++] = *a;
    }
    catnip_bar_open(b, items, k, (catnip_handle)7, 3);
}

int main(void)
{
    catnip_bar b;

    memset(&b, 0, sizeof(b));

    printf("nothing offered is no bar\n");
    catnip_bar_open(&b, NULL, 0, (catnip_handle)7, 0);
    CHECK(!catnip_bar_up(&b), "an app that answers with nothing gets no bar");
    CHECK(catnip_bar_activate(&b) == NULL, "and A means nothing while it is down");

    printf("an id the manifest never declared is dropped, not drawn\n");
    {
        static const char *const ids[] = {"open", "teleport"};
        put(&b, ids, 2);
        CHECK(catnip_bar_up(&b) && b.n == 1,
              "one of the two is in the catalogue and the other is not");
        CHECK(is(catnip_bar_activate(&b), "open"), "A runs the one that is");
    }

    printf("two actions are the two buttons, and take no directions\n");
    {
        static const char *const ids[] = {"open", "cancel"};
        put(&b, ids, 2);
        CHECK(!catnip_bar_modal(&b), "two is not a modal");
        CHECK(is(catnip_bar_activate(&b), "open"), "the left one is A");
        CHECK(is(catnip_bar_cancel(&b), "cancel"), "and the right one is B");
        catnip_bar_step(&b, 1);
        CHECK(is(catnip_bar_activate(&b), "open"),
              "right steps nothing, because both are already bound to a button");
        CHECK(is(catnip_bar_tap(&b, 1), "cancel"),
              "a finger runs the right one directly, which is its only route to it");
    }

    printf("B is the escape, so it is never bound to something that cannot be undone\n");
    {
        static const char *const ids[] = {"open", "delete"};
        put(&b, ids, 2);
        CHECK(b.n == 2, "the app offered both");
        CHECK(catnip_bar_cancel(&b) == NULL, "B refuses to carry the destructive one");
        /* And so the bar cannot be said as two buttons, and steps instead: an
         * action nothing can reach is not an action. */
        CHECK(catnip_bar_modal(&b), "which makes two of them a modal");
        CHECK(is(catnip_bar_activate(&b), "open"), "it opens on the first");
        catnip_bar_step(&b, 1);
        CHECK(is(catnip_bar_activate(&b), "delete"), "and right reaches the other");
        CHECK(is(catnip_bar_tap(&b, 1), "delete"),
              "a finger may run it directly: the guard is about the reflex on B");
    }

    printf("three is a modal: it takes left and right, and B is the way out\n");
    {
        static const char *const ids[] = {"open", "rename", "delete"};
        put(&b, ids, 3);
        CHECK(catnip_bar_modal(&b), "three is a modal");
        CHECK(is(catnip_bar_activate(&b), "open"), "it opens on the first");
        catnip_bar_step(&b, 1);
        CHECK(is(catnip_bar_activate(&b), "rename"), "right steps to the next");
        catnip_bar_step(&b, 1);
        CHECK(is(catnip_bar_activate(&b), "delete"), "and again");
        catnip_bar_step(&b, 1);
        CHECK(is(catnip_bar_activate(&b), "delete"), "and clamps at the end");
        catnip_bar_step(&b, -3);
        CHECK(is(catnip_bar_activate(&b), "open"), "and at the start");
        CHECK(catnip_bar_cancel(&b) == NULL,
              "B leaves a modal rather than running one of the three");
    }

    printf("it carries who asked and about what\n");
    {
        static const char *const ids[] = {"open", "cancel"};
        put(&b, ids, 2);
        CHECK(b.owner == (catnip_handle)7 && b.index == 3,
              "the list the long press landed on, and the row it was about");
        catnip_bar_close(&b);
        CHECK(!catnip_bar_up(&b), "and closing it puts it all down");
        CHECK(b.owner == CATNIP_HANDLE_NONE,
              "including who asked, so a stale answer cannot be delivered");
    }

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
