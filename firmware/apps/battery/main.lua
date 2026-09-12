-- Battery - the charge and the number, on a bare face.
--
-- A glance app in the shape of the Clock's face: it shows one fact and takes no
-- input. There is no A and no long A here - nothing on this screen is a thing
-- to press - so the screen carries no on_click and no on_options. B leaves, the
-- way it leaves every app; that is the platform's, not this app's.
--
-- The number is drawn in a prose face, not the `display` one the clock uses:
-- `display` is digits, a colon and a dot only, so a percent sign asked of it
-- simply would not appear - and "89" without its "%" is a different, quieter
-- claim than the one this app exists to make. On a bare face a prose role is
-- the panel's largest, which is big enough.
--
-- device.battery() answers a whole percent, or -1 when the gauge cannot be
-- read. -1 is not 0: a cell that cannot be measured is drawn as unknown rather
-- than as flat, for the same reason the clock says --:-- rather than midnight.

local CELLS = 10

local pct = ui.label{ id = "pct", text = "--", align = "center", style = "body" }
local gauge = ui.label{ id = "gauge", text = "", align = "center", style = "body" }
ui.screen{ gauge, pct }

local function paint()
  local b = device.battery()
  if b and b >= 0 then
    pct.text = b .. "%"
    local n = math.floor(b / 100 * CELLS + 0.5)
    if n < 0 then n = 0 elseif n > CELLS then n = CELLS end
    gauge.text = "[" .. string.rep("#", n) .. string.rep("-", CELLS - n) .. "]"
  else
    pct.text = "--%"
    gauge.text = "[" .. string.rep("-", CELLS) .. "]"
  end
end

paint()
while true do
  sys.sleep(2000)
  paint()
end
