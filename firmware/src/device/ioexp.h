/*
 * ioexp.h - the PCA9557 I/O expander that gates the panel.
 *
 * The MeowKit routes the LCD's chip-select and its reset through this
 * expander rather than MCU pins, so the display cannot be used until the
 * expander has released the reset and asserted the select. That makes this a
 * prerequisite of display bring-up, not an accessory to it.
 */
#ifndef CATNIP_IOEXP_H
#define CATNIP_IOEXP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pulse the panel's reset, then drive its chip-select active, the way the
 * stock firmware does. Returns true if the expander acknowledged every
 * write. */
bool catnip_ioexp_begin(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_IOEXP_H */
