-- Air Mouse (#59) - the device as a BLE mouse. Move it, and the cursor moves.
--
-- The first app that is an output device. Everything else catnip runs draws the
-- thing it is for; this one's product happens on somebody else's screen, which
-- makes the screen here a status page about a thing you cannot see from it.
--
-- **It is a mouse in the air, not a wand.** Move the device left and the cursor
-- goes left. That is the model this app went through two others to arrive at,
-- and the two it discarded are worth recording because each failed for a
-- structural reason rather than a tuning one:
--
--   *Tilt.* An accelerometer reports where gravity IS, so "not moving" is a
--   different reading in every posture - held upright rather than flat, one
--   axis sits at a full g and the cursor is pinned against an edge before the
--   user has moved at all. And the axis that matters most is invisible to it:
--   aiming across is rotation about gravity, which moves no accelerometer axis
--   at any speed.
--
--   *Rotation.* A gyroscope fixes both of those - a rate has a true zero, and
--   yaw is exactly what it measures. It is what every commercial air mouse
--   does. But it means the cursor only moves when the device TURNS, so sliding
--   the device sideways while keeping it aimed does nothing, and that is the
--   gesture people actually make.
--
-- So: linear acceleration, with gravity subtracted, integrated once into a
-- velocity. Integrating a velocity is a displacement, which is why moving the
-- device twenty centimetres to the right leaves the cursor moved and keeps it
-- there rather than springing back.
--
-- **The honest cost.** An integral accumulates its own error, so a velocity
-- derived this way walks away on its own. Two things hold it: a mild decay, and
-- a zero-velocity update - when the device is genuinely still, the velocity is
-- not damped toward zero, it is *set* to zero. Without the second one this app
-- would be a cursor that slides off the screen while the device sits on a desk.
--
-- **Wi-Fi is off for the duration**, and the driver does that rather than this
-- app - see device.mouse.start().
--
-- Leaving is the platform's: B, long B, or a fault all end the app, and the
-- main loop's invariant takes the mouse down with it.

-- ---------------------------------------------------------------------------
-- The numbers. All of them here, because every one is a feel decision that can
-- only be settled with the device in a hand.
-- ---------------------------------------------------------------------------

local FRAME_MS = 20 -- 50 reports a second, what a mouse is expected to produce
local FRAME_S = FRAME_MS / 1000

-- How fast the gravity estimate follows the accelerometer. Slow on purpose: it
-- has to track the device being turned over, which takes a second, while NOT
-- following a hand movement, which takes a tenth of one. At 50 Hz this is a
-- time constant of about a second, so anything faster than that is left behind
-- as linear acceleration - which is exactly the signal wanted.
local GRAV_FOLLOW = 0.02

-- Under this, an acceleration is the part's noise floor rather than a hand.
-- Applied before the integral, because noise that reaches the integral never
-- leaves it.
local ACC_DEADZONE = 0.02 -- g

-- What a velocity bleeds off per frame. Mild: every bit of decay is also the
-- cursor under-travelling the gesture, because the displacement it draws is the
-- integral of this. The zero-velocity update below is what actually controls
-- the drift; this only stops a long slow one building while the device is being
-- carried around.
local VEL_DECAY = 0.96

-- Cursor pixels per unit of velocity, and the two are NOT the same number: a
-- 16:9 screen is about 1.8 times as many pixels across as down, so the same
-- hand movement has to be worth more pixels horizontally to cover the same
-- *fraction of the screen* - which is what "as sensitive" means to a hand.
local PX_PER_V_X = 600
local PX_PER_V_Y = 340

local MAX_STEP = 64 -- the most one report may carry (HID allows 127)

-- The zero-velocity update. When the device is still by both senses - not
-- accelerating and not turning - the velocity is set to zero rather than
-- decayed toward it. Both senses, because either one alone has a blind spot: a
-- device being rotated steadily has little linear acceleration, and a device
-- being moved at a constant speed has none at all.
local STILL_ACC = 0.03 -- g
local STILL_RATE = 12 -- dps
local ZUPT_MS = 100

-- Lifting the mouse. A real mouse that runs out of desk is picked up, moved
-- back and put down, and the cursor stays where it was; in the air the gesture
-- is speed - a deliberate move is gentle, a flick to reset your reach is not.
-- Measured on acceleration now rather than rotation, because in this model a
-- reposition need not turn the device at all.
--
-- Two thresholds and a settle, not one: a single threshold is crossed several
-- times during one flick and the cursor would stutter out in bursts.
local LIFT_ENTER = 0.60 -- g
local LIFT_EXIT = 0.25 -- g
local LIFT_SETTLE_MS = 120

-- How sure of the grip the app has to be before it changes its mind about it.
local GRIP_CERTAIN = 0.5 -- g

-- ---------------------------------------------------------------------------
-- The pure part. Everything that decides where the cursor goes lives here,
-- because it is the only part of this app a host can check.
-- ---------------------------------------------------------------------------

-- Round away from zero rather than down, so -2.6 is -3 and not -2. math.floor
-- alone biases every negative step toward zero, and over a stream of frames
-- that is a cursor that drifts right and down on its own.
local function round(v)
  if v >= 0 then return math.floor(v + 0.5) end
  return -math.floor(-v + 0.5)
end

-- Below the deadzone is nothing; above it, measured from the edge rather than
-- from zero, so the first real movement is small instead of a jump.
local function dead(a)
  if a > -ACC_DEADZONE and a < ACC_DEADZONE then return 0 end
  return a > 0 and (a - ACC_DEADZONE) or (a + ACC_DEADZONE)
end

-- Which way the glass is facing, from gravity alone.
--
-- Held as a mouse-in-the-air with the screen's top edge forward, the part's Y
-- axis lies along the world's vertical - so gravity is almost entirely on it,
-- and its *sign* is the whole of the difference between holding the device with
-- the glass to your right and with the glass to your left. Those two grips are
-- exact mirrors, so one reading settles both axes.
--
-- The accelerometer's convention (see imu_map.c) is that the axis reading
-- positive is the one pointing at the ceiling.
--
-- `was` is returned unchanged in between, which is the point of taking it: a
-- device being waved passes through the middle several times a second, and an
-- app that re-decided there would invert the cursor mid-gesture.
function grip_flipped(gravity_y, was)
  if not gravity_y then return was end
  if gravity_y > GRIP_CERTAIN then return true end
  if gravity_y < -GRIP_CERTAIN then return false end
  return was
end

-- One frame of the integral: a velocity, a linear acceleration, and the next
-- velocity. Separate from the loop so the drift behaviour can be driven
-- directly - a hundred frames of a constant small offset is a test, and is also
-- exactly the failure this app has to not have.
function velocity_step(v, acc)
  return v * VEL_DECAY + dead(acc) * FRAME_S
end

-- Velocity to a cursor step. The two gains differ; see above.
function cursor_step(vx, vy)
  if not vx or not vy then return 0, 0 end
  local function px(v, gain)
    local s = v * gain
    if s > MAX_STEP then s = MAX_STEP end
    if s < -MAX_STEP then s = -MAX_STEP end
    return round(s)
  end
  return px(vx, PX_PER_V_X), px(vy, PX_PER_V_Y)
end

-- Which linear axes the screen's across and down actually are.
--
-- Held with the screen's top edge forward, the part's Z axis lies along your
-- right and its Y along the vertical - so moving the device right shows up on
-- az, and moving it up on ay. Screen y counts downward, which is the sign on
-- the second one. Both mirror with the grip.
--
-- These signs are NOT settled by argument. The last version derived one of them
-- correctly and the other backwards, and the difference was invisible until a
-- hand moved: the chain runs through the part's mounting, imu_map's frame, the
-- panel rotation and the direction screen y counts, and being right about three
-- of those four still gives a cursor that goes the wrong way. The debug line
-- below shows both raw axes so this is settled by reading.
function axes_for(lin_y, lin_z, flipped)
  local across = flipped and -lin_z or lin_z
  local down = flipped and -lin_y or lin_y
  return across, down
end

-- The lift, as a state machine over the strongest acceleration on any axis.
function lift_next(state, mag, calm_ms)
  if state == "lifted" then
    if mag < LIFT_EXIT and calm_ms >= LIFT_SETTLE_MS then return "pointing" end
    return "lifted"
  end
  if mag >= LIFT_ENTER then return "lifted" end
  return "pointing"
end

-- ---------------------------------------------------------------------------
-- The screen. It cannot be read while the device is being moved, so the LED
-- carries the same story - see led_for().
-- ---------------------------------------------------------------------------

local state_line = ui.label{ id = "state", text = "starting", style = "title" }
local who = ui.label{ id = "who", text = "Pair with \"MeowKit Mouse\"", style = "body" }
local how = ui.label{ id = "how", text = "Move it  -  A to click", style = "caption" }
-- DEBUG (#59): the linear axes, the grip, and what the lift is doing. Here to
-- settle the signs and the gains by reading rather than guessing; it comes out
-- once they are settled.
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

-- The LED says the same three things the header does, because while the device
-- is being moved the screen cannot be read. Breathing blue is waiting to be
-- paired, green is connected, amber is lifted - and that last earns a colour of
-- its own, because "why is the cursor ignoring me" is the question this app can
-- most easily provoke.
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

-- nil until the first sample: the estimate starts AT the first reading rather
-- than at zero, or the first second of the app is a cursor being flung by a
-- gravity estimate still climbing toward 1g.
local gx_, gy_, gz_ = nil, nil, nil
local vx, vy = 0, 0
local still_ms, calm_ms = 0, 0
local lift = "pointing"
local flipped = false
local phase = 0

while true do
  local m = sensor.imu()
  local state = device.mouse.state()

  if m.ax and m.ay and m.az then
    -- The gravity estimate, and what is left over after it.
    if not gx_ then
      gx_, gy_, gz_ = m.ax, m.ay, m.az
    else
      gx_ = gx_ + (m.ax - gx_) * GRAV_FOLLOW
      gy_ = gy_ + (m.ay - gy_) * GRAV_FOLLOW
      gz_ = gz_ + (m.az - gz_) * GRAV_FOLLOW
    end
    local lx, ly, lz = m.ax - gx_, m.ay - gy_, m.az - gz_

    flipped = grip_flipped(gy_, flipped)
    local across, down = axes_for(ly, lz, flipped)

    local acc_mag = math.max(math.abs(lx), math.abs(ly), math.abs(lz))
    local rate_mag = 0
    if m.gx and m.gy and m.gz then
      rate_mag = math.max(math.abs(m.gx), math.abs(m.gy), math.abs(m.gz))
    end

    calm_ms = (acc_mag < LIFT_EXIT) and (calm_ms + FRAME_MS) or 0
    lift = lift_next(lift, acc_mag, calm_ms)

    vx = velocity_step(vx, across)
    vy = velocity_step(vy, down)

    -- The zero-velocity update. Set, not decayed: a velocity that is only ever
    -- damped keeps whatever error it has picked up, and this app draws the
    -- integral of that error.
    if acc_mag < STILL_ACC and rate_mag < STILL_RATE then
      still_ms = still_ms + FRAME_MS
      if still_ms >= ZUPT_MS then vx, vy = 0, 0 end
    else
      still_ms = 0
    end

    if state == "connected" then
      local dx, dy = 0, 0
      -- Lifted means the device is being repositioned, not aimed: the buttons
      -- still report, because letting go of A during a flick must still reach
      -- the host, but the movement does not.
      if lift == "pointing" then dx, dy = cursor_step(vx, vy) end
      local buttons = device.button("a") and device.mouse.LEFT or 0
      device.mouse.move(dx, dy, buttons)
    end

    -- DEBUG (#59): the two linear axes that steer, the velocity they have
    -- integrated to, the grip gravity says this is, and the lift state.
    dbg.text = string.format("ly %.2f lz %.2f | v %.3f %.3f | %s | %s", ly, lz, vx, vy,
      flipped and "left" or "right", lift)
  else
    dbg.text = "no accelerometer on this device"
  end

  say(state)
  phase = phase + 0.12
  local r, g, b = led_for(state, lift, phase)
  device.led(r, g, b)

  sys.sleep(FRAME_MS)
end
