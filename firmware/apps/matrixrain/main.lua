-- Matrix Rain (#88) - columns of glyphs falling down the panel, forever, until B.
--
-- It is the lightest app there is and the most revealing: no input, no
-- selection, no logic beyond a clock. Everything else catnip draws is event
-- driven - a press moves a cursor, a handler redraws. This is the opposite
-- shape, an app that redraws on its own clock whether or not anything happened,
-- and building it is how the platform finds out whether an app can hold the
-- frame and how cheap a full-panel redraw is (every one is a 153,600-byte blit).
--
-- It is also the first screen with nothing to press: no focus, no hint. The
-- manifest's `hints: false` is the first real user of that opt-out (#80). If a
-- hint draws over the rain, that is the bug this app exists to surface.
--
-- The colour is the point of the `color` prop. A style role names an emphasis
-- and the platform paints it; this app is not choosing an emphasis, it is
-- drawing a picture out of green glyphs, and the green is the content. That is
-- the one thing a role cannot say, which is why the prop exists.
--
-- The whole panel is one label. A monospace font makes a newline-joined block
-- of text a tight grid with no placement asked for - the platform never has to
-- position a single cell, because a line of characters already is a row and a
-- stack of lines already is a column. One node, rewritten each frame, is also
-- the cheapest thing to reconcile: the tree is built once and only its text
-- changes, which is exactly what the retained renderer is for.

-- The panel in cells. Close to what catnip_font_16 gives on 320x240; the grid
-- fills whatever it actually is and does not need these exact.
local COLS = 34
local ROWS = 11

-- The alphabet the rain falls in. The Matrix uses half-width katakana; this
-- font is ASCII, so it falls in the glyphs that read as "code". A space is
-- never picked: a space is the absence of a glyph, which is how the gaps and
-- the dark between the drops are drawn.
local GLYPHS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789<>[]{}/\\|=+*#$%&@"
local function glyph()
  local i = math.random(#GLYPHS)
  return GLYPHS:sub(i, i)
end

-- A drop per column: where its head is now, how long a lit trail follows it,
-- and how fast it falls. The head starts above the top so a column is dark for
-- a while, rains, and empties again rather than every column always raining.
local head, trail, speed = {}, {}, {}
local function reset(x)
  head[x] = -math.random(0, ROWS)
  trail[x] = math.random(3, ROWS)
  speed[x] = math.random(1, 2)
end
for x = 1, COLS do reset(x) end

-- The whole grid as one block of text: ROWS lines of COLS characters, a glyph
-- where a drop's trail is lit and a space everywhere else. Rebuilt each frame;
-- the drops are what carry state between frames, not the text.
local function frame()
  local lines = {}
  for y = 1, ROWS do
    local row = {}
    for x = 1, COLS do
      local h = head[x]
      -- Lit when this cell is within the drop's trailing window. A fresh glyph
      -- each time it is drawn, so the trail shimmers the way the real one does.
      if y <= h and y > h - trail[x] then
        row[x] = glyph()
      else
        row[x] = " "
      end
    end
    lines[y] = table.concat(row)
  end
  return table.concat(lines, "\n")
end

-- One label, the whole panel. Its text is rewritten every frame; the tree is
-- never rebuilt.
local grid = ui.label{ id = "grid", text = frame(), color = "#3BE24A" }
ui.screen{ grid }

-- Advance every drop one step. A column whose trail has fallen entirely past
-- the bottom starts again from above the top with a new speed and length, so
-- the rain never settles into a pattern.
local function tick()
  for x = 1, COLS do
    head[x] = head[x] + speed[x]
    if head[x] - trail[x] > ROWS then reset(x) end
  end
end

-- The frame loop. Ninety milliseconds a tick is about eleven frames a second -
-- slower than the panel can blit, on purpose. The rain reads better falling
-- gently than racing, and every tick is a full 153,600-byte blit, so a slower
-- clock is also the cheaper one. sys.sleep yields to the scheduler between
-- frames, so the watchdog is never tripped and B is still heard. There is no
-- on_back: a single-screen app needs none, and B leaves it the way it leaves
-- any app.
math.randomseed(sys.now())
while true do
  tick()
  grid.text = frame()
  sys.sleep(90)
end
