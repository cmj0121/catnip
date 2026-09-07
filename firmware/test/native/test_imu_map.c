/* Native test for issue #43: the axis-to-edge map behind the IMU driver.
 *
 * WHAT THESE ASSERTIONS NOW MEAN. They were written as a falsifiable spec
 * around an assumption about how the part is mounted. That assumption has since
 * been measured: the device was turned so that each screen edge in turn pointed
 * at the ceiling, and the diagnostic page's arrow pointed at the ceiling all
 * four times. So the four edge cases below pin a measurement, and imu_map.c
 * carries the record of how it was taken.
 *
 * WHICH MEANS A FAILURE HERE IS A REGRESSION IN THE TABLE, not a hypothesis
 * being disproved. If "+X up names the bottom edge" starts failing, the table
 * has been changed away from what the device actually does, and the fix is to
 * put the table back - NOT to update the expectation until it passes again. The
 * expected names are written out in full rather than derived from the table
 * precisely so that they cannot follow it silently: a test derived from the
 * thing it tests would agree with any edit at all.
 *
 * The two flat cases are the exception, and they are marked where they appear.
 * The four edges were exercised on the device; screen-up and screen-down were
 * not, so those two assertions still pin an assumption rather than a reading.
 *
 * Around all six, these cases also pin the properties the page rests on: that
 * the table covers all six half-axes so no attitude falls through it, that the
 * dominant axis is picked by magnitude rather than by order, that the threshold
 * below which no edge is named actually holds, that the two flat attitudes are
 * named but carry no arrow direction, and that every named edge points somewhere
 * different. Those are exactly what would break if someone "simplified" the
 * lookup. */
#include <stdio.h>
#include <string.h>

#include "device/imu_map.h"

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

/* Resolve one attitude and check the name and the arrow that come back. */
static void expect(int32_t x, int32_t y, int32_t z, const char *want_name, int want_dx,
                   int want_dy, const char *what)
{
    int32_t mg[3];
    const catnip_imu_up *up;

    mg[0] = x;
    mg[1] = y;
    mg[2] = z;
    up = catnip_imu_up_edge(mg);

    if (up == NULL) {
        printf("  FAIL - %s: no edge was named, wanted \"%s\"\n", what, want_name);
        failures++;
        return;
    }
    if (strcmp(up->name, want_name) != 0) {
        printf("  FAIL - %s: named \"%s\", wanted \"%s\"\n", what, up->name, want_name);
        failures++;
        return;
    }
    if (up->dx != want_dx || up->dy != want_dy) {
        printf("  FAIL - %s: arrow (%d, %d), wanted (%d, %d)\n", what, up->dx, up->dy,
               want_dx, want_dy);
        failures++;
        return;
    }
    printf("  ok   - %s\n", what);
}

/* Every attitude the table is meant to name, one per half-axis. Gravity is
 * 1000 mg on the axis pointing at the ceiling and roughly nothing on the other
 * two, which is what a device held still against a wall or laid on a desk
 * actually reads. */
static void test_six_half_axes(void)
{
    printf("-- the six half-axes, each with a name and an arrow\n");
    /* These four were read off the device, one turn each. */
    expect(+1000, 0, 0, "bottom edge", 0, +1, "+X up names the bottom edge");
    expect(-1000, 0, 0, "top edge", 0, -1, "-X up names the top edge");
    expect(0, +1000, 0, "left edge", -1, 0, "+Y up names the left edge");
    expect(0, -1000, 0, "right edge", +1, 0, "-Y up names the right edge");
    /* These two were not. They still encode the assumption that +Z points out
     * through the glass; laying the device flat each way up and reading the
     * headline is what would confirm or swap them. */
    expect(0, 0, +1000, "flat, screen up", 0, 0, "+Z up is flat, screen up");
    expect(0, 0, -1000, "flat, screen down", 0, 0, "-Z up is flat, screen down");
}

/* The arrow points at the edge it names. This is the property the diagnostic
 * page rests on: the owner reads a word and sees a direction, and if those two
 * could disagree the page would be able to lie in a way nobody would catch. */
static void test_arrow_agrees_with_name(void)
{
    static const struct {
        const char *name;
        int dx, dy;
    } kWant[] = {
        {"top edge", 0, -1},
        {"bottom edge", 0, +1},
        {"left edge", -1, 0},
        {"right edge", +1, 0},
    };
    int32_t mg[3] = {0, 0, 0};
    int found = 0;
    int axis, sign, i;

    printf("-- every named edge has its own direction, and it is that edge's\n");
    for (axis = 0; axis < 2; axis++) {
        for (sign = -1; sign <= 1; sign += 2) {
            const catnip_imu_up *up;

            mg[0] = mg[1] = mg[2] = 0;
            mg[axis] = sign * 1000;
            up = catnip_imu_up_edge(mg);
            if (up == NULL) continue;
            for (i = 0; i < 4; i++) {
                if (strcmp(up->name, kWant[i].name) == 0 && up->dx == kWant[i].dx &&
                    up->dy == kWant[i].dy) {
                    found++;
                }
            }
        }
    }
    CHECK(found == 4, "the four screen edges are each named exactly once");
}

/* Nothing dominant enough to name. A device in mid-turn, or on a corner. */
static void test_no_dominant_axis(void)
{
    int32_t moving[3] = {200, -300, 150};
    int32_t just_under[3] = {CATNIP_IMU_UP_MIN_MG - 1, 0, 0};
    int32_t just_over[3] = {CATNIP_IMU_UP_MIN_MG, 0, 0};
    int32_t free_fall[3] = {0, 0, 0};

    printf("-- when no axis is dominant, no edge is named\n");
    CHECK(catnip_imu_up_edge(moving) == NULL, "a device in mid-turn names no edge");
    CHECK(catnip_imu_up_edge(free_fall) == NULL, "and neither does one reading zero");
    CHECK(catnip_imu_up_edge(just_under) == NULL,
          "one milli-g under the threshold names no edge");
    /* The boundary is checked from both sides because a >= that should have
     * been a > is the kind of edit that passes every other case here. */
    CHECK(catnip_imu_up_edge(just_over) != NULL, "and exactly at it, one is named");
}

/* The largest axis wins, not the first one that clears the threshold. A device
 * tilted off a face has two axes well above 500 mg, and reading the wrong one
 * of them would make the arrow flick to a neighbouring edge on a small tilt. */
static void test_largest_axis_wins(void)
{
    int32_t x_leads[3] = {900, 600, 0};
    int32_t y_leads[3] = {600, 900, 0};
    int32_t z_leads[3] = {600, 700, -900};
    const catnip_imu_up *up;

    printf("-- the dominant axis is the largest one, whichever it is\n");
    up = catnip_imu_up_edge(x_leads);
    CHECK(up != NULL && strcmp(up->name, "bottom edge") == 0,
          "X leading Y is read as X, though Y also clears the threshold");
    up = catnip_imu_up_edge(y_leads);
    CHECK(up != NULL && strcmp(up->name, "left edge") == 0,
          "Y leading X is read as Y, though X comes first");
    up = catnip_imu_up_edge(z_leads);
    CHECK(up != NULL && strcmp(up->name, "flat, screen down") == 0,
          "a negative Z leading both is read by magnitude, not by value");
}

/* Only the flat attitudes have no arrow. If a screen edge ever came back with
 * a zero direction the page would name it and then draw nothing, which looks
 * exactly like the part being absent - the one confusion the page exists to
 * prevent. */
static void test_only_flat_has_no_arrow(void)
{
    int32_t mg[3];
    int axis, sign;
    int edges_without_arrow = 0;
    int flats_with_arrow = 0;

    printf("-- an arrow is missing only where there is genuinely no edge up\n");
    for (axis = 0; axis < 3; axis++) {
        for (sign = -1; sign <= 1; sign += 2) {
            const catnip_imu_up *up;

            mg[0] = mg[1] = mg[2] = 0;
            mg[axis] = sign * 1000;
            up = catnip_imu_up_edge(mg);
            if (up == NULL) continue;
            if (up->dx == 0 && up->dy == 0) {
                if (axis != 2) edges_without_arrow++;
            } else {
                if (axis == 2) flats_with_arrow++;
            }
        }
    }
    CHECK(edges_without_arrow == 0, "every screen edge has a direction to point in");
    CHECK(flats_with_arrow == 0, "and neither flat attitude pretends to have one");
}

int main(void)
{
    printf("== imu_map: the axis-to-edge table ==\n");
    test_six_half_axes();
    test_arrow_agrees_with_name();
    test_no_dominant_axis();
    test_largest_axis_wins();
    test_only_flat_has_no_arrow();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
