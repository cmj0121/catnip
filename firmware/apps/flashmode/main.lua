-- Flash Mode - the BOOT+RESET dance in software (#61).
--
-- Catnip ships as a TinyUSB composite (a CDC console, a HID keyboard and a Mass
-- Storage drive on one USB device). That is what those three surfaces need, and
-- it is also what gives up the ROM's USB-Serial/JTAG reset-to-download path - so
-- every flash otherwise needs the manual dance by hand (hold BOOT, tap RESET,
-- release BOOT). See docs/INSTALL.md.
--
-- This app is that dance in software: it reboots the device into ROM download
-- mode over the same USB cable, so the next `make install` / esptool run
-- connects with no buttons pressed. The reboot does not come back to Catnip on
-- its own - the chip waits in the bootloader, and a flash or unplugging and
-- replugging the USB cable is what brings the firmware back up. The power
-- button does nothing there: it is the firmware that answers it, and the
-- firmware is not running. So this is deliberately two presses behind a
-- warning, not one: A here opens a confirmation, and A there does it.
--
-- On a build with no composite to leave behind (the host, and the poc/probe
-- sketches) service.usb.flash_mode() returns false instead of rebooting, and
-- the confirmation says so rather than pretending the screen is about to go dark.

local confirm_screen, confirm_note

-- The point of no return. service.usb.flash_mode() does not come back on the
-- device; the only line after it that ever runs is the one for a build that
-- cannot do this at all.
local function do_reboot()
  service.usb.flash_mode()
  confirm_note.text = "this build has no bootloader\n"
    .. "to fall back to - flash mode\n"
    .. "is not available here"
end

local function open_confirm()
  ui.title("Reboot to flash?")
  if not confirm_screen then
    confirm_note = ui.label{ id = "fm_confirm_note", align = "center", style = "body",
      text = "the screen goes dark and the\n"
        .. "device waits for esptool.\n"
        .. "replug USB-C to bring Catnip\n"
        .. "back if you change your mind." }
    local go_row = ui.label{ id = "fm_go_row", align = "center", text = "A: reboot now" }
    local go = ui.list{ id = "fm_go", layout = "text",
      on_prev = function() end,
      on_next = function() end,
      on_click = function() do_reboot() end }
    go:set_children({ go_row })
    -- B backs out to the root and drops the title, the way the HID app's
    -- sub-screens do; nothing has happened yet, so there is nothing to undo.
    confirm_screen = ui.push{ id = "fm_confirm", confirm_note, go,
      on_back = function() ui.title(nil); return false end }
  else
    confirm_note.text = "the screen goes dark and the\n"
      .. "device waits for esptool.\n"
      .. "replug USB-C to bring Catnip\n"
      .. "back if you change your mind."
  end
  ui.push(confirm_screen)
end

-- ---- the root ---------------------------------------------------------------

local title = ui.label{ id = "fm_title", align = "center", style = "title", text = "Flash Mode" }
local body = ui.label{ id = "fm_body", align = "center", style = "caption",
  text = "reboot into the ROM\n"
    .. "downloader so the next flash\n"
    .. "needs no BOOT dance" }

local ctl_row = ui.label{ id = "fm_ctl_row", align = "center", text = "A: reboot to flash mode" }
local ctl = ui.list{ id = "fm_ctl", layout = "text",
  on_prev = function() end,
  on_next = function() end,
  on_click = function() open_confirm() end }
ctl:set_children({ ctl_row })

ui.screen{ title, body, ctl }

-- Nothing on these screens changes on its own, so the loop only keeps the app
-- alive for B to leave and for the handlers above to fire. The same shape the
-- Battery app uses, without the repaint.
while true do
  sys.sleep(200)
end
