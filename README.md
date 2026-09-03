# Catnip

> Your meowkit solution.

**Languages:** English · [繁體中文](README.zh-TW.md)

Catnip turns the [MeowKit](https://www.kickstarter.com/projects/whitecliff/meowkit-versatile-device-for-makers)
— an ESP32-S3 handheld for makers — into a **Lua app platform**. A C++ runtime on the
device loads Lua apps straight from the SD card, and each app writes both its **UI** and its
**logic** in Lua. Anyone can install an app by dropping a folder onto the card — no reflash,
no toolchain.

It is the "Lua plugin system" the MeowKit was imagined to have, built for real.

## Why

The stock firmware compiles every app into the binary in C++. Adding one means forking the
firmware and reflashing. Catnip flips that: the device carries a stable runtime, and apps
live as portable Lua folders on the SD card. Write once, copy anywhere, run on any Catnip
device.

## How it works

Catnip owns the whole on-device experience and exposes a small, stable API surface to Lua:

```txt
┌──────────────────────────────────────────────────┐
│  Lua apps on SD:  /catnip/apps/<name>/           │ ← anyone writes this
├──────────────────────────────────────────────────┤
│  Catnip Lua API:   ui.*   device.*   service.*   │ ← the stable contract
├──────────────────────────────────────────────────┤
│  Lua 5.4 VM  (PSRAM heap, cooperative, guarded)  │
├──────────────────────────────────────────────────┤
│  Catnip C++ runtime  (bridges Lua ↔ LVGL / BSP)  │
├──────────────────────────────────────────────────┤
│  MeowKit firmware:  LVGL · BSP (wifi/ir/sd/...)  │ ← existing
└──────────────────────────────────────────────────┘
```

## What a Catnip app looks like

```lua
-- /catnip/apps/ci-dashboard/main.lua
local status

function catnip.view()
  return ui.screen{
    ui.label{ text = "CI Dashboard", style = "title" },
    ui.button{ text = "Rebuild", on_click = function()
      service.http.post("http://ci/rebuild"); device.vibrate(120)
    end },
    ui.label{ id = "status", text = "…" },
  }
end

function catnip.on_open()
  status = catnip.view:get("status")
  while true do                       -- looks synchronous; yields under the hood
    status.text = service.http.get("http://ci/status").state
    sys.sleep(5000)                   -- UI keeps running while we wait
  end
end
```

An app is just a folder on the SD card:

```txt
/catnip/apps/ci-dashboard/
├── manifest.json     # id, name, version, icon, catnip_api, permissions
├── main.lua          # catnip.view() + logic
├── icon.png          # 70×70
└── assets/           # optional
```

## App call-flow

How an app runs — from launch to the cooperative loop that keeps the UI live
while it waits:

```txt
   Catnip shell
        │  launch app
        ▼
   Loader ──── read manifest.json · check catnip_api · resolve perms
        │  ok
        ▼
   Runtime ─── start Lua 5.4 VM (PSRAM, sandbox) · run main.lua
        │
        ▼
   catnip.view()  ──►  build ui.* tree  ──►  render on LVGL
        │
        ▼
   catnip.on_open()   — runs inside a coroutine
        │
        ▼
   ┌───────────────── cooperative loop ─────────────────┐
   │  service.http.get(url) / sys.sleep(ms)             │
   │        │ yield                                     │
   │        ▼                                           │
   │  runtime pumps lv_timer_handler   (UI stays live)  │
   │        │ resume  ◄── result / timer fires          │
   │        ▼                                           │
   │  update ui.* widgets  ──►  back to top of loop     │
   └────────────────────────────────────────────────────┘
        │  back / exit
        ▼
   catnip.on_close()  ──►  free VM + widgets  ──►  return to shell
```

## Status

Early design. Catnip is an independent firmware distribution layered on the open-source
MeowKit firmware; see the [issues](https://github.com/cmj0121/catnip/issues) for the
current plan.

## DDD (Dream-Driven Development)

Features are driven by what the author dreams of and needs — nothing more.
