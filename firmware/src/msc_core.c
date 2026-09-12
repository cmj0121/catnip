/* msc_core.c - see msc_core.h. */
#include "msc_core.h"

bool catnip_msc_can_enter(bool has_card, bool active)
{
    return has_card && !active;
}

bool catnip_msc_io_range(uint32_t lba, uint32_t offset, uint32_t bufsize,
                         uint32_t block_size, uint32_t block_count, uint32_t *out_lba,
                         uint32_t *out_count)
{
    if (block_size == 0 || bufsize == 0) return false;
    /* Whole sectors only: a transfer that does not divide evenly into the block
     * size is not one this device can turn into sdmmc_read_sectors, and a host
     * doing standard block I/O never sends one. */
    if (offset % block_size != 0 || bufsize % block_size != 0) return false;

    uint32_t start = lba + offset / block_size;
    uint32_t count = bufsize / block_size;

    /* Off the end, or wrapped past UINT32_MAX getting there. The host picks the
     * address and this is the only thing between it and the card's bounds:
     * without the check a bad or malicious LBA reads or writes memory that is
     * not the sector it named. */
    if (start < lba) return false; /* offset overflowed the add */
    if (start > block_count) return false;
    if (count > block_count - start) return false;

    *out_lba = start;
    *out_count = count;
    return true;
}
