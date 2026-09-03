# Catnip

> Your meowkit solution.

**語言：** [English](README.md) · 繁體中文

Catnip 把 [MeowKit](https://www.kickstarter.com/projects/whitecliff/meowkit-versatile-device-for-makers)
——一台給 maker 的 ESP32-S3 掌上機——變成一個 **Lua app 平台**。裝置上的 C++ runtime 直接
從 SD card 載入 Lua app，每個 app 用 Lua 同時寫 **UI** 與 **邏輯**。任何人把一個資料夾丟進
SD card 就能安裝——不必重刷韌體、不必裝 toolchain。

這就是 MeowKit 當初被想像成、但原廠韌體其實沒做的「Lua plugin system」，把它真正做出來。

## 為什麼

原廠韌體把每個 app 用 C++ 編進 binary，要加一個就得 fork 韌體並重刷。Catnip 反過來：裝置
帶著一個穩定的 runtime，app 以可攜的 Lua 資料夾住在 SD card 上。寫一次、複製到任何地方、
在任何 Catnip 裝置上都能跑。

## 運作方式

Catnip 接管整個裝置端體驗，並對 Lua 開放一個小而穩定的 API 表面：

```txt
┌──────────────────────────────────────────────────┐
│  SD 上的 Lua app：  /catnip/apps/<name>/         │ ← 任何人寫這層
├──────────────────────────────────────────────────┤
│  Catnip Lua API：   ui.*   device.*   service.*  │ ← 穩定合約
├──────────────────────────────────────────────────┤
│  Lua 5.4 VM （PSRAM heap、協作式、有看門狗）     │
├──────────────────────────────────────────────────┤
│  Catnip C++ runtime （橋接 Lua ↔ LVGL / BSP）    │
├──────────────────────────────────────────────────┤
│  MeowKit 韌體：  LVGL · BSP (wifi/ir/sd/...)     │ ← 既有
└──────────────────────────────────────────────────┘
```

## 一個 Catnip app 長什麼樣

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
  while true do                       -- 看似同步，實際在底層 yield
    status.text = service.http.get("http://ci/status").state
    sys.sleep(5000)                   -- 等待期間畫面持續運作
  end
end
```

一個 app 就是 SD card 上的一個資料夾：

```txt
/catnip/apps/ci-dashboard/
├── manifest.json     # id、name、version、icon、catnip_api、permissions
├── main.lua          # catnip.view() + 邏輯
├── icon.png          # 70×70
└── assets/           # 選用
```

## App 呼叫流程

一個 app 怎麼跑——從啟動，到讓畫面在等待時仍持續運作的協作式迴圈：

```txt
   Catnip shell
        │  啟動 app
        ▼
   Loader ──── 讀 manifest.json · 檢查 catnip_api · 解析權限
        │  ok
        ▼
   Runtime ─── 啟動 Lua 5.4 VM (PSRAM, sandbox) · 跑 main.lua
        │
        ▼
   catnip.view()  ──►  組出 ui.* tree  ──►  在 LVGL 上繪製
        │
        ▼
   catnip.on_open()   — 跑在 coroutine 裡
        │
        ▼
   ┌─────────────────── 協作式迴圈 ───────────────────┐
   │  service.http.get(url) / sys.sleep(ms)           │
   │        │ yield                                   │
   │        ▼                                         │
   │  runtime pump lv_timer_handler   (畫面持續運作)  │
   │        │ resume  ◄── 結果 / timer 觸發           │
   │        ▼                                         │
   │  更新 ui.* widgets  ──►  回到迴圈頂端            │
   └──────────────────────────────────────────────────┘
        │  back / exit
        ▼
   catnip.on_close()  ──►  釋放 VM + widgets  ──►  回到 shell
```

## 現況

早期設計階段。Catnip 是疊在開源 MeowKit 韌體之上的獨立韌體發行版；目前規劃見
[issues](https://github.com/cmj0121/catnip/issues)。

## DDD (Dream-Driven Development)

功能只由作者夢想與需要的東西驅動——僅此而已。
