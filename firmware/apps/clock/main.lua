-- Clock (#73) - three screens, and each one is a different one of the five.
--
--   carousel   the launcher's cell: HH:MM, and nothing else
--   face       this app's root: the whole panel, `frame = "bare"`
--   setter     pushed above it: the standard setting page, bar and all
--
-- The face used to not exist. The launcher's cell had taken the job, and an app
-- you open to be shown again what you were already looking at is an app nobody
-- opens twice - so opening the clock went straight to the setter. That was the
-- wrong repair: the cell is one position on a ring being stepped past, and what
-- it owes a passing glance is the one fact somebody came for. The date, the
-- weekday and where the time came from are all worth reading, and reading takes
-- a screen. So the cell gives them up and the face takes them.
--
-- Short A climbs: the cell opens the face, the face opens the setter. Short B
-- comes back down the same three, and long B is the cat, from any of them.

local FIELDS = {
  -- In the order a date is written and read: `Y M D h m` is `YYYY-MM-DD` then
  -- `HH:MM`. Capital M is the month and small m the minute, which is the
  -- convention every date format already uses - the case is what tells them
  -- apart, so neither needs a word.
  { name = "Y", lo = 2000, hi = 2099 },
  { name = "M", lo = 1,    hi = 12 },
  { name = "D", lo = 1,    hi = 31 },
  { name = "h", lo = 0,    hi = 23 },
  { name = "m", lo = 0,    hi = 59 },
}

local DAYS = { "S", "M", "T", "W", "T", "F", "S" }
local MONTHS = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }

-- Days since the epoch, into a calendar date, and back. Howard Hinnant's
-- algorithms - the same pair rtc_time.c uses and test_rtc_time.c pins, ported
-- rather than re-derived. Done here rather than through os.date, which would
-- answer from the C library's idea of local time, and that is a timezone this
-- device has never been told.
local function civil(z)
  z = z + 719468
  local era = (z >= 0 and z or z - 146096) // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local y = yoe + era * 400
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp + (mp < 10 and 3 or -9)
  return y + (m <= 2 and 1 or 0), m, d
end

-- And back. Howard Hinnant's algorithm, exact for every date this chip can hold
-- and with no table of month lengths to disagree with the one above.
local function days_from_civil(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

-- 1 January 1970 was a Thursday, and the strip counts from Sunday. The same
-- line rtc_time.c has, for the same reason: a weekday derived from the date is
-- one that cannot disagree with it.
local function weekday(z) return (z + 4) % 7 end


-- ---------------------------------------------------------------- the face --

-- Seven letters, and today is the bright one. It is seven nodes because a node
-- carries one role and "these seven, with today's brighter than the rest" is
-- seven roles - which is exactly what `layout = "row"` exists for. The strip
-- gives them all one size, so the line does not shift under the eye at
-- midnight, and lets them differ in ink, which is the whole of what is being
-- said.
--
-- It was one label with brackets round today, which said the same thing in one
-- node and said it louder than it deserved: the strip earns its place by not
-- needing to be read at all, and punctuation is read.
local letters = {}
for i, d in ipairs(DAYS) do
  letters[i] = ui.label{ id = "day" .. i, text = d, style = "caption" }
end

local function light(wd)
  for i = 1, #letters do
    letters[i].style = (i == wd + 1) and "body" or "caption"
  end
end

-- Where the time came from, and only here.
--
-- Not on the carousel, where it would be three quarters of what is on screen;
-- and not on the setter, where a line saying the network already answered this
-- would be reading over the shoulder of somebody in the middle of answering it
-- themselves. On the face it is the one place a doubt about the time can be
-- settled, which is when anyone ever asks.
--
-- `service.ntp.last()` is about this run, not all time: the RTC survives a
-- power cycle holding a number whose origin it does not record, so nothing here
-- means "not since this boot" and the app says nothing rather than claiming
-- otherwise. A clock set by hand is not a failure and does not need annotating.
local function source_line()
  local t = service.ntp.last()
  if not t then return "" end
  -- Short because the face is a face: the bottom line already carries the
  -- weekday strip, and a sentence of prose beside it would run into it. Three
  -- letters and a time say the whole of it - this came off the network, and
  -- this is when.
  return string.format("NTP %02d:%02d", (t % 86400) // 3600, (t % 3600) // 60)
end

-- Three layers, three sizes, and none of them asks to be read twice: the date
-- waits in a corner for whoever wants it, the time meets the eye in the middle,
-- the strip is read without being read at all, and the source line is working
-- rather than an answer - so it is the one in the caption role.
-- Top-left and a step up from the body: it is the heading of the face, the one
-- line that says which day all of this is about, and it waits in a corner for
-- whoever wants it. `align` is what puts it in that corner - a line with
-- nothing else on it is both the first and the last thing on its line, and the
-- canvas would otherwise centre it on a guess.
local face_date = ui.label{ id = "face_date", text = "", style = "title",
                            align = "left" }
local face_time = ui.label{ id = "face_time", text = "--:--", style = "display" }
-- Centred at the foot, with the source line to its right: the two share the
-- bottom line and the strip is the one that should be in the middle of it,
-- because it is the one that is looked at rather than read.
local face_week = ui.list{ id = "face_week", layout = "row", align = "center" }
face_week:set_children(letters)
-- In the body role, not the caption one. A caption is a footnote, and where the
-- time came from is not a footnote: on the one screen where a doubt about the
-- time can be settled, it is the sentence that settles it, so it is written in
-- the ink that is meant to be read.
--
-- And no `align`. The canvas puts the last child of the bottom line at the right
-- edge on its own, and a bare face has no hint's corner to clear because
-- nothing is drawn over one. `align = "right"` here would be a second answer to
-- a question already answered, and the wide box it takes to range text right
-- would sit on top of the weekday strip to do it.
local face_src = ui.label{ id = "face_src", text = "", style = "body" }

-- An unset clock says so. The RTC comes up never having been set, and a
-- confident 00:00 on the first of January is worse than an admission: it will
-- be believed. So the same face shows `--:--` at the same size in the same
-- place, with no letter bracketed in the strip - seven positions, none of them
-- today - and a line under it saying what to press.
local function paint_face()
  local now = sensor.rtc()
  if now == 0 then
    face_time.text = "--:--"
    face_date.text = ""
    light(-1)
    face_src.text = "press A to set it"
    return
  end
  local days = now // 86400
  local y, m, d = civil(days)
  face_time.text = string.format("%02d:%02d", (now % 86400) // 3600,
                                 (now % 3600) // 60)
  face_date.text = string.format("%04d-%02d-%02d", y, m, d)
  light(weekday(days))
  face_src.text = source_line()
end


-- -------------------------------------------------------------- the setter --

local at = { 2026, 1, 1, 0, 0 }
local cols, setter = {}, nil
local sel = 1

local function fill(i)
  local f = FIELDS[i]
  return math.floor((at[i] - f.lo) * 100 / (f.hi - f.lo) + 0.5)
end

local function paint_setter()
  setter.selected = sel
  for i, col in ipairs(cols) do
    col.value = fill(i)
    col.value_text = (i == 2) and MONTHS[at[2]] or tostring(at[i])
    col.style = (i == sel) and "primary" or "body"
  end
end

-- Every field wraps. A clock is a ring in every one of its parts: the hour
-- after 23 is 0, and the month after December is January.
local function put(i, v)
  local f = FIELDS[i]
  local span = f.hi - f.lo + 1
  at[i] = f.lo + ((v - f.lo) % span + span) % span
  paint_setter()
end

for i, f in ipairs(FIELDS) do
  cols[i] = ui.label{ id = "set" .. i, text = f.name, steps = 0,
                      on_drag = function(self, pct)
                        sel = i
                        put(i, f.lo + math.floor(pct * (f.hi - f.lo) / 100 + 0.5))
                      end }
end

local function commit()
  sensor.rtc_set(days_from_civil(at[1], at[2], at[3]) * 86400
                 + at[4] * 3600 + at[5] * 60)
  paint_face()
end

-- What the column held when it was taken up, which is the only thing B needs to
-- be able to put it back.
local held = nil

setter = ui.list{ id = "setter", layout = "mixer",
  on_prev = function() if sel > 1 then sel = sel - 1 end held = nil paint_setter() end,
  on_next = function() if sel < #cols then sel = sel + 1 end held = nil paint_setter() end,
  -- A tap names the column it landed on; the platform then takes that column
  -- up, exactly as it does for a press of A.
  -- A on a column that is taken up writes the clock. There is no "are you
  -- sure": the page already shows exactly what it is about to write, and a
  -- question whose answer is on the screen is a question worth not asking.
  on_click = function(self, i)
    if i then sel = i; held = nil; paint_setter(); return end
    commit(); held = nil
  end,
  on_engage = function(self, i)
    if i then sel = i end
    held = at[sel]
    paint_setter()
  end,
  -- B on a column puts that column back and lets go. The clock is not written
  -- on the way back, because it was not written on the way out either - the
  -- columns are a number being built, and `commit` is the only thing that ever
  -- reaches the RTC.
  on_cancel = function()
    if held then at[sel] = held; held = nil; paint_setter() end
  end,
  -- Two presses of A: write it and leave. It pops rather than exiting the app,
  -- because there is a face under this screen now and leaving the device from
  -- the middle of the setter would skip the one screen that shows what was
  -- just set.
  on_save = function() commit(); held = nil; ui.pop() end,
  on_raise = function() put(sel, at[sel] + 1) end,
  on_lower = function() put(sel, at[sel] - 1) end }
setter:set_children(cols)

-- The setter opens on the time it is about to overwrite, not on the time it was
-- opened with once: the face may have been up for an hour, and a page that
-- started an hour behind is a page that sets the clock backwards for anyone who
-- only meant to change the year.
--
-- `frame = "standard"` is the disagreement that matters here. The manifest says
-- bare because the face is what the app usually is; this one screen hands the
-- bar back, because a page of columns wants the battery, the title and the
-- counter, and because the whole point of the setting screen is that it looks
-- the same wherever it is reached from.
local function open_setter()
  local now = sensor.rtc()
  if now == 0 then
    -- Nothing to start from, so start somewhere a person would rather edit than
    -- a 1970 they would have to walk all the way out of.
    at = { 2026, 1, 1, 0, 0 }
  else
    local y, m, d = civil(now // 86400)
    at = { y, m, d, (now % 86400) // 3600, (now % 3600) // 60 }
  end
  sel = 1
  ui.push{ setter, frame = "standard" }
  paint_setter()
end


-- ----------------------------------------------------------------- the app --

-- Short A opens the setter, where rule 7 would put it behind long A. The face
-- has no selection for long A to offer options on and nothing else short A
-- could mean, so the clock spends it on the one operation it has; long A does
-- the same thing, so a user arriving from any other app finds it where they
-- expect. Nothing is drawn either way, which is the part of rule 7 that binds.
--
-- Nothing claims B. The platform pops the setter for the face and leaves the
-- app from the face, which is exactly the climb this wants, and it costs no
-- code to say so.
ui.screen{ face_date, face_time, face_week, face_src,
           on_click = function() open_setter() end,
           on_options = function() open_setter() end }
paint_face()

-- The app is its own loop: build the screen, then repaint forever. A main chunk
-- that never returns stays live and is stepped between its sleeps, which is how
-- an app without a background thread keeps a face that has to be right about
-- the minute. Two seconds rather than sixty: a minute's sleep would show the
-- wrong minute for up to a minute after arriving.
while true do
  sys.sleep(2000)
  paint_face()
end
