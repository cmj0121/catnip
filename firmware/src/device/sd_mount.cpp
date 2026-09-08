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
    if (!SD_MMC.begin(CATNIP_SD_MOUNT_POINT, true /* 1-bit */)) {
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
    Serial.printf("[catnip] sd: %s card, %llu MB, mounted at " CATNIP_SD_MOUNT_POINT "\n",
                  type, SD_MMC.cardSize() / (1024ULL * 1024ULL));
    g_mounted = true;
    return true;
}

bool catnip_sd_mounted(void)
{
    return g_mounted;
}

/* A second is far longer than a card swap takes to matter and far shorter than
 * a person's patience, and it keeps a filesystem open off the loop's back. */
#define SD_POLL_MS 1000

bool catnip_sd_poll(void)
{
    static unsigned long last;
    unsigned long now = millis();

    if (now - last < SD_POLL_MS) return false;
    last = now;

    if (!g_mounted) return catnip_sd_mount(); /* true only when it just came up */

    /* Mounted: ask the card something. cardType() and cardSize() answer from
     * what begin() learned and would go on saying a card is there after it has
     * been pulled out, so the question has to reach the card itself. */
    File root = SD_MMC.open("/");
    if (root) {
        bool ok = root.isDirectory();
        root.close();
        if (ok) return false;
    }

    Serial.println("[catnip] sd: card gone");
    SD_MMC.end();
    g_mounted = false;
    return true;
}
