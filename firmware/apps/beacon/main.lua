-- BLE Beacon (#60) - one steady advertisement, in either language a beacon speaks.
--
-- A beacon is not a connection. It is a small packet repeated on an interval,
-- and the whole of it is the bytes: an app that broadcasts one is choosing a
-- format, filling in an id, and handing the finished advertisement to the radio
-- to say over and over. The two formats a phone knows how to read are here -
-- Apple's iBeacon and Google's Eddystone - and they differ only in how those
-- bytes are laid out, which is why the choice between them lives up here in Lua
-- rather than down in the driver. device.beacon.start takes the raw bytes and
-- does not read them.
--
-- B leaves, and leaving stops the radio. Nothing here claims B: the platform
-- exits the app on it, and the main loop's invariant takes the beacon down the
-- moment the app is no longer running - the same rule that ends the mouse, for
-- the same reason. So there is no way out of this app that leaves it quietly
-- advertising behind a launcher.

-- ---- the identity a beacon carries ----------------------------------------
--
-- Fixed, because a beacon is a name you register once and hand out - the
-- adjustable id below is the one field a person changes to tell one of these
-- from another. iBeacon carries a 16-byte proximity UUID; Eddystone a 10-byte
-- namespace, which is that UUID's first ten bytes here so there is one constant
-- to trust rather than two to keep agreeing.
local UUID = { 0xCA, 0x7E, 0x1B, 0xEA, 0xC0, 0x00, 0x4C, 0xA7,
               0x9A, 0x11, 0xB3, 0xAC, 0x0F, 0xFE, 0xED, 0x00 }
local NAMESPACE = {}
for i = 1, 10 do NAMESPACE[i] = UUID[i] end

-- iBeacon's major is fixed and its minor carries the id. Measured power is the
-- RSSI a receiver should see at one metre - a receiver ranges off it, and 0xC5
-- is the -59 dBm every stock beacon ships with. Eddystone's ranging data is the
-- same idea at zero metres; -18 dBm is a typical value.
local IBEACON_MAJOR = { 0x00, 0x01 }
local IBEACON_POWER = 0xC5
local EDDY_TX = 0xEE

-- ---- the two payloads ------------------------------------------------------
--
-- Each returns the finished advertising data: the AD structures concatenated,
-- length bytes and type bytes and all, ready for the radio. Exposed on a global
-- rather than kept local because the byte layout is the one part of this app a
-- host has no radio to check - so the test drives these very functions, not a
-- copy of them.
beacon = {}

-- iBeacon: a Flags AD, then Apple's manufacturer-specific AD. 0x004C is Apple,
-- 0x02/0x15 is "this is an iBeacon, 21 bytes of it", and then the UUID, the
-- major, the minor and the measured power. 0x1A is the 26 bytes that follow the
-- length byte.
function beacon.ibeacon(id)
  return string.char(0x02, 0x01, 0x06,
                     0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15)
      .. string.char(table.unpack(UUID))
      .. string.char(IBEACON_MAJOR[1], IBEACON_MAJOR[2], 0x00, id, IBEACON_POWER)
end

-- Eddystone-UID: a Flags AD, then the complete-16-bit-service-UUIDs AD naming
-- 0xFEAA (Eddystone), then the service-data AD under that same UUID. 0x00 is the
-- UID frame type, then the ranging data, the 10-byte namespace, the 6-byte
-- instance (its low byte the id), and two reserved bytes. 0x17 is the 23 bytes
-- of service data after the length byte.
function beacon.eddystone(id)
  return string.char(0x02, 0x01, 0x06,
                     0x03, 0x03, 0xAA, 0xFE,
                     0x17, 0x16, 0xAA, 0xFE, 0x00, EDDY_TX)
      .. string.char(table.unpack(NAMESPACE))
      .. string.char(0x00, 0x00, 0x00, 0x00, 0x00, id)
      .. string.char(0x00, 0x00)
end

-- ---- the app ---------------------------------------------------------------

local FORMATS = { { name = "iBeacon",   build = beacon.ibeacon },
                  { name = "Eddystone", build = beacon.eddystone } }
-- The intervals a person actually sets a beacon to: a hundred milliseconds for
-- something you walk up to, a second for something you leave in a room.
local INTERVALS = { 100, 250, 500, 1000 }

local fmt = 1   -- which format
local id = 1    -- the adjustable id, 0..255, into minor / instance
local ivl = 2   -- which interval
local sel = 1   -- which row is focused

-- Put the current choices on the air. Called on every change, because the whole
-- app is one advertisement being edited in place: start() re-arms rather than
-- stacking, so handing it the new bytes is all it takes to change what is out
-- there.
local function rearm()
  device.beacon.start{ payload = FORMATS[fmt].build(id),
                       interval_ms = INTERVALS[ivl] }
end

local status = ui.label{ id = "status", align = "center", style = "caption" }
local rows = {
  ui.label{ id = "row_fmt" },
  ui.label{ id = "row_id" },
  ui.label{ id = "row_ivl" },
}

local list
local function paint()
  rows[1].text = string.format("Format    %s", FORMATS[fmt].name)
  rows[2].text = string.format("ID        %d", id)
  rows[3].text = string.format("Interval  %d ms", INTERVALS[ivl])
  status.text = device.beacon.state() == "advertising"
                and "advertising" or "off"
  list.selected = sel
end

-- A advances whichever row is focused: the format toggles, the id steps and
-- wraps a byte, the interval cycles the presets. Every field is a ring, so a
-- press never dead-ends at an edge.
local function advance()
  if sel == 1 then
    fmt = fmt % #FORMATS + 1
  elseif sel == 2 then
    id = (id + 1) % 256
  else
    ivl = ivl % #INTERVALS + 1
  end
  rearm()
  paint()
end

list = ui.list{ id = "fields", layout = "text",
  on_prev = function() if sel > 1 then sel = sel - 1; list.selected = sel end end,
  on_next = function() if sel < #rows then sel = sel + 1; list.selected = sel end end,
  -- A tap names the row it landed on and stops there; A with no index is the
  -- press that changes a value. The same split the scanner's grid makes.
  on_click = function(self, i)
    if i then sel = i; list.selected = sel else advance() end
  end }
list:set_children(rows)

ui.screen{ status, list }
rearm()
paint()

-- The app is its own loop: build the screen, then hold. Nothing here changes on
-- its own - a beacon is steady by definition - so this only wakes often enough
-- to keep the status line honest if the radio were ever to drop underneath it.
while true do
  sys.sleep(1000)
  paint()
end
