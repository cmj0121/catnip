-- File Browser - catnip demo app.
--
-- Browse the SD card, read a file, delete one, or reset the card. Every action
-- goes through fs.*, so the app dogfoods the device API; the shape of the app
-- is the other half of what it is for. The renderer is retained: it keeps the
-- object it made for a node and matches children by table identity, so an app
-- that rebuilt its rows through ui.screen on every keypress would have the
-- whole screen destroyed and drawn again, and would demonstrate the opposite of
-- what it was written to demonstrate. So the tree here is built once, at the
-- bottom of this file, and after that only properties change. Moving the
-- selection writes one number.
--
-- It is also the worked example in docs/app-ui-spec.md, so it follows that
-- model exactly rather than inventing chrome of its own:
--
--   * a short A, or a tap, activates the selected row - a folder is entered, a
--     file is shown;
--   * a long A opens that row's options, built at the moment it is asked
--     because which actions apply depends on whether the row is a file or a
--     folder - which is why there are no Delete and Reset buttons any more.
--     Deleting is done *to an item*, so it lives on the item;
--   * a short B climbs one directory and says it handled it, and at the top of
--     the card it declines, which is how the platform knows to close the app.
--     There is no Up button for the same reason there is no Back button: B is
--     already that, everywhere;
--   * a long B is home, and this app never sees it.
--
-- Input arrives as events on the nodes that can take them - the list and the
-- buttons - rather than through a global this app exports for the shell to know
-- by name. A global would not be a contract; on_* is the one the input layer
-- delivers against. The list's `selected` prop only ever travels downward, so
-- this app owns the index and moves it when it is told to: `prev` and `next`
-- are what the input layer posts to the focused list.

local B = { cwd = "", sel = 0, entries = {} }

-- The browser screen, built once at the bottom.
local rows, status
-- The screens shown over it, each built on first use and kept afterwards,
-- because ui.push takes a screen node as readily as a spec.
local viewer, viewer_name, viewer_text
local confirm, confirm_text
local menu, menu_rows
local pending -- what the confirmation's Confirm button will run

local function join(a, b)
  if a == "" then return b end
  return a .. "/" .. b
end

-- One line under the list for whatever needs saying, hidden when nothing does.
-- Emptiness itself says nothing: a directory with no files is drawn as a
-- directory with no rows, because that is what it is.
local function say(msg)
  status.text = msg or ""
  status.hidden = (msg == nil)
end

-- Point the row nodes at the current directory. A directory change is the one
-- moment when the rows genuinely hold different content, and the cheap thing is
-- still to keep the nodes: a reused row is one text update, and the widget
-- behind it - with its geometry and its place in the input group - is never
-- rebuilt.
--
-- When the new directory has more entries than there are row nodes, the surplus
-- rows are new nodes and the backend creates a widget for each, because there
-- is nothing to reuse; the rows that already existed still keep theirs. A
-- shorter directory drops the nodes off the end and those widgets are
-- destroyed. Either way the cost is the difference in length, not the length.
local function fill_rows()
  local old, kids = rows.children, {}
  for i, e in ipairs(B.entries) do
    kids[i] = old[i] or ui.label{ id = "row" .. i }
    kids[i].text = e.name
    -- The category, not a picture of the file: the platform owns what a folder
    -- looks like, and this only says which of them this row is.
    kids[i].icon = e.is_dir and "folder" or "file"
  end
  rows:set_children(kids)
  rows.selected = B.sel
  say(nil)
end

local function refresh()
  B.entries = fs.list(B.cwd) or {}
  if #B.entries == 0 then
    B.sel = 0
  elseif B.sel < 1 then
    B.sel = 1
  elseif B.sel > #B.entries then
    B.sel = #B.entries
  end
  -- The header is the frame's, and the path is what it should say. The app
  -- names it; it does not draw it, and it has no idea where it ends up.
  ui.title("SD:/" .. B.cwd)
  fill_rows()
end

-- The whole point of the rewrite: one property write, and so one update.
local function move(d)
  if #B.entries == 0 then return end
  B.sel = B.sel + d
  if B.sel < 1 then B.sel = 1 end
  if B.sel > #B.entries then B.sel = #B.entries end
  rows.selected = B.sel
end

local function show_file(e)
  if viewer then
    ui.push(viewer)
  else
    viewer_name = ui.label{ id = "file_name", style = "title" }
    viewer_text = ui.label{ id = "file_text" }
    viewer = ui.push{ id = "viewer", viewer_name, viewer_text }
  end
  viewer_name.text = e.name
  viewer_text.text = fs.read(join(B.cwd, e.name)) or "(cannot read)"
end

-- Ask before doing something destructive. The question is a screen pushed over
-- the browser rather than a mode the browser renders as itself: the list keeps
-- its widgets, and so its scroll and its selection, while the question is up.
--
-- There is no Cancel: B is cancel. A pushed screen with no on_back is popped by
-- the platform, so declining costs this app no widget and no code.
local function ask(prompt, action)
  pending = action
  if confirm then
    ui.push(confirm)
  else
    confirm_text = ui.label{ id = "confirm_text" }
    confirm = ui.push{ id = "confirm", confirm_text,
                       ui.button{ id = "confirm_yes", text = "Confirm",
                                  style = "danger", icon = "ok",
                                  on_click = function()
                                    -- `pending`, not the `action` this closure
                                    -- could capture: the screen is built once,
                                    -- so that upvalue would be the first
                                    -- question's answer forever.
                                    local run = pending
                                    pending = nil
                                    ui.pop() -- the question
                                    ui.pop() -- the options it was asked from
                                    if run then run() end
                                  end } }
  end
  confirm_text.text = prompt
end

local function enter(e)
  B.cwd = join(B.cwd, e.name)
  B.sel = 1
  refresh()
end

local function up()
  if B.cwd == "" then return false end
  B.cwd = B.cwd:match("^(.*)/[^/]+$") or ""
  B.sel = 1
  refresh()
  return true
end

local function delete(e)
  local name = e.name
  ask("Delete " .. name .. "?", function()
    if fs.delete(join(B.cwd, name)) then
      refresh()
    else
      say("could not delete " .. name)
    end
  end)
end

local function reset_card()
  ask("Reset (format) SD card?", function()
    local ok, err = fs.reset()
    -- There is deliberately nothing here for the success path. #46 has a
    -- successful format reboot the device, so on the hardware this app runs on
    -- control does not come back, and code written for it now would be code
    -- written against a reboot that does not exist yet. Today sd_reset is not
    -- wired up at all and the call returns false with a reason, which is the
    -- only outcome this app can honestly show - so it shows it, instead of
    -- refreshing a listing as though the card had been reformatted.
    if not ok then say(err or "reset failed") end
  end)
end

-- The actions an item can offer. Each is a row in the options menu: a name, the
-- glyph the platform draws for it, and what it does. `when` decides whether it
-- applies to the item that was asked about, which is the reason this menu is
-- built at on_options time and could not have been a fixed structure.
-- `confirms` says the action pushes a question of its own, so the menu is left
-- standing under it and `ask` unwinds both. It is a field rather than a list of
-- which functions are special, because a fourth confirming action should be one
-- more table entry and not an edit in two places.
local ACTIONS = {
  { id = "open",   name = "Open",   icon = "folder",
    when = function(e) return e.is_dir end,
    run  = enter },
  { id = "view",   name = "View",   icon = "file",
    when = function(e) return not e.is_dir end,
    run  = show_file },
  { id = "delete", name = "Delete", icon = "trash", confirms = true,
    when = function(e) return not e.is_dir end,
    run  = delete },
  { id = "format", name = "Reset card", icon = "warning", confirms = true,
    when = function() return true end,
    run  = reset_card },
}

-- Long A: the options for one item. The menu is an ordinary pushed screen
-- holding an ordinary selectable list, so B closes it with nothing written here
-- and the platform frames it exactly like every other screen.
local function options(_, index)
  local e = B.entries[index or B.sel]
  if not e then return end

  local applicable = {}
  for _, a in ipairs(ACTIONS) do
    if a.when(e) then applicable[#applicable + 1] = a end
  end

  if not menu then
    menu_rows = ui.list{ id = "menu_rows",
                         on_prev = function(self) self.selected = math.max(1, (self.selected or 1) - 1) end,
                         on_next = function(self)
                           self.selected = math.min(#self.children, (self.selected or 1) + 1)
                         end,
                         on_click = function(self, i)
                           local pick = self.picks[i or self.selected]
                           if not pick then return end
                           if i then self.selected = i end
                           -- An action that asks a question keeps the menu
                           -- underneath it, so `ask` can unwind both at once;
                           -- anything else is done with the menu right away.
                           if not pick.confirms then ui.pop() end
                           pick.run(self.entry)
                         end }
    menu = ui.push{ id = "menu", menu_rows }
  else
    ui.push(menu)
  end

  local old, kids = menu_rows.children, {}
  for i, a in ipairs(applicable) do
    kids[i] = old[i] or ui.label{ id = "act" .. i }
    kids[i].text = a.name
    kids[i].icon = a.icon
  end
  menu_rows:set_children(kids)
  -- The actions themselves, and the one item they are all about: a table per
  -- row carrying a copy of both would say nothing more.
  menu_rows.picks = applicable
  menu_rows.entry = e
  menu_rows.selected = 1
end

-- Short A, or a tap. A tap names the row it landed on and the joystick names
-- nothing, which is the whole difference between the two here.
local function activate(self, index)
  if index then
    B.sel = index
    self.selected = index
  end
  local e = B.entries[B.sel]
  if not e then return end
  if e.is_dir then enter(e) else show_file(e) end
end

rows = ui.list{ id = "rows",
                on_prev = function() move(-1) end,
                on_next = function() move(1) end,
                on_click = activate,
                on_options = options }
status = ui.label{ id = "status", hidden = true }

-- Short B. Climbing is content replacement rather than a pushed screen, so it
-- is this app's to do; only the last step out - declining at the top of the
-- card - is the platform's.
ui.screen{ rows, status, on_back = up }
refresh()
