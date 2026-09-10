-- WiFi Prober (#54) - what is on the air.
--
-- A scan of the 2.4 GHz band: the access points nearby, strongest first, with
-- how strong each is and the channel it is on. It is the first real consumer of
-- service.wifi.scan(), and the simplest useful thing to build on it - a device
-- that can list networks is a device that can be shown to detect them.
--
-- The scan is asynchronous down in the driver: service.wifi.scan() returns nil
-- while one is running and a list when it finishes, then starts the next. So
-- this app does nothing but ask on a timer and redraw when the answer changes -
-- there is no "start" and no "stop", only the most recent air.
--
-- A scan briefly drops an active connection, because the radio cannot hold an
-- association and sweep every channel at once. That is the prober's to spend:
-- it is here to look at the air, and looking at the air costs the link.
--
-- Waiting is not drawn here. The platform has one ring for that, in one place
-- at one rhythm, and it is up from the first ask until the first answer - so
-- there is no "scanning..." in this file and there is no place for one.

-- Lines, not rows. A row reserves an icon slot in front of its text and draws a
-- ring around the one the cursor is on; neither is true here - there is no icon
-- coming, and a ring would promise that pressing A on a network did something
-- to it. What this page is, is a list of what is on the air, read.
-- Declared before the handlers that move it, because they name it.
local rows
local sel = 1

-- Up and down are the reading position, and A is "look again".
--
-- The list had neither, which meant it was not a focus stop at all - a page of
-- networks longer than the screen could not be scrolled to the end of it. What
-- it looked like was a page with nothing below the fold, which is the worst
-- shape a bug can take: nothing on the screen was wrong.
rows = ui.list{ id = "aps", layout = "text",
  on_prev = function()
    if sel > 1 then sel = sel - 1; rows.selected = sel end
  end,
  on_next = function()
    if sel < #rows.children then sel = sel + 1; rows.selected = sel end
  end,
  -- A throws the last scan away and asks again. The driver was going to rescan
  -- anyway - what this buys is the platform's ring coming back up, which is the
  -- device saying "yes, I heard you". A page that answers a press with the list
  -- it was already showing has not answered it.
  on_click = function() service.wifi.rescan() end }

-- And one line for the one thing the header cannot say.
--
-- The header already counts: `1/8` over this list is how many networks there
-- are, so a line saying "8 networks" would be the same fact twice. What it
-- cannot say is "I looked, and there was nothing" - a blank page and a page
-- with nothing on the air are the same picture, and only one of them is an
-- answer. So the line exists for exactly that case and is hidden otherwise.
local status = ui.label{ id = "status", hidden = true, align = "center" }

-- How strong, in words rather than a raw dBm nobody reads at a glance. The
-- thresholds are the usual ones: -60 and up is a room away, -75 and up is
-- through a wall, below that is the edge of hearing.
local function bars(rssi)
  if rssi >= -60 then return "||||"
  elseif rssi >= -70 then return "|||."
  elseif rssi >= -80 then return "||.."
  else return "|..." end
end

-- How much stronger one network has to be than the one above it before they
-- swap places.
--
-- Strongest first is the order somebody reads this page for, and a plain sort
-- by RSSI gives it - but RSSI wanders a few dB while nothing moves, and a scan
-- lands every second and a half. Two networks within a decibel of each other
-- would trade places on every scan, and a list that reorders itself while being
-- read is worse than one in the wrong order. Five decibels is wider than the
-- noise and narrower than any difference worth seeing.
local HYSTERESIS = 5

-- The order the page is currently in, by ssid. Kept between scans because that
-- is what the hysteresis is measured against: the question is never "what order
-- are these in" but "is this one now enough stronger than that one to be worth
-- moving".
local order = {}

-- Put the scan into that order. New networks go on the end, gone ones drop out,
-- and then neighbours swap only where the gap is worth it.
--
-- The pass is bounded rather than run to a fixed point: the comparison is not a
-- total order - a can be within five of b, and b within five of c, while a is
-- eight below c - so a sort that insisted on settling could walk in a circle.
-- Bounded, it settles over two or three scans instead, which is itself another
-- helping of the damping this is for.
local function reorder(aps)
  local by_ssid, seen = {}, {}
  for _, ap in ipairs(aps) do by_ssid[ap.ssid] = ap end

  local next_order = {}
  for _, ssid in ipairs(order) do
    if by_ssid[ssid] then
      next_order[#next_order + 1] = ssid
      seen[ssid] = true
    end
  end
  -- Newly arrived, strongest first among themselves so a fresh page opens in
  -- the right order rather than in scan order.
  local fresh = {}
  for _, ap in ipairs(aps) do
    if not seen[ap.ssid] then fresh[#fresh + 1] = ap end
  end
  table.sort(fresh, function(a, b) return a.rssi > b.rssi end)
  for _, ap in ipairs(fresh) do next_order[#next_order + 1] = ap.ssid end

  for _ = 1, #next_order do
    local moved = false
    for i = 1, #next_order - 1 do
      local a, b = by_ssid[next_order[i]], by_ssid[next_order[i + 1]]
      if b.rssi - a.rssi > HYSTERESIS then
        next_order[i], next_order[i + 1] = next_order[i + 1], next_order[i]
        moved = true
      end
    end
    if not moved then break end
  end

  order = next_order
  local out = {}
  for i, ssid in ipairs(order) do out[i] = by_ssid[ssid] end
  return out
end

local function refresh()
  local aps = service.wifi.scan()
  if not aps then
    -- nil is "still scanning", not "nothing there": leave what is shown.
    return
  end
  aps = reorder(aps)

  local cells = {}
  for i, ap in ipairs(aps) do
    local name = ap.ssid
    if name == "" then name = "(hidden)" end
    cells[i] = ui.label{ id = "ap" .. i,
                         text = string.format("%s  %s  ch%d", bars(ap.rssi), name,
                                              ap.channel) }
  end
  rows:set_children(cells)
  if sel > #cells then sel = #cells > 0 and #cells or 1 end
  rows.selected = sel
  -- Two sentences, because they are two different things: what was found, and
  -- what to do about it. Centred, because on a page with nothing else on it a
  -- line ranged left reads as the first of a list that never arrived.
  status.text = "nothing on the air\npress A to look again"
  status.hidden = (#aps > 0)
end

ui.screen{ rows, status }

-- The app is its own loop: build the screen, then poll forever. A main chunk
-- that never returns stays live and is stepped between its sleeps, which is how
-- an app without a background thread keeps a screen fresh (sys.sleep yields to
-- the scheduler; the watchdog is never tripped because the app is not spinning).
-- B backs out of it, as everywhere.
while true do
  refresh()
  sys.sleep(1500)
end
