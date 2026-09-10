#!/usr/bin/env python3
"""Write the WiFi Prober's app icon (#54). Hand-run, like emit_icon_svgs.py.

The icon is generated rather than drawn, for the same reason the twelve platform
glyphs are: it is a flat geometric mark, and a script that emits it is a source
anyone can re-run and adjust, where a binary is a thing to be trusted.

The shape is the signal fan everyone already reads as "wireless": three arcs off
one origin and a dot at the origin. It borrows the File Browser's palette so the
launcher's icons look like a set - the body in the light ink, the one accent in
the earthy yellow.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(os.path.dirname(HERE), "apps", "wifiprober")

SIZE = 128
CX, CY = 64.0, 96.0          # the origin the fan radiates from
RADII = (26.0, 50.0, 74.0)   # the three arcs
STROKE = 13.0
SPREAD = 45.0                # degrees either side of straight up
BODY = "#DCE3EC"
ACCENT = "#E5B845"
DOT = 10.0


def arc_path(r):
    """One arc of radius `r`, swept SPREAD degrees either side of vertical."""
    a0 = math.radians(270.0 - SPREAD)
    a1 = math.radians(270.0 + SPREAD)
    x0, y0 = CX + r * math.cos(a0), CY + r * math.sin(a0)
    x1, y1 = CX + r * math.cos(a1), CY + r * math.sin(a1)
    # sweep 1: clockwise in SVG's y-down space, which is up and over the top.
    return "M %.3f %.3f A %.3f %.3f 0 0 1 %.3f %.3f" % (x0, y0, r, r, x1, y1)


def svg():
    parts = []
    for r in RADII:
        parts.append(
            '  <path fill="none" stroke="%s" stroke-width="%.1f" '
            'stroke-linecap="round" d="%s"/>' % (BODY, STROKE, arc_path(r))
        )
    parts.append(
        '  <circle fill="%s" cx="%.1f" cy="%.1f" r="%.1f"/>' % (ACCENT, CX, CY, DOT)
    )
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">\n%s\n</svg>\n' % (SIZE, SIZE, SIZE, SIZE, "\n".join(parts))
    )


def png():
    """The same shape rasterised, which is what gen_apps.py embeds."""
    from PIL import Image, ImageDraw

    # Supersampled and shrunk, because PIL's arc has no antialiasing of its own
    # and an aliased curve at 128 px looks like a staircase.
    ss = 4
    im = Image.new("RGBA", (SIZE * ss, SIZE * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for r in RADII:
        box = [
            (CX - r) * ss,
            (CY - r) * ss,
            (CX + r) * ss,
            (CY + r) * ss,
        ]
        d.arc(box, 270.0 - SPREAD, 270.0 + SPREAD, fill=BODY, width=int(STROKE * ss))
    d.ellipse(
        [(CX - DOT) * ss, (CY - DOT) * ss, (CX + DOT) * ss, (CY + DOT) * ss],
        fill=ACCENT,
    )
    return im.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    with open(os.path.join(APP, "icon.svg"), "w") as f:
        f.write(svg())
    png().save(os.path.join(APP, "icon.png"))
    print("emit_wifi_icon: wrote icon.svg and icon.png in apps/wifiprober")


if __name__ == "__main__":
    main()
