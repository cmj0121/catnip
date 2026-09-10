-- Clock (#73) - setting the time.
--
-- This app has no face. It used to have one, and the launcher took the job: the
-- clock's carousel cell shows the time itself at the size a glance wants, with
-- the date in the header beside it (#75). An app you open to be shown again
-- what you were already looking at is an app nobody opens twice, so opening it
-- does the one thing the cell cannot - it changes the time.
--
-- The setter is `ui.list{ layout = "mixer" }`, the shape the preference page
-- uses, so it costs a user nothing to learn: left and right choose a field, up
-- and down change it, and a finger dragged up a column sets it directly.
--
-- A saves and leaves; B leaves without saving. Neither is a mode: there is no
-- "now you are editing" state to be in or out of, because every column here
-- exists to be edited and the page was opened by somebody who came to edit one.

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

-- Days since the epoch, into a calendar date, and back. Howard Hinnant's
-- algorithms - the same pair rtc_time.c uses and test_rtc_time.c pins, ported
-- rather than re-derived. Done here rather than through os.date, which would
-- answer from the C library's idea of local time, and that is a timezone this
-- device has never been told.
--
-- The forward direction used to be a year-then-month loop with its own table of
-- month lengths and its own leap rule, sitting directly above the algorithm
-- that needs neither: two calendars in one file, one of them untested.
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

local now = sensor.rtc()
local at
if now == 0 then
  -- Nothing to start from, so start somewhere a person would rather edit than a
  -- 1970 they would have to walk all the way out of.
  at = { 2026, 1, 1, 0, 0 }
else
  local y, m, d = civil(now // 86400)
  at = { y, m, d, (now % 86400) // 3600, (now % 3600) // 60 }
end

local cols, list = {}, nil
local sel = 1

local function fill(i)
  local f = FIELDS[i]
  return math.floor((at[i] - f.lo) * 100 / (f.hi - f.lo) + 0.5)
end

local function paint()
  list.selected = sel
  for i, col in ipairs(cols) do
    col.value = fill(i)
    col.value_text = tostring(at[i])
    col.style = (i == sel) and "primary" or "body"
  end
end

-- Every field wraps. A clock is a ring in every one of its parts: the hour
-- after 23 is 0, and the month after December is January.
local function put(i, v)
  local f = FIELDS[i]
  local span = f.hi - f.lo + 1
  at[i] = f.lo + ((v - f.lo) % span + span) % span
  paint()
end

for i, f in ipairs(FIELDS) do
  cols[i] = ui.label{ id = "set" .. i, text = f.name, steps = 0,
                      on_drag = function(self, pct)
                        sel = i
                        put(i, f.lo + math.floor(pct * (f.hi - f.lo) / 100 + 0.5))
                      end }
end

list = ui.list{ id = "setter", layout = "mixer",
  on_prev = function() if sel > 1 then sel = sel - 1 end paint() end,
  on_next = function() if sel < #cols then sel = sel + 1 end paint() end,
  -- Short A is the whole confirmation. There is no "are you sure": the page
  -- already shows exactly what it is about to write, and a question whose
  -- answer is on the screen is a question worth not asking.
  on_click = function()
    sensor.rtc_set(days_from_civil(at[1], at[2], at[3]) * 86400
                   + at[4] * 3600 + at[5] * 60)
    sys.exit()
  end,
  on_raise = function() put(sel, at[sel] + 1) end,
  on_lower = function() put(sel, at[sel] - 1) end }
list:set_children(cols)

-- Where the time you are about to overwrite came from.
--
-- It matters most on exactly this page: a clock the network set is already
-- right, and hand-setting it is usually a mistake somebody is one press away
-- from making. So the setter says so, and says nothing when there is nothing to
-- say - a clock set by hand is not a failure and does not need annotating.
--
-- `service.ntp.last()` is about this run, not all time: the RTC survives a
-- power cycle holding a number whose origin it does not record, so "not synced"
-- here means "not since this boot", and the wording avoids claiming otherwise.
local function source_line()
  local t = service.ntp.last()
  if not t then return "" end
  return string.format("network time, synced %02d:%02d",
                       (t % 86400) // 3600, (t % 3600) // 60)
end

-- In the body role, not the caption one. A caption is a footnote, and this is
-- not a footnote: on the page where you are about to overwrite the time, "this
-- came off the network" is the most useful sentence on the screen, and the one
-- most likely to stop a mistake. It is read, so it is written in the ink that
-- is meant to be read.
-- Ranged right, because the bottom-left corner of every screen belongs to the
-- control hint and a line starting there would be read through four arrows.
local source = ui.label{ id = "source", text = source_line(), style = "body",
                         align = "right" }

-- Nothing claims B, so the platform takes it and leaves - which is exactly
-- "leave without saving", and it costs no code to say so. The write happens on
-- A and nowhere else, so no way out of here saves by accident.
ui.screen{ list, source }
paint()
