# Installing Catnip on a MeowKit

Catnip installs onto a MeowKit (ESP32-S3) from your machine over USB. The flow is
**brick-safe and rollbackable**: it always takes a verified backup before writing,
and the ESP32-S3 ROM download mode means a flash can always be redone.

## Requirements

- [esptool](https://github.com/espressif/esptool) — `pip install esptool`
- [PlatformIO](https://platformio.org) — `pip install platformio` (builds the firmware)
- A USB-C cable and the MeowKit

## First-time setup

```sh
make flash
```

Puts the MeowKit into flash/download status and confirms your machine can talk to
it. If it cannot connect, enter ROM download mode by hand: **hold BOOT, tap RESET,
release BOOT**, then re-run.

On Linux you may need serial permission: `sudo usermod -aG dialout "$USER"` (re-login).

### The BOOT+RESET dance is now required on every flash

Catnip ships as a single **TinyUSB composite** image (a CDC console plus a USB
HID keyboard, `ARDUINO_USB_MODE=0`). Moving onto the TinyUSB stack is what lets
the keyboard and the console share one USB device — but it gives up the ROM's
USB-Serial/JTAG reset-to-download path that used to put the board into download
mode on its own. So from this firmware onward **every** flash needs the manual
dance, not just a first-time or stuck one:

> **hold BOOT, tap RESET, release BOOT**, then run `make flash` / `make install`.

This is a known, accepted trade of the composite design. The device is never
bricked — ROM download mode is always reachable this way.

## Install

```sh
make install
```

1. Backs up the **full 16 MB** stock flash and verifies it (kept under `backup/`).
2. Builds Catnip and flashes it via PlatformIO.

Install refuses to write unless a verified stock backup exists, so you can always
get back.

## Uninstall / rollback

```sh
make uninstall
```

Restores the MeowKit from your backup. If no local backup is found, it downloads
and flashes the official stock firmware instead.

## Recovery (worst case)

The ESP32-S3 ROM download mode is always reachable over USB and is never disabled,
so the device cannot be permanently bricked by flashing. If anything looks wrong:

1. Enter download mode (hold BOOT, tap RESET, release BOOT).
2. `make uninstall` (restores your backup or official stock).

## USB HID and BadUSB

The composite image presents a USB keyboard to the host, but it is **disabled at
boot and types nothing** until you turn it on. Open the **USB HID** app and press
**A** to enable typing (the header grows a keyboard glyph while it is on); press A
again to disable it.

**Long A** in that app opens the HID feature menu. **BadUSB** runs a
[Ducky Script](https://docs.hak5.org/hak5-usb-rubber-ducky) over the keyboard. It
is greyed until HID is enabled and until there is at least one script on the card:

- Put `.txt` scripts in **`/sd/catnip/badusb/`** on the SD card
  (e.g. `/sd/catnip/badusb/hello.txt`).
- A US-QWERTY subset is understood: `REM`, `STRING`, `STRINGLN`, `ENTER`, `TAB`,
  `ESC`, `DELETE`, `BACKSPACE`, `DELAY <ms>`, the arrow keys, and the modifier
  words `GUI`/`WINDOWS`, `CTRL`, `ALT`, `SHIFT` (alone or leading a chord such as
  `GUI r` or `CTRL ALT DELETE`).

Nothing types until HID is enabled and a script is deliberately picked and run —
there is no autostart. A minimal script:

```text
REM opens a run box and says hello
GUI r
DELAY 500
STRING notepad
ENTER
DELAY 800
STRINGLN hello from catnip
```

Mass Storage appears in the same menu but is disabled in this release; it lands in
a later batch.

## Configuration

Override via environment variables:

| Var          | Default         | Meaning                   |
| ------------ | --------------- | ------------------------- |
| `PORT`       | auto-detected   | serial port               |
| `BAUD`       | `921600`        | upload baud               |
| `BACKUP`     | timestamped     | backup file path          |
| `STOCK_BASE` | upstream GitHub | where to fetch stock bins |
