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

## Configuration

Override via environment variables:

| Var          | Default         | Meaning                   |
| ------------ | --------------- | ------------------------- |
| `PORT`       | auto-detected   | serial port               |
| `BAUD`       | `921600`        | upload baud               |
| `BACKUP`     | timestamped     | backup file path          |
| `STOCK_BASE` | upstream GitHub | where to fetch stock bins |
