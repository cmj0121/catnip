/* sd_mount.cpp - see sd_mount.h. */
#include <Arduino.h>
#include <SD_MMC.h>

#include "board.h"
#include "sd_mount.h"

namespace {
bool g_mounted = false;
} /* namespace */

bool catnip_sd_mount(void)
{
    if (g_mounted) return true;

    /* The card is wired for 1-bit SDMMC: three lines, no D1-D3. Asking for
     * 4-bit mode here fails on a card that is present and working. */
    if (!SD_MMC.setPins(CATNIP_PIN_SD_CLK, CATNIP_PIN_SD_CMD, CATNIP_PIN_SD_D0)) {
        Serial.println("[catnip] sd: pins rejected");
        return false;
    }
    if (!SD_MMC.begin("/sd", true /* 1-bit */)) {
        Serial.println("[catnip] sd: no card");
        return false;
    }
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[catnip] sd: no card");
        SD_MMC.end();
        return false;
    }

    const char *type = "unknown";
    switch (SD_MMC.cardType()) {
    case CARD_MMC: type = "MMC"; break;
    case CARD_SD: type = "SD"; break;
    case CARD_SDHC: type = "SDHC"; break;
    default: break;
    }
    Serial.printf("[catnip] sd: %s card, %llu MB, mounted at /sd\n", type,
                  SD_MMC.cardSize() / (1024ULL * 1024ULL));
    g_mounted = true;
    return true;
}

bool catnip_sd_mounted(void)
{
    return g_mounted;
}
