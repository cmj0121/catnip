# The device layer

Everything here talks to the MeowKit's hardware directly. Above it sits the Lua
runtime, which reaches hardware only through `catnip_hal.h`; below it there is
nothing but the board.

Most of this is real, built into the firmware, and verified on a device. Three
files are still scaffolds: `hal_meowkit.cpp`, `lvgl_backend.cpp` and
`shell_ui.cpp` are guarded by `CATNIP_DEVICE_WIP` and compile to nothing. They
are waiting on the `ui.*` renderer (#30) and the on-screen shell (#33).

## The rule that matters most

**`board.h` records what was measured, not what the documentation claims.**

The vendor's published source has been wrong about this board twice, in ways
that each cost a day. The display's chip-select and reset are on the I/O
expander rather than MCU pins, so following the published map holds the panel
in reset while asserting its select and the screen stays dark, looking exactly
like a dead backlight. And all three of the published button pins are wrong:
GPIO4 is B, A is on GPIO6, and the GPIO5 it gives for B is the joystick's
centre press.

So every entry in `board.h` carries how it was found. When you learn something
new about the hardware, write it there with its evidence — a pin number without
a measurement behind it is a guess wearing a constant's name.

Two failures of method are recorded there too, because both produced confident
wrong answers:

- Reading a switch with `INPUT_PULLUP` when the board already pulls it up makes
  pressed and released look identical. That is how the buttons were first
  written off as not reaching the MCU at all.
- Inferring a mapping from the order events arrive in does not survive a lossy
  link, and this chip's native USB CDC drops lines under load. Counting events
  does survive it. A and B were recorded backwards until the probe was changed
  to count.

## Testing what has no hardware

Anything that is arithmetic rather than I/O is split into a plain C file with no
`Arduino.h` in sight, so the host build can exercise it:

| Pure logic         | Tested by                           | What it decides                         |
| ------------------ | ----------------------------------- | --------------------------------------- |
| `input_debounce.c` | `test/native/test_input_debounce.c` | when a contact has settled              |
| `touch_map.c`      | `test/native/test_touch_map.c`      | panel coordinates to screen coordinates |
| `diag_layout.c`    | `test/native/test_diag_layout.c`    | which box a point falls in              |
| `imu_map.c`        | `test/native/test_imu_map.c`        | which screen edge is pointing up        |

This is not ceremony. The debounce filter's first design — accept an edge, then
ignore the pin for a while — was proved wrong on the host by replaying a real
trace: the rebound lasted 160 ms and would have been reported as a second press.
It never reached hardware.

These files are listed one by one in `firmware/Makefile` rather than wildcarded,
so that adding a device source that needs Arduino cannot quietly break the host
build.

## The I2C bus is shared, and it is small

The PMIC, the I/O expander, the touch controller, an RTC and an IMU all sit on
one 400 kHz bus. A driver that polls as fast as the main loop allows is not
merely wasteful — it is spending everyone else's time, and the symptom will show
up in a different driver than the one at fault.

So a driver that talks to a part gives itself a budget and says why in its own
comment: the touch controller updates at about 60 Hz and is read at most every
12 ms, and the IMU updates at 125 Hz but is read at most every 50 ms, because a
device is turned over by hand in about half a second and nothing on screen has
to follow it faster than that. Read a run of registers with
`catnip_i2c_read_regs()` rather than one at a time; besides the transactions it
saves, it makes the sample atomic, which is how a stale X stopped being paired
with a fresh Y.

## Seeing it work

`diag.h` draws the input diagnostic: every switch, the touch position, and the
numbers behind both. Enter it with a `/sd/catnip/diag` marker file on the card,
or by typing `diag` over the serial console — never by holding a key, because
the keys are what it exists to test and that gesture would fail exactly when it
was needed.

It draws an arrow from the centre of the screen toward whichever edge the
firmware believes is up, and prints the IMU's raw `CHIP_ID` beside it. That is
how the axis mapping in `imu_map.c` gets settled: hold the device any way round
and the arrow should point at the ceiling. The identity is on the same screen
because an axis mapping read out of the wrong chip is not a mapping that needs
correcting — it is a number that means nothing, and the two look identical from
the arrow alone.

That check has already paid for itself: 0x68 reports `0x24`, not the `0x05` a
QMI8658A reports, so the part the vendor's code named was never there. It is a
Bosch BMI270, which produces no data at all until an 8 KB configuration image
has been uploaded into it — `catnip_imu_begin()` does that at every boot, and
`board.h` records the registers that identified it and where the image came
from. Because the identity register answers whether or not that upload
succeeded, the page reports `INTERNAL_STATUS` too: a BMI270 that never reached
`init_ok` acknowledges every transaction and reports nothing, which without
that byte would read as a wiring fault.

The axis mapping has since been settled the same way the touch rotation was:
with the accelerometer running, the device was turned so each screen edge in
turn pointed at the ceiling, and the arrow followed it all four times. Those
four rows in `imu_map.c` are a measurement now, and its host test pins them —
a failure there means the table regressed, not that the mapping was wrong.

The two flat attitudes are still unverified. They draw no arrow, so getting
them the wrong way round would look like nothing happening rather than like a
mistake; lay the device flat each way up and read whether the headline says
`flat, screen up` or `flat, screen down`.

It has already earned its place twice. It settled the touch rotation in one
drag, after two attempts to infer the same thing from serial output gave
contradictory answers; and it exposed its own colours being byte-swapped, which
is how the sprite buffer and the blit path turned out to disagree about byte
order.

`src/probe_input.cpp` is the other instrument: a standalone `[env:probe]` sketch
that interrogates pins and parts whose identity is not yet established. It is
deliberately not built on these drivers — `catnip_touch_begin()` refuses to
interpret a part that is not an FT6336, and asking what an unknown part is, is
the probe's whole job.

## Adding a driver

Follow `touch.h` and `input.h`: a `begin()` that verifies the part is what you
think it is and returns whether it was, a `poll()` for the main loop, state
queries, and edges that clear on read — the convention
`catnip_pmu_power_key_pressed()` established, so callers never keep a previous
state around to diff against.

Identify a part by its identity registers before trusting any others. The touch
controller is only called an FT6336 because 0xA3 and 0xA8 returned 0x64 and
0x11. A different part answering the same address would otherwise produce
entirely plausible readings.

Say in a comment what an API is for when it is for one caller. Raw panel
coordinates live in `touch_debug.h` rather than beside `catnip_touch_position()`
in `touch.h`, so that including it is a statement about what you are doing.
