/* catnip_busy.c - see catnip_busy.h. */
#include "catnip_busy.h"

/* Sine and cosine of k*45 degrees, times 1024. A table rather than <math.h>:
 * eight angles is eight pairs of numbers, and pulling in the floating-point
 * library to compute a constant would be paying for generality nothing here
 * asks for. Index 0 is the top and they run clockwise.
 *
 * 724 is 1024/sqrt(2), rounded. */
static const int kSin[CATNIP_BUSY_DOTS] = {0, 724, 1024, 724, 0, -724, -1024, -724};
static const int kCos[CATNIP_BUSY_DOTS] = {-1024, -724, 0, 724, 1024, 724, 0, -724};

unsigned catnip_busy_phase(unsigned now_ms)
{
    return (now_ms / CATNIP_BUSY_STEP_MS) % CATNIP_BUSY_DOTS;
}

int catnip_busy_dots(unsigned phase, int cx, int cy, int radius, catnip_busy_dot *out,
                     int max)
{
    int n = 0;
    int i;

    if (!out || max <= 0) return 0;
    for (i = 0; i < CATNIP_BUSY_DOTS && n < max; i++) {
        /* How far behind the leader this dot is, which is what its brightness
         * says. The leader is the one at `phase`, and the tail runs backwards
         * from it - so the bright end is the one the ring is moving towards,
         * and the ring reads as turning rather than as flickering. */
        int behind =
            (int)(((unsigned)phase + CATNIP_BUSY_DOTS - (unsigned)i) % CATNIP_BUSY_DOTS);

        out[n].x = cx + (radius * kSin[i]) / 1024;
        out[n].y = cy + (radius * kCos[i]) / 1024;
        out[n].level = (uint8_t)(CATNIP_BUSY_DOTS - 1 - behind);
        n++;
    }
    return n;
}
