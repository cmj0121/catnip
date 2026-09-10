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

-- No "scanning..." here, and that is the point of it being gone. Waiting is a
-- state the platform draws - one ring, in one place, at one rhythm, over every
-- app that ever waits for anything - and an app that wrote its own would be one
-- more shape a user has to learn for a thing they already know. The status line
-- is left for what this app knows and the platform does not: how many networks
-- came back.
local rows = ui.list{ id = "aps" }
local status = ui.label{ id = "status", text = "" }

-- How strong, in words rather than a raw dBm nobody reads at a glance. The
-- thresholds are the usual ones: -60 and up is a room away, -75 and up is
-- through a wall, below that is the edge of hearing.
local function bars(rssi)
  if rssi >= -60 then return "||||"
  elseif rssi >= -70 then return "|||."
  elseif rssi >= -80 then return "||.."
  else return "|..." end
end

local function refresh()
  local aps = service.wifi.scan()
  if not aps then
    -- nil is "still scanning", not "nothing there": leave what is shown.
    return
  end
  local cells = {}
  for i, ap in ipairs(aps) do
    local name = ap.ssid
    if name == "" then name = "(hidden)" end
    cells[i] = ui.label{ id = "ap" .. i,
                         text = string.format("%s  %s  ch%d", bars(ap.rssi), name,
                                              ap.channel) }
  end
  rows:set_children(cells)
  if #aps == 0 then
    status.text = "nothing on the air"
  else
    status.text = string.format("%d network%s", #aps, #aps == 1 and "" or "s")
  end
end

ui.screen{ status, rows }

-- The app is its own loop: build the screen, then poll forever. A main chunk
-- that never returns stays live and is stepped between its sleeps, which is how
-- an app without a background thread keeps a screen fresh (sys.sleep yields to
-- the scheduler; the watchdog is never tripped because the app is not spinning).
-- B backs out of it, as everywhere.
while true do
  refresh()
  sys.sleep(1500)
end
