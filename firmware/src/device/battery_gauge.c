/* battery_gauge.c - see battery_gauge.h. */
#include "battery_gauge.h"

int catnip_battery_percent_from_mv(uint32_t mv)
{
    if (mv < CATNIP_BATTERY_PLAUSIBLE_MIN_MV || mv > CATNIP_BATTERY_PLAUSIBLE_MAX_MV) {
        return -1;
    }
    /* A cell can sit below the empty mark and above the full one and still be a
     * cell - a battery under load sags, and one straight off the charger reads
     * a little high. Those are clamped, unlike the band above, because here the
     * reading is believed and only the estimate has run out of curve. */
    if (mv <= CATNIP_BATTERY_EMPTY_MV) return 0;
    if (mv >= CATNIP_BATTERY_FULL_MV) return 100;
    return (int)((mv - CATNIP_BATTERY_EMPTY_MV) * 100 /
                 (CATNIP_BATTERY_FULL_MV - CATNIP_BATTERY_EMPTY_MV));
}
