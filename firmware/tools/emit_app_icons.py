#!/usr/bin/env python3
"""Write the built-in apps' identity PNGs. Hand-run.

An app ships `icon.png`; that is what the manifest names and what gen_apps.py
embeds. This script can rebuild those PNGs. It does not write an `icon.svg`
into the app folder — the device never reads one.

One stroke, one slate. The launcher ground is already dark, and the focus
ring is `#00B0FF`, so the ink stays off both. Matrix Rain keeps a dim green,
because that colour is the app. Judged at 64 px, which is the grid.
"""
import math
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
APPS = os.path.join(os.path.dirname(HERE), "apps")

SIZE = 128
INK = "#B0BFD0"
GREEN = "#5AAA7C"
SW = 8.0


def _svg(parts):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d">\n%s\n</svg>\n' % (SIZE, SIZE, SIZE, SIZE, "\n".join(parts))
    )


def _line(x1, y1, x2, y2, stroke, w):
    return (
        '  <line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" '
        'stroke="%s" stroke-width="%.2f" stroke-linecap="round" fill="none"/>'
        % (x1, y1, x2, y2, stroke, w)
    )


def _circle(cx, cy, r, stroke, w, fill="none"):
    return (
        '  <circle cx="%.2f" cy="%.2f" r="%.2f" fill="%s" stroke="%s" '
        'stroke-width="%.2f"/>' % (cx, cy, r, fill, stroke, w)
    )


def _rect(x, y, w, h, rx, stroke, sw, fill="none"):
    return (
        '  <rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f" ry="%.2f" '
        'fill="%s" stroke="%s" stroke-width="%.2f"/>'
        % (x, y, w, h, rx, rx, fill, stroke, sw)
    )


def _path(d, stroke, w, fill="none"):
    return (
        '  <path d="%s" fill="%s" stroke="%s" stroke-width="%.2f" '
        'stroke-linecap="round" stroke-linejoin="round"/>' % (d, fill, stroke, w)
    )


def _arc(cx, cy, r, a0, a1):
    """Screen-clockwise arc. 0° is east, 90° is south."""
    if a1 < a0:
        a1 += 360.0

    def pt(a):
        rad = math.radians(a)
        return cx + r * math.cos(rad), cy + r * math.sin(rad)

    x0, y0 = pt(a0)
    x1, y1 = pt(a1)
    large = 1 if (a1 - a0) > 180 else 0
    return "M %.2f %.2f A %.2f %.2f 0 %d 1 %.2f %.2f" % (
        x0, y0, r, r, large, x1, y1
    )


def _segs(x, y, w, h, color, sw, which):
    m = sw * 0.55
    x0, x1 = x + m, x + w - m
    y0, y1 = y + m, y + h - m
    ym = (y0 + y1) / 2.0
    spines = {
        "a": (x0, y0, x1, y0),
        "d": (x0, y1, x1, y1),
        "g": (x0, ym, x1, ym),
        "f": (x0, y0, x0, ym),
        "b": (x1, y0, x1, ym),
        "e": (x0, ym, x0, y1),
        "c": (x1, ym, x1, y1),
    }
    return [_line(*spines[name], color, sw) for name in which]


# ---- File Browser: an SD card, three contacts -----------------------------
# The card is the thing the app walks. A folder would steal the 14 px glyph.

def filebrowser_svg():
    d = (
        "M 42 26 H 72 L 102 56 V 100 "
        "A 12 12 0 0 1 90 112 H 42 "
        "A 12 12 0 0 1 30 100 V 38 "
        "A 12 12 0 0 1 42 26 Z"
    )
    parts = [_path(d, INK, SW)]
    for x in (46, 58, 70):
        parts.append(_line(x, 40, x, 56, INK, SW * 0.7))
    return _svg(parts)


# ---- Clock: 12:00, centred in the capsule ---------------------------------
# A 7-seg "1" is only its right stem. A full cell leaves a hole, and the
# readout drifts into the right curve.

def clock_svg():
    hx, hy, hw, hh = 14.0, 40.0, 100.0, 48.0
    cx, cy = hx + hw / 2.0, hy + hh / 2.0
    parts = [_rect(hx, hy, hw, hh, 16, INK, SW)]
    swd = max(4.4, SW * 0.58)
    dh, dw = 26.0, 13.0
    w1, gap, cgap, colon = 4.2, 3.4, 5.0, 4.4
    items = (
        ("1", w1), ("gap", gap),
        ("2", dw), ("gap", cgap),
        (":", colon), ("gap", cgap),
        ("0", dw), ("gap", gap),
        ("0", dw),
    )
    x = cx - sum(w for _, w in items) / 2.0
    y = cy - dh / 2.0
    pad = swd * 0.55
    for kind, w in items:
        if kind == "1":
            parts.append(_line(x + w / 2.0, y + pad, x + w / 2.0, y + dh - pad, INK, swd))
        elif kind == ":":
            parts.append(_circle(x + w / 2.0, cy - 5.2, 2.6, INK, 0, INK))
            parts.append(_circle(x + w / 2.0, cy + 5.2, 2.6, INK, 0, INK))
        elif kind in ("2", "0"):
            which = "abged" if kind == "2" else "abcdef"
            parts += _segs(x, y, w, dh, INK, swd, which)
        x += w
    return _svg(parts)


# ---- Scanner: a source, waves either side ---------------------------------
# Symmetric, so it is listening rather than a one-sided fan.

def scanner_svg():
    parts = []
    for r in (30, 46):
        parts.append(_path(_arc(64, 64, r, -38, 38), INK, SW * 0.92))
        parts.append(_path(_arc(64, 64, r, 142, 218), INK, SW * 0.92))
    parts.append(_circle(64, 64, 6.5, INK, 0, INK))
    return _svg(parts)


# ---- Matrix Rain: dim green columns inside a screen -----------------------
# The housing matches the set. The green is the app, pulled down from neon
# so it sits with the slate.

def matrixrain_svg():
    parts = [_rect(26, 26, 76, 76, 18, INK, SW)]
    cols = (3, 5, 2, 6, 4, 3)
    for i, n in enumerate(cols):
        x = 38 + i * 10.4
        for k in range(n):
            y0 = 36 + k * 9.2
            parts.append(_line(x, y0, x, y0 + 6.0, GREEN, 5.0))
    return _svg(parts)


# ---- Beacon: a lighthouse, one beam to the right -------------------------
# Straight rays, clear of the building. Arcs would read as the Scanner.
# A circle on a mast reads as a person.

def beacon_svg():
    lantern = "M 48 64 H 80 V 42 A 16 14 0 0 0 48 42 Z"
    return _svg([
        _path("M 32 110 L 50 64", INK, SW),
        _path("M 78 64 L 96 110", INK, SW),
        _line(26, 110, 102, 110, INK, SW),
        _path(lantern, INK, SW),
        _line(88, 46, 114, 34, INK, SW),
        _line(88, 54, 114, 66, INK, SW),
    ])


# ---- BLE Spam: three advertisers, each a circle and one outward arc ------
# A stack of cards knots in one colour. Three separate sources still say a
# room full of them, and stay off the Scanner's single symmetric mark.

def blespam_svg():
    parts = []
    reach, spread = 30.0, 34.0
    for i in range(3):
        aim = -90.0 + i * 120.0
        rad = math.radians(aim)
        cx = 64.0 + reach * math.cos(rad)
        cy = 64.0 + reach * math.sin(rad)
        parts.append(_circle(cx, cy, 12, INK, SW))
        parts.append(_path(_arc(cx, cy, 24, aim - spread, aim + spread), INK, SW))
    return _svg(parts)


# ---- Battery: a tall cell, three bars, a short terminal -------------------
# The cap is a stub on the right. A round loop reads as a handle.

def battery_svg():
    parts = [
        _rect(14, 32, 82, 64, 16, INK, SW),
        _path(
            "M 96 52 L 108 52 A 5 5 0 0 1 113 57 "
            "L 113 71 A 5 5 0 0 1 108 76 L 96 76",
            INK, SW,
        ),
    ]
    for x in (32, 48, 64):
        parts.append(_line(x, 48, x, 80, INK, SW))
    return _svg(parts)


# ---- USB HID: a USB-C plug face -------------------------------------------
# The hub is USB (HID, BadUSB, MSC), not only a keyboard. A pill with a
# tongue, not a battery cell.

def hid_svg():
    return _svg([
        _rect(54, 24, 20, 18, 4, INK, SW * 0.68),
        _rect(16, 46, 96, 36, 18, INK, SW),
        _rect(36, 56, 56, 16, 8, INK, SW * 0.7),
    ])


# ---- Flash Mode: the ROM package ------------------------------------------
# The app reboots into the download ROM. An arrow into a tray reads as a
# generic download, and a bolt would read as power. Pins and a die are the
# part that waits for esptool.

def flashmode_svg():
    parts = [_rect(36, 32, 56, 64, 8, INK, SW)]
    for t in (0.26, 0.50, 0.74):
        y = 32 + 64 * t
        parts.append(_line(18, y, 36, y, INK, SW))
        parts.append(_line(92, y, 110, y, INK, SW))
    parts.append(_circle(64, 64, 9, INK, SW))
    return _svg(parts)


ICONS = {
    "filebrowser": filebrowser_svg,
    "clock": clock_svg,
    "scanner": scanner_svg,
    "matrixrain": matrixrain_svg,
    "beacon": beacon_svg,
    "blespam": blespam_svg,
    "battery": battery_svg,
    "hid": hid_svg,
    "flashmode": flashmode_svg,
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
        png_path = os.path.join(APPS, app, "icon.png")
        write_png(svg(), png_path)
        print("emit_app_icons: wrote icon.png in apps/%s" % app)


if __name__ == "__main__":
    main()
