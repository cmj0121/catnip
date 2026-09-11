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


# ---- Scanner: rings, a gap, and something found ---------------------------
# Two open rings and a blip sitting in the gap: a radar rather than any one
# protocol, because this app listens on several and its face must not promise
# the one everybody recognises. The fan that used to be here went to the
# platform's `wifi` glyph, where it now means Wi-Fi and only Wi-Fi.
#
# The gap is what makes it a sweep instead of a target. A pair of closed rings
# reads as a bullseye, which is a thing you aim at rather than a thing that is
# looking.
S_CX, S_CY = 64.0, 64.0
S_RADII = (50.0, 26.0)
S_STROKE = 12.0
S_FROM, S_TO = 318.0, 318.0 + 300.0  # clockwise, leaving the gap at the blip
S_DOT = 9.0
S_BLIP = 11.0
S_BLIP_XY = (99.4, 28.6)  # on the outer ring, in the gap


def scanner_svg():
    parts = []
    for r in S_RADII:
        a0, a1 = math.radians(S_FROM), math.radians(S_TO)
        x0, y0 = S_CX + r * math.cos(a0), S_CY + r * math.sin(a0)
        x1, y1 = S_CX + r * math.cos(a1), S_CY + r * math.sin(a1)
        parts.append(
            '  <path fill="none" stroke="%s" stroke-width="%.1f" '
            'stroke-linecap="round" d="M %.3f %.3f A %.3f %.3f 0 1 1 %.3f %.3f"/>'
            % (BODY, S_STROKE, x0, y0, r, r, x1, y1)
        )
    parts.append(
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>' % (ACCENT, S_CX, S_CY, S_DOT)
    )
    parts.append(
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>'
        % (ACCENT, S_BLIP_XY[0], S_BLIP_XY[1], S_BLIP)
    )
    return _svg(parts)


def scanner_png():
    from PIL import Image, ImageDraw

    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for r in S_RADII:
        box = [(S_CX - r) * SS, (S_CY - r) * SS, (S_CX + r) * SS, (S_CY + r) * SS]
        d.arc(box, S_FROM, S_TO, fill=BODY, width=int(S_STROKE * SS))
    d.ellipse(
        [(S_CX - S_DOT) * SS, (S_CY - S_DOT) * SS, (S_CX + S_DOT) * SS, (S_CY + S_DOT) * SS],
        fill=ACCENT,
    )
    bx, by = S_BLIP_XY
    d.ellipse(
        [(bx - S_BLIP) * SS, (by - S_BLIP) * SS, (bx + S_BLIP) * SS, (by + S_BLIP) * SS],
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


ICONS = {
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
