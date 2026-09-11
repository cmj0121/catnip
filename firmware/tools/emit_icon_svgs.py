#!/usr/bin/env python3
"""Write the 128x128 solid colour SVG masters. Hand-run."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "assets", "icons")
KAPPA = 0.5522847498

IDS = (
    "folder",
    "file",
    "app",
    "image",
    "audio",
    "settings",
    "edit",
    "trash",
    "refresh",
    "warning",
    "ok",
    "close",
    # The radios, for the Scanner (#57). Four pictures that have to be told
    # apart at 64 px and, harder, told apart from each other: three of the four
    # protocols are "waves", so only one of them is allowed to be drawn as
    # waves. Wi-Fi keeps the fan because that is the one the whole world already
    # reads; Bluetooth has a rune of its own; infrared is a remote and a beam,
    # which is what a person actually points; NFC is a card, because its
    # "nearby" is four centimetres and a card is the thing you touch.
    "wifi",
    "ble",
    "ir",
    "nfc",
    # And the Scanner's own face: rings and a sweep, which is a radar rather
    # than any one of the four.
    "radar",
)

COLOUR = {
    "wifi": "#3D9EFF",
    "ble": "#4D8DF6",
    "ir": "#E05A5A",
    "nfc": "#5AD1A0",
    "radar": "#E5B845",
    "folder": "#3D9EFF",
    "file": "#8FA3B8",
    "app": "#E09A4A",
    "image": "#FFFFFF",
    "audio": "#F472B6",
    "settings": "#C0C0C0",
    "edit": "#38BDF8",
    "trash": "#FF4B3E",
    "refresh": "#4ADE80",
    "warning": "#F9E154",
    "ok": "#22C55E",
    "close": "#9B1C1C",
}


def svg(ds, colour):
    if isinstance(ds, str):
        ds = [ds]
    parts = []
    for item in ds:
        rule = "nonzero"
        if isinstance(item, tuple):
            if len(item) == 3:
                d, c, rule = item
            else:
                d, c = item
        else:
            d, c = item, colour
        # A shape may name its own colour or leave it None to take the icon's,
        # which is what a hole punched with evenodd wants: it is still the body.
        if c is None:
            c = colour
        parts.append(
            '  <path fill="%s" fill-rule="%s" d="%s"/>' % (c, rule, d)
        )
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128"'
        ' viewBox="0 0 128 128">\n%s\n</svg>\n' % "\n".join(parts)
    )


def rrect(x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    return (
        "M%.3f %.3f H%.3f A%.3f %.3f 0 0 1 %.3f %.3f V%.3f "
        "A%.3f %.3f 0 0 1 %.3f %.3f H%.3f A%.3f %.3f 0 0 1 %.3f %.3f "
        "V%.3f A%.3f %.3f 0 0 1 %.3f %.3f Z"
        % (
            x + r, y, x + w - r, r, r, x + w, y + r, y + h - r,
            r, r, x + w - r, y + h, x + r, r, r, x, y + h - r,
            y + r, r, r, x + r, y,
        )
    )


def circle(cx, cy, r):
    return (
        "M%.3f %.3f A%.3f %.3f 0 1 1 %.3f %.3f A%.3f %.3f 0 1 1 %.3f %.3f Z"
        % (cx - r, cy, r, r, cx + r, cy, r, r, cx - r, cy)
    )


def capsule(x1, y1, x2, y2, width):
    dx, dy = x2 - x1, y2 - y1
    L = math.hypot(dx, dy) or 1.0
    ux, uy = dx / L, dy / L
    px, py = -uy, ux
    hw = width / 2.0
    ax, ay = x1 + px * hw, y1 + py * hw
    bx, by = x1 - px * hw, y1 - py * hw
    cx, cy = x2 - px * hw, y2 - py * hw
    dx_, dy_ = x2 + px * hw, y2 + py * hw
    r = hw
    # Caps go clockwise so they bulge along the line, not back into it.
    return (
        "M%.3f %.3f L%.3f %.3f A%.3f %.3f 0 1 0 %.3f %.3f "
        "L%.3f %.3f A%.3f %.3f 0 1 0 %.3f %.3f Z"
        % (ax, ay, dx_, dy_, r, r, cx, cy, bx, by, r, r, ax, ay)
    )


def gear(cx, cy, n=8, r_tip=54, r_root=38, tip_half_deg=8, root_half_deg=13,
         tip_r=10, hole_r=14):
    """8-tooth cog; centre hole is a second subpath (evenodd)."""
    parts = []
    step = 360.0 / n
    for i in range(n):
        a = math.radians(-90.0 + i * step)

        def pt(r, deg_off):
            ang = a + math.radians(deg_off)
            return (cx + r * math.cos(ang), cy + r * math.sin(ang))

        lr = pt(r_root, -root_half_deg)
        lt = pt(r_tip, -tip_half_deg)
        rt = pt(r_tip, tip_half_deg)
        rr = pt(r_root, root_half_deg)
        if i == 0:
            parts.append("M%.3f %.3f" % lr)
        else:
            parts.append("L%.3f %.3f" % lr)
        parts.append("L%.3f %.3f" % lt)
        parts.append(
            "A%.3f %.3f 0 0 1 %.3f %.3f" % (tip_r, tip_r, rt[0], rt[1])
        )
        parts.append("L%.3f %.3f" % rr)
    parts.append("Z")
    parts.append(
        "M%.3f %.3f A%.3f %.3f 0 1 1 %.3f %.3f "
        "A%.3f %.3f 0 1 1 %.3f %.3f Z"
        % (
            cx - hole_r, cy, hole_r, hole_r, cx + hole_r, cy,
            hole_r, hole_r, cx - hole_r, cy,
        )
    )
    return " ".join(parts)


def refresh_arrows():
    """Two clockwise FA-rotate arrows: annulus, round tail, sharp head."""
    cx, cy = 64.0, 64.0
    r, w = 38.0, 16.0
    hw = w / 2.0
    r_out, r_in = r + hw, r - hw
    tip_len, base_hw, base_back = 28.0, 18.0, 1.0

    def polar(th, rad):
        return (cx + rad * math.cos(th), cy + rad * math.sin(th))

    def one(t0, t1):
        o0, o1 = polar(t0, r_out), polar(t1, r_out)
        i0, i1 = polar(t0, r_in), polar(t1, r_in)
        ring = (
            "M%.3f %.3f A%.3f %.3f 0 0 1 %.3f %.3f "
            "L%.3f %.3f A%.3f %.3f 0 0 0 %.3f %.3f Z"
            % (
                o0[0], o0[1], r_out, r_out, o1[0], o1[1],
                i1[0], i1[1], r_in, r_in, i0[0], i0[1],
            )
        )
        tail = circle(cx + r * math.cos(t0), cy + r * math.sin(t0), hw)
        ex, ey = polar(t1, r)
        tx, ty = -math.sin(t1), math.cos(t1)
        nx, ny = math.cos(t1), math.sin(t1)
        bx, by = ex - tx * base_back, ey - ty * base_back
        tip = (ex + tx * tip_len, ey + ty * tip_len)
        outer_b = (bx + nx * base_hw, by + ny * base_hw)
        inner_b = (bx - nx * base_hw, by - ny * base_hw)
        head = "M%.3f %.3f L%.3f %.3f L%.3f %.3f Z" % (
            outer_b[0], outer_b[1], tip[0], tip[1], inner_b[0], inner_b[1]
        )
        return [ring, tail, head]

    t0, t1 = math.radians(185), math.radians(305)
    return one(t0, t1) + one(t0 + math.pi, t1 + math.pi)


def hexagon(cx, cy, r):
    pts = []
    for i in range(6):
        a = math.radians(30 + i * 60)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    rr = 10.0
    parts = []
    n = 6
    for i in range(n):
        p0 = pts[(i - 1) % n]
        p1 = pts[i]
        p2 = pts[(i + 1) % n]
        v1x, v1y = p0[0] - p1[0], p0[1] - p1[1]
        v2x, v2y = p2[0] - p1[0], p2[1] - p1[1]
        l1 = math.hypot(v1x, v1y) or 1
        l2 = math.hypot(v2x, v2y) or 1
        cut = min(rr, l1 * 0.45, l2 * 0.45)
        a1 = (p1[0] + v1x / l1 * cut, p1[1] + v1y / l1 * cut)
        a2 = (p1[0] + v2x / l2 * cut, p1[1] + v2y / l2 * cut)
        if i == 0:
            parts.append("M%.3f %.3f" % a1)
        else:
            parts.append("L%.3f %.3f" % a1)
        parts.append("A%.3f %.3f 0 0 1 %.3f %.3f" % (cut, cut, a2[0], a2[1]))
    parts.append("Z")
    return " ".join(parts)


def rounded_tri(cx, top, bottom, half_w, r):
    p_top = (cx, top)
    p_l = (cx - half_w, bottom)
    p_r = (cx + half_w, bottom)
    pts = [p_top, p_r, p_l]
    parts = []
    n = 3
    for i in range(n):
        p0 = pts[(i - 1) % n]
        p1 = pts[i]
        p2 = pts[(i + 1) % n]
        v1x, v1y = p0[0] - p1[0], p0[1] - p1[1]
        v2x, v2y = p2[0] - p1[0], p2[1] - p1[1]
        l1 = math.hypot(v1x, v1y) or 1
        l2 = math.hypot(v2x, v2y) or 1
        cut = min(r, l1 * 0.4, l2 * 0.4)
        a1 = (p1[0] + v1x / l1 * cut, p1[1] + v1y / l1 * cut)
        a2 = (p1[0] + v2x / l2 * cut, p1[1] + v2y / l2 * cut)
        if i == 0:
            parts.append("M%.3f %.3f" % a1)
        else:
            parts.append("L%.3f %.3f" % a1)
        parts.append("A%.3f %.3f 0 0 1 %.3f %.3f" % (cut, cut, a2[0], a2[1]))
    parts.append("Z")
    return " ".join(parts)


def ear(apex_x, apex_y, base_l, base_r):
    """Rounded cat ear: triangle with a round tip."""
    return rounded_tri_pts(apex_x, apex_y, base_l, base_r, 8)


def rounded_tri_pts(ax, ay, b, c, r):
    pts = [(ax, ay), b, c]
    parts = []
    n = 3
    for i in range(n):
        p0 = pts[(i - 1) % n]
        p1 = pts[i]
        p2 = pts[(i + 1) % n]
        v1x, v1y = p0[0] - p1[0], p0[1] - p1[1]
        v2x, v2y = p2[0] - p1[0], p2[1] - p1[1]
        l1 = math.hypot(v1x, v1y) or 1
        l2 = math.hypot(v2x, v2y) or 1
        cut = min(r if i == 0 else r * 0.6, l1 * 0.4, l2 * 0.4)
        a1 = (p1[0] + v1x / l1 * cut, p1[1] + v1y / l1 * cut)
        a2 = (p1[0] + v2x / l2 * cut, p1[1] + v2y / l2 * cut)
        if i == 0:
            parts.append("M%.3f %.3f" % a1)
        else:
            parts.append("L%.3f %.3f" % a1)
        parts.append("A%.3f %.3f 0 0 1 %.3f %.3f" % (cut, cut, a2[0], a2[1]))
    parts.append("Z")
    return " ".join(parts)


def arc_band(cx, cy, r, width, deg0, deg1):
    """A slice of an annulus with round ends - one wave of a fan.

    Angles are degrees clockwise from east, which on a screen (y down) is the
    direction they visibly turn. The ends are capped with half-circles so a
    wave that stops mid-air stops the way the capsules elsewhere do, rather
    than on a blunt radial edge that reads as a cut."""
    hw = width / 2.0
    ro, ri = r + hw, r - hw
    # Always the clockwise way round, so a band may be given its ends in the
    # order they are read - 318 to 258 is the long way, not a sixty degree
    # stub - and a ring with a gap in it can be said as one call.
    sweep = (deg1 - deg0) % 360.0 or 360.0
    a0, a1 = math.radians(deg0), math.radians(deg0 + sweep)
    large = 1 if sweep > 180.0 else 0

    def at(rad, a):
        return (cx + rad * math.cos(a), cy + rad * math.sin(a))

    o0, o1 = at(ro, a0), at(ro, a1)
    i0, i1 = at(ri, a0), at(ri, a1)
    return (
        "M%.3f %.3f A%.3f %.3f 0 %d 1 %.3f %.3f A%.3f %.3f 0 0 1 %.3f %.3f "
        "A%.3f %.3f 0 %d 0 %.3f %.3f A%.3f %.3f 0 0 1 %.3f %.3f Z"
        % (
            o0[0], o0[1], ro, ro, large, o1[0], o1[1],
            hw, hw, i1[0], i1[1],
            ri, ri, large, i0[0], i0[1],
            hw, hw, o0[0], o0[1],
        )
    )


def annulus(cx, cy, r_out, r_in):
    """A ring: the outer circle with the inner one punched out (evenodd)."""
    return (circle(cx, cy, r_out) + " " + circle(cx, cy, r_in), None, "evenodd")


def paths():
    folder = (
        "M28 22 H56 A10 10 0 0 1 66 32 V36 A6 6 0 0 0 72 42 H90 "
        "A20 20 0 0 1 110 62 V88 A20 20 0 0 1 90 108 H38 "
        "A20 20 0 0 1 18 88 V32 A10 10 0 0 1 28 22 Z"
    )
    file_ = rrect(40, 14, 48, 100, 10)
    app = [
        circle(64, 78, 40),
        ear(36, 14, (28, 58), (58, 50)),
        ear(92, 14, (70, 50), (100, 58)),
    ]
    image = [
        (rrect(14, 24, 100, 80, 20), "#FFFFFF"),
        (circle(40, 48, 11), "#000000"),
        (
            "M34.000 92.000 A4.000 4.000 0 0 1 32.533 88.904 "
            "L43.970 74.925 A6.364 6.364 0 0 1 52.500 74.500 "
            "L55.172 77.172 A4.000 4.000 0 0 1 60.603 76.963 "
            "L76.794 58.074 A8.000 8.000 0 0 1 84.971 59.428 "
            "L96.514 88.286 A4.000 4.000 0 0 1 94.000 92.000 Z",
            "#000000",
        ),
    ]
    audio = (
        "M24 50 A10 10 0 0 1 34 40 H54 L86 22 A10 10 0 0 1 104 32 "
        "V96 A10 10 0 0 1 86 106 L54 88 H34 A10 10 0 0 1 24 78 Z"
    )
    settings = [(gear(64, 64), "#C0C0C0", "evenodd")]
    ang = math.radians(-45)
    ux, uy = math.cos(ang), math.sin(ang)
    cx, cy = 62.0, 66.0
    x1, y1 = cx - ux * 38, cy - uy * 38
    x2, y2 = cx + ux * 24, cy + uy * 24
    tip = (cx + ux * 48, cy + uy * 48)
    px, py = -uy, ux
    w = 11.0
    edit = [
        capsule(x1, y1, x2, y2, 22),
        "M%.3f %.3f L%.3f %.3f L%.3f %.3f Z"
        % (
            x2 + px * w, y2 + py * w,
            tip[0], tip[1],
            x2 - px * w, y2 - py * w,
        ),
    ]
    trash = [
        (rrect(50, 16, 28, 16, 6), "#FF4B3E"),
        (rrect(22, 28, 84, 14, 6), "#FF4B3E"),
        (rrect(30, 38, 68, 72, 12), "#FF4B3E"),
        (
            "M43.757 62.243 L75.757 94.243 A6.000 6.000 0 1 0 84.243 85.757 "
            "L52.243 53.757 A6.000 6.000 0 1 0 43.757 62.243 Z",
            "#FFF3EE",
        ),
        (
            "M75.757 53.757 L43.757 85.757 A6.000 6.000 0 1 0 52.243 94.243 "
            "L84.243 62.243 A6.000 6.000 0 1 0 75.757 53.757 Z",
            "#FFF3EE",
        ),
    ]
    refresh = [(p, "#4ADE80") for p in refresh_arrows()]
    warning = [
        (
            "M58.095 22.447 A14.000 14.000 0 0 1 69.905 22.447 "
            "L114.095 101.553 A14.000 14.000 0 0 1 106.000 114.000 "
            "L22.000 114.000 A14.000 14.000 0 0 1 13.905 101.553 Z",
            "#F9E154",
        ),
        (
            "M58 40 V70 A6 6 0 1 0 70 70 V40 A6 6 0 1 0 58 40 Z",
            "#C9A428",
        ),
        (
            "M64 82 A7 7 0 1 1 64 96 A7 7 0 1 1 64 82 Z",
            "#C9A428",
        ),
    ]
    ok = [
        (rrect(18, 18, 92, 92, 20), "#22C55E"),
        (
            "M34.534 70.373 L50.534 90.373 A7.000 7.000 0 1 0 61.466 81.627 "
            "L45.466 61.627 A7.000 7.000 0 1 0 34.534 70.373 Z",
            "#FFF3EE",
        ),
        (
            "M61.298 90.575 L99.298 46.575 A7.000 7.000 0 1 0 88.702 37.425 "
            "L50.702 81.425 A7.000 7.000 0 1 0 61.298 90.575 Z",
            "#FFF3EE",
        ),
    ]
    close = [
        (capsule(38, 38, 90, 90, 18), "#9B1C1C"),
        (capsule(90, 38, 38, 90, 18), "#9B1C1C"),
    ]
    # The radios. Each is built from the primitives above rather than drawn by
    # hand, for the reason the repo keeps every other master here: art that a
    # script emits can be re-emitted, and art that was nudged in an editor
    # cannot.
    #
    # A fan of waves over a dot. Everyone already reads this one, which is why
    # it is the only icon in the group allowed to be waves and nothing else -
    # the other three have to be told apart from it as well as from each other.
    wifi = [
        arc_band(64, 100, 30, 14, 213, 327),
        arc_band(64, 100, 54, 14, 213, 327),
        arc_band(64, 100, 78, 14, 218, 322),
        circle(64, 98, 11),
    ]
    # The rune, as five strokes of one width: the stem, and the two flags that
    # cross it. Not a fan, not a wave - the one protocol here with a mark of
    # its own, and the cell under it says BLE rather than Bluetooth because
    # this radio cannot hear the Classic devices that mark also stands for.
    ble = [
        capsule(64, 16, 64, 112, 13),
        capsule(36, 40, 92, 88, 13),
        capsule(92, 40, 36, 88, 13),
        capsule(64, 16, 92, 40, 13),
        capsule(64, 112, 92, 88, 13),
    ]
    # An emitter and a beam of straight chevrons. Infrared is the one protocol
    # here a person aims, and the one whose picture must not be an arc: three
    # of these four are "waves", so only Wi-Fi and NFC are allowed to curve.
    # These bars are straight and angled, which at 14 px is a different shape
    # rather than a different radius.
    ir = [
        rrect(14, 46, 28, 36, 13),
        capsule(54, 44, 72, 64, 12),
        capsule(72, 64, 54, 84, 12),
        capsule(84, 44, 102, 64, 12),
        capsule(102, 64, 84, 84, 12),
    ]
    # A card and a fan leaving its edge. NFC's "nearby" is four centimetres, so
    # the picture is the gesture - a thing held against a reader - and the card
    # is what keeps it from being Wi-Fi turned on its side.
    nfc = [
        rrect(12, 34, 58, 62, 10),
        arc_band(70, 64, 22, 12, -58, 58),
        arc_band(70, 64, 42, 12, -52, 52),
    ]
    # The Scanner's own face. Not any one of the four: rings and a sweep, which
    # is the act rather than the protocol.
    radar = [
        arc_band(64, 64, 50, 12, 318, 258),
        arc_band(64, 64, 26, 12, 318, 258),
        circle(64, 64, 9),
        circle(97, 31, 11),
    ]
    return {
        "folder": folder,
        "file": file_,
        "app": app,
        "image": image,
        "audio": audio,
        "settings": settings,
        "edit": edit,
        "trash": trash,
        "refresh": refresh,
        "warning": warning,
        "ok": ok,
        "close": close,
        "wifi": wifi,
        "ble": ble,
        "ir": ir,
        "nfc": nfc,
        "radar": radar,
    }


def main():
    """Write the masters named on the command line, or all of them.

    Name them, in practice. The twelve original masters in assets/icons were
    refined after this script last emitted them - colours moved, second-colour
    marks were added - and re-emitting all of them would quietly undo that
    work. So this takes ids: `emit_icon_svgs.py wifi ble ir nfc radar` writes
    the five and leaves the rest of the tree alone."""
    d = paths()
    want = sys.argv[1:] or list(IDS)
    for name in want:
        if name not in d:
            raise SystemExit("no such icon: %s" % name)
    for name in want:
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w") as f:
            f.write(svg(d[name], COLOUR[name]))
        print("wrote", path)


if __name__ == "__main__":
    main()
