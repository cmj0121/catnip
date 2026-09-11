#!/usr/bin/env python3
"""Write the built-in apps' icons. Hand-run, like emit_icon_svgs.py.

They are generated rather than drawn, for the same reason the twelve platform
glyphs are: these are flat geometric marks, and a script that emits one is a
source anyone can re-run and adjust, where a binary is a thing to be trusted.

They share a palette with the File Browser's icon so the launcher's grid looks
like a set rather than a collection: the body in a light ink, exactly one accent
in the earthy yellow. Each app gets an `icon.svg` master and the `icon.png`
gen_apps.py actually embeds.

To replace one with real artwork, drop in an `icon.svg` and stop running this
for that app - nothing downstream knows the difference.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
APPS = os.path.join(os.path.dirname(HERE), "apps")

SIZE = 128
BODY = "#DCE3EC"
ACCENT = "#E5B845"

# Supersample and shrink: PIL's arc and ellipse have no antialiasing of their
# own, and an aliased curve at 128 px is a staircase.
SS = 4


def _svg(parts):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">\n%s\n</svg>\n' % (SIZE, SIZE, SIZE, SIZE, "\n".join(parts))
    )


# ---- Scanner: a source, and waves either side of it -----------------------
# The mark everybody reads as "radio". Symmetric, which is what keeps it off the
# Wi-Fi cell's one-sided fan inside the app: a fan means one particular radio,
# and this app listens on several.
#
# It was a radar - two open rings and a sweep - until somebody looked at it
# small, where a radar is a bullseye: a thing you aim at rather than a thing
# that is listening.
S_CX, S_CY = 64.0, 64.0
S_DOT = 12.0
S_STROKE = 12.0
# (radius, degrees either side of straight out). The outer pair is narrower, so
# the two arcs on a side nest rather than run parallel.
S_ARCS = ((32.0, 50.0), (52.0, 42.0))


def _arc_path(r, spread, side):
    """One arc, as an SVG path. `side` is 0 for the right, 180 for the left."""
    a0 = math.radians(side - spread)
    a1 = math.radians(side + spread)
    x0, y0 = S_CX + r * math.cos(a0), S_CY + r * math.sin(a0)
    x1, y1 = S_CX + r * math.cos(a1), S_CY + r * math.sin(a1)
    return (
        '  <path fill="none" stroke="%s" stroke-width="%.1f" '
        'stroke-linecap="round" d="M %.3f %.3f A %.3f %.3f 0 0 1 %.3f %.3f"/>'
        % (BODY, S_STROKE, x0, y0, r, r, x1, y1)
    )


def scanner_svg():
    parts = []
    for r, spread in S_ARCS:
        parts.append(_arc_path(r, spread, 0.0))
        parts.append(_arc_path(r, spread, 180.0))
    parts.append(
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>' % (ACCENT, S_CX, S_CY, S_DOT)
    )
    return _svg(parts)


def scanner_png():
    from PIL import Image, ImageDraw

    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for r, spread in S_ARCS:
        box = [(S_CX - r) * SS, (S_CY - r) * SS, (S_CX + r) * SS, (S_CY + r) * SS]
        for side in (0.0, 180.0):
            d.arc(box, side - spread, side + spread, fill=BODY, width=int(S_STROKE * SS))
    d.ellipse(
        [(S_CX - S_DOT) * SS, (S_CY - S_DOT) * SS, (S_CX + S_DOT) * SS, (S_CY + S_DOT) * SS],
        fill=ACCENT,
    )
    return im.resize((SIZE, SIZE), Image.LANCZOS)


# ---- Clock: a face and two hands ------------------------------------------
# A ring rather than a filled disc, because the hands have to be the accent and
# an accent on a light disc is the one pair in this palette with no contrast.
# The hands point at twelve and three: any other time is a detail nobody reads
# at 64 px, and those two are the ones that say "clock" fastest.
C_CX, C_CY = 64.0, 64.0
C_R = 48.0           # ring radius (centre of the stroke)
C_RING = 12.0        # ring thickness
C_HAND = 11.0        # hand thickness
C_MINUTE = 34.0      # straight up
C_HOUR = 24.0        # to the right
C_DOT = 7.0


def clock_svg():
    parts = [
        '  <circle fill="none" stroke="%s" stroke-width="%.1f" cx="%.1f" cy="%.1f" '
        'r="%.1f"/>' % (BODY, C_RING, C_CX, C_CY, C_R),
        '  <path stroke="%s" stroke-width="%.1f" stroke-linecap="round" '
        'd="M %.1f %.1f V %.1f"/>' % (ACCENT, C_HAND, C_CX, C_CY, C_CY - C_MINUTE),
        '  <path stroke="%s" stroke-width="%.1f" stroke-linecap="round" '
        'd="M %.1f %.1f H %.1f"/>' % (ACCENT, C_HAND, C_CX, C_CY, C_CX + C_HOUR),
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>'
        % (ACCENT, C_CX, C_CY, C_DOT),
    ]
    return _svg(parts)


def clock_png():
    from PIL import Image, ImageDraw

    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    box = [(C_CX - C_R) * SS, (C_CY - C_R) * SS, (C_CX + C_R) * SS, (C_CY + C_R) * SS]
    d.ellipse(box, outline=BODY, width=int(C_RING * SS))
    d.line(
        [C_CX * SS, C_CY * SS, C_CX * SS, (C_CY - C_MINUTE) * SS],
        fill=ACCENT,
        width=int(C_HAND * SS),
    )
    d.line(
        [C_CX * SS, C_CY * SS, (C_CX + C_HOUR) * SS, C_CY * SS],
        fill=ACCENT,
        width=int(C_HAND * SS),
    )
    d.ellipse(
        [(C_CX - C_DOT) * SS, (C_CY - C_DOT) * SS, (C_CX + C_DOT) * SS, (C_CY + C_DOT) * SS],
        fill=ACCENT,
    )
    return im.resize((SIZE, SIZE), Image.LANCZOS)


# ---- Matrix Rain: a crop of the screen the app draws -----------------------
# Not an abstract mark - a small piece of the actual screen. Columns of glyph
# cells fall down a grid; each drop is brightest at its head and fades up its
# trail, and the columns sit at different heights the way one frame of the rain
# catches them. Cells are squares, not letters: a letter is unreadable at this
# size, and the app's own gaps and drops read fine as lit and dark blocks. The
# green is the app's identity, so this icon keeps green where the rest of the
# set is light + yellow - a grey matrix would say nothing.
M_MARGIN = 8.0
M_NCOLS = 6
M_NROWS = 7
# Green from the head down the trail: bright head, then the app's green, then
# ever dimmer. Index is distance from the head; past the end stays the dimmest.
M_FADE = ("#C8FFCE", "#5BF06A", "#34C63F", "#1E9128", "#135C18", "#0C3E10")
# One drop per column: (head_row, trail_len). A head past the last row is a drop
# whose bright end has already fallen off the bottom - only its trail shows,
# which is what makes the columns look caught mid-fall rather than lined up.
M_DROPS = ((5, 4), (8, 6), (3, 5), (7, 4), (2, 6), (6, 3))


def _m_cells():
    """Yield (col, row, colour) for every lit cell, head brightest."""
    for col, (head, trail) in enumerate(M_DROPS):
        for dist in range(trail):
            row = head - dist
            if 0 <= row < M_NROWS:
                yield col, row, M_FADE[dist if dist < len(M_FADE) else -1]


def _m_geom():
    area = SIZE - 2.0 * M_MARGIN
    px, py = area / M_NCOLS, area / M_NROWS
    cell = min(px, py) * 0.62
    return px, py, cell


def matrixrain_svg():
    px, py, cell = _m_geom()
    parts = []
    for col, row, colour in _m_cells():
        cx = M_MARGIN + (col + 0.5) * px
        cy = M_MARGIN + (row + 0.5) * py
        parts.append(
            '  <rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" '
            'fill="%s"/>'
            % (cx - cell / 2, cy - cell / 2, cell, cell, cell * 0.28, colour)
        )
    return _svg(parts)


def matrixrain_png():
    from PIL import Image, ImageDraw

    px, py, cell = _m_geom()
    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for col, row, colour in _m_cells():
        cx = M_MARGIN + (col + 0.5) * px
        cy = M_MARGIN + (row + 0.5) * py
        d.rounded_rectangle(
            [
                (cx - cell / 2) * SS,
                (cy - cell / 2) * SS,
                (cx + cell / 2) * SS,
                (cy + cell / 2) * SS,
            ],
            radius=cell * 0.28 * SS,
            fill=colour,
        )
    return im.resize((SIZE, SIZE), Image.LANCZOS)


# ---- Air Mouse: the mouse, and the pointer it produces ---------------------
# Two objects, not one mark: the device on the left and the thing it moves on
# the right. Either alone would be ambiguous - a mouse body on its own is a
# mouse, not an *air* mouse, and a bare cursor is every pointing app there has
# ever been - and together they read as cause and effect.
#
# They must not touch. At the size this is looked at, two light shapes ten
# pixels apart at 128 are two and a half apart, which is the whole of what keeps
# them from fusing into one blob; the body stops at x=64 and the cursor's tip
# starts at x=74. The scroll wheel takes the accent, which is the set's habit -
# a light body with one small accent detail, the way the clock's hands and the
# Scanner's dot do it.
#
# The cursor is one list of seven corners used by both renderers, so the SVG
# path and the PIL polygon cannot drift apart.
A_BODY = (14.0, 32.0, 50.0, 84.0)  # x, y, w, h of the mouse shell
A_BODY_R = 24.0  # nearly half the width: a shell, not a box
A_WHEEL = (35.5, 44.0, 7.0, 16.0)  # x, y, w, h
A_WHEEL_R = 3.5
A_ARROW = ((74, 10), (74, 69), (88, 55), (97, 76), (108, 70), (99, 50), (118, 49))


def airmouse_svg():
    bx, by, bw, bh = A_BODY
    wx, wy, ww, wh = A_WHEEL
    d = "M %d %d " % A_ARROW[0]
    d += " ".join("L %d %d" % p for p in A_ARROW[1:])
    return _svg(
        [
            '  <rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" '
            'fill="%s"/>' % (bx, by, bw, bh, A_BODY_R, BODY),
            '  <rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="%.1f" '
            'fill="%s"/>' % (wx, wy, ww, wh, A_WHEEL_R, ACCENT),
            '  <path fill="%s" d="%s Z"/>' % (BODY, d),
        ]
    )


def airmouse_png():
    from PIL import Image, ImageDraw

    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bx, by, bw, bh = A_BODY
    d.rounded_rectangle(
        [bx * SS, by * SS, (bx + bw) * SS, (by + bh) * SS],
        radius=A_BODY_R * SS,
        fill=BODY,
    )
    wx, wy, ww, wh = A_WHEEL
    d.rounded_rectangle(
        [wx * SS, wy * SS, (wx + ww) * SS, (wy + wh) * SS],
        radius=A_WHEEL_R * SS,
        fill=ACCENT,
    )
    d.polygon([(x * SS, y * SS) for x, y in A_ARROW], fill=BODY)
    return im.resize((SIZE, SIZE), Image.LANCZOS)


ICONS = {
    "airmouse": (airmouse_svg, airmouse_png),
    "matrixrain": (matrixrain_svg, matrixrain_png),
    "scanner": (scanner_svg, scanner_png),
    "clock": (clock_svg, clock_png),
}


def main():
    for app, (svg, png) in sorted(ICONS.items()):
        d = os.path.join(APPS, app)
        with open(os.path.join(d, "icon.svg"), "w") as f:
            f.write(svg())
        png().save(os.path.join(d, "icon.png"))
        print("emit_app_icons: wrote icon.svg and icon.png in apps/%s" % app)


if __name__ == "__main__":
    main()
