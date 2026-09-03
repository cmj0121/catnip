-- File Browser - catnip demo app.
--
-- Browse the SD card, open (read) a file, delete a file, or reset the card.
-- The UI is described with ui.*; the shell's input layer drives the browser
-- (joystick/keys -> move/enter/back, buttons -> open/delete/reset). Every
-- action goes through fs.*, so this app also dogfoodss the device API.

local B = { cwd = "", sel = 1, entries = {}, mode = "list", view = nil, pending = nil }

local function join(a, b)
  if a == "" then return b end
  return a .. "/" .. b
end

local function render()
  local kids = { ui.label{ id = "title", text = "SD:/" .. B.cwd, style = "title" } }
  if B.mode == "view" then
    kids[#kids + 1] = ui.label{ id = "viewer", text = B.view or "" }
  elseif B.mode == "confirm" then
    kids[#kids + 1] = ui.label{ id = "confirm", text = B.pending.prompt }
  else
    for i, e in ipairs(B.entries) do
      local mark = (i == B.sel) and "> " or "  "
      local tag = e.is_dir and "[DIR] " or ""
      kids[#kids + 1] = ui.label{ id = "row" .. i, text = mark .. tag .. e.name }
    end
    if #B.entries == 0 then
      kids[#kids + 1] = ui.label{ id = "empty", text = "(empty)" }
    end
  end
  ui.screen(kids)
end

function B.refresh()
  B.entries = fs.list(B.cwd) or {}
  if B.sel > #B.entries then B.sel = #B.entries end
  if B.sel < 1 then B.sel = 1 end
  B.mode = "list"
  render()
end

function B.move(d)
  if B.mode ~= "list" or #B.entries == 0 then return end
  B.sel = B.sel + d
  if B.sel < 1 then B.sel = 1 end
  if B.sel > #B.entries then B.sel = #B.entries end
  render()
end

function B.selected()
  return B.entries[B.sel]
end

function B.open()
  local e = B.selected()
  if not e or e.is_dir then return end
  B.view = fs.read(join(B.cwd, e.name))
  B.mode = "view"
  render()
end

function B.enter()
  local e = B.selected()
  if not e then return end
  if e.is_dir then
    B.cwd = join(B.cwd, e.name)
    B.sel = 1
    B.refresh()
  else
    B.open()
  end
end

function B.up()
  if B.cwd == "" then return end
  B.cwd = B.cwd:match("^(.*)/[^/]+$") or ""
  B.sel = 1
  B.refresh()
end

function B.back()
  if B.mode ~= "list" then
    B.mode = "list"
    render()
  end
end

function B.delete()
  local e = B.selected()
  if not e or e.is_dir then return end
  B.pending = {
    prompt = "Delete " .. e.name .. "?  confirm / cancel",
    action = function()
      fs.delete(join(B.cwd, e.name))
      B.refresh()
    end,
  }
  B.mode = "confirm"
  render()
end

function B.reset_sd()
  B.pending = {
    prompt = "Reset (format) SD card?  confirm / cancel",
    action = function()
      fs.reset()
      B.cwd = ""
      B.refresh()
    end,
  }
  B.mode = "confirm"
  render()
end

function B.confirm()
  if B.mode == "confirm" and B.pending then
    local action = B.pending.action
    B.pending = nil
    action()
  end
end

function B.cancel()
  if B.mode == "confirm" then
    B.pending = nil
    B.refresh()
  end
end

-- Exposed for the shell's input layer and for tests.
browser = B
B.refresh()
