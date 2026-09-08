# Brief: the catnip icon set

A small solid-colour icon family for **catnip**, the firmware of MeowKit: a
320x240 landscape LCD in the hand, a 5-way joystick, two buttons, a touch panel,
running small Lua apps. Its mascot is a cat. It is a toy console for people who
like taking things apart, not an appliance and not a phone.

The interface is dark. Selected rows keep white ink on black and take a 2px
`#00B0FF` rounded border — they do not invert.

## Where they live

One SVG per icon is the source; a size is a rendering of it, never a redraw.

**14 px, in front of text.** In a list row, and in an operator button — the two
actions a long press raises from the bottom are each an icon and a word, at the
same size as a row. One size, one place: beside words.

## Hard constraints

1. **It must read at 14x14.** That is its actual and only size; never judge one
   at a size it is not used at, because a large preview flatters everything.
2. **Solid body, one assigned colour.** Inner marks (a `!`, an `x`) may be a
   second colour. Not an outline, not a two-tone tab, not the row's text ink.
3. **Selected rows do not invert**, so a hole in a shape is the black ground
   showing through and is allowed if it still reads at 14. Gear centre holes
   use `fill-rule="evenodd"`; bang / check / x marks stay a second colour.
4. **One optical weight across the set.**
5. **No text, letters or numbers** inside an icon.

## Grid and delivery

- **128x128 master**, artwork inside a comfortable margin.
- One **SVG per icon**: `viewBox="0 0 128 128"`, fill only, no stroke,
  `fill="#RRGGBB"` on a transparent ground; no `<style>`, classes, or
  unflattened transforms. Multiple `<path>` elements are painted in order.
- File names are the ids below: `folder.svg`, `file.svg`, …

`tools/gen_icons.py` rasters them to RGB565A8 at 14. The SVG is the
source; the bitmaps are build output. The renderer accepts `M L H V C S A Z`
and `fill-rule` `nonzero` (default) or `evenodd`.

## The concept

Solid, rounded, high-tech. `folder` is the locked sample: `#5AABFF`, stepped
tab, large body radius. The cat is not one of these twelve: the mascot is a
platform image with an id of its own, drawn whole where it is given the room.

## The icons

| id            | means                                           | fill                                              |
| ------------- | ----------------------------------------------- | ------------------------------------------------- |
| `folder`      | a directory you can go into                     | `#5AABFF` stepped tab                             |
| `file`        | a plain file of no particular kind              | `#CFC7B6`                                         |
| `placeholder` | a slot that must show something and has nothing | `#7B7D7B` hollow frame                            |
| `image`       | a picture file                                  | `#E6E6E6` + `#000000` card + black sun/mountain   |
| `audio`       | a sound file                                    | `#45E0CB`                                         |
| `settings`    | configure this                                  | `#C6CDD6` 8-tooth gear, evenodd centre hole       |
| `edit`        | rename or change this                           | `#C6CDD6` pencil + rule                           |
| `trash`       | delete this                                     | `#FF4B3E` + `#FFF3EE` + warm-white `x`            |
| `refresh`     | re-read or reset this                           | `#C6CDD6` two clockwise arrows                    |
| `warning`     | this cannot be undone                           | `#F9E154` + `#C9A428` + honey `!`                 |
| `ok`          | confirmed, done, yes                            | `#22C55E` + `#FFF3EE` checkbox + warm-white check |
| `close`       | dismissed, cancelled, no                        | `#C6CDD6` bold x                                  |

`ok` and `close` are the pair a user answers a question with. They must be
unmistakable from each other — that pair matters more than any single icon.

## How it is judged

1. **At 14x14 on black**, every icon is still identifiable.
2. **Stacked as a list**, no icon is optically heavier than another.
3. `ok` and `close` cannot be confused — they are the pair an operator bar
   offers side by side.
4. The set looks like one hand drew it in one sitting.
5. It looks like it belongs on a device with a cat on the box.
