# Writing a catnip app

**Languages:** English · [繁體中文](app-ui-spec.zh-TW.md)

What an app provides, what the platform provides, and how the two meet. All of
it works today except where a line says otherwise; _What this asked for, and
where each of it landed_ at the foot of the page maps the recent changes onto
the code.

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
| **Lines of text**   | a page to be read rather than chosen from        | one line per line, nothing ringed        |
| **Values**          | settings — quantities, not choices               | **vertical bars**, side by side          |
| **A face**          | one thing to be looked at, with labels around it | **canvas** — a centrepiece and two lines |

**Rows and lines are not the same shape.** A row is a thing you may pick: it
reserves an icon slot, it is ringed when the cursor is on it, and A on it does
something. A line is a thing you read: no slot, no ring, and A on it is the
page's business rather than the line's. `ui.list{ layout = "text" }` is the
second, and the device page is what asked for it — a ring round "free heap"
would be promising that pressing A there did something to the free heap.

The cursor is still there in a column of lines; it is simply not drawn. What is
left of a selection on a page nobody is choosing from is the reading position,
and the page that is up is the half of it that shows.

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

More than six icons is **the next page**, not a longer scroll. The columns are
fixed at three and the rows at two, and a seventh icon does not reflow the six
above it into a tighter arrangement — a grid that became four columns on a longer
card would move every icon whose position a user had learned. The selection still
leads: it steps cell by cell, and when it steps off the page the page turns under
it. A grid is `ui.list{ layout = "grid" }` — the same node, the same `selected`
integer, the same events; only the flow differs.

### The five screens

The shapes above say what the content _is_. This says what **chrome** goes around
it — and chrome is not chosen one piece at a time. There are five screens in the
device, an app picks one, and everything else follows from that pick.

| Screen             | Main region      | Header | Control hint    | Action bar |
| ------------------ | ---------------- | ------ | --------------- | ---------- |
| **1. Full screen** | anything         | none   | none            | none       |
| **2. Mascot**      | one icon         | over   | optional, float | may float  |
| **3. Icon grid**   | 3x2 icons, paged | always | reserved corner | may float  |
| **4. List**        | rows of text     | always | reserved corner | may float  |
| **5. Setting**     | values, paged    | always | reserved corner | may float  |

**1. Full screen** (`frame: "bare"`). The platform draws **nothing** — no bar, no
hint, no counter, not one pixel — and keeps exactly one gesture, long B. This is
the only screen where an app is alone with the panel, and that is the whole of
what it buys: there is no reserved corner to work around because there is nothing
reserved.

**2. Mascot / single icon.** One icon, centred, sized against **the full 240** —
the content is not pushed down by the bar, the bar **floats over it**. The bar is
always drawn. The hint is optional here and nowhere else, because a centred
figure leaves both bottom corners empty whether or not anything is put in one.

**3. Icon grid.** Three columns, two rows, paged, as above. The header is not
optional here: `2/3` is the only thing on the screen that says a third page
exists, and a grid without it is a grid that appears to be all there is.

**A cell is a picture, and it may carry a count.** Nothing else — no caption, no
second line, no word. The focused cell's name is the header's `[TITLE]`, which
is where a grid says what you are on, and a cell that could hold a label would
hold two lines of one within a week. `badge = 3` puts a small disc of the
primary colour on the corner; the value is a _number_, and a number cannot grow
into a sentence.

**It packs from the top left.** A grid is a directory, and a directory has a
first item. Centred, a row that is not full floats in the middle of the region
and the first cell lands in a different place for four cells than for six — so
"the top left one" stops being something a user can learn. The other shapes do
centre, because none of them wraps.

Absent is not zero. A cell with no badge has not been counted yet; a cell
showing `0` has been counted and came to nothing. The Scanner needs both — it
lists one radio at a time and the others have to say "not yet" while they wait,
which `0` would say as "nothing there".

**4. List.** One item per line — a row you may pick, or a line you may only
read — and **paged**, on fixed boundaries: lines one to eight, then nine to
sixteen. The region is cut to a whole number of them, so a half-line at the
bottom edge is not a thing this screen can show; a half-line reads as a
rendering fault rather than as an invitation to scroll, and it is not needed as
one because `2/4` in the header already says there is more.

It used to slide a viewport a row at a time, which meant the lines on screen
were a different set every time the page was opened and nothing said why. Paged,
it is the grid's shape in one dimension: the cursor moves within the page, and
the page turns under it when it steps off.

**5. Setting.** The **Values** shape, paged — as many columns as fit at a
readable width, and the next screenful is the next page rather than a sideways
scroll. It is the one screen with a gesture of its own; see _Setting something_.

### The floating widgets

Two things float above the main region. Both are the platform's, neither is
reachable from a tree, and both sit on the top layer so an app cannot draw over
one:

**The control hint**, bottom-left. Whether it costs the app anything is a
per-screen answer, and the question is always the same one: _can the content
reach that corner?_

Three of the five can, and they pay for it: a list's bottom row runs the full
width, a setting page's leftmost column is the height of the region, and a
grid's bottom-left cell is exactly there. Those three **reserve** the strip and
get a shorter region for it. A mascot cannot — a centred figure is a middle, and
holding back 34 px for a hint that will be drawn over empty panel is the
platform charging rent on space it is not using. Full screen reserves nothing
because nothing is drawn over it at all.

The rule is the question, not the list: a shape added later answers it for
itself.

**The action bar**, along the bottom, and it has two shapes.

_Two icons_ — the ordinary case, and it is **not a menu**. The left one _is_ A and
the right one _is_ B: the bar is a picture of the two buttons already under the
user's thumbs, not a third thing to aim at. The default pair is Yes / No. An app
may override the affirmative and may override what B cancels; B stays the
negative one, and the platform refuses to bind it to an action the manifest
marks destructive.

_Three icons_ — a modal, because three does not map onto two buttons. While it is
up the bar takes left and right for itself: they step between the three, A runs
the focused one, B puts the bar away. Up and down still belong to the content
underneath, which stays visible.

**The shape is not a thing an app chooses.** A bar is a modal exactly when it
cannot be said as two buttons, and that is one rule with two ways of being true:
there are three of them, or there are two and the second cannot go on B. An
action nothing can reach is not an action, so a pair whose right-hand side is
destructive steps rather than binding.

A tap runs an icon directly in either shape. That is the finger's only route to
Cancel, which is the reason the bar carries a visible one at all.

**When both are up, the hint rides on the bar.** The bar rises from the bottom
edge and needs its full width for two or three icons, so it does not start to the
right of anything; the hint is lifted by exactly the bar's height and sits on its
top-left shoulder. It is still the lowest, leftmost thing that is not the bar,
which is the only sense in which "bottom-left" was ever a position rather than a
rank.

**And it answers for the bar, because the bar is what is listening.** The rule
has not changed — a direction is lit when whatever owns it does something with it
— only the owner has. In the two-icon shape the bar owns no directions at all
(the icons _are_ A and B), so every arrow keeps meaning what the page underneath
means and the hint changes nothing but its height. In the three-icon shape the
bar owns left and right, so those two are lit for stepping between the three
while up and down go on answering for the content below. A user who has just
been handed a third choice is exactly the user who needs telling that left and
right now do something, which is the case the hint was built for.

An app that set `"hints": false` gets no hint here either. The bar is unaffected
by that flag: it is not an explanation of the controls, it is one of them.

### Waiting

There is one picture of waiting in this device and the platform draws it: a ring
of eight dots with a bright one running round it, once a second, taken from the
clock rather than counted — a spinner that sped up when the device had less to
do would be saying the opposite of the truth.

**It takes the whole panel**, and takes it from the bar and the hint as well.
While it is up nothing underneath is true any more — the app being loaded is not
the menu behind it, the list being scanned for is not the list still on screen —
and a half-covered screen of stale content invites reading. The bar would be
naming a screen that is going away and the hint would be a picture of controls
that do nothing; both would be lying, quietly, in a corner.

**No app raises it and no app draws one.** What waiting looks like was never an
app's to decide, and an app that wrote its own would be one more shape a user
has to learn for a thing they already know. The platform raises it for what it
can see: a scan with nothing to show yet, an app being read off the card.

**"Nothing to show yet", not "something is running."** A caller that keeps
polling keeps a scan in the air the whole time it is open, and a spinner that
never stops tells nobody anything.

So it is raised by **throwing the last answer away**, not by asking for one. A
scan's claim begins at `rescan()` and ends at the first answer after it; asking
begins nothing, because asking is only how an answer is collected and a caller
polling for one it can already draw is not waiting. An app that opens on a page
of results asks for a fresh look on the way in, which is what it wanted anyway,
and gets the ring for it.

It used to begin at the first ask and lapse when the asking stopped. That read
perfectly with one radio and one page, and fell apart the moment a page polled
two radios in turn: each one's claim lapsed while the other was sweeping, so
every round put a full-screen opaque ring back over a grid that had counts on it
and was perfectly readable. **A page with something to show is not waiting**,
whatever is running behind it.

It ends on its own either way — a claim that takes the whole device must be one
that can end without anybody clearing it.

**The status LED says it too**, which is the one place an answer can be read
without looking at the screen — and that matters most in the case the ring
cannot help with, because loading an app is the one moment the screen is about
to be replaced anyway. Green is resting, the working colour is the same earthy
yellow the ring is drawn in, and white is the screen being off: a dark panel is
the state most likely to be read as a device that has died.

**Long B is the safety zone and is never on the bar.** Short B is negotiable —
an app may claim it, and the bar may rebind it. Long B reaches the cat from any
depth and reaches no handler of anyone's.

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
6. **Let `[PAGE]` be derived, and let it count screenfuls.** The frame works it
   out from what the main region holds, and there is no API that sets it:
   everything that pages counts **pages** — a grid, a list, a page of values — a
   carousel counts **positions**, and a single icon counts nothing and stays
   blank. `2/3` rather than `8/14`, because what a counter is for is how much
   further there is to go, and the unit a reader can act on is screenfuls. "Row
   twelve of forty-five" is an ordinal, and an ordinal is not an answer.
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

Four of them mean the same thing everywhere in the device, platform screens and
apps alike. An app that means something else by one of these is not different,
it is wrong:

| Gesture     | Means           | Which is to say                        |
| ----------- | --------------- | -------------------------------------- |
| **Short A** | select          | activate the thing that is focused     |
| **Short B** | cancel          | put back, climb, or leave              |
| **Long A**  | more options    | the action bar, for the selected item  |
| **Long B**  | exit to the cat | the platform's, at every depth, always |

Everything else depends on where you are — which is exactly why the device draws
a picture of it in the corner.

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
one, move one, or discover whether one is up. **What it costs is per-screen**,
per _The floating widgets_ above: a list reserves the corner and gets a narrower
bottom row for it, a grid and a setting page and the mascot give up nothing
because their content never reaches that corner, and a `frame: "bare"` app
reserves nothing because nothing is drawn over one.

**A line at the foot of a page can move out of its way.** A column ranges its
text left, which puts the start of the bottom line right where the hint is, so a
label may ask for `align = "right"` and sit at the other end instead. It exists
for that: a canvas gets the same thing free from the order its children are
named in, and a column had no way to say it at all.

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
of positions and the grid turns a page at a time, and momentum is a solution to
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

**Long A.**

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

| Knob                                  | Where    | Effect                                                        |
| ------------------------------------- | -------- | ------------------------------------------------------------- |
| `name`                                | manifest | default title, and the name on your page                      |
| `icon`                                | manifest | your identity icon; absent → the mascot                       |
| `actions[]`                           | manifest | the action catalogue: `id`, `name`, `icon`                    |
| `frame`                               | manifest | `"standard"` (default) or `"bare"` — screen 1 or 2–5          |
| `counter`                             | manifest | `false` if you are not a list                                 |
| `hints`                               | manifest | `false` to draw no control hint over you                      |
| `ui.title(s)`                         | Lua      | a title that changes at runtime                               |
| `layout`                              | Lua      | on a list: `"grid"`, `"mixer"`, `"row"`, `"canvas"`, `"text"` |
| `badge`                               | Lua      | a count on a grid cell; absent is "not counted", not zero     |
| `style`                               | Lua      | which of the roles below a node is set in                     |
| `align`                               | Lua      | `"right"` on a line that must clear the hint's corner         |
| `on_click` / `on_options` / `on_back` | Lua      | claim short A, long A, short B                                |

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

The sizes are the platform's and are not yours to set, but they are worth
writing down because they are the reason a role is enough:

| Role      | Size | Why that one                                        |
| --------- | ---- | --------------------------------------------------- |
| `body`    | 16   | a full row of a list, read at arm's length          |
| `title`   | 20   | a heading, one step clear of the rows under it      |
| `caption` | 10   | working, deliberately not competing with the answer |
| `display` | 96   | one number owning the panel                         |

**The typeface is monospaced, and that is a decision rather than a taste.**
Nearly everything this device shows is a value beside a label: a channel next to
a name, a strength next to both, a heap size under a version. In a proportional
face those columns line up only by accident — the number after a short name sits
somewhere else than the number after a long one — and a page has to be read
across instead of down. Monospaced, the columns are a property of the type
rather than of whichever strings happen to be in them.

`body` and `title` were 14 and 16. A list is the screen this device spends most
of its time being, and 14 is a size you read by leaning in — which on a device
held in two hands at a desk is the wrong posture to design for. The step also
keeps the gap between body and title: moving one and not the other would have
left a heading a hair larger than its rows, which is worse than no heading. It
costs rows per page, and _The five screens_ already says the region is cut to
whole rows, so what it costs is visible rather than clipped.

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

_The face is built; the setter is not (#73)._ The File Browser above is a list;
this is the other end of the same vocabulary, and it needs nothing the platform
does not already have. **It is three screens**, and naming which of the five each
one is settles almost every question about it:

| Level        | Screen      | Shows                                         | Reached by              |
| ------------ | ----------- | --------------------------------------------- | ----------------------- |
| **Carousel** | Mascot      | `14:32` — and nothing else                    | left/right from the cat |
| **Face**     | Full screen | time, date, weekday, where the time came from | short A                 |
| **Setter**   | Setting     | hour, minute, day, month, year                | short A                 |

Short B climbs back down the same three, and long B goes to the cat from any of
them.

**The carousel shows the time and stops there.** It is one position on a ring of
apps, one of a handful the user will step past looking for something else, and
what it owes them at that speed is the one fact they came for. A date and a
weekday and a sync line at carousel size are four things to read where one was
asked for; put them where there is room and they are worth reading.

**The face is the whole panel** — `frame: "bare"`, no bar, no counter, **no
hint**. That is the point of promoting it: this is a clock being looked at, and
a clock being looked at should have nothing on it that is about the device.

```text
┌────────────────────────────────────────────────┐
│  2026-09-10                                    │  title
│                                                │
│                   14:32                        │  display
│                                                │
│           S M T W [T] F S         NTP 14:30    │  a strip, and body
└────────────────────────────────────────────────┘
```

Three layers, three sizes, and **none of them asks to be read twice**: the date
waits in the corner for whoever wants it, the time meets the eye in the middle,
and the weekday is seven letters with today's the bright one. The strip earns
its place by not needing to be read at all — seven positions, and the lit one is
the answer.

```lua
local letters = {}
for i, d in ipairs{ "S", "M", "T", "W", "T", "F", "S" } do
  letters[i] = ui.label{ id = "day" .. i, text = d, style = "caption" }
end
local week = ui.list{ id = "week", layout = "row", align = "center" }

ui.screen{
  ui.label{ id = "date", text = "2026-09-10", style = "title", align = "left" },
  ui.label{ id = "time", text = "14:32",      style = "display" },
  week,
  ui.label{ id = "src",  text = "NTP 14:30",  style = "body" },
}
week:set_children(letters)      -- and today's is set to `body`
```

**Seven nodes, because a node carries one role.** "These seven letters, with
today's brighter than the rest" is seven roles or it is nothing, and that is
what `layout = "row"` is for. A strip gives all of them **one size** and lets
them differ only in ink, so the line does not shift under the eye at midnight —
which is the one moment nobody is watching it.

**The order is the layout, and `align` breaks the ties the order cannot.** The
date is named before the centrepiece, so it is the line above; the strip and the
source line are named after it, so they are the line below, first-to-the-left
and last-to-the-right — which is why the source line lands in the bottom-right
corner with nothing saying a coordinate.

The two that _do_ say `align` are the two the order could not settle. A line
with nothing else on it is both the first and the last thing on its line, so
"centred" was a guess: `align = "left"` is what puts the date in the corner. And
the strip is the first of two on the bottom line, which would range it left,
where the thing that is looked at rather than read belongs in the middle.

**Where the time came from lives here and only here.** Not on the carousel, where
it would be three quarters of what is on screen; and not on the setter, where a
line saying the network already answered this would be reading over the shoulder
of someone in the middle of answering it themselves. On the face it is the one
place a doubt about the time can be settled, which is when anyone ever asks.

An unsynced clock says nothing rather than saying it is unsynced — the line is
simply absent.

**An unset clock says so.** The RTC comes up never having been set, and a
confident `00:00` on the first of January is worse than an admission: it will be
believed. So the same face shows `--:--` at the same size and in the same place,
with **no** letter lit in the strip — seven positions, none of them today — and
one line of `caption` under it saying to press A.

**Setting it is the Setting screen**, unchanged, which is the whole reason for
naming the five:

```text
      14      32       9      Sep     2026
     ╔═══╗  ┌───┐   ┌───┐   ┌───┐   ┌───┐
     ║███║  │███│   │███│   │███│   │███│
     ╚═══╝  └───┘   └───┘   └───┘   └───┘
     Hour    Min     Day    Month    Year
```

Left and right choose a column, A focuses it, up and down change it, A writes it,
B puts it back — the preference page's page, with the clock's numbers in it, and
a user who has changed the brightness once has already learned this. **Hours and
minutes wrap** where a preference clamps: 23 goes to 00, because a clock is a
ring and a brightness is not.

**Short A reaches it from the face, where long A would be the rule.** Rule 7 puts
operations behind long A because there is a selected item for them to act on; the
face has no selection and nothing else short A could possibly mean, so the clock
spends it on the one operation it has. Long A does the same thing, so a user
arriving from any other app finds it where they expect. Nothing is drawn either
way, which is the part of rule 7 that actually binds.

## Example: the Scanner

_Built (#57)._ The File Browser is a list and the Clock is a face; this is the
third shape — **a grid that an app builds for itself**, which until now only the
launcher had.

It listens on four protocols and can hear two of them, and every decision on the
page comes out of that sentence.

```text
┌────────────────────────────────────────────────┐
│  [BATTERY]        BLE               1/1        │  the focused cell names itself
│                                                │
│   (( ))⁸     ✳³      •))       ▮))             │  wifi  ble   ir    nfc
│                                                │
│   ▮))                                          │
└────────────────────────────────────────────────┘
```

**Four cells, and two of them greyed.** IR and NFC are drawn rather than left
out, because "this device does not have it" and "this device has not been told
to look" are different answers and a grid showing only what works cannot tell
them apart — a user whose headphones do not appear needs to be able to see
whether the scanner is broken. Pressing one opens its page, which says the true
thing. **A press that does nothing at all is the one answer nobody can tell from
a crash.**

**The count is the badge, and absent is not zero.** A cell with no badge has not
been listened to yet; a cell showing `0` has been, and heard nobody.

**The word is in the header, as it is in the launcher.** That matters more here
than there: the mark on the BLE cell is the Bluetooth rune, which stands for
more than this radio can hear, and the header is where the claim gets narrowed
to what is true.

**One radio at a time, and the turn waits for an answer.** The 2.4 GHz front end
is shared, so a Wi-Fi sweep and a BLE window running together are two scans each
getting a fraction of the radio. Nothing has to be stopped to arrange it: each
driver starts a scan when asked and rests when it is not, so "one at a time" is
only the app asking one at a time. The turn moves when a radio answers and not
before — a round robin that stepped on a "still listening" would leave every
cell permanently half-asked.

**Which one is listening needs no marker**, because the three levels of ink
already say it: full for the radio listening now, quiet for one resting, faint
for one this board cannot hear. The brightest cell moves, and that movement is
also the page saying it is working.

**One round, and then it holds what it found.** A scanner that never stops is a
front end that never stops, kept busy to refresh a number nobody is watching
change. When the round ends nothing is resting either, so every cell this board
can use goes back to full ink.

**Long A looks again — at all of them.** An operation lives behind a long press
and is done rather than offered, the way the launcher's pin is. The counts stay
up while the new round runs: blanking them would replace a true-a-minute-ago
answer with the one thing this page reserves for "not looked yet". Short A opens
one protocol and asks that radio, which is the same rule at one cell's scale.

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
what is answering on the bus — as one column of prose, and nothing else:

```text
  ┌──────────── Device ────────────┐
  │ catnip v0.4.0                  │   the facts scroll: up and down
  │ clock 2026-09-10 14:18 (ntp)   │   move through them. Nothing is
  │ battery 82%                    │   ringed, because nothing on this
  │ card none                      │   page is a choice - what is on
  │ built 2026-09-10               │   it is a reading position, and
  │ ESP32-S3 rev 2, 240 MHz        │   the scroll is the half of it
  │ flash 16 MB                    │   that shows
  └────────────────────────────────┘
        ╭──────────────────────────╮
        │ ⚙ Prefs  ▤ Sizes  ⚠ Diag │  ← A, and the platform draws it
        ╰──────────────────────────╯
```

It used to carry two tiles under the facts, and they were the thing this page
was least entitled to have: **operations drawn on a screen**, on the one page
whose whole point is that it answers before it offers. They are behind A now,
where every operation in the device is.

Short A rather than long, and for the same reason the clock's face takes short
A: there is no selection here for long A to be about, and nothing else short A
could mean. Long A reaches the same three.

Three doors, so the bar is a modal — left and right step it, A goes through,
B puts it away. **Sizes** is every type role drawn in itself, largest first,
because an argument about whether `body` is big enough is otherwise an argument
about numbers. All three are the platform's own — none is in the grid, none can
be pinned, and nothing you write can replace any of them.

**Preference** is the **Values** shape above. A keeps and leaves, B puts back and
leaves, and both come back here rather than to the cat; long B is the one that
goes all the way home, as it does everywhere.

**Diagnostic** takes the whole panel — it is testing the panel, so it can hardly
share it — and **long B gives it back**. It used to restart the device instead,
which is a way out in the sense that a device that has started again is showing
the cat, and is not one in any sense a person holding it cares about.

### Setting something

Every page that sets a value works the same way, whether it is the platform's or
yours — the preference page and the clock's setter are the same page. It is the
**Setting** screen from _The five screens_: the Values shape, paged like the
grid, and **two levels**.

|              | Unfocused — you are on the page  | Focused — you are on one column      |
| ------------ | -------------------------------- | ------------------------------------ |
| Up / Down    | previous / next page             | change this value; hold to repeat    |
| Left / Right | step between this page's columns | nothing                              |
| **Short A**  | **focus the column**             | **write it, and let go**             |
| **Short B**  | **leave the page**               | **put this column back, and let go** |
| **Double A** | **write everything and leave**   | **write everything and leave**       |
| Long B       | home                             | home                                 |
| Tap a column | focus it                         | set it to where your finger is       |

**Focus is not a mode. It is select and cancel, one level down.** A selects: on
the page that means this column, on the column it means this value. B cancels: on
the column that means the value you were changing, on the page it means the page.
The four fixed gestures still mean exactly what they mean everywhere else, and
nothing new has to be learned — which is also why up and down are lit in both
levels without the hint having to say anything different. They do something in
both; that is all the hint ever claims.

**Left and right go dead inside a column**, and that is the one thing the level
buys that is worth paying a gesture for. Changing a value is the one place in
the device where a sideways push that landed on the neighbour would be silently
wrong — you would have moved a number you were not looking at. Unfocused, the
same push is free.

**A value is applied as you step it**, because one you cannot see while choosing
it is one you choose twice. What A writes is what you have already been looking
at; what B puts back is what the column held when you focused it. So leaving the
page unfocused loses nothing that was not already returned.

**Double A writes everything and leaves, and it exists only here.** Every other
screen in the device has one meaning for A and this one has two, so the shape has
somewhere to fit; and this is the only screen where "and I am done" is a distinct
thing to say, because it is the only one where you may have set three things and
would otherwise have to walk back out past all three. A finger says it the same
way — two taps.

A tap and a press of A are the same event and not the same act: a finger carries
the column it landed on, so it focuses that one; a press has none to carry, so it
can only mean whichever the ring is already on. **The grid is the exception to "down is settings"**: inside it, down
moves the selection on and turns the page under it when it runs out, because a
rule that let a long list fall out of itself while being walked would cost more
than the one gesture it saves.

**The grid is everything.** Up from anywhere on the carousel opens the **Icon
grid** screen: 3x2, every app there is, **paged** when there are more than six.
Finding the twelfth app is two pages rather than twelve steps, which is what the
carousel alone could not do — a carousel is a shortlist, and it stops working the
moment it is used as a directory.

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
turns the grid's pages the way the joystick does, and a tap picks out an icon.

**In the grid a tap selects and stops there.** Launching is short A and pinning
is long A, and a finger does neither. The grid is where a stray touch would cost
the most — start the wrong app, or unpin the one you rely on — and the guard
against that is not a confirmation but that touch simply cannot reach the act.
A tap still moves the ring, so a finger is the fast way to _get_ somewhere and
the joystick is what commits.

## What this asked for, and where each of it landed

_The five screens_ was a description of where the device was going. All seven of
the changes it asked for are in, and so is a second round that came out of using
them - a sixth layout for a page that is read rather than chosen from, one
picture of waiting, a monospaced face, and a counter that says screenfuls. The
table is the map of where the first seven landed rather than a list of what is
owed:

| #   | Change                                                                                  | Where                                       |
| --- | --------------------------------------------------------------------------------------- | ------------------------------------------- |
| 1   | ~~The grid pages instead of scrolling; three columns, fixed~~ — built                   | `catnip_app_grid.c`, the grid layout        |
| 2   | ~~The list region is cut to a whole number of rows~~ — built                            | the frame's region maths                    |
| 3   | ~~`body` 14 → 16, `title` 16 → 20~~ — built                                             | `lvgl_backend.cpp` role → font              |
| 4   | ~~The setting screen: paged, two levels of focus, double A~~ — built                    | the preference page and the input pass      |
| 5   | ~~The hint reserves its corner where content reaches it, and rides on the bar~~ — built | the frame                                   |
| 6   | ~~The action bar: the two-icon and three-icon shapes~~ — built                          | `catnip_bar.c`, `frame.cpp`, the input pass |
| 7   | ~~The clock becomes three screens~~ — built                                             | `apps/clock/main.lua`, `manifest.json`      |

**Two apps changed, and neither had to.** The clock became three screens
because it _is_ three screens; the File Browser changed because it was building
its own options menu and the bar took that over — it lost sixty lines and gained
a manifest catalogue. The WiFi Prober — now the Scanner's Wi-Fi page — was not
touched at all: every one of 1–6 is the platform's side of a seam it already
sits behind, which is the argument for the seam. An app that had to be edited to
get a taller `body` would mean the roles never worked.

A third round since, from building the Scanner (#57), and it went the same way:
a cell that carries a count, a grid that packs from the top left, and a waiting
ring that is raised by throwing an answer away rather than by asking for one.
All three are the platform's side of the same seam — the app names four
protocols and a count each, and decides nothing about what any of it looks
like.

Each of 1–6 is testable at the host layer that already exists — `test/native`
for what the input pass and the page machine decide, `test/layout` for what LVGL
actually places, and screenshots out of the latter for what it looks like.
Number 7 is testable the way `test_filebrowser.c` tests the browser: load the
app's Lua, drive it, and read the tree back.
