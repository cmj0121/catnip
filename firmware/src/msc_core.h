/*
 * msc_core.h - the parts of USB Mass Storage that touch no hardware (Batch 2).
 *
 * The driver in device/usb_msc.cpp owns the card and the USB stack; those are
 * device-only and cannot be reached by a host test. What a host test can hold
 * are the two decisions that keep the surface honest, so they live here in
 * plain C, the way ducky.c holds the keymap out of the keyboard:
 *
 *   - when Mass Storage may be entered at all, and
 *   - whether a read or write the host asked for lands inside the card.
 *
 * The second is the one that matters: a host is free to send any LBA, and a
 * driver that trusts it would read or write off the end of the card. This is
 * where that request is bounds-checked, once, so the callback below it does not
 * have to be trusted to.
 */
#ifndef CATNIP_MSC_CORE_H
#define CATNIP_MSC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether Mass Storage may be handed the card now. Only when a card is present
 * and it is not already handed over - the SD has exactly one owner, so entering
 * twice is not a thing that can happen. */
bool catnip_msc_can_enter(bool has_card, bool active);

/* Turn a host's block request into a sector range on the card, or refuse it.
 *
 * `lba`/`offset` and `bufsize` are what the MSC read/write callback is handed:
 * a logical block address, a byte offset within it (0 for the whole-sector
 * transfers a host normally sends), and a buffer length. `block_size` and
 * `block_count` are the card's geometry.
 *
 * On success `*out_lba` and `*out_count` are the first sector and how many, and
 * the function returns true. It returns false - and the callback returns an
 * error - when the length is not a whole number of sectors, when the offset is
 * not sector-aligned, or when the range runs off the end of the card. That last
 * check is the point of the function: the host chooses the LBA, and nothing
 * else stops it choosing one past the end. */
bool catnip_msc_io_range(uint32_t lba, uint32_t offset, uint32_t bufsize,
                         uint32_t block_size, uint32_t block_count, uint32_t *out_lba,
                         uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_MSC_CORE_H */
