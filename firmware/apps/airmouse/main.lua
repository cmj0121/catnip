-- Air Mouse (#59) - the device as a BLE mouse. Wave to move, A to click.
--
-- The first app that is an output device. Everything else catnip runs draws the
-- thing it is for; this one's product happens on somebody else's screen, which
-- makes the screen here a status page about a thing you cannot see from it.
--
-- **It is held like a wand**, sideways, with the top edge of the screen pointing
-- at the host's display. That posture is the reason this reads a gyroscope and
-- not the accelerometer: pointing left and right is rotation about gravity, and
-- rotation about gravity moves no accelerometer axis at all. A tilt version was
-- built first and could not be made to work, for that reason and one other - it
-- had no neutral. An accelerometer reports where gravity is, so "not moving" is
-- a different reading in every posture, and any posture but the assumed one
-- pins the cursor against an edge. A rate has a true zero: not turning is zero
-- however the device is held.
--
-- **Wi-Fi is off for the duration**, and the driver does that rather than this
-- app - see device.mouse.start(). The two radios share one 2.4 GHz front end,
-- and a mouse is held for as long as the app is open.
--
-- Leaving is the platform's: B, long B, or a fault all end the app, and the
-- main loop's invariant takes the mouse down with it. There is deliberately no
-- teardown here to forget to run.

-- ---------------------------------------------------------------------------
-- The numbers. All of them here, because every one is a feel decision that can
-- only be settled with the device in a hand.
-- ---------------------------------------------------------------------------

local FRAME_MS = 20 -- 50 reports a second, what a mouse is expected to produce
local PX_PER_DEG = 10 -- cursor pixels per degree turned
local RATE_DEADZONE = 3 -- dps. Under this it is noise, not aim.
local MAX_STEP = 64 -- the most one report may carry (HID allows 127)

-- Which way round the two axes go. NOT derived - the part's +Z points out
-- through the glass, and holding the wand with the glass to your left rather
-- than your right mirrors both. One session with the device settles them; the
-- debug line below shows the raw rates so it can be settled by reading rather
-- than by guessing.
local INVERT_X = false
local INVERT_Y = false

-- Lifting the mouse. A real mouse that runs out of desk is picked up, moved
-- back and put down, and the cursor stays where it was. A wand cannot be picked
-- up, so the gesture is speed: a deliberate aim is tens of degrees a second, and
-- a flick to reset your wrist is several hundred.
--
-- Two thresholds and a settle, not one threshold - a single one would be
-- crossed several times during one flick and the cursor would stutter out in
-- bursts. Entering is immediate because the flick has already started; leaving
-- waits for the wrist to actually stop, or the tail of the flick back would be
-- read as the next aim.
local LIFT_ENTER = 250 -- dps, any axis
local LIFT_EXIT = 120 -- dps, all axes
local LIFT_SETTLE_MS = 120

-- Zeroing the gyroscope. Every gyroscope reads something other than zero when
-- it is still, and this app turns a rate straight into a movement - so the bias
-- is not an abstraction, it is the cursor sliding across the screen on its own
-- while the device lies on a table. Corrected continuously rather than once at
-- startup, because the bias moves with temperature and the part has just been
-- switched on.
local STILL_RATE = 5 -- dps, all axes, to count as "not being moved"
local STILL_MS = 300 -- how long that has to hold before the zero is believed
local BIAS_FOLLOW = 0.05 -- how fast the estimate walks toward the reading

-- ---------------------------------------------------------------------------
-- The mapping. Pure, because it is the only part of this app a host can check,
-- and because sign-and-threshold decisions are exactly where the bugs are.
-- ---------------------------------------------------------------------------

-- Round away from zero rather than down, so -2.6 is -3 and not -2. math.floor
-- alone biases every negative step toward zero, and over a stream of frames
-- that is a cursor that drifts right and down on its own.
local function round(v)
  if v >= 0 then return math.floor(v + 0.5) end
  return -math.floor(-v + 0.5)
end

-- One axis: a rate in degrees per second to a step in pixels. Measured from the
-- edge of the deadzone rather than from zero, so the first movement past it is
-- one pixel and not thirty - the difference between a cursor you can aim and
-- one that jumps the moment you are not perfectly still.
local function step(rate)
  if rate > -RATE_DEADZONE and rate < RATE_DEADZONE then return 0 end
  local past = rate > 0 and (rate - RATE_DEADZONE) or (rate + RATE_DEADZONE)
  local s = past * (FRAME_MS / 1000) * PX_PER_DEG
  if s > MAX_STEP then s = MAX_STEP end
  if s < -MAX_STEP then s = -MAX_STEP end
  return round(s)
end

-- Held as a wand with the screen's top edge pointing forward, the part's Y axis
-- lies along the vertical and its Z axis along your right - so turning left and
-- right (yaw) shows up on gy, and raising and lowering the tip (pitch) shows up
-- on gz. gx is the wand being rolled, which this app does not steer with.
--
-- Global so the host test can drive it directly; everything else in this file
-- is a radio or a clock.
function cursor_step(gy, gz)
  -- A device with no gyroscope reports no axes at all rather than zeros (see
  -- catnip_hal.h), so this is a real case, not a defensive one.
  if not gy or not gz then return 0, 0 end
  local dx = step(INVERT_X and -gy or gy)
  local dy = step(INVERT_Y and -gz or gz)
  return dx, dy
end

-- The lift, as a state machine over the largest rate on any axis. Global for
-- the same reason: a threshold with hysteresis is a thing worth a test.
function lift_next(state, mag, calm_ms)
  if state == "lifted" then
    if mag < LIFT_EXIT and calm_ms >= LIFT_SETTLE_MS then return "pointing" end
    return "lifted"
  end
  if mag >= LIFT_ENTER then return "lifted" end
  return "pointing"
end

-- ---------------------------------------------------------------------------
-- The screen. It cannot be read while the wand is pointed at anything, so the
-- LED carries the same story - see led_for().
-- ---------------------------------------------------------------------------

local state_line = ui.label{ id = "state", text = "starting", style = "title" }
local who = ui.label{ id = "who", text = "Pair with \"MeowKit Mouse\"", style = "body" }
local how = ui.label{ id = "how", text = "Point and wave  -  A to click", style = "caption" }
-- DEBUG (#59): the raw rates and what the lift is doing. This is here to settle
-- INVERT_X / INVERT_Y and the thresholds above by reading rather than guessing,
-- and comes out once they are settled.
local dbg = ui.label{ id = "dbg", text = "", style = "caption" }
ui.screen{ state_line, who, how, dbg }

local SAYS = {
  off = { title = "Air Mouse", line = "not started" },
  advertising = { title = "Air Mouse - pairing", line = "waiting for a host" },
  connected = { title = "Air Mouse - connected", line = "connected" },
}

-- Repaint only when the word changed. Every touch of the tree is a full-panel
-- blit and this loop runs fifty times a second; a page that rewrote the same
-- word every frame would spend the whole app redrawing a screen nobody is
-- looking at while the cursor it exists to move stuttered.
local shown
local function say(state)
  if state == shown then return end
  shown = state
  local s = SAYS[state] or SAYS.off
  ui.title(s.title)
  state_line.text = s.line
end

-- The LED says the same three things the header does, because in this posture
-- the screen is edge-on to the user and the header cannot be read. Blue
-- breathing is waiting to be paired, green is connected and pointing, amber is
-- lifted - and that last one earns its own colour, because "why is the cursor
-- ignoring me" is the question this app can most easily provoke.
local function led_for(state, lift, phase)
  if state ~= "connected" then
    -- A slow breath, so "nothing has happened yet" does not look like a fault.
    local b = 4 + math.floor(10 * (0.5 + 0.5 * math.sin(phase)))
    return 0, 0, b
  end
  if lift == "lifted" then return 14, 7, 0 end
  return 0, 10, 2
end

-- ---------------------------------------------------------------------------

if not device.mouse.start() then
  ui.title("Air Mouse")
  state_line.text = "no BLE on this device"
  who.text = "This build cannot present a mouse."
  how.text = "B goes back."
  dbg.text = ""
  device.led(0, 0, 0)
  while true do
    sys.sleep(200)
  end
end

local bias_x, bias_y, bias_z = 0, 0, 0
local still_ms, calm_ms = 0, 0
local lift = "pointing"
local phase = 0

while true do
  local m = sensor.imu()
  local gx, gy, gz = m.gx, m.gy, m.gz
  local state = device.mouse.state()

  if gx and gy and gz then
    local cx, cy, cz = gx - bias_x, gy - bias_y, gz - bias_z

    -- The largest rate on any axis. All three, not just the two that steer: a
    -- flick to reset the wrist rolls the wand as much as it turns it, and a
    -- lift that watched only the steering axes would miss half of them.
    local mag = math.max(math.abs(cx), math.abs(cy), math.abs(cz))

    -- Re-zero while nothing is happening. Judged on the corrected rate, so a
    -- bias that is already right keeps being confirmed rather than walking.
    if mag < STILL_RATE then
      still_ms = still_ms + FRAME_MS
    else
      still_ms = 0
    end
    if still_ms >= STILL_MS then
      bias_x = bias_x + (gx - bias_x) * BIAS_FOLLOW
      bias_y = bias_y + (gy - bias_y) * BIAS_FOLLOW
      bias_z = bias_z + (gz - bias_z) * BIAS_FOLLOW
    end

    calm_ms = (mag < LIFT_EXIT) and (calm_ms + FRAME_MS) or 0
    lift = lift_next(lift, mag, calm_ms)

    if state == "connected" then
      local dx, dy = 0, 0
      -- Lifted means the wand is being repositioned, not aimed: the buttons
      -- still report, because letting go of A during a flick must still reach
      -- the host, but the movement does not.
      if lift == "pointing" then dx, dy = cursor_step(cy, cz) end
      local buttons = device.button("a") and device.mouse.LEFT or 0
      device.mouse.move(dx, dy, buttons)
    end

    -- DEBUG (#59): raw rates, the bias being subtracted, and the lift state.
    dbg.text = string.format("gx %.0f gy %.0f gz %.0f | b %.1f %.1f %.1f | %s", gx, gy,
      gz, bias_x, bias_y, bias_z, lift)
  else
    dbg.text = "no gyroscope on this device"
  end

  say(state)
  phase = phase + 0.12
  local r, g, b = led_for(state, lift, phase)
  device.led(r, g, b)

  sys.sleep(FRAME_MS)
end
