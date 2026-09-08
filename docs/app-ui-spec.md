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

Three shapes, and no fourth. Each is a `list`; what differs is how the platform
lays it out.

| Shape               | When                                          | Layout                               |
| ------------------- | --------------------------------------------- | ------------------------------------ |
| **One icon**        | a landing page — the cat, a splash            | centred, alone                       |
| **Up to six icons** | a set of things to choose between             | **3x2 grid**, the focused one ringed |
| **Rows of text**    | anything longer, or anything that needs words | one row per line                     |

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
   counts **rows**, a carousel counts **positions**, and a single icon counts
   nothing and stays blank. A grid says `2/3` rather than `8/14` because what a
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

| Gesture          | Meaning                                        | Reaches you as                         |
| ---------------- | ---------------------------------------------- | -------------------------------------- |
| Up / Down        | move the selection; the list scrolls to follow | `prev` / `next` to the focused list    |
| Left / Right     | move the focus between focusable things        | nothing; the platform moves the ring   |
| Short A          | activate the focused item                      | `on_click(self)`                       |
| Long A           | options for the **selected item**              | `on_options(self, index)` → action ids |
| Tap a row        | select and activate it                         | `on_click(self, index)`                |
| Long-press a row | options for that row                           | `on_options(self, index)` → action ids |
| Swipe            | the joystick's four directions, by finger      | whatever that direction means          |
| Short B          | back — pop, climb, or exit                     | `on_back(self)` on the visible screen  |
| Long B           | home — the cat, from any depth                 | nothing; the platform keeps it         |
| Hold power       | power off                                      | nothing; the PMIC keeps it             |

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
climb. To leave from inside a handler, call `ui.exit()`.

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

| Knob                                  | Where    | Effect                                         |
| ------------------------------------- | -------- | ---------------------------------------------- |
| `name`                                | manifest | default title, and the name on your page       |
| `icon`                                | manifest | your identity icon; absent → the mascot        |
| `actions[]`                           | manifest | the action catalogue: `id`, `name`, `icon`     |
| `frame`                               | manifest | `"standard"` (default) or `"bare"`             |
| `counter`                             | manifest | `false` if you are not a list                  |
| `ui.title(s)`                         | Lua      | a title that changes at runtime                |
| `layout`                              | Lua      | `"grid"` on a list, for icons rather than rows |
| `on_click` / `on_options` / `on_back` | Lua      | claim short A, long A, short B                 |

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

## Home, and where your app is found

_Designed, not built._

**Home is the cat.** The landing page is not an app and not a menu; it is the
one screen every gesture can reach, and where long B always lands. It is the
main region's one-icon shape.

Two levels sit around it, and they are not two ways of doing the same thing —
one is _your_ things, the other is _everything_.

**The carousel is the shortlist.** From home, each direction opens a plane: up
is the clock, down is settings and preferences, left and right step through the
apps you keep. One per step, big.

```text
                        ▲  the clock
                        │
   apps  ◄──────────  [cat]  ──────────►  apps
                        │
                        ▼  settings, and preferences
```

**The grid is everything.** One carousel position is _All apps_, and it opens
the main region's six-icon shape: a 3x2 grid of every app on the card, scrolling
when there are more than six. Finding the twelfth app is two screens rather than
twelve steps, which is what the carousel alone could not do.

The focused icon is ringed, and its name is the header's `[TITLE]` — so an icon
needs no label under it and the name is still there to read.

**Pinning moves an app between the two.** Long A on a grid icon offers `Pin`;
long A on a pinned one offers `Unpin`. The list is a file under `/sd/catnip/`,
because that is where the platform already keeps its own state and `fs.*`
already exists — no new capability. Pins live with the card they point at: swap
the card and they go, which is right, since the apps went with it. The
built-in planes do not depend on the file and are unaffected.

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

Touch reaches all of this without any addition: a swipe steps the carousel and
scrolls the grid the way the joystick does, and a tap on an icon launches it the
way short A does.
