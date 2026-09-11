-- Air Mouse (#59) - the device as a BLE mouse. Point and move, A to click.
--
-- The first app that is an output device. Everything else catnip runs draws the
-- thing it is for; this one's product happens on somebody else's screen, which
-- makes the screen here a status page about a thing you cannot see from it.
--
-- **The algorithm is the vendor's, deliberately.** MeowKit's own firmware ships
-- an air mouse for this exact board, and after three attempts of our own it is
-- the thing worth copying rather than competing with - it runs on the same
-- BMI270, in the same hand, and it works. What was taken from it: the gyroscope
-- range, the integration to an angle, the deadzone measured in degrees rather
-- than in rate, the gain, and above all the sub-pixel residual. What is ours:
-- the lift gesture, the state on the header and the LED, and the tests.
--
-- Three models were tried before this one, and each failed structurally rather
-- than for want of tuning. They are recorded because the next person will have
-- the same instincts:
--
--   *Tilt.* An accelerometer reports where gravity IS, so "not moving" reads
--   differently in every posture - held upright, one axis sits at a full g and
--   the cursor is pinned against an edge before the user has moved. And aiming
--   across is rotation about gravity, which moves no accelerometer axis at all.
--
--   *Translation.* Gravity removed, the remainder integrated into a velocity so
--   that sliding the device slides the cursor - the model a hand expects. It
--   does not survive the part: a MEMS accelerometer's noise floor is a few
--   milli-g and a deliberate movement peaks at a few hundred, and integrating
--   everything in between is indistinguishable from integrating noise. Tried on
--   the device and unusable.
--
--   *Rotation, our own arithmetic.* Right in shape and wrong in the details -
--   the axes were swapped against the vendor's, the gain was a third of theirs,
--   and every frame's fractional pixel was rounded away, so nothing slower than
--   half a pixel a frame moved the cursor at all. That last one is why it could
--   not be aimed: fine control is *entirely* made of movements that small.
--
-- **Wi-Fi is off for the duration**, and the driver does that rather than this
-- app - see device.mouse.start().
--
-- Leaving is the platform's: B, long B, or a fault all end the app, and the
-- main loop's invariant takes the mouse down with it.

-- ---------------------------------------------------------------------------
-- The numbers, and where they come from. The first block is the vendor's, and
-- changing one of those is disagreeing with a thing that demonstrably works.
-- ---------------------------------------------------------------------------

-- One degree of rotation is fifty pixels of cursor, and a frame's rotation has
-- to reach one fiftieth of a degree to count at all. Both axes the same: the
-- asymmetry our own version had was compensating for a gain that was too low to
-- begin with.
local DEG_PER_PIXEL = 0.02
local DEAD_DEG = 0.02

-- Which way round each axis goes. The vendor has both false and the same part
-- in the same orientation, so these start there; they are the one-line fix if a
-- direction comes out backwards.
local INV_X = false
local INV_Y = false

-- A frame longer than this is a frame something else held the loop up for, and
-- integrating it would turn a pause into a jump across the screen.
local DT_CLAMP = 0.100 -- seconds

-- What counts as the device not being moved, for the purpose of learning the
-- gyroscope's zero. Both senses have to agree: the accelerometer reading a
-- plain 1g means it is not being accelerated, and the rates being small means
-- it is not being turned. Either alone has a blind spot.
local STATIONARY_ACC_TOL = 0.08 -- g away from 1.0
local STATIONARY_GYRO = 8.0 -- dps
local BIAS_ALPHA = 0.01 -- how fast the zero is learned while still

-- Pressing a button shakes the device, and the shake arrives as a movement
-- that drags the cursor off the thing being clicked. Nothing is reported for a
-- moment after a press or a release.
local CLICK_SETTLE_MS = 200

-- ---- ours -----------------------------------------------------------------

local FRAME_MS = 20 -- 50 reports a second, what a mouse is expected to produce

-- How much of each new rate reading to believe.
--
-- A light low-pass, and deliberately not a bigger deadzone. Hand tremor is fast
-- and aiming is slow, so a filter separates them; a deadzone only separates big
-- from small, and the small movements it throws away are the ones fine aiming
-- is made of. This is the knob for "the cursor will not sit still" - lower is
-- steadier and laggier.
local RATE_SMOOTH = 0.45

-- Lifting the mouse. A real mouse that runs out of desk is picked up, moved
-- back and put down, and the cursor stays where it was; in the air the gesture
-- is speed. Well clear of any aiming movement: the vendor has no lift at all,
-- so this is the one place this app is deliberately doing more than its
-- reference, and it must not be able to swallow a fast but genuine aim.
local LIFT_ENTER = 350 -- dps, any axis
local LIFT_EXIT = 120 -- dps, all axes
local LIFT_SETTLE_MS = 120

-- ---------------------------------------------------------------------------
-- The pure part. Everything that decides where the cursor goes lives here,
-- because it is the only part of this app a host can check.
-- ---------------------------------------------------------------------------

-- Toward zero, not downward: this is the integer part of a signed number whose
-- fraction is about to be carried forward, and math.floor would bias every
-- negative frame by a whole pixel.
local function trunc(v)
  if v >= 0 then return math.floor(v) end
  return math.ceil(v)
end

-- The core, and the piece three earlier versions got wrong.
--
-- `ang_y` and `ang_z` are this frame's rotation in DEGREES - a rate multiplied
-- by the frame's own measured time, not by an assumed one. `rx`/`ry` are the
-- fractional pixels left over from previous frames.
--
-- **The residual is the whole of fine control.** A deliberate slow movement is
-- a fraction of a pixel per frame; rounding each frame to an integer throws
-- every one of those away and the cursor simply does not move until the hand
-- moves fast enough - which is exactly the speed at which it can no longer be
-- aimed. Carrying the fraction means a slow movement arrives as one pixel every
-- few frames instead of nothing at all.
--
-- Below the deadzone the residual decays rather than being kept, so a hand
-- trembling below the threshold cannot bank a jump for later.
--
-- Returns the step and the new residuals; nothing here is stateful.
function pointer_delta(ang_y, ang_z, rx, ry)
  if math.abs(ang_y) < DEAD_DEG and math.abs(ang_z) < DEAD_DEG then
    return 0, 0, rx * 0.5, ry * 0.5
  end

  local fx = ang_z / DEG_PER_PIXEL
  local fy = ang_y / DEG_PER_PIXEL
  if INV_X then fx = -fx end
  if INV_Y then fy = -fy end

  rx = rx + fx
  ry = ry + fy
  local dx, dy = trunc(rx), trunc(ry)
  rx, ry = rx - dx, ry - dy

  if dx > 127 then dx = 127 elseif dx < -127 then dx = -127 end
  if dy > 127 then dy = 127 elseif dy < -127 then dy = -127 end
  return dx, dy, rx, ry
end

-- Whether the device is being held still enough to learn the gyroscope's zero
-- from. Both senses, because each alone has a blind spot: a device turning at a
-- constant rate is not accelerating, and a device being shaken in a straight
-- line is not turning.
function is_stationary(acc_norm, gyro_norm)
  if not acc_norm or not gyro_norm then return false end
  return math.abs(acc_norm - 1.0) < STATIONARY_ACC_TOL and gyro_norm < STATIONARY_GYRO
end

-- The lift, as a state machine over the strongest rate on any axis.
function lift_next(state, mag, calm_ms)
  if state == "lifted" then
    if mag < LIFT_EXIT and calm_ms >= LIFT_SETTLE_MS then return "pointing" end
    return "lifted"
  end
  if mag >= LIFT_ENTER then return "lifted" end
  return "pointing"
end

-- ---------------------------------------------------------------------------
-- The screen. It cannot be read while the device is being pointed at anything,
-- so the LED carries the same story - see led_for().
-- ---------------------------------------------------------------------------

local state_line = ui.label{ id = "state", text = "starting", style = "title" }
local who = ui.label{ id = "who", text = "Pair with \"MeowKit Mouse\"", style = "body" }
local how = ui.label{ id = "how", text = "Point and move  -  A to click", style = "caption" }
-- DEBUG (#59): the rates, the zero being subtracted, and the lift state. Here
-- to settle the directions and the feel by reading rather than guessing; it
-- comes out once they are settled.
local dbg = ui.label{ id = "dbg", text = "", style = "caption" }
ui.screen{ state_line, who, how, dbg }

local SAYS = {
  off = { title = "Air Mouse", line = "not started" },
  advertising = { title = "Air Mouse - pairing", line = "waiting for a host" },
  connected = { title = "Air Mouse - connected", line = "connected" },
}

-- Repaint only when the word changed. Every touch of the tree is a full-panel
-- blit and this loop runs fifty times a second.
local shown
local function say(state)
  if state == shown then return end
  shown = state
  local s = SAYS[state] or SAYS.off
  ui.title(s.title)
  state_line.text = s.line
end

local function led_for(state, lift, phase)
  if state ~= "connected" then
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
local s_wy, s_wz = 0, 0
local res_x, res_y = 0, 0
local calm_ms = 0
local lift = "pointing"
local settle_until = 0
local a_was = false
local phase = 0
local last_ms = sys.now()

while true do
  local m = sensor.imu()
  local state = device.mouse.state()
  local now = sys.now()

  -- The frame's own length, not an assumed one. sys.sleep asks for 20 ms and
  -- the scheduler gives what it can; integrating a rate over the wrong interval
  -- is a cursor whose speed depends on what else the device is doing.
  local dt = (now - last_ms) / 1000
  last_ms = now
  if dt <= 0 then dt = FRAME_MS / 1000 end
  if dt > DT_CLAMP then dt = DT_CLAMP end

  if m.gx and m.gy and m.gz then
    local acc_norm = nil
    if m.ax and m.ay and m.az then
      acc_norm = math.sqrt(m.ax * m.ax + m.ay * m.ay + m.az * m.az)
    end
    local gyro_norm = math.sqrt(m.gx * m.gx + m.gy * m.gy + m.gz * m.gz)

    if is_stationary(acc_norm, gyro_norm) then
      bias_x = bias_x + BIAS_ALPHA * (m.gx - bias_x)
      bias_y = bias_y + BIAS_ALPHA * (m.gy - bias_y)
      bias_z = bias_z + BIAS_ALPHA * (m.gz - bias_z)
    end

    -- Pitch turns into vertical and yaw into horizontal, which is the vendor's
    -- mapping on this part: gyro.y is ΔY and gyro.z is ΔX. Ours had these the
    -- other way round.
    local wy = m.gy - bias_y
    local wz = m.gz - bias_z

    -- Smoothed before the integral, not after: a filter on the output would be
    -- smoothing pixels that have already been quantised and clamped.
    s_wy = s_wy + (wy - s_wy) * RATE_SMOOTH
    s_wz = s_wz + (wz - s_wz) * RATE_SMOOTH

    local mag = math.max(math.abs(m.gx - bias_x), math.abs(wy), math.abs(wz))
    calm_ms = (mag < LIFT_EXIT) and (calm_ms + FRAME_MS) or 0
    lift = lift_next(lift, mag, calm_ms)

    local dx, dy
    dx, dy, res_x, res_y = pointer_delta(s_wy * dt, s_wz * dt, res_x, res_y)

    -- A press or a release shakes the device; hold the pointer still across it
    -- so the click lands on what was being aimed at.
    local a_now = device.button("a")
    if a_now ~= a_was then
      settle_until = now + CLICK_SETTLE_MS
      a_was = a_now
    end
    local settling = now < settle_until

    if state == "connected" then
      -- Lifted means the device is being repositioned, not aimed: the buttons
      -- still report, because letting go of A during a flick must still reach
      -- the host, but the movement does not.
      if lift ~= "pointing" or settling then dx, dy = 0, 0 end
      device.mouse.move(dx, dy, a_now and device.mouse.LEFT or 0)
    end

    dbg.text = string.format("gy %.0f gz %.0f | b %.1f %.1f | r %.2f %.2f | %s", wy, wz,
      bias_y, bias_z, res_x, res_y, lift)
  else
    dbg.text = "no gyroscope on this device"
  end

  say(state)
  phase = phase + 0.12
  local r, g, b = led_for(state, lift, phase)
  device.led(r, g, b)

  sys.sleep(FRAME_MS)
end
