-- Scanner (#57) - what is nearby, on every radio this board has.
--
-- It began as the WiFi Prober and grew a second radio, which changed the shape
-- of the page rather than adding a line to it. One radio is a list. Several are
-- a group: a cell per protocol with a count on it, all of them readable at
-- once, and the list behind whichever one you press.
--
-- Four protocols are drawn and two of them work. IR and NFC are here greyed
-- rather than absent because "this device does not have it" and "this device
-- has not been told to look" are different answers, and a grid that showed only
-- what works cannot tell them apart - a user would be left wondering whether
-- the Bluetooth speaker in the room means the scanner is broken. They open a
-- page that says the true thing (#52).
--
-- One radio at a time, in turn.
--
-- Not both at once: the 2.4 GHz front end is shared, and a Wi-Fi sweep and a
-- BLE window running together are two scans each getting a fraction of the
-- radio and each taking longer for it. So the grid asks one protocol until it
-- answers, then moves on - which also gives the cell marker something true to
-- say, because at any moment exactly one radio is listening.
--
-- The drivers make that free. Each starts a scan when asked and rests when it
-- is not, so "one at a time" is nothing more than this app asking one at a
-- time. Nothing has to be stopped.

-- The four, in the order they are drawn. `live` is whether this firmware can
-- hear it at all; `why` is what its page says when it cannot.
local PROTOS = {
  { key = "wifi", name = "WiFi", icon = "wifi", live = true },
  { key = "ble",  name = "BLE",  icon = "ble",  live = true },
  { key = "ir",   name = "IR",   icon = "ir",   live = false,
    why = "no infrared receiver\non this board" },
  { key = "nfc",  name = "NFC",  icon = "nfc",  live = false,
    why = "no NFC reader\non this board" },
}

-- The live ones, in the order the turn goes round.
local ROUND = {}
for _, p in ipairs(PROTOS) do
  if p.live then ROUND[#ROUND + 1] = p.key end
end

-- What each radio last heard: a list of lines, ready to draw, and a count. The
-- count is nil until the first answer, which is the difference between "none
-- nearby" and "not looked yet" - a cell that showed 0 before it had listened
-- would be the app saying something it does not know.
local lines = {}
local counts = {}

-- Whose turn it is to be listened to, and which protocol's list is open. They
-- are not the same thing: with a list open that protocol gets every ask, which
-- is why the page you are reading is the one that refreshes fastest.
local turn = 1
local open_key = nil

-- One round, and then it stops.
--
-- A scanner that never stops is a radio that never stops: the 2.4 GHz front end
-- is busy the whole time the app is open and the Wi-Fi link stays parked for
-- it, to keep a number fresh that nobody is watching change. So the grid scans
-- each radio once and then holds what it found - a snapshot, which is what a
-- page of counts is anyway - and looks again when it is asked to.
--
-- `left` is how many radios still owe an answer this round; `fresh` is which of
-- them still owe a *new* one, because a round asked for by hand must throw the
-- last answers away and the ring only goes up for a rescan.
local scanning = true
local left = #ROUND
local fresh = {}
for _, k in ipairs(ROUND) do fresh[k] = true end

-- How strong, in words rather than a raw dBm nobody reads at a glance. The
-- thresholds are the usual ones: -60 and up is a room away, -75 and up is
-- through a wall, below that is the edge of hearing. Both radios report dBm and
-- both are read the same way, so there is one of these.
local function bars(rssi)
  if rssi >= -60 then return "||||"
  elseif rssi >= -70 then return "|||."
  elseif rssi >= -80 then return "||.."
  else return "|..." end
end

-- How much stronger one entry has to be than the one above it before they swap.
--
-- Strongest first is the order somebody reads this page for, and a plain sort
-- by RSSI gives it - but RSSI wanders a few dB while nothing moves, and a scan
-- lands every few seconds. Two entries within a decibel of each other would
-- trade places on every scan, and a list that reorders itself while being read
-- is worse than one in the wrong order. Five decibels is wider than the noise
-- and narrower than any difference worth seeing.
local HYSTERESIS = 5

-- The order each protocol's page is currently in, by key. Kept between scans
-- because that is what the hysteresis is measured against: the question is
-- never "what order are these in" but "is this one now enough stronger than
-- that one to be worth moving".
local order = {}

-- Put a scan into that order. New entries go in at their strength, gone ones
-- drop out, and then neighbours swap only where the gap is worth it.
--
-- One adjacent pass, not a sort run to a fixed point. "Stronger by more than
-- five" is not a total order - a can be within five of b, and b within five of
-- c, while a is eight below c - so a sort that insisted on settling could walk
-- in a circle. One pass cannot: each neighbour is considered once, the order
-- settles over two or three scans, and that is another helping of the damping
-- this is for.
local function reorder(proto, items, id_of)
  local by_id, seen = {}, {}
  for _, it in ipairs(items) do by_id[id_of(it)] = it end

  local prev = order[proto] or {}
  local next_order = {}
  for _, id in ipairs(prev) do
    if by_id[id] then
      next_order[#next_order + 1] = id
      seen[id] = true
    end
  end

  -- Newly arrived, put in at their strength rather than on the end. The
  -- hysteresis exists to protect an order that is already on screen, and
  -- something nobody has seen yet has no position to protect.
  local fresh = {}
  for _, it in ipairs(items) do
    if not seen[id_of(it)] then fresh[#fresh + 1] = it end
  end
  table.sort(fresh, function(a, b) return a.rssi > b.rssi end)
  for _, it in ipairs(fresh) do
    local at = #next_order + 1
    for i, id in ipairs(next_order) do
      if it.rssi > by_id[id].rssi then at = i break end
    end
    table.insert(next_order, at, id_of(it))
  end

  for i = 1, #next_order - 1 do
    local a, b = by_id[next_order[i]], by_id[next_order[i + 1]]
    if b.rssi - a.rssi > HYSTERESIS then
      next_order[i], next_order[i + 1] = next_order[i + 1], next_order[i]
    end
  end

  order[proto] = next_order
  local out = {}
  for i, id in ipairs(next_order) do out[i] = by_id[id] end
  return out
end

-- One line of a Wi-Fi page: strength, channel, name, in that order and in that
-- many columns. Both of the things in front of the name are fixed width - four
-- marks and two digits - so the names start in the same place on every line and
-- the page can be read down rather than across.
local function wifi_line(ap)
  local name = ap.ssid
  if name == "" then name = "(hidden)" end
  return string.format("%s  %02d  %s", bars(ap.rssi), ap.channel, name)
end

-- And one of a BLE page: strength, then whatever the device is willing to be
-- called. Most advertisers carry no name at all - a pair of headphones
-- announces a service and a manufacturer blob and nothing a person would
-- recognise - so the address stands in, because it is the only identity there
-- is. It is not written as "(unknown)": the address is a true answer and a
-- word nobody said is not.
local function ble_line(d)
  return string.format("%s  %s", bars(d.rssi), d.name ~= "" and d.name or d.addr)
end

-- Ask one radio, and say whether it answered.
--
-- nil is "still listening", not "nothing there", so a nil leaves what is shown
-- alone - and leaves the turn where it is, which is what makes the round robin
-- wait for each radio rather than racing past it.
local function poll(key)
  local items, to_line, id_of
  if key == "wifi" then
    items, to_line, id_of = service.wifi.scan(), wifi_line, function(a) return a.ssid end
  elseif key == "ble" then
    items, to_line, id_of = service.ble.scan(), ble_line, function(d) return d.addr end
  else
    return true -- nothing to wait for on a radio that is not there
  end
  if not items then return false end

  items = reorder(key, items, id_of)
  local out = {}
  for i, it in ipairs(items) do out[i] = to_line(it) end
  lines[key] = out
  counts[key] = #out
  return true
end

-- Which cell is focused, and what the header reads because of it.
--
-- Nothing is selected on arrival, which leaves the header showing the app's own
-- name: a grid nobody has touched is the Scanner, and only once a cell is
-- picked is the page about one protocol. Declared up here because the list
-- below puts the title back when it closes.
local sel = 0

local function name_focus()
  ui.title(sel >= 1 and PROTOS[sel].name or nil)
end

-- ---- the list behind a cell ----------------------------------------------
--
-- Built once and refilled, rather than rebuilt: a page that made new nodes on
-- every scan would hand the renderer a different tree each time and redraw the
-- whole panel for a list that had not changed.
local list, list_rows, list_note, list_sel

local function fill_list()
  local proto
  for _, p in ipairs(PROTOS) do if p.key == open_key then proto = p end end
  if not proto then return end

  local these = lines[open_key] or {}
  local cells = {}
  for i, text in ipairs(these) do
    cells[i] = ui.label{ id = "row" .. i, text = text }
  end
  list_rows:set_children(cells)
  if list_sel > #cells then list_sel = #cells > 0 and #cells or 1 end
  list_rows.selected = list_sel

  -- An empty list is still a list, and a list is still a shape that stacks from
  -- the top. Taking it away leaves the note as the only thing on the screen,
  -- which is what puts it in the middle rather than under an empty box.
  list_rows.hidden = (#cells == 0)
  list_note.hidden = (#cells > 0)
  if not proto.live then
    list_note.text = proto.why
  else
    -- Two sentences, because they are two different things: what was found, and
    -- what to do about it.
    list_note.text = "nothing on the air\npress A to look again"
  end
end

-- Throw a radio's last answer away and ask for another.
--
-- This is also the only thing that raises the platform's waiting ring: the
-- claim begins at rescan() and ends at the first answer after it. So every
-- place this app wants the device to say "working on it" says it by asking for
-- the look it wanted anyway, and there is no second way to put a ring up.
local function rescan(key)
  if key == "wifi" then service.wifi.rescan()
  elseif key == "ble" then service.ble.rescan() end
end

local function look_again()
  rescan(open_key)
end

local function open_list(proto)
  open_key = proto.key
  list_sel = 1
  ui.title(proto.name)
  if not list then
    list_rows = ui.list{ id = "rows", layout = "text",
      on_prev = function()
        if list_sel > 1 then list_sel = list_sel - 1; list_rows.selected = list_sel end
      end,
      on_next = function()
        if list_sel < #list_rows.children then
          list_sel = list_sel + 1; list_rows.selected = list_sel
        end
      end,
      on_click = function() look_again() end }
    -- Centred, because on a page with nothing else on it a line ranged left
    -- reads as the first of a list that never arrived.
    list_note = ui.label{ id = "note", align = "center", hidden = true }
    -- A is on the screen as well as on the list, and the empty page is why: a
    -- press goes to whatever is focused and falls through to the screen when
    -- nothing is, which is exactly the state the empty page is in.
    --
    -- on_back only remembers that the list closed; it declines the press, so
    -- the platform still pops. Claiming it would mean writing the pop by hand,
    -- and a level that pops itself is a level that eventually forgets to.
    list = ui.push{ id = "list", list_rows, list_note,
      on_click = function() look_again() end,
      on_back = function() open_key = nil; name_focus(); return false end }
  else
    ui.push(list)
  end
  fill_list()
  -- A fresh look on the way in, every time and not only when the page is empty.
  -- The grid stopped scanning when its round finished, so what is behind a cell
  -- is as old as the last time somebody looked - and opening a protocol is
  -- asking about it now. It is also what puts the ring up, since the claim
  -- begins at rescan().
  look_again()
end

-- ---- the grid --------------------------------------------------------------
-- A cell is a picture and a count, and the word is in the header.
--
-- That is the platform's rule for a grid and not this app's preference: the
-- launcher's grid works the same way, the focused cell's name is the title, and
-- a device where one grid labels its cells and another does not is a device
-- with two grids. It matters more here than there, because the Bluetooth rune
-- on the BLE cell stands for more than this radio can hear - so the moment a
-- user reaches that cell, the header says BLE.
local cells = {}
for i, p in ipairs(PROTOS) do
  cells[i] = ui.label{ id = "cell" .. p.key, icon = p.icon }
end

-- Declared before the grid, because the grid's long press names it and the
-- round it restarts is defined below - a Lua local that is not in scope yet is
-- a global that is nil when the handler runs.
local scan_all

-- A grid that opens with a cell already ringed has made a choice before the
-- user has looked at it, and on a screen that is mostly pictures the ring is
-- the loudest thing on it. So the first direction press is what starts
-- choosing - the launcher's grid opens the same way.
local grid
grid = ui.list{ id = "protos", layout = "grid",
  on_prev = function()
    if sel > 1 then sel = sel - 1 elseif sel == 0 then sel = 1 end
    grid.selected = sel
    name_focus()
  end,
  on_next = function()
    if sel < #PROTOS then sel = sel + 1 end
    grid.selected = sel
    name_focus()
  end,
  on_click = function(self, i)
    -- A tap carries the cell it landed on: it selects, and stops there. A is
    -- the press with no index, and it opens. The same guard the launcher's grid
    -- has, and for the same reason - a grid is where a stray touch would cost
    -- the most, so a finger points and never enters.
    --
    -- Every cell opens, including the two that cannot hear anything: the page
    -- behind a dead cell is where the reason lives, and a press that does
    -- nothing at all is the one answer a user cannot tell from a crash.
    if i then sel = i; grid.selected = sel
    elseif sel >= 1 then open_list(PROTOS[sel]) end
  end,
  on_options = function() scan_all() end }
grid:set_children(cells)

-- Three states of ink, and the platform already has all three: full for the
-- radio listening right now, quiet for a radio that is resting, and disabled
-- for one this board cannot hear at all. So "which one is listening" needs no
-- marker of its own - it is the brightest cell, and the brightness moves.
--
-- Resting only means something while a round is running. Once it has finished
-- nothing is resting - the page is a snapshot and every radio it could use is
-- equally available - so they all go back to full and only the two this board
-- cannot hear stay faint.
local function paint_grid()
  local now = scanning and not open_key and ROUND[turn] or nil
  for i, p in ipairs(PROTOS) do
    local cell = cells[i]
    cell.disabled = not p.live
    cell.style = (p.live and (not scanning or p.key == now)) and "body" or "caption"
    -- A count, or nothing. Nothing is not zero: a cell with no badge has not
    -- been listened to yet, and one showing 0 has been and heard nobody.
    cell.badge = counts[p.key]
  end
end

-- The first round is the one the device has nothing to show during, so it is
-- the one round that gets the ring.
--
-- A radio that has never answered is asked for a fresh look the moment its turn
-- comes up, and asking for one is what raises the ring - so the ring is up from
-- the app opening until every radio has said something once, and then never
-- again however many rounds run behind the grid. A grid with counts on it is
-- not waiting.
--
-- Done in the same breath as the turn moving, rather than on the next tick, or
-- the ring would blink out for half a second between one radio finishing and
-- the next being asked - which reads as finished, and it is not.
local function first_look()
  local key = ROUND[turn]
  if fresh[key] then
    fresh[key] = nil
    rescan(key)
  end
end

-- Long A: look at all of them again.
--
-- An operation, and operations live behind a long press - that is the platform
-- rule, and this is the launcher's shape for it: one action, done rather than
-- offered, because a menu with a single item is a press spent on nothing.
--
-- The counts stay up while the new round runs. Blanking them would replace a
-- true-a-minute-ago answer with nothing at all, and nothing is the one thing
-- this page has that means "not looked yet".
function scan_all()
  for _, k in ipairs(ROUND) do fresh[k] = true end
  turn = 1
  left = #ROUND
  scanning = true
  first_look()
end

ui.screen{ grid }
first_look()

-- The app is its own loop: build the screen, then poll forever. A main chunk
-- that never returns stays live and is stepped between its sleeps, which is how
-- an app without a background thread keeps a screen fresh (sys.sleep yields to
-- the scheduler; the watchdog is never tripped because the app is not
-- spinning). B backs out of it, as everywhere.
--
-- Half a second between asks, not because an answer arrives that often - a
-- Wi-Fi sweep is five and a half seconds and a BLE window four - but because
-- asking is how an answer is collected, and one that has arrived should be
-- drawn rather than waited out.
while true do
  if open_key then
    -- A list is open: that radio gets every ask, so the page being read is the
    -- one that refreshes fastest, and it keeps refreshing for as long as you
    -- are looking at it.
    if poll(open_key) then fill_list() end
    sys.sleep(500)
  elseif scanning then
    if poll(ROUND[turn]) then
      left = left - 1
      if left > 0 then
        -- Hand the ring straight over, in the same breath as the turn moving
        -- rather than on the next tick: half a second of ring-off between one
        -- radio finishing and the next being asked reads as finished.
        turn = turn % #ROUND + 1
        first_look()
      else
        scanning = false
      end
    end
    paint_grid()
    sys.sleep(500)
  else
    -- The round is over. Nothing is being asked, nothing is changing, and the
    -- grid holds what it found until long A asks for another look - so this
    -- sleeps rather than redrawing a page nobody has touched.
    sys.sleep(1000)
  end
end
