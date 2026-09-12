-- USB HID - the hub for everything the composite keyboard does (#61).
--
-- The device ships as a USB keyboard the host can always see, but which types
-- nothing until it is turned on here. That is the whole shape of this app:
--
--   * Short A toggles typing on and off. The header grows a keyboard glyph
--     while it is on, so the state is legible from any screen, not only this
--     one. Off is where every boot starts.
--   * Long A opens the menu of HID features - BadUSB, and Mass Storage (which
--     lands in a later batch). A feature is greyed when it cannot run: when HID
--     is off, or - for BadUSB - when there is no script on the card. A greyed
--     item's page says the true reason rather than doing nothing, the way the
--     Scanner's dead radios do.
--
-- BadUSB runs a Ducky Script from /sd/catnip/badusb/*.txt over the keyboard. It
-- is dual-use, and it stays inside the frame #64 asks for: nothing types until
-- HID is deliberately enabled and a script is deliberately picked and run;
-- there is no autostart and nothing runs in the background; the screen says
-- what a run will do before it does it.
--
-- The parser and the US-QWERTY keymap are in host-tested C (service.usb.*); the
-- typing loop is here, because a DELAY has to yield to the scheduler rather than
-- freeze the screen. So the handlers only set what to do, and the app's own loop
-- at the bottom - the same shape the Scanner uses - is what does it, where
-- sys.sleep is legal and B is still heard.

-- ---- HID state -------------------------------------------------------------

local function hid_on()
  return service.usb.hid_enabled()
end

-- ---- the features, and when each one is reachable --------------------------

local FEATURES = {
  { key = "badusb", name = "BadUSB" },
  { key = "msc",    name = "Mass Storage" },
}

-- The .txt scripts on the card, or an empty list when there is no card, no
-- folder, or nothing in it. Guarded, because fs.list raises when there is no
-- card at all and returns nil when the folder is not there - two ways of saying
-- "no scripts", both of which this turns into the empty list.
local function list_scripts()
  local out = {}
  local ok, entries = pcall(fs.list, "catnip/badusb")
  if ok and entries then
    for _, e in ipairs(entries) do
      if not e.is_dir and e.name:match("%.txt$") then
        out[#out + 1] = e.name
      end
    end
  end
  table.sort(out)
  return out
end

-- What is on the card right now. Re-read whenever the menu or the picker opens,
-- because a script may have been dropped on since the app started.
local scripts = {}

local function feat_available(key)
  -- Mass Storage is not typing, so it does not wait on HID being on - only on
  -- there being a card to hand over.
  if key == "msc" then return service.usb.has_card() end
  if not hid_on() then return false end
  if key == "badusb" then return #scripts > 0 end
  return false
end

local function feat_why(key)
  if key == "msc" then
    if not service.usb.has_card() then
      return "no SD card in the slot\ninsert one first"
    end
    return "A hands the whole card to the\ncomputer as a USB drive"
  end
  if not hid_on() then
    return "HID is off\nenable it first (B, then A)"
  end
  if key == "badusb" then
    if #scripts == 0 then
      return "no scripts found in\n/sd/catnip/badusb/*.txt"
    end
    return "A opens the script list\nthe chosen one types to the host"
  end
  return ""
end

-- ---- what the picker's choice asks the loop to do --------------------------

-- The handlers below never type; they set this, and the loop at the bottom
-- picks it up. A run that happened inside a handler could not sleep between
-- keystrokes without freezing the screen, and a DELAY that froze the screen
-- would also stop B being heard.
local pending_run = nil

-- ---- the root screen -------------------------------------------------------

-- The mode, plainly, so entering the app answers "is typing on" at a glance.
-- A prose role and not `display`: `display` is digits and a colon only, so its
-- letters would come out as missing-glyph boxes; `title` is the panel's largest
-- prose and carries the whole word.
local state = ui.label{ id = "hid_state", align = "center", style = "title", text = "" }
local caption = ui.label{ id = "hid_cap", align = "center", style = "caption",
                          text = "A toggles typing\nlong A for features" }

local function paint_root()
  state.text = hid_on() and "HID: enabled" or "HID: disabled"
end

-- The features menu, built once and refreshed on open.
local feat_screen, feat_list, feat_note
local feat_rows = {}
local feat_sel = 1

-- The script picker, built on first use.
local pick_screen, pick_list, pick_note
local pick_sel = 1

-- ---- the features menu -----------------------------------------------------

local open_picker -- defined below; the features menu opens it
local open_msc    -- defined below; the features menu opens it too

local function paint_feat_note()
  local f = FEATURES[feat_sel]
  if f then feat_note.text = feat_why(f.key) end
end

local function refresh_features()
  scripts = list_scripts()
  for i, f in ipairs(FEATURES) do
    local avail = feat_available(f.key)
    local row = feat_rows[i]
    row.text = f.name
    row.disabled = not avail
    -- Greyed is the caption ink, lit is the body ink - the Scanner's three
    -- states of ink, minus the "resting" one a menu has no use for.
    row.style = avail and "body" or "caption"
  end
  paint_feat_note()
end

local function activate_feature()
  local f = FEATURES[feat_sel]
  if not f then return end
  if not feat_available(f.key) then
    -- A press on a greyed item says why rather than doing nothing, which is the
    -- one answer a user cannot tell from a crash.
    paint_feat_note()
    return
  end
  if f.key == "badusb" then open_picker()
  elseif f.key == "msc" then open_msc() end
end

local function open_features()
  scripts = list_scripts()
  feat_sel = 1
  ui.title("HID features")
  if not feat_list then
    for i, f in ipairs(FEATURES) do
      feat_rows[i] = ui.label{ id = "feat" .. i, text = f.name }
    end
    feat_list = ui.list{ id = "feat_list", layout = "text",
      on_prev = function()
        if feat_sel > 1 then feat_sel = feat_sel - 1; feat_list.selected = feat_sel
          paint_feat_note() end
      end,
      on_next = function()
        if feat_sel < #FEATURES then feat_sel = feat_sel + 1; feat_list.selected = feat_sel
          paint_feat_note() end
      end,
      on_click = function(self, index)
        if index then feat_sel = index; self.selected = index end
        activate_feature()
      end }
    feat_list:set_children(feat_rows)
    feat_note = ui.label{ id = "feat_note", align = "center", style = "caption" }
    -- on_back returns the title to the app when the menu closes, then declines
    -- so the platform pops - the same shape the Scanner's list uses.
    feat_screen = ui.push{ id = "feat_screen", feat_list, feat_note,
      on_back = function() ui.title(nil); return false end }
  else
    ui.push(feat_screen)
  end
  feat_list.selected = feat_sel
  refresh_features()
end

-- ---- the script picker -----------------------------------------------------

local function run_note(msg)
  if pick_note then pick_note.text = msg end
end

function open_picker()
  scripts = list_scripts()
  pick_sel = 1
  ui.title("BadUSB scripts")
  local rows = {}
  for i, name in ipairs(scripts) do
    rows[i] = ui.label{ id = "pick" .. i, text = name }
  end
  if not pick_list then
    pick_list = ui.list{ id = "pick_list", layout = "text",
      on_prev = function()
        if pick_sel > 1 then pick_sel = pick_sel - 1; pick_list.selected = pick_sel end
      end,
      on_next = function()
        if pick_sel < #pick_list.children then
          pick_sel = pick_sel + 1; pick_list.selected = pick_sel
        end
      end,
      on_click = function(self, index)
        if index then pick_sel = index; self.selected = index end
        local name = scripts[pick_sel]
        if not name then return end
        -- Ask the loop to run it. It states what it is about to do here, before
        -- a key is sent.
        pending_run = name
        run_note("typing " .. name .. " ...\nto the USB host")
      end }
    pick_note = ui.label{ id = "pick_note", align = "center", style = "caption" }
    pick_screen = ui.push{ id = "pick_screen", pick_list, pick_note,
      on_back = function() ui.title("HID features"); return false end }
  else
    ui.push(pick_screen)
  end
  pick_list:set_children(rows)
  pick_list.selected = pick_sel
  run_note("A runs the highlighted script\nit types to the USB host")
end

-- ---- Mass Storage ----------------------------------------------------------

-- Entering hands the whole SD card to the computer as a USB drive. The card has
-- one owner: while it is the host's, /sd is unmounted here, so SD apps, config
-- and BadUSB scripts are gone until it comes back - built-in apps still run.
-- Leaving (B) takes the card back and remounts it, which is why the screen says
-- to eject on the computer first. This is the workflow BadUSB relies on: drop a
-- Ducky script over USB, take the card back, then run it.
local msc_screen, msc_note

function open_msc()
  ui.title("Mass Storage")
  -- The enable is refused if there is no card (has_card is the same gate the
  -- menu greyed on), so the note reflects what actually happened.
  local active = service.usb.msc_enable(true)
  if not msc_note then
    msc_note = ui.label{ id = "msc_note", align = "center", style = "body" }
    -- B takes the card back before the screen closes, so the owner never leaves
    -- with the card still handed away by accident. Then the title returns to the
    -- features menu and the platform pops, the same shape the picker uses.
    msc_screen = ui.push{ id = "msc_screen", msc_note,
      on_back = function()
        service.usb.msc_enable(false)
        ui.title("HID features")
        return false
      end }
  else
    ui.push(msc_screen)
  end
  if active then
    msc_note.text = "the SD card is the\ncomputer's now.\n\neject it there, then press B\nto take it back."
  else
    msc_note.text = "could not hand over the card\n(no card, or already busy)"
  end
end

-- ---- running a script ------------------------------------------------------

-- Read, parse in C, then walk the events: tap each key, sleep each pause. The
-- small gap between keys is what a host needs to register them as distinct
-- presses; sys.sleep yields, so the screen stays live and B is still heard.
local function run_script(name)
  if not hid_on() then
    run_note("HID is off - nothing sent")
    return
  end
  local text = fs.read("catnip/badusb/" .. name)
  if not text then
    run_note("could not read " .. name)
    return
  end
  local evs, err = service.usb.ducky_parse(text)
  if not evs then
    run_note(err or "could not parse " .. name)
    return
  end
  local keys = 0
  for _, e in ipairs(evs) do
    if not hid_on() then
      -- Disarmed mid-run - stop rather than wait it out.
      run_note("HID turned off - stopped")
      return
    end
    if e.delay then
      sys.sleep(e.delay)
    else
      service.usb.hid_tap(e.usage, e.mods)
      keys = keys + 1
      sys.sleep(8)
    end
  end
  run_note("done: " .. name .. "\n" .. keys .. " keys sent")
end

-- ---- the control, and the loop ---------------------------------------------

-- A one-row list is the focusable control the root's A and long A land on. Its
-- row shows the state; toggling rewrites it.
local ctl_row = ui.label{ id = "hid_ctl_row", align = "center", text = "typing on/off" }
local ctl = ui.list{ id = "hid_ctl", layout = "text",
  on_prev = function() end,
  on_next = function() end,
  on_click = function()
    service.usb.hid_enable(not hid_on())
    paint_root()
  end,
  on_options = function() open_features() end }
ctl:set_children({ ctl_row })

ui.screen{ state, ctl, caption }
paint_root()

-- The app is its own loop (as the Scanner is): the handlers set what to do, and
-- this does it. A run happens here, where sys.sleep is legal; the rest of the
-- time it idles, since nothing on these screens changes on its own. B leaves the
-- app, and leaving disarms nothing on purpose - HID stays in whatever state the
-- owner set, because it is a device-wide mode, not this app's private toggle.
while true do
  if pending_run then
    local name = pending_run
    pending_run = nil
    run_script(name)
  end
  paint_root()
  sys.sleep(120)
end
