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
│  內建 app：         firmware/apps/<name>/        │ ← 隨韌體出貨
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

## Home，以及你的 app 怎麼出現

Home 就是那隻**貓** —— mascot，每個手勢都能回到的唯一畫面，長按 **B** 永遠落在
這裡。它不是選單;選單是離開它的一個方向。四個方向各代表不同的意義:

```text
                          ▲  所有 app —— 3×2 grid，分頁
                          │
    pinned  ◄─────────  (=^·^=)  ─────────►  pinned
                          │
                          ▼  這台裝置 —— 版本、時鐘、電量、卡片
```

- **上**是_你的_:你擁有的每個 app，一個分頁的 3×2 icon grid。
- **下**是_裝置的_:一頁事實,操作收在 **A** 後面。
- **左 / 右**翻動 **carousel** —— 你 pin 的 app,一次一個、大大地顯示。時鐘只是
  一個 pin 在這裡的 app;你寫的任何東西都能取代它。

一個 tile 刻意只顯示很少的東西:一個 app 是**一張圖,頂多再加一個數字** ——
不會有標題文字,不會有第二行。

```text
  ┌──────── Apps ─────── 1/2 ──┐
  │   ▦        ▦        ▦ ³     │   cell 是一個 icon;它唯一帶的
  │  Files    Clock    Scan     │   數字是 badge(Scan 的 ³)。被
  │                             │   選中 cell 的名字是 header 標題;
  │   ▦        ▦                 │   空的 cell 只是還沒到那裡而已
  │  Rain     Notes             │
  └─────────────────────────────┘
```

| 位置                 | app 可以顯示                                   | 由什麼決定                      |
| -------------------- | ---------------------------------------------- | ------------------------------- |
| Carousel(pinned)     | 它的 `icon`,放大,名字在 header                 | `manifest.icon`                 |
| Grid cell(所有 app） | 它的 `icon`,加一個角落 **badge** —— 一個計數   | `manifest.icon` · `badge`(Lua） |
| 開啟時               | header 上的 **counter**(`2/3`),如果它是個 list | `manifest.counter`              |

**badge** 是一個數字,而數字長不成一個句子:`badge = 3` 是 cell 角落一個小圓點。
**沒有不等於零** —— 沒 badge 的 cell 是還沒被數過,顯示 `0` 的 cell 是數過了、結果
是空的。缺 `icon` 會退回 mascot;grid **從左上開始排**,所以不管這頁有四個還是六
個,第一個 app 永遠在左上角那格。

## 讓它變成你的

開機體驗由 SD 卡上的一個檔案決定，不需要重新編譯韌體。這些都不是必要的：沒有
卡、沒有檔案，或檔案裡有錯，裝置都會照出廠的樣子開機，並在序列埠 log 說明它對
這個檔案的解讀。

```txt
/sd/catnip/
├── config.json       # 下列設定
├── boot/             # 選用：你自己的開機動畫
│   ├── frame00.jpg
│   ├── frame01.jpg
│   └── frame02.jpg
└── apps/             # 你的 Lua app
```

```json
{
  "led": { "brightness": 8, "breaths_per_second": 0.4 },
  "boot": { "frames": "/sd/catnip/boot", "frame_ms": 400 }
}
```

| 設定                     | 意義                                                                                      | 預設  |
| ------------------------ | ----------------------------------------------------------------------------------------- | ----- |
| `led.brightness`         | 狀態 LED 呼吸的最亮點，0-255。WS2812 在同樣數值下遠比一般人想像的亮，所以預設值很低。     | `8`   |
| `led.breaths_per_second` | 每秒呼吸幾次。`0.4` 是每兩秒半一次。                                                      | `0.4` |
| `boot.frames`            | 動畫影格所在的資料夾，依檔名順序播放。`.jpg`、`.png`、`.qoi` 都支援。不填就用內建吉祥物。 | 內建  |
| `boot.frame_ms`          | 每一格顯示多久。                                                                          | `400` |

影格為 320x240，開機時一次解碼進 PSRAM，之後播放不再有解碼成本；上限十六格。
超出合理範圍的值會被夾到範圍內而不是被拒絕，而且一行寫錯只會損失那一行，不會
損失整個檔案。

## 按鍵

| 操作       | 行為                                                                      |
| ---------- | ------------------------------------------------------------------------- |
| 電源鍵短按 | 開關螢幕。狀態 LED 持續呼吸但降到微光，所以黑掉的螢幕仍看得出裝置是活的。 |
| 電源鍵長按 | 關機。                                                                    |

## 現況

早期設計階段。Catnip 是疊在開源 MeowKit 韌體之上的獨立韌體發行版；目前規劃見
[issues](https://github.com/cmj0121/catnip/issues)。

## DDD (Dream-Driven Development)

功能只由作者夢想與需要的東西驅動——僅此而已。
