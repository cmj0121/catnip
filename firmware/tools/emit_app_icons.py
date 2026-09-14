#!/usr/bin/env python3
"""Write the built-in apps' identity icons. Hand-run, like emit_icon_svgs.py.

The SVG is the source. The PNG is a raster of it, which is what gen_apps.py
embeds. Palette is the File Browser's: body in a light ink, one accent in the
earthy yellow, so the launcher grid is a set. Matrix Rain keeps green, because
that colour is the app.

rsvg-convert does the raster (transparent ground). Geometry lives once, in the
SVG emitters.
"""
import math
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
APPS = os.path.join(os.path.dirname(HERE), "apps")

SIZE = 128
BODY = "#DCE3EC"
ACCENT = "#E5B845"
GREEN = ("#C8FFCE", "#5BF06A", "#34C63F", "#1E9128", "#135C18", "#0C3E10")


def _svg(parts):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">\n%s\n</svg>\n' % (SIZE, SIZE, SIZE, SIZE, "\n".join(parts))
    )


def _rrect_d(x, y, w, h, r):
    r = min(r, w / 2.0, h / 2.0)
    return (
        "M %.2f %.2f H %.2f A %.2f %.2f 0 0 1 %.2f %.2f "
        "V %.2f A %.2f %.2f 0 0 1 %.2f %.2f "
        "H %.2f A %.2f %.2f 0 0 1 %.2f %.2f "
        "V %.2f A %.2f %.2f 0 0 1 %.2f %.2f Z"
        % (
            x + r, y, x + w - r, r, r, x + w, y + r,
            y + h - r, r, r, x + w - r, y + h,
            x + r, r, r, x, y + h - r,
            y + r, r, r, x + r, y,
        )
    )


def _path(d, fill, rule="nonzero"):
    return '  <path fill="%s" fill-rule="%s" d="%s"/>' % (fill, rule, d)


def _rect(x, y, w, h, fill, r=0):
    if r:
        return (
            '  <rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" '
            'rx="%.2f" ry="%.2f" fill="%s"/>' % (x, y, w, h, r, r, fill)
        )
    return (
        '  <rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="%s"/>'
        % (x, y, w, h, fill)
    )


def _circle(cx, cy, r, fill):
    return '  <circle fill="%s" cx="%.2f" cy="%.2f" r="%.2f"/>' % (fill, cx, cy, r)


# ---- File Browser: an SD card, three gold contacts ------------------------
# The card is the thing the app walks. A folder would steal the 14 px glyph.

def filebrowser_svg():
    return _svg([
        _path(
            "M32 10 H74 L104 40 V108 A12 12 0 0 1 92 120 H32 A12 12 0 0 1 20 108 "
            "V22 A12 12 0 0 1 32 10 Z",
            BODY,
        ),
        _rect(34, 22, 12, 26, ACCENT, 4),
        _rect(52, 22, 12, 26, ACCENT, 4),
        _rect(70, 22, 12, 26, ACCENT, 4),
    ])


# ---- Clock: a seven-segment module showing 12:00 --------------------------
# Digit-shaped marks are the object, not a label. The housing is a hole so the
# launcher ground is the LCD.

_SEGS = {
    "0": "abcdef",
    "1": "bc",
    "2": "abged",
    "3": "abgcd",
    "4": "fgbc",
    "5": "afgcd",
    "6": "afgecd",
    "7": "abc",
    "8": "abcdefg",
    "9": "abfgcd",
}


def _seven_seg(x, y, w, h, t, which):
    """Filled bars for one digit. `which` is the characters of 7-seg `a`–`g`."""
    on = set(which)
    parts = []
    inner_w = w - 2 * t
    half = (h - t) / 2.0
    bars = {
        "a": (x + t, y, inner_w, t),
        "d": (x + t, y + h - t, inner_w, t),
        "g": (x + t, y + half, inner_w, t),
        "f": (x, y + t * 0.6, t, half - t * 0.4),
        "b": (x + w - t, y + t * 0.6, t, half - t * 0.4),
        "e": (x, y + half + t * 0.4, t, half - t * 0.4),
        "c": (x + w - t, y + half + t * 0.4, t, half - t * 0.4),
    }
    for name, (sx, sy, sw, sh) in bars.items():
        if name in on:
            parts.append(_rect(sx, sy, sw, sh, ACCENT, min(t, sw, sh) * 0.4))
    return parts


def clock_svg():
    outer = _rrect_d(10, 36, 108, 56, 16)
    inner = _rrect_d(22, 48, 84, 32, 6)
    parts = [_path(outer + " " + inner, BODY, "evenodd")]
    dw, dh, t = 14.0, 26.0, 3.6
    y = 51.0
    parts.extend(_seven_seg(26.0, y, dw, dh, t, _SEGS["1"]))
    parts.extend(_seven_seg(44.0, y, dw, dh, t, _SEGS["2"]))
    parts.append(_circle(64.0, 58.0, 2.2, ACCENT))
    parts.append(_circle(64.0, 70.0, 2.2, ACCENT))
    parts.extend(_seven_seg(70.0, y, dw, dh, t, _SEGS["0"]))
    parts.extend(_seven_seg(88.0, y, dw, dh, t, _SEGS["0"]))
    return _svg(parts)


# ---- Scanner: a source, waves either side ---------------------------------
# Symmetric, so it is listening rather than a one-sided Wi-Fi fan.

S_CX, S_CY = 64.0, 64.0
S_DOT = 12.0
S_STROKE = 12.0
S_ARCS = ((32.0, 50.0), (52.0, 42.0))


def _arc_path(r, spread, side):
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
    parts.append(_circle(S_CX, S_CY, S_DOT, ACCENT))
    return _svg(parts)


# ---- Matrix Rain: green rain inside a gadget screen -----------------------
# The housing matches the set; the green is the app.

M_NCOLS, M_NROWS = 6, 7
M_DROPS = ((5, 4), (8, 6), (3, 5), (7, 4), (2, 6), (6, 3))
M_HOLE = (28.0, 28.0, 72.0, 72.0)


def matrixrain_svg():
    hx, hy, hw, hh = M_HOLE
    outer = _rrect_d(14, 14, 100, 100, 18)
    inner = _rrect_d(hx, hy, hw, hh, 8)
    parts = [_path(outer + " " + inner, BODY, "evenodd")]
    px, py = hw / M_NCOLS, hh / M_NROWS
    cell = min(px, py) * 0.62
    for col, (head, trail) in enumerate(M_DROPS):
        for dist in range(trail):
            row = head - dist
            if 0 <= row < M_NROWS:
                cx = hx + (col + 0.5) * px
                cy = hy + (row + 0.5) * py
                colour = GREEN[dist if dist < len(GREEN) else -1]
                parts.append(
                    _rect(cx - cell / 2, cy - cell / 2, cell, cell, colour, cell * 0.28)
                )
    return _svg(parts)


# ---- Beacon: a lighthouse, one beam ---------------------------------------
# Speaks in one direction. Not a Wi-Fi fan, not the Scanner's two-sided puck.

def beacon_svg():
    return _svg([
        _path("M 46 56 L 82 56 L 76 112 L 52 112 Z", BODY),
        _rect(40, 108, 48, 12, BODY, 3),
        _rect(48, 28, 32, 10, BODY, 3),
        _rect(52, 38, 24, 20, BODY, 3),
        _rect(56, 42, 16, 12, ACCENT, 2),
        _path("M 76 48 L 118 32 L 118 64 Z", ACCENT),
    ])


# ---- BLE Spam: three thick overlapping cards ------------------------------
# A room full of advertisers. Chunky so it still reads at 64 px.

def blespam_svg():
    cards = ((16, 16), (36, 30), (56, 44))
    cw, ch, r = 56.0, 72.0, 10.0
    parts = []
    for x, y in cards:
        parts.append(_rect(x, y, cw, ch, BODY, r))
        parts.append(_rect(x + cw - 14, y + 10, 10, 20, ACCENT, 3))
    return _svg(parts)


# ---- Battery: a cell three-quarters full ----------------------------------

def battery_svg():
    outer = _rrect_d(14, 42, 86, 44, 12)
    inner = _rrect_d(24, 52, 66, 24, 5)
    fill_w = 66 * 0.75
    return _svg([
        _path(outer + " " + inner, BODY, "evenodd"),
        _rect(100, 54, 14, 20, BODY, 4),
        _rect(28, 56, fill_w - 8, 16, ACCENT, 4),
    ])


# ---- USB HID: a USB-C plug face -------------------------------------------
# The hub is USB (HID, BadUSB, MSC), not only a keyboard. A pill with a
# tongue, not a battery cell.

def hid_svg():
    outer = _rrect_d(16, 46, 96, 36, 18)
    inner = _rrect_d(30, 54, 68, 20, 10)
    return _svg([
        _rect(54, 28, 20, 22, ACCENT, 5),
        _path(outer + " " + inner, BODY, "evenodd"),
    ])


ICONS = {
    "filebrowser": filebrowser_svg,
    "clock": clock_svg,
    "scanner": scanner_svg,
    "matrixrain": matrixrain_svg,
    "beacon": beacon_svg,
    "blespam": blespam_svg,
    "battery": battery_svg,
    "hid": hid_svg,
}


def write_png(svg_text, png_path):
    fd, tmp = tempfile.mkstemp(suffix=".svg")
    try:
        os.write(fd, svg_text.encode("utf-8"))
        os.close(fd)
        subprocess.check_call(
            [
                "rsvg-convert",
                "-w",
                str(SIZE),
                "-h",
                str(SIZE),
                "--background-color",
                "none",
                "-o",
                png_path,
                tmp,
            ]
        )
    finally:
        os.unlink(tmp)


def main():
    for app, svg in sorted(ICONS.items()):
        d = os.path.join(APPS, app)
        text = svg()
        svg_path = os.path.join(d, "icon.svg")
        png_path = os.path.join(d, "icon.png")
        with open(svg_path, "w") as f:
            f.write(text)
        write_png(text, png_path)
        print("emit_app_icons: wrote icon.svg and icon.png in apps/%s" % app)


if __name__ == "__main__":
    main()
