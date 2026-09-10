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


# ---- WiFi Prober: the signal fan ------------------------------------------
# Three arcs off one origin and a dot at the origin - the mark everyone already
# reads as "wireless".
W_CX, W_CY = 64.0, 96.0
W_RADII = (26.0, 50.0, 74.0)
W_STROKE = 13.0
W_SPREAD = 45.0  # degrees either side of straight up
W_DOT = 10.0


def wifi_svg():
    parts = []
    for r in W_RADII:
        a0 = math.radians(270.0 - W_SPREAD)
        a1 = math.radians(270.0 + W_SPREAD)
        x0, y0 = W_CX + r * math.cos(a0), W_CY + r * math.sin(a0)
        x1, y1 = W_CX + r * math.cos(a1), W_CY + r * math.sin(a1)
        parts.append(
            '  <path fill="none" stroke="%s" stroke-width="%.1f" '
            'stroke-linecap="round" d="M %.3f %.3f A %.3f %.3f 0 0 1 %.3f %.3f"/>'
            % (BODY, W_STROKE, x0, y0, r, r, x1, y1)
        )
    parts.append(
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>' % (ACCENT, W_CX, W_CY, W_DOT)
    )
    return _svg(parts)


def wifi_png():
    from PIL import Image, ImageDraw

    im = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for r in W_RADII:
        box = [(W_CX - r) * SS, (W_CY - r) * SS, (W_CX + r) * SS, (W_CY + r) * SS]
        d.arc(box, 270.0 - W_SPREAD, 270.0 + W_SPREAD, fill=BODY, width=int(W_STROKE * SS))
    d.ellipse(
        [(W_CX - W_DOT) * SS, (W_CY - W_DOT) * SS, (W_CX + W_DOT) * SS, (W_CY + W_DOT) * SS],
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
    "wifiprober": (wifi_svg, wifi_png),
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
