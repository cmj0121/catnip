# Catnip firmware

The Catnip runtime for the MeowKit (ESP32-S3).

## Layout

| Path | What |
| --- | --- |
| `lib/lua/` | Vendored Lua 5.4 (C core + stdlib) |
| `src/` | Catnip runtime (embeds Lua, runs scripts) |
| `test/native/` | Host tests, run with `make test` |
| `platformio.ini` | On-device build (ESP32-S3) |

## Host tests

The runtime is portable C and is verified on the host, no device needed:

```sh
make test      # build & run every test/native/*.c
make clean
```

## On-device build

Requires PlatformIO:

```sh
pio run                 # build for the MeowKit
pio run -t upload       # flash (see the repo Makefile: make flash/install)
```
