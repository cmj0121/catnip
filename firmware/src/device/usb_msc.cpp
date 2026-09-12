/* usb_msc.cpp - see usb_msc.h. */
#include "usb_msc.h"

/* The Mass Storage drive exists only on a TinyUSB build (USB_MODE=0), which is
 * what the shipping meowkit env is - the same condition usb_hid.cpp compiles
 * under. On any other build the four calls are stubs, so the file compiles
 * everywhere and the symbols the HAL wires are always present; a device that is
 * not the composite reports no card and hands nothing over. */
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

#include "board.h"
#include "sd_mount.h"
#include "../msc_core.h"

namespace {

/* Constructed at static-init, before app_main and therefore before the core's
 * USB.begin(): the constructor registers the MSC interface, so the device the
 * host first enumerates carries a drive beside the CDC console and the keyboard.
 * It stays media-absent until the app hands the card over. */
USBMSC g_msc;
bool g_begun = false;

/* True while the host owns the card. Its inverse is what lets the local
 * filesystem exist: the two are never both true. */
bool g_active = false;

/* The raw card, brought up directly on the SDMMC host once FatFs has let go of
 * it. sdmmc_card_t carries its own host reference, so the read/write callbacks
 * need nothing but its address. Geometry is read from it at enable time and
 * handed to MSC, because a card's size is the card's to state, not ours. */
sdmmc_card_t g_card;
bool g_card_ready = false;
uint32_t g_block_size;
uint32_t g_block_count;

/* Bring the card up on the bare SDMMC host. The caller has already unmounted
 * /sd, so the host is free; this mirrors how sd_mount.cpp's SD_MMC.begin() sets
 * the slot up - slot 1, 1-bit, the board's three lines - but stops at the raw
 * card rather than mounting a filesystem on it, because the filesystem is the
 * host's now. */
bool raw_card_up(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)CATNIP_PIN_SD_CLK;
    slot.cmd = (gpio_num_t)CATNIP_PIN_SD_CMD;
    slot.d0 = (gpio_num_t)CATNIP_PIN_SD_D0;
    slot.width = 1;

    if (sdmmc_host_init() != ESP_OK) return false;
    if (sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) != ESP_OK) {
        sdmmc_host_deinit();
        return false;
    }
    if (sdmmc_card_init(&host, &g_card) != ESP_OK) {
        sdmmc_host_deinit();
        return false;
    }
    g_block_size = g_card.csd.sector_size; /* 512 on every card this meets */
    g_block_count = (uint32_t)g_card.csd.capacity;
    g_card_ready = true;
    return true;
}

void raw_card_down(void)
{
    g_card_ready = false;
    sdmmc_host_deinit();
}

/* The host's block requests. media_present already gates these inside the MSC
 * wrapper, and g_card_ready gates them again here; catnip_msc_io_range then
 * bounds the address to the card, so nothing below is trusted to. */
int32_t on_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    uint32_t start, count;
    if (!g_card_ready) return -1;
    if (!catnip_msc_io_range(lba, offset, bufsize, g_block_size, g_block_count, &start,
                             &count))
        return -1;
    if (sdmmc_read_sectors(&g_card, buffer, start, count) != ESP_OK) return -1;
    return (int32_t)bufsize;
}

int32_t on_write(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    uint32_t start, count;
    if (!g_card_ready) return -1;
    if (!catnip_msc_io_range(lba, offset, bufsize, g_block_size, g_block_count, &start,
                             &count))
        return -1;
    if (sdmmc_write_sectors(&g_card, buffer, start, count) != ESP_OK) return -1;
    return (int32_t)bufsize;
}

/* The host's eject (Start Stop Unit with start=0). Allowed - it makes the
 * volume safe to remove on the host - but it does not itself reclaim the card:
 * that waits for B on the Mass Storage screen, so the SD driver is only ever
 * touched from the main context and never from this USB callback. */
bool on_start_stop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

} /* namespace */

void catnip_usb_msc_begin(void)
{
    if (g_begun) return;
    g_begun = true;

    g_msc.vendorID("catnip");
    g_msc.productID("SD Card");
    g_msc.productRevision("1.0");
    g_msc.onRead(on_read);
    g_msc.onWrite(on_write);
    g_msc.onStartStop(on_start_stop);
    /* No media until the app hands the card over. The host sees an empty drive,
     * not a broken one. */
    g_msc.mediaPresent(false);
    Serial.println("[catnip] usb-msc: drive present, no media");
}

void catnip_usb_msc_enable(bool on)
{
    if (on == g_active) return;

    if (on) {
        if (!catnip_sd_mounted()) return; /* nothing to hand over */

        /* The order here is the whole safety of the surface. FatFs must let go
         * of the card BEFORE the host is allowed to touch it - the two writing
         * the same sectors at once is the corruption this app exists to avoid -
         * so the sequence is: unmount /sd, take the raw card, and only then mark
         * the media present. Nothing between the unmount and the expose can
         * reach the card, because the local filesystem is already gone and the
         * host cannot see media yet. */
        catnip_sd_unmount();
        if (!raw_card_up()) {
            /* Could not take the card: give it straight back rather than leave
             * it owned by no one. */
            catnip_sd_mount();
            Serial.println("[catnip] usb-msc: raw card init failed, /sd remounted");
            return;
        }
        g_msc.begin(g_block_count, (uint16_t)g_block_size);
        g_msc.mediaPresent(true);
        g_active = true;
        Serial.printf("[catnip] usb-msc: card handed to host (%u sectors x %u B)\n",
                      g_block_count, g_block_size);
    } else {
        /* And back, in the mirror order: the host loses the media first, so it
         * can no longer read or write, THEN we release the raw card, THEN FatFs
         * remounts. At no point do both own it. */
        g_msc.mediaPresent(false);
        raw_card_down();
        catnip_sd_mount();
        g_active = false;
        Serial.println("[catnip] usb-msc: card taken back, /sd remounted");
    }
}

bool catnip_usb_msc_active(void)
{
    return g_active;
}

bool catnip_usb_msc_has_card(void)
{
    /* A card mounted locally now, or one already in the host's hands. Both are
     * "there is a card"; only the owner differs. */
    return catnip_sd_mounted() || g_active;
}

#else /* not a TinyUSB build: no Mass Storage here. */

void catnip_usb_msc_begin(void)
{
}
void catnip_usb_msc_enable(bool on)
{
    (void)on;
}
bool catnip_usb_msc_active(void)
{
    return false;
}
bool catnip_usb_msc_has_card(void)
{
    return false;
}

#endif
