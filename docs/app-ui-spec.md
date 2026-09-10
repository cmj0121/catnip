# Writing a catnip app

**Languages:** English · [繁體中文](app-ui-spec.zh-TW.md)

What an app provides, what the platform provides, and how the two meet. All of
it works today except where a line says otherwise.

## The canvas

320x240, landscape, dark theme. There are two modes, and an app is in the first
one unless it asks for the second.

**General.** A platform-drawn header, and the main region below it for you.

```text
┌──────────────────────────────────────────────┐
│ [BATTERY]      [TITLE]           [PAGE]      │  header — the platform's
├──────────────────────────────────────────────┤
│                                              │
│                main region                   │  yours
│                                              │
└──────────────────────────────────────────────┘
```

You supply two things: **a title** and **a content tree**. The frame draws the
header around whatever screen is visible — including a screen you push, so a
dialog is framed like your root.

**Full screen** (`frame: "bare"`). No header; the whole 320x240 is the app's,
for something that is genuinely a canvas — a game, a clock face, a signal plot.
It buys **space, not coordinates**: rule 1 still holds and you still build a
geometry-free tree.

### What the main region holds

Four shapes, and no fifth. Each is a `list`; what differs is how the platform
lays it out.

| Shape               | When                                             | Layout                                   |
| ------------------- | ------------------------------------------------ | ---------------------------------------- |
| **One icon**        | a landing page — the cat, a splash               | centred, alone                           |
| **Up to six icons** | a set of things to choose between                | **3x2 grid**, the focused one ringed     |
| **Rows of text**    | anything longer, or anything that needs words    | one row per line                         |
| **Values**          | settings — quantities, not choices               | **vertical bars**, side by side          |
| **A face**          | one thing to be looked at, with labels around it | **canvas** — a centrepiece and two lines |

The first three all answer "which one?". **Values** is the fourth because none of
them can answer "how much?" — a quantity has no natural row and no natural icon,
and a shape that had to fake one would put the number somewhere it is not the
point. It is `ui.list{ layout = "mixer" }`: every child carries a `value` (0–100,
how far up its bar) and a `value_text` (what is printed above it), and **short A
steps that column to its next value**. Height is the number, so a page of them
can be read against each other at a glance, and the touch target is a column the
height of the region rather than a line of text.

```text
    70%      30 s      5%     2.5 s
   ╔═══╗    ┌───┐    ┌───┐    ┌───┐
   ║███║    │   │    │   │    │███│
   ║███║    │███│    │███│    │███│
   ╚═══╝    └───┘    └───┘    └───┘
   Screen   Idle-off   LED    Breath
```

A value is a **ladder of a few named steps**, not a range, because "the next
value" needs a finite list to come from. Pick the rungs so that the one after the
last is somewhere safe to land: the ladder wraps.

**A face** answers neither question: it is one thing to be looked at with labels
around it, which is what a clock is. `ui.list{ layout = "canvas" }` - and what
`frame: "bare"` gives a screen - is laid out around its centrepiece: the child in
the `display` role goes in the middle of the region, what you named before it
forms the top line and what you named after it the bottom, each running
first-to-the-left and last-to-the-right. **The order you name them in is the
layout**, and you still never say where anything goes.

More than six icons scrolls rather than paginating: the selection leads and the
view follows, exactly as a list does, so there is one scrolling model and not
two. A grid is `ui.list{ layout = "grid" }` — the same node, the same `selected`
integer, the same events; only the flow differs.

**Every row reserves a leading icon slot**, whether it has an icon or not: 14 px
for the glyph and 6 px of gap, so text starts 20 px in. Always, rather than only
when some row in the list has one — one rule with no condition in it, and text
that cannot shift when a row gains an icon. A row must have text; its icon is
optional.

## The rules

1. **Name things; never place them.** Say what you want shown — a label, a list,
   an icon. Never a pixel, a coordinate, a size, or a corner.
2. **Build the tree once, then write properties.** The renderer is retained: it
   keeps the object it made for a node and matches children by table identity.
   Moving a selection is one property write, not a rebuilt screen.
3. **A row is `[icon] [name]`, and the icon slot is always there.** Rows are the
   children of a `list`, selectable and touchable. A tap is delivered to the
   **list**, carrying the row index. Text is required; the icon is not, and the
   space for it is reserved either way.
4. **Own your `selected`.** It is one integer, and it only travels downward. The
   platform posts `prev`/`next` to your focused list; you move the number.
5. **Pick an icon by name, or ship one.** A built-in glyph (`folder`, `file`, …)
   names a _category_; your `icon.png` is your app's _identity_. An unknown glyph
   degrades safely; a missing image falls back to the mascot. The built-in set is
   catnip's own solid colour set (`firmware/assets/icons`), not a borrowed symbol font.
6. **Let `[PAGE]` be derived.** The frame works it out from what the main region
   holds, and there is no API that sets it: a grid counts **pages**, a list
   counts **rows**, a carousel and a mixer count **positions**, and a single icon
   counts nothing and stays blank. A grid says `2/3` rather than `8/14` because what a
   user sees there is a position on a page, not an ordinal.
7. **Short press is primary, long press is secondary — the same on A and B.**
   Short A activates; long A offers options for the selected item. Nothing an
   app can _do_ is ever on the screen — operations exist only behind long A.
8. **Claim short B only when you have somewhere to climb.** Return truthy from
   `on_back` when you handled it, falsy or nothing when you did not.
9. **Never expect long B.** It is home, it is the platform's, and it arrives at no
   handler of yours. So is the power button.
10. **Declare in the manifest, decide in Lua.** The manifest owns identity and
    appearance (`id`, `name`, `icon`); Lua owns behaviour and applicability. The
    `id` is the seam between them.
11. **Empty is blank.** Show no rows and no placeholder. Blank is the honest
    rendering of nothing.

## The gestures

| Gesture          | Meaning                                                              | Reaches you as                         |
| ---------------- | -------------------------------------------------------------------- | -------------------------------------- |
| Up / Down        | move the selection; the list scrolls to follow. **Held, it repeats** | `prev` / `next` to the focused list    |
| Left / Right     | move the focus between focusable things                              | nothing; the platform moves the ring   |
| Short A          | activate the focused item — a value's next step                      | `on_click(self)`                       |
| Long A           | options for the **selected item**                                    | `on_options(self, index)` → action ids |
| Tap a row        | select and activate it                                               | `on_click(self, index)`                |
| Long-press a row | options for that row                                                 | `on_options(self, index)` → action ids |
| Swipe            | the joystick's four directions, by finger                            | whatever that direction means          |
| Short B          | back — pop, climb, or exit                                           | `on_back(self)` on the visible screen  |
| Long B           | home — the cat, from any depth                                       | nothing; the platform keeps it         |
| Hold power       | power off                                                            | nothing; the PMIC keeps it             |

### The device says which of them do anything

Four small arrows in the bottom-left corner, one per direction, **lit when that
direction does something here and dimmed when it does not**.

```text
        ▲          up      lit on the cat: it opens the grid of every app
      ◄   ►        left/right   lit: they step the ring
        ▼          down    lit: it reaches the device page
```

It is **drawn, not typed** — four triangles off one centre, in an earthy yellow
when lit and the faint ink when not. Four font glyphs placed by hand cannot be
made symmetric, because the arrow characters have different widths and heights,
so a shared centre is the only thing that keeps a cross square. The colour is
deliberately not the body white: the hint is not writing, it is a picture of the
control under your thumb.

The dimming is the whole point, and it is why the hint exists at all. Several
directions do nothing depending on where you are — a page of two buttons has
nothing for up and down to move, and the ring at the last stop has nowhere
further right to go. On a device with no hint, a
direction that does nothing is indistinguishable from one that has stopped
listening, and the second is what a user concludes.

**Nothing declares it.** The answers are derived from the tree: a direction is
lit when the focused node has a handler for what that direction posts, plus the
platform's own bindings on top. The same function answers the press, so an arrow
cannot be lit for something that does nothing — a page that gains an `on_next`
gains its arrow with no line written anywhere.

**A and B are not on it.** They are the two gestures that mean the same thing
everywhere — activate, and leave — and a hint is for what changes.

It is drawn by the platform on the top layer, like the bar, so no app can draw
one, move one, or discover whether one is up. The corner it occupies is reserved
the way the bar's height is: a canvas's bottom line starts to the right of it, and
a column leaves room below. **A `frame: "bare"` app reserves neither**, because
no bar and no hint are drawn over one.

**An app can turn it off** with `"hints": false` in its manifest — a different
claim from `bare`, and worth making on its own: _the directions here need no
explaining_. The default is on, and that direction is deliberate: the apps most
likely to need the hint are the ones whose authors did not think about it, so an
app that says nothing gets it.

**A swipe is not a new gesture.** It is the joystick, made with a finger: swipe
up and down where you would push up and down, left and right the same. Touch and
the joystick are two ways of saying the same small vocabulary, which is already
true of a tap — a tap is short A on the row it landed on — and the directions
follow the same rule rather than inventing a second grammar for fingers.

That has one consequence worth stating plainly, because it is a thing being
taken away: **a list does not free-scroll under a finger.** Dragging it moves the
selection, one row at a time, and the view follows the selection exactly as it
does for the joystick. Letting the finger drag the viewport instead would leave
the selection somewhere off-screen and break the one rule the whole scrolling
model rests on — the selection leads, the viewport follows.

**Browsing and choosing are entirely touchable. Leaving is not.** A finger can
step, select, activate and ask for options — everything that moves you around
inside what is on screen. Back and home have no swipe and no corner to tap: B is
a physical button and stays one.

**What a tap has already been through before it reaches you.** The platform
guards touch so your `on_click` does not have to, and these are the guards, so
you know what you are being handed:

| Guard                                                         | What it means for you                                             |
| ------------------------------------------------------------- | ----------------------------------------------------------------- |
| The tap that wakes a dark screen does nothing else            | a device picked up out of a pocket cannot start you               |
| Taps are ignored for a moment after a screen is built         | a finger still down through a page change lands on nothing        |
| A tap begins and ends on the same target, having barely moved | a drag that started on your row is a swipe, not a click           |
| Nothing destructive is ever one tap away                      | which is why operations live behind long A, and not on the screen |

There is deliberately **no inertia and no momentum**: the carousel has a handful
of positions and the grid scrolls a row at a time, and momentum is a solution to
long lists that here would only overshoot the thing being aimed at.

That line is where it is on purpose, and it earns its place twice. Leaving is the
one action a stray touch must never cause — a pocket, a thumb resting on the
glass, a panel reading a drop of water. And it is the one action a user must
always be able to reach, including when the panel has stopped answering at all.
A physical button is better than a gesture at both of those, and it is the only
thing on the device that is.

Long-press threshold is `LONG_PRESS_MS` (600 ms). Released before it → short; still
held at it → long, and the short is suppressed.

**Short B.** Your `on_back` runs and its return value is the signal:

- **truthy** → you handled it (you climbed a directory, cleared a selection). The
  platform does nothing.
- **falsy, or no `on_back` at all** → the platform acts: it pops a screen you pushed
  above your root, else it exits your app.

So a single-screen app writes no `on_back` and B still leaves it; a pushed dialog
needs no `on_back` and B still cancels it. Write one only for a level of your own to
climb. To leave from inside a handler, call `sys.exit()`.

```text
 cat  (home)                 ◄── long B, from anywhere, always
   └─ the page you came from ◄── short B from an app root lands back here
        └─ app root          ◄── short B climbs one level (your on_back)
             └─ pushed screen ◄── short B pops it (context menu, dialog)
```

**Long A.** _Revised; not built._

**No operation is ever on the screen.** Not Delete, not Open, not Reset — no app
draws a button for a thing it can do, and neither does the platform. Operations
exist in exactly one place, behind a long press on the item they act on, and
nowhere until that press. A screen therefore shows what _is_, never what _can be
done_, and the main region stays the content rather than half-content and
half-toolbar. It binds the platform as much as apps: the launcher offers no
operations on its rows either.

**They arrive as a bar that rises from the bottom.** Two actions across, each
`[icon] [name]`; more than two scroll up a row at a time. It is a
`layout = "grid"` list two columns wide, so left and right step within a row and
up and down change rows — the grid's own navigation, not a third set of rules.

```text
┌──────────────────────────────────────────────┐
│ [==] 87%        File Browser           12/45 │
│                                              │
│  the content stays visible underneath        │
│  ╭────────────────────────────────────────╮  │
│  │  [✓] Ok            [✕] Cancel          │  │  ← rises from the bottom
│  ╰────────────────────────────────────────╯  │     scroll up for more
└──────────────────────────────────────────────┘
```

**A performs the focused action. B cancels** — that is the default, and an app
may rebind B to something else. One guardrail the platform keeps: it refuses to
bind B to an action the manifest marks destructive. B is the escape everywhere
else in the device, and an app that put Delete there would misfire on exactly
the reflex a user has when they want out.

The default pair is `Ok` / `Cancel`. Cancel is not redundant with B: it is there
**for the finger**, since back and home have no touch and a touch user needs a
visible way out.

Because the platform draws the bar, `on_options(self, index)` **answers which
actions apply** rather than building and pushing a screen: it returns action ids
from the manifest's catalogue. Every app's options then look and behave
identically, which is the point of a system icon set — an app that drew its own
menu would eventually draw a different Delete.

## Who owns what

|                          | The platform                                         | You                                |
| ------------------------ | ---------------------------------------------------- | ---------------------------------- |
| **Layout**               | all geometry, band heights, row height, scrolling    | —                                  |
| **Top bar**              | battery, header, `N/total`                           | the title _string_                 |
| **Content**              | reserves the region, draws your tree                 | build the tree                     |
| **Selection**            | posts `prev`/`next`/`click`, scrolls to follow       | the `selected` integer             |
| **Icons**                | resolves glyph names and `icon.png`, mascot fallback | pick _which_ icon                  |
| **Short A / tap**        | detects, posts                                       | `on_click` — what activation means |
| **Long A**               | detects, posts the index, **draws the options**      | `on_options` — which actions apply |
| **Short B**              | pops or exits when you decline                       | `on_back` — claim it or not        |
| **Long B, power, theme** | all of it                                            | —                                  |

The right column is your whole surface. If something is not in it, it is not yours
to decide.

## Config

The manifest carries the knobs; Lua carries the behaviour.

| Knob                                  | Where    | Effect                                              |
| ------------------------------------- | -------- | --------------------------------------------------- |
| `name`                                | manifest | default title, and the name on your page            |
| `icon`                                | manifest | your identity icon; absent → the mascot             |
| `actions[]`                           | manifest | the action catalogue: `id`, `name`, `icon`          |
| `frame`                               | manifest | `"standard"` (default) or `"bare"`                  |
| `counter`                             | manifest | `false` if you are not a list                       |
| `hints`                               | manifest | `false` to draw no control hint over you            |
| `ui.title(s)`                         | Lua      | a title that changes at runtime                     |
| `layout`                              | Lua      | on a list: `"grid"` for icons, `"mixer"` for values |
| `style`                               | Lua      | which of the roles below a node is set in           |
| `on_click` / `on_options` / `on_back` | Lua      | claim short A, long A, short B                      |

### The style roles

You name a role; the platform decides what it looks like. There is no font, no
size and no colour in your tree, for the same reason there are no coordinates.

| Role      | For                                              |
| --------- | ------------------------------------------------ |
| `body`    | the default — anything you have not marked       |
| `title`   | a heading inside your region                     |
| `caption` | the working under an answer: units, paths, hints |
| `primary` | the affirmative one of two                       |
| `danger`  | the one that cannot be undone                    |
| `display` | _one number_ large enough to own the screen      |

**`display` takes digits and nothing else** — `0-9`, a colon, a full stop, a
dash. Ask for it with letters in the string and you get missing-glyph boxes, on
purpose: it exists for a quantity that has earned the whole panel, and a role
that also worked as "big text" would immediately become that instead, leaving
the five prose roles no longer the only way to set words.

**`frame: "bare"`** drops the top bar and gives you the whole 320x240 — for an app
that is genuinely a canvas (a game, a clock face) rather than a list. It costs you
the battery, the title and the counter, and it buys **space, not coordinates**: rule
1 still holds and you still build a geometry-free tree.

## Example: the File Browser

The manifest declares identity and the action catalogue:

```json
{
  "id": "filebrowser",
  "name": "File Browser",
  "entry": "main.lua",
  "icon": "icon.png",
  "permissions": ["fs.read", "fs.delete", "fs.reset"],
  "actions": [
    { "id": "open", "name": "Open", "icon": "file" },
    { "id": "rename", "name": "Rename", "icon": "edit" },
    { "id": "delete", "name": "Delete", "icon": "trash" }
  ]
}
```

Lua decides behaviour. The tree is built once at the bottom of the file; after that
only properties change (rule 2):

```lua
local rows = ui.list{ id = "rows",
  on_prev = function() move(-1) end,
  on_next = function() move(1) end,

  -- short A, or a tap: the index arrives for the tap, and is absent for A
  on_click = function(self, index)
    if index then B.sel = index; self.selected = index end
    local e = B.entries[B.sel]
    if not e then return end
    if e.is_dir then descend(e) else show_file(e) end
  end,

  -- long A: answer which of the manifest's actions apply to this item. The
  -- platform draws them; this only decides. A folder cannot be renamed the
  -- way a file is, so the answer depends on the item and cannot be declared.
  on_options = function(self, index)
    local e = B.entries[index]
    if not e then return end
    if e.is_dir then return { "open" } end
    return { "open", "rename", "delete" }
  end,

  -- and what one does when it is chosen
  on_action = function(self, id, index)
    local e = B.entries[index]
    if id == "delete" then confirm_delete(e) end
    ...
  end,
}

ui.screen{ rows,
  -- short B: climb while there is somewhere to climb, then let the platform out
  on_back = function()
    if B.cwd == "" then return false end   -- at the root: the platform exits us
    up(); return true                      -- climbed: the platform does nothing
  end,
}
ui.title("SD:/")   -- and again in refresh(), as the path changes
```

Filling the rows keeps the nodes and writes their properties — a reused row is one
text update, and the widget behind it is never rebuilt:

```lua
local function fill_rows()
  local old, kids = rows.children, {}
  for i, e in ipairs(B.entries) do
    kids[i] = old[i] or ui.label{ id = "row" .. i }
    kids[i].text = e.name
    kids[i].icon = e.is_dir and "folder" or "file"   -- rule 5
  end
  rows:set_children(kids)
  rows.selected = B.sel
end
```

## Example: the Clock

_Designed, not built (#73)._ The File Browser above is a list; this is the other
end of the same vocabulary, and it needs nothing the platform does not already
have.

```text
┌────────────────────────────────────────────────┐
│  09-09                                         │  caption
│                                                │
│                                                │
│                 14:32                          │  display
│                                                │
│                                                │
│           S   M   T  [W]  T   F   S            │  caption, one lit
│                                                │
│  ▮ 87%                                         │  caption
└────────────────────────────────────────────────┘
```

Three layers, three sizes, and **none of them asks to be read twice**: the date
waits in a corner for someone who wants it, the time meets the eye in the middle,
and the weekday is a row of almost-invisible letters with today's lit. The strip
earns its place by not needing to be read at all — seven positions, and the lit
one is the answer.

```lua
ui.screen{ ui.list{
  ui.label{ id = "date",    text = "09-09",             style = "caption" },
  ui.label{ id = "time",    text = "14:32",             style = "display" },
  ui.label{ id = "weekday", text = "S M T [W] T F S",   style = "caption" },
  ui.label{ id = "battery", text = "▮ 87%",             style = "caption" },
}}
```

**An unset clock says so.** The RTC comes up never having been set, and a
confident `00:00` on the first of January is worse than an admission: it will be
believed. So the same face shows `--:--` at the same size and in the same place,
with **no** letter lit in the strip — seven positions, none of them today — and
one line of `caption` under it saying to hold A.

**Setting it is a mixer**, the shape from _What the main region holds_:

```text
      14      32       9      Sep     2026
     ╔═══╗  ┌───┐   ┌───┐   ┌───┐   ┌───┐
     ║███║  │███│   │███│   │███│   │███│
     ╚═══╝  └───┘   └───┘   └───┘   └───┘
     Hour    Min     Day    Month    Year
```

`ui.list{ layout = "mixer" }` is yours as much as it is the preference page's,
and using it here costs a user nothing to learn: left and right choose a field,
A makes it live, up and down change it, a finger sets it directly, B lets go and
B again leaves. **Hours and minutes wrap** where a preference clamps — 23 goes to
00, because a clock is a ring and a brightness is not.

The screen is reached from `on_options`, which is what long A delivers, so the
clock is doing exactly what the File Browser does with its actions — asking the
platform for the one gesture that means "there is more here", and answering it.

## Home, and where your app is found

_Designed, not built._

**Home is the cat.** The landing page is not an app and not a menu; it is the
one screen every gesture can reach, and where long B always lands. It is the
main region's one-icon shape.

Two levels sit around it, and they are not two ways of doing the same thing —
one is _your_ things, the other is _everything_.

**The carousel is the shortlist.** From home, up is every app you have, down is
the system, and left and right step through the ones you keep. One per step,
big.

```text
                        ▲  all apps — a 3x2 grid
                        │
  pinned  ◄──────────  [cat]  ──────────►  pinned
                        │
                        ▼  what this device is
```

Up and down are not two of the same thing: **up is yours and down is the
device's**. The clock is not a direction — it is an app, pinned to the carousel
like any other, and anything you write can take its place. A whole plane
maintained for one screen buys only that nobody may replace it, which is not
worth having.

The four directions mean four different things here, and that is what makes this
the home section rather than a list laid out sideways. **Down reaches the device
page from any position on the carousel**, not only from the cat — you never have
to come back to the middle to change the brightness.

**Down is the device, and the device answers before it offers.** What down opens
is a page of facts — the version, the chip, what is free, what the clock says,
what is answering on the bus — with two tiles under them:

```text
  ┌──────────── Device ────────────┐
  │ catnip v0.3.1                  │   the facts scroll: up and down
  │ built 2026-09-09               │   move through them, and A on a
  │ clock 2026-09-09 14:03         │   fact does nothing, because a
  │ battery 82%   card none        │   fact is not a choice
  ├────────────────────────────────┤
  │      ⚙            ⚠           │   left and right choose a tile,
  │  Preference   Diagnostic       │   A goes there
  └────────────────────────────────┘
```

That order is the design: "what is this thing" is answered before "change it",
and long before "is the joystick broken". Both pages below it are the platform's
own — neither is in the grid, neither can be pinned, and nothing you write can
replace either.

**Preference** is the **Values** shape above. A keeps and leaves, B puts back and
leaves, and both come back here rather than to the cat; long B is the one that
goes all the way home, as it does everywhere.

**Diagnostic** takes the whole panel — it is testing the panel, so it can hardly
share it — and **long B gives it back**. It used to restart the device instead,
which is a way out in the sense that a device that has started again is showing
the cat, and is not one in any sense a person holding it cares about.

### Setting something

Every page that sets a value works the same way, whether it is the platform's or
yours — the preference page and the clock's setter are the same six words:

|               |                                           |
| ------------- | ----------------------------------------- |
| Left / Right  | choose a column                           |
| Up / Down     | change the one you are on; hold to repeat |
| Drag a column | set it to where your finger is            |
| Tap a column  | move to it, and stay                      |
| **Short A**   | **keep it and leave**                     |
| **Short B**   | **put it back and leave**                 |
| Long B        | home, keeping                             |

**A value is applied as you step it**, because one you cannot see while choosing
it is one you choose twice — and B putting it back is what lets "leave without
saving" mean anything. Nothing is written until A.

There is no "now you are editing" state, and that is what B buys: a press that
can be taken back does not need guarding against. A tap and a press of A are the
same event and not the same act — a finger carries the column it landed on, so
it moves there and stays; a press has none to carry, so it can only mean the
page. **The grid is the exception to "down is settings"**: inside it, down
is the next row, because a rule that let a long list fall out of itself while
being scrolled would cost more than the one gesture it saves.

**The grid is everything.** Up from anywhere on the carousel opens the main
region's six-icon shape: a 3x2 grid of every app there is, scrolling when there
are more than six. Finding the twelfth app is two screens rather than twelve
steps, which is what the carousel alone could not do — a carousel is a
shortlist, and it stops working the moment it is used as a directory.

The focused icon is ringed, and its name is the header's `[TITLE]` — so an icon
needs no label under it and the name is still there to read.

**Pinning moves an app between the two.** Long A on a grid icon pins it or
unpins it — the toggle itself, not a menu offering one choice, because a menu
with a single item is a press spent on nothing.

The set is kept in the config, beside every other setting, and takes the same
road: NVS first, mirrored to the card when there is one. It records the apps
kept _off_ the ring rather than the ones on it, so the default — an empty set —
is every app pinned, which is what a device with no history should show;
unpinning is the exception a user makes, and the exceptions are what is worth
storing.

**Short B returns to where you came from** — from an app to the page or grid
cell it was launched from, from a plane to home. **Long B goes to the cat from
any depth.** That is what keeps the two apart at an app's root, where they would
otherwise mean the same thing.

**What this asks of your app.** Your `icon.png` is your face in the grid and in
the carousel, at a size where it is actually read. Ship one; an app with none
falls back to the mascot, which is honest but makes every app without an icon
look like every other. Your manifest `name` is what the header reads while you
are focused, so it is read on its own rather than in a column — make it stand
alone.

Touch reaches most of this without any addition: a swipe steps the carousel and
scrolls the grid the way the joystick does, and a tap picks out an icon.

**In the grid a tap selects and stops there.** Launching is short A and pinning
is long A, and a finger does neither. The grid is where a stray touch would cost
the most — start the wrong app, or unpin the one you rely on — and the guard
against that is not a confirmation but that touch simply cannot reach the act.
A tap still moves the ring, so a finger is the fast way to _get_ somewhere and
the joystick is what commits.
