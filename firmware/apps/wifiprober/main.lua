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

-- A throws the last scan away and asks again. The driver was going to rescan
-- anyway - what this buys is the platform's ring coming back up, which is the
-- device saying "yes, I heard you". A page that answers a press with the list
-- it was already showing has not answered it.
local function look_again()
  service.wifi.rescan()
end

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
  on_click = function() look_again() end }

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
-- One adjacent pass, not a sort run to a fixed point.
--
-- "Stronger by more than five" is not a total order - a can be within five of
-- b, and b within five of c, while a is eight below c - so a sort that insisted
-- on settling could walk in a circle. One pass cannot: each neighbour is
-- considered once, the order settles over two or three scans, and that is
-- another helping of the damping this is for.
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
  -- Newly arrived, put in at their strength rather than on the end.
  --
  -- The hysteresis exists to protect an order that is already on screen, and a
  -- network nobody has seen yet has no position to protect - putting it at the
  -- bottom and letting it climb one place a scan would be damping a jump that
  -- never happened. Strongest first among themselves, so several arriving at
  -- once arrive in order too.
  local fresh = {}
  for _, ap in ipairs(aps) do
    if not seen[ap.ssid] then fresh[#fresh + 1] = ap end
  end
  table.sort(fresh, function(a, b) return a.rssi > b.rssi end)
  for _, ap in ipairs(fresh) do
    local at = #next_order + 1
    for i, ssid in ipairs(next_order) do
      if ap.rssi > by_ssid[ssid].rssi then at = i break end
    end
    table.insert(next_order, at, ap.ssid)
  end

  for i = 1, #next_order - 1 do
    local a, b = by_ssid[next_order[i]], by_ssid[next_order[i + 1]]
    if b.rssi - a.rssi > HYSTERESIS then
      next_order[i], next_order[i + 1] = next_order[i + 1], next_order[i]
    end
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
    -- Strength, channel, name - in that order and in that many columns.
    --
    -- The name was in the middle and the channel after it, which meant the
    -- channel sat wherever the name happened to end: a column of numbers that
    -- was not a column. Both of the things in front of the name are fixed width
    -- now - four marks and two digits - so the names start in the same place on
    -- every line and the page can be read down rather than across.
    --
    -- Two digits, zero-padded. `ch6` and `ch11` are the same fact at two widths,
    -- and the `ch` was a label on a column that needs none: every number in that
    -- position is a channel.
    cells[i] = ui.label{ id = "ap" .. i,
                         text = string.format("%s  %02d  %s", bars(ap.rssi),
                                              ap.channel, name) }
  end
  rows:set_children(cells)
  if sel > #cells then sel = #cells > 0 and #cells or 1 end
  rows.selected = sel
  -- An empty list is still a list, and a list is still a shape that stacks from
  -- the top. Taking it away leaves the message as the only thing on the screen,
  -- which is what puts it in the middle rather than under an empty box.
  rows.hidden = (#aps == 0)
  -- Two sentences, because they are two different things: what was found, and
  -- what to do about it. Centred, because on a page with nothing else on it a
  -- line ranged left reads as the first of a list that never arrived.
  status.text = "nothing on the air\npress A to look again"
  status.hidden = (#aps > 0)
end

-- A is on the screen as well as on the list, and the empty page is why.
--
-- A press goes to whatever is focused, and falls through to the screen when
-- nothing is - which is exactly the state the empty page is in, because the
-- list is hidden there so that the message can have the middle. So the one page
-- that says "press A to look again" was the one page where A had nowhere to
-- land. It is on both now: the list answers while there is a list, and the
-- screen answers when there is not.
ui.screen{ rows, status, on_click = function() look_again() end }

-- The app is its own loop: build the screen, then poll forever. A main chunk
-- that never returns stays live and is stepped between its sleeps, which is how
-- an app without a background thread keeps a screen fresh (sys.sleep yields to
-- the scheduler; the watchdog is never tripped because the app is not spinning).
-- B backs out of it, as everywhere.
-- A second between asks, not because a scan takes a second - it takes five and
-- a half, and the driver paces the sweeps itself - but because asking is how
-- the answer is collected, and an answer that has arrived should be drawn
-- rather than waited out. The driver returns nil until it has one.
while true do
  refresh()
  sys.sleep(1000)
end
