-- Air Mouse (#59) - the device as a BLE mouse. Tilt to move, A to click.
--
-- The first app that is an output device. Everything else catnip runs draws on
-- its own screen; this one's whole product happens on somebody else's, which
-- makes the screen here a status page about a thing you cannot see from it.
-- That is why the header carries the connection and not the app's name: "is a
-- host listening" is the only question this screen can usefully answer, and a
-- mouse nobody has connected to looks exactly like a mouse that is working.
--
-- **Wi-Fi is off for the duration**, and the driver does that rather than this
-- app. The two radios share one 2.4 GHz front end, and a mouse is held for as
-- long as the app is open rather than for the few seconds a scan takes - so the
-- association goes down at device.mouse.start() and comes back when the mouse
-- does. An app that had to remember it would not fail visibly when it forgot;
-- it would produce a mouse that stutters, which is the hardest kind of bug to
-- attribute. The header stops naming a network on its own, because while the
-- link is parked the radio really is not on one.
--
-- Leaving is the platform's: B, long B, or a fault all end the app, and the
-- main loop's invariant takes the mouse down with it. There is deliberately no
-- teardown here to forget to run.

-- Turning a tilt into a step. This is the only part of the app a host can
-- check, so as much as possible lives in it and the loop below stays thin.
--
-- The axes are the accelerometer's and the mapping is measured, not guessed -
-- see the table in device/imu_map.c, where each screen edge was held at the
-- ceiling and confirmed. Two rows are what this needs:
--
--   ax > 0  =>  the bottom edge is up
--   ay > 0  =>  the left edge is up
--
-- and the cursor follows the marble rather than the arrow: it rolls toward the
-- side that has been lowered, which is what makes "tilt it right, it goes
-- right" true. Bottom edge up means the top is the low side, so the cursor goes
-- up the screen; left edge up means the right is low, so it goes right. Hence
-- dx = +ay and dy = -ax, which is the negation of imu_map's arrow, and it
-- should be: that arrow points at the raised edge, a marble does the opposite.
local DEADZONE = 0.10 -- g. Below this the device is level and nothing moves.
local GAIN = 26 -- steps per g past the deadzone
local MAX_STEP = 12 -- the most one frame may ask for
local SMOOTH = 0.35 -- how much of each new reading to believe

-- Round away from zero rather than down, so a step of -2.6 is -3 and not -2.
-- math.floor alone biases every negative step toward zero, which over a stream
-- of frames is a cursor that drifts right and down on its own.
local function round(v)
  if v >= 0 then return math.floor(v + 0.5) end
  return -math.floor(-v + 0.5)
end

-- One axis, in g, to one axis of cursor step. Zero inside the deadzone, and
-- measured from the edge of it rather than from zero - so the first step out of
-- the deadzone is one pixel and not thirteen, which is the difference between a
-- cursor you can aim and one that jumps as soon as you are not perfectly level.
local function step(v)
  if v > -DEADZONE and v < DEADZONE then return 0 end
  local past = v > 0 and (v - DEADZONE) or (v + DEADZONE)
  local s = past * GAIN
  if s > MAX_STEP then s = MAX_STEP end
  if s < -MAX_STEP then s = -MAX_STEP end
  return round(s)
end

-- The whole mapping: two smoothed acceleration axes to one cursor step.
-- Exposed as a global so the host test can drive it directly; there is nothing
-- else in this file a test can reach, because everything else is a radio.
function cursor_step(ax, ay)
  -- A device with no accelerometer reports no axes at all rather than zeros
  -- (see catnip_hal.h), so this is a real case and not a defensive one.
  if not ax or not ay then return 0, 0 end
  return step(ay), step(-ax)
end

-- The screen. Three lines that do not change and one word that does.
local state_line = ui.label{ id = "state", text = "starting", style = "title" }
local who = ui.label{ id = "who", text = "Pair with \"MeowKit Mouse\"", style = "body" }
local how = ui.label{ id = "how", text = "Tilt to move  -  A to click", style = "caption" }
local net = ui.label{ id = "net", text = "Wi-Fi is off while the mouse is on", style = "caption" }
ui.screen{ state_line, who, how, net }

-- What each state says, on the header and in the body. The header is the one
-- the user reads without looking away from the other screen.
local SAYS = {
  off = { title = "Air Mouse", line = "not started" },
  advertising = { title = "Air Mouse - pairing", line = "waiting for a host" },
  connected = { title = "Air Mouse - connected", line = "connected" },
}

-- Repaint only when the word actually changed. Every touch of the tree is a
-- full-panel blit, and this loop runs fifty times a second: a page that
-- rewrote the same word every frame would spend the whole app redrawing a
-- screen nobody is looking at, while the cursor it exists to move stuttered.
local shown
local function say(state)
  if state == shown then return end
  shown = state
  local s = SAYS[state] or SAYS.off
  ui.title(s.title)
  state_line.text = s.line
end

-- Become a mouse. False is a device that has no radio for this rather than one
-- nobody has connected to yet, and those must not read the same: a page that
-- told the user to go and pair with something that is not being advertised is a
-- page that wastes their afternoon.
if not device.mouse.start() then
  ui.title("Air Mouse")
  state_line.text = "no BLE on this device"
  who.text = "This build cannot present a mouse."
  how.text = "B goes back."
  net.text = ""
  while true do
    sys.sleep(200)
  end
end

-- The frame loop. Twenty milliseconds is fifty reports a second, which is about
-- what a mouse is expected to produce and cheap here because a report is a
-- notification on an open connection, not a redraw - the screen only changes
-- when the connection does.
local s_ax, s_ay = 0, 0
while true do
  local m = sensor.imu()
  local ax, ay = m.ax, m.ay
  if ax and ay then
    -- Low-pass both axes before mapping. A hand is never still, and an
    -- unsmoothed accelerometer turns that tremor into a cursor that will not
    -- settle on anything small enough to click.
    s_ax = s_ax + (ax - s_ax) * SMOOTH
    s_ay = s_ay + (ay - s_ay) * SMOOTH
  end

  local state = device.mouse.state()
  say(state)

  if state == "connected" then
    local dx, dy = cursor_step(s_ax, s_ay)
    local buttons = device.button("a") and device.mouse.LEFT or 0
    -- Sent even when both steps are zero: that is how a held button carries on
    -- being held while the device is still, and how a release is reported.
    device.mouse.move(dx, dy, buttons)
  end

  sys.sleep(20)
end
