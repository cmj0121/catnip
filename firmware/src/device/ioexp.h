/*
 * ioexp.h - the PCA9557 I/O expander that gates the panel.
 *
 * The MeowKit routes the LCD's chip-select through this expander rather than
 * an MCU pin, so the display cannot be used until the expander is configured.
 * That makes this a prerequisite of display bring-up, not an accessory to it.
 */
#ifndef CATNIP_IOEXP_H
#define CATNIP_IOEXP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the I2C bus and drive the panel's chip-select active. Returns true
 * if the expander acknowledged. */
bool catnip_ioexp_begin(void);


#ifdef __cplusplus
}
#endif

#endif /* CATNIP_IOEXP_H */
