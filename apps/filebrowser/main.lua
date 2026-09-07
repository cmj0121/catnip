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
-- Input arrives as events on the nodes that can take them - the list and the
-- buttons - rather than through a global this app exports for the shell to know
-- by name. A global would not be a contract; on_* is the one #31's input layer
-- will deliver against. The list's `selected` prop only ever travels downward,
-- so this app owns the index and moves it when it is told to: `prev` and `next`
-- are what the input layer posts to the focused list.

local B = { cwd = "", sel = 0, entries = {} }

-- The browser screen, built once at the bottom.
local title, rows, status
-- The two screens shown over it, each built on first use and kept afterwards,
-- because ui.push takes a screen node as readily as a spec.
local viewer, viewer_name, viewer_text
local confirm, confirm_text
local pending -- what the confirmation's Confirm button will run

local function join(a, b)
  if a == "" then return b end
  return a .. "/" .. b
end

local function row_text(e)
  return (e.is_dir and "[DIR] " or "") .. e.name
end

-- One line under the list for whatever needs saying, hidden when nothing does.
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
    kids[i].text = row_text(e)
  end
  rows:set_children(kids)
  rows.selected = B.sel
  say(#B.entries == 0 and "(empty)" or nil)
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
  title.text = "SD:/" .. B.cwd
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
    viewer = ui.push{ id = "viewer", viewer_name, viewer_text,
                      ui.button{ id = "file_back", text = "Back",
                                 on_click = function() ui.pop() end } }
  end
  viewer_name.text = e.name
  viewer_text.text = fs.read(join(B.cwd, e.name)) or "(cannot read)"
end

-- Ask before doing something destructive. The question is a screen pushed over
-- the browser rather than a mode the browser renders as itself: the list keeps
-- its widgets, and so its scroll and its selection, while the question is up.
local function ask(prompt, action)
  pending = action
  if confirm then
    ui.push(confirm)
  else
    confirm_text = ui.label{ id = "confirm_text" }
    confirm = ui.push{ id = "confirm", confirm_text,
                       ui.button{ id = "confirm_yes", text = "Confirm",
                                  style = "danger",
                                  on_click = function()
                                    -- `pending`, not the `action` this closure
                                    -- could capture: the screen is built once,
                                    -- so that upvalue would be the first
                                    -- question's answer forever.
                                    local run = pending
                                    pending = nil
                                    ui.pop()
                                    if run then run() end
                                  end },
                       ui.button{ id = "confirm_no", text = "Cancel",
                                  on_click = function()
                                    pending = nil
                                    ui.pop()
                                  end } }
  end
  confirm_text.text = prompt
end

local function activate()
  local e = B.entries[B.sel]
  if not e then return end
  if e.is_dir then
    B.cwd = join(B.cwd, e.name)
    B.sel = 1
    refresh()
  else
    show_file(e)
  end
end

local function up()
  if B.cwd == "" then return end
  B.cwd = B.cwd:match("^(.*)/[^/]+$") or ""
  B.sel = 1
  refresh()
end

local function delete()
  local e = B.entries[B.sel]
  if not e or e.is_dir then return end
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

title = ui.label{ id = "title", style = "title" }
rows = ui.list{ id = "rows",
                on_prev = function() move(-1) end,
                on_next = function() move(1) end,
                on_click = activate }
status = ui.label{ id = "status", hidden = true }

ui.screen{ title, rows, status,
           ui.button{ id = "up", text = "Up", on_click = up },
           ui.button{ id = "delete", text = "Delete", style = "danger",
                      on_click = delete },
           ui.button{ id = "reset", text = "Reset card", style = "danger",
                      on_click = reset_card } }
refresh()
