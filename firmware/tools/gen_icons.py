#!/usr/bin/env python3
"""Raster the 128x128 colour SVG masters to RGB565A8 and emit C arrays.

Hand-run. Not a PlatformIO extra_script: the device build has no SVG rasteriser.
"""
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

FIRMWARE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(FIRMWARE, "assets", "icons")
SRC = os.path.join(FIRMWARE, "src")
BITS_H = os.path.join(SRC, "catnip_icon_bits.h")
IMG_C = os.path.join(SRC, "device", "catnip_icon_img.c")
IMG_H = os.path.join(SRC, "device", "catnip_icon_img.h")
SHEET = os.path.join(ICON_DIR, "contact-sheet.png")

IDS = (
    "folder",
    "file",
    "placeholder",
    "image",
    "audio",
    "settings",
    "edit",
    "trash",
    "refresh",
    "warning",
    "ok",
    "close",
    # The radios and the act of listening for them (#57). Four protocols and
    # one radar: the Scanner's grid draws one cell per protocol and the app
    # itself wears the radar, which is none of them.
    "wifi",
    "ble",
    "ir",
    "nfc",
    "radar",
)
# List rows at 14, long-A actions at 32. Same SVG, different raster.
# 14 beside a word in a row or an operator button; 64 alone, filling the main
# region, which is what the carousel shows one of at a time.
SIZES = (14, 64)
VB = 128.0
SS = 4
PATH_CMD = re.compile(
    r"[MmLlHhVvCcSsAaZz]|[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?"
)
CUBIC_STEPS = 12
ARC_STEPS = 16


def die(msg):
    raise SystemExit(msg)


def bytes_for(px):
    return px * px * 3  # RGB565 plane + A8 plane


def tokenize_path(d):
    tokens = PATH_CMD.findall(d.replace(",", " "))
    if not tokens:
        die("empty path data")
    return tokens


def flatten_cubic(p0, p1, p2, p3, steps=CUBIC_STEPS):
    out = []
    for n in range(1, steps + 1):
        t = n / float(steps)
        u = 1.0 - t
        x = (
            u * u * u * p0[0]
            + 3 * u * u * t * p1[0]
            + 3 * u * t * t * p2[0]
            + t * t * t * p3[0]
        )
        y = (
            u * u * u * p0[1]
            + 3 * u * u * t * p1[1]
            + 3 * u * t * t * p2[1]
            + t * t * t * p3[1]
        )
        out.append((x, y))
    return out


def flatten_arc(x1, y1, rx, ry, phi_deg, fa, fs, x2, y2):
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(phi_deg)
    cos_p, sin_p = math.cos(phi), math.sin(phi)
    dx = (x1 - x2) / 2.0
    dy = (y1 - y2) / 2.0
    x1p = cos_p * dx + sin_p * dy
    y1p = -sin_p * dx + cos_p * dy
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den)) if den else 0.0
    if fa == fs:
        coef = -coef
    cxp = coef * rx * y1p / ry
    cyp = -coef * ry * x1p / rx
    cx = cos_p * cxp - sin_p * cyp + (x1 + x2) / 2.0
    cy = sin_p * cxp + cos_p * cyp + (y1 + y2) / 2.0

    def ang(ux, uy, vx, vy):
        sign = 1.0 if ux * vy - uy * vx >= 0 else -1.0
        nrm = math.hypot(ux, uy) * math.hypot(vx, vy)
        if nrm == 0:
            return 0.0
        c = max(-1.0, min(1.0, (ux * vx + uy * vy) / nrm))
        return sign * math.acos(c)

    theta1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = ang(
        (x1p - cxp) / rx,
        (y1p - cyp) / ry,
        (-x1p - cxp) / rx,
        (-y1p - cyp) / ry,
    )
    if fs == 0 and dtheta > 0:
        dtheta -= 2 * math.pi
    elif fs == 1 and dtheta < 0:
        dtheta += 2 * math.pi
    n = max(ARC_STEPS, int(abs(dtheta) / (math.pi / 12)))
    out = []
    for i in range(1, n + 1):
        t = theta1 + dtheta * i / n
        x = cx + rx * math.cos(t) * cos_p - ry * math.sin(t) * sin_p
        y = cy + rx * math.cos(t) * sin_p + ry * math.sin(t) * cos_p
        out.append((x, y))
    return out


def parse_path(d):
    tokens = tokenize_path(d)
    polys = []
    i = 0
    cmd = None
    cx = cy = 0.0
    sx = sy = 0.0
    pts = []
    last_c2 = None

    def take(n):
        nonlocal i
        out = []
        for _ in range(n):
            if i >= len(tokens):
                die("path data truncated after %s" % cmd)
            try:
                out.append(float(tokens[i]))
            except ValueError:
                die("expected number, got %r" % tokens[i])
            i += 1
        return out

    def flush():
        nonlocal pts, last_c2
        if len(pts) >= 3:
            if pts[0] == pts[-1] and len(pts) > 3:
                pts = pts[:-1]
            polys.append(pts)
        pts = []
        last_c2 = None

    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            if t not in "MmLlHhVvCcSsAaZz":
                die("unsupported path command %r" % t)
            cmd = t
            i += 1
            if cmd in "Zz":
                if pts:
                    cx, cy = sx, sy
                flush()
            continue
        if cmd is None:
            die("path data starts with a number")
        if cmd in "Mm":
            x, y = take(2)
            if cmd == "m":
                x += cx
                y += cy
            flush()
            pts = [(x, y)]
            cx, cy = x, y
            sx, sy = x, y
            cmd = "l" if cmd == "m" else "L"
        elif cmd in "Ll":
            x, y = take(2)
            if cmd == "l":
                x += cx
                y += cy
            pts.append((x, y))
            cx, cy = x, y
            last_c2 = None
        elif cmd in "Hh":
            x = take(1)[0]
            if cmd == "h":
                x += cx
            pts.append((x, cy))
            cx = x
            last_c2 = None
        elif cmd in "Vv":
            y = take(1)[0]
            if cmd == "v":
                y += cy
            pts.append((cx, y))
            cy = y
            last_c2 = None
        elif cmd in "Cc":
            x1, y1, x2, y2, x, y = take(6)
            if cmd == "c":
                x1 += cx
                y1 += cy
                x2 += cx
                y2 += cy
                x += cx
                y += cy
            pts.extend(flatten_cubic((cx, cy), (x1, y1), (x2, y2), (x, y)))
            last_c2 = (x2, y2)
            cx, cy = x, y
        elif cmd in "Ss":
            x2, y2, x, y = take(4)
            if cmd == "s":
                x2 += cx
                y2 += cy
                x += cx
                y += cy
            if last_c2 is None:
                x1, y1 = cx, cy
            else:
                x1 = 2 * cx - last_c2[0]
                y1 = 2 * cy - last_c2[1]
            pts.extend(flatten_cubic((cx, cy), (x1, y1), (x2, y2), (x, y)))
            last_c2 = (x2, y2)
            cx, cy = x, y
        elif cmd in "Aa":
            rx, ry, phi, fa, fs, x, y = take(7)
            if cmd == "a":
                x += cx
                y += cy
            pts.extend(
                flatten_arc(cx, cy, rx, ry, phi, int(fa), int(fs), x, y)
            )
            last_c2 = None
            cx, cy = x, y
        else:
            die("unsupported path command %r" % cmd)
    flush()
    return polys


def is_left(x1, y1, x2, y2, px, py):
    return (x2 - x1) * (py - y1) - (px - x1) * (y2 - y1)


def winding(px, py, poly):
    wn = 0
    n = len(poly)
    for k in range(n):
        x1, y1 = poly[k]
        x2, y2 = poly[(k + 1) % n]
        if y1 <= py:
            if y2 > py and is_left(x1, y1, x2, y2, px, py) > 0:
                wn += 1
        else:
            if y2 <= py and is_left(x1, y1, x2, y2, px, py) < 0:
                wn -= 1
    return wn


def point_in_path(px, py, polys, evenodd=False):
    total = 0
    for poly in polys:
        total += winding(px, py, poly)
    if evenodd:
        return (abs(total) % 2) == 1
    return total != 0


def local_name(tag):
    if tag[0] == "{":
        return tag.split("}", 1)[1]
    return tag


def parse_fill(el):
    raw = el.get("fill") or "#000000"
    if raw == "none":
        return None
    if raw.startswith("#") and len(raw) == 7:
        return (int(raw[1:3], 16), int(raw[3:5], 16), int(raw[5:7], 16))
    die("fill must be #RRGGBB, got %r" % raw)


def read_layers(path):
    tree = ET.parse(path)
    root = tree.getroot()
    vb = root.get("viewBox") or root.get("viewbox")
    if vb is None or vb.split() != ["0", "0", "128", "128"]:
        die("%s: viewBox must be '0 0 128 128'" % path)
    layers = []
    for el in root.iter():
        for forbidden in ("style", "class", "transform"):
            if el.get(forbidden):
                die("%s: no %s attributes" % (path, forbidden))
        stroke = el.get("stroke")
        if stroke is not None and stroke != "none":
            die("%s: strokes are forbidden" % path)
        if local_name(el.tag) != "path":
            continue
        fill = parse_fill(el)
        if fill is None:
            continue
        d = el.get("d")
        if not d:
            die("%s: path has no d" % path)
        evenodd = (el.get("fill-rule") or "nonzero") == "evenodd"
        layers.append((parse_path(d), fill, evenodd))
    if not layers:
        die("%s: no filled paths" % path)
    return layers


def raster_rgba(path, px, ss=SS):
    layers = read_layers(path)
    cell = VB / px
    rows = []
    for j in range(px):
        row = []
        for i in range(px):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    ox = (i + (sx + 0.5) / ss) * cell
                    oy = (j + (sy + 0.5) / ss) * cell
                    col = None
                    for polys, fill, evenodd in layers:
                        if point_in_path(ox, oy, polys, evenodd):
                            col = fill
                    if col is not None:
                        acc[0] += col[0]
                        acc[1] += col[1]
                        acc[2] += col[2]
                        acc[3] += 255
            n = ss * ss
            row.append((acc[0] // n, acc[1] // n, acc[2] // n, acc[3] // n))
        rows.append(row)
    return rows


def pack_rgb565a8(rows):
    px = len(rows)
    rgb = bytearray(px * px * 2)
    a8 = bytearray(px * px)
    k = 0
    for row in rows:
        for r, g, b, a in row:
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            rgb[k * 2] = v & 0xFF
            rgb[k * 2 + 1] = (v >> 8) & 0xFF
            a8[k] = a
            k += 1
    blob = bytes(rgb) + bytes(a8)
    if len(blob) != bytes_for(px):
        die("packer produced %d bytes, want %d" % (len(blob), bytes_for(px)))
    return blob


def unpack_alpha(blob, px):
    off = px * px * 2
    return blob[off : off + px * px]


def c_bytes(blob):
    return ", ".join("0x%02X" % b for b in blob)


def emit_bits_h(per_size):
    lines = [
        "/* Generated by tools/gen_icons.py - do not edit. */",
        "#ifndef CATNIP_ICON_BITS_H",
        "#define CATNIP_ICON_BITS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define CATNIP_ICON_COUNT %d" % len(IDS),
        "",
        "/* clang-format off */",
        "",
    ]
    for px in SIZES:
        nbytes = bytes_for(px)
        lines.append(
            "/* %d x %d bytes: %dx%d RGB565 then A8, native endian, stride %d. */"
            % (len(IDS), nbytes, px, px, px * 2)
        )
        lines.append(
            "static const uint8_t catnip_icon_rgb565a8_%d[%d][%d] = {"
            % (px, len(IDS), nbytes)
        )
        for name, blob in per_size[px]:
            lines.append("    { %s }, /* %s */" % (c_bytes(blob), name))
        lines.append("};")
        lines.append("")
    lines.append("/* clang-format on */")
    lines.append("")
    lines.append("#endif /* CATNIP_ICON_BITS_H */")
    lines.append("")
    return "\n".join(lines)


def emit_img_h():
    return (
        "/* Generated by tools/gen_icons.py - do not edit. */\n"
        "#ifndef CATNIP_ICON_IMG_H\n"
        "#define CATNIP_ICON_IMG_H\n"
        "\n"
        "#include <lvgl.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        'extern "C" {\n'
        "#endif\n"
        "\n"
        + "".join(
            "extern const lv_image_dsc_t catnip_icon_img_%d[%d];\n" % (px, len(IDS))
            for px in SIZES
        )
        + "\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n"
        "\n"
        "#endif /* CATNIP_ICON_IMG_H */\n"
    )


def emit_img_c():
    def block(px):
        nbytes = bytes_for(px)
        stride = px * 2
        lines = [
            "const lv_image_dsc_t catnip_icon_img_%d[%d] = {" % (px, len(IDS))
        ]
        for i, name in enumerate(IDS):
            lines.append("    {")
            lines.append("        .header = {")
            lines.append("            .magic = LV_IMAGE_HEADER_MAGIC,")
            lines.append("            .cf = LV_COLOR_FORMAT_RGB565A8,")
            lines.append("            .flags = 0,")
            lines.append("            .w = %d," % px)
            lines.append("            .h = %d," % px)
            lines.append("            .stride = %d," % stride)
            lines.append("            .reserved_2 = 0,")
            lines.append("        },")
            lines.append("        .data_size = %d," % nbytes)
            lines.append(
                "        .data = catnip_icon_rgb565a8_%d[%d]," % (px, i)
            )
            lines.append("        .reserved = NULL,")
            lines.append("    }, /* %s */" % name)
        lines.append("};")
        return "\n".join(lines)

    return (
        "/* Generated by tools/gen_icons.py - do not edit. */\n"
        "#include <lvgl.h>\n"
        "\n"
        '#include "../catnip_icon_bits.h"\n'
        '#include "catnip_icon_img.h"\n'
        "\n"
        "/* clang-format off */\n"
        + "\n\n".join(block(px) for px in SIZES)
        + "\n"
        "/* clang-format on */\n"
    )


def png_chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(path, width, height, rgb):
    raw = b"".join(
        b"\x00" + bytes(rgb[y * width * 3 : (y + 1) * width * 3])
        for y in range(height)
    )
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(raw, 9))
    png += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def emit_sheet(rasters_14):
    n = 14
    gap = 2
    width = len(IDS) * n + (len(IDS) - 1) * gap
    height = n
    rgb = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            off = (y * width + x) * 3
            rgb[off : off + 3] = b"\x00\x00\x00"
    for idx, rows in enumerate(rasters_14):
        x0 = idx * (n + gap)
        for j in range(n):
            for i in range(n):
                r, g, b, a = rows[j][i]
                off = (j * width + (x0 + i)) * 3
                if a:
                    rgb[off] = r
                    rgb[off + 1] = g
                    rgb[off + 2] = b
    write_png(SHEET, width, height, rgb)


def write_text(path, text):
    with open(path, "w") as f:
        f.write(text)


def svg_path(name):
    svg = os.path.join(ICON_DIR, name + ".svg")
    if not os.path.isfile(svg):
        die("missing %s" % svg)
    return svg


BITS_ROW = re.compile(
    r"\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*)+0x[0-9A-Fa-f]{2})\s*\}\s*,\s*/\*\s*(\w+)\s*\*/"
)


def load_bits_h(px=14):
    if not os.path.isfile(BITS_H):
        die("missing %s" % BITS_H)
    with open(BITS_H) as f:
        raw = f.read()
    marker = "catnip_icon_rgb565a8_%d" % px
    start = raw.find(marker)
    if start < 0:
        die("%s: no %s" % (BITS_H, marker))
    chunk = raw[start:]
    names = []
    packed = {}
    for hexes, name in BITS_ROW.findall(chunk):
        blob = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", hexes))
        if len(blob) != bytes_for(px):
            continue
        names.append(name)
        packed[name] = blob
        if len(names) == len(IDS):
            break
    if names != list(IDS):
        die("%s: expected ids %s, got %s" % (BITS_H, list(IDS), names))
    return packed


def check():
    packed = load_bits_h(14)
    failed = False
    for name in IDS:
        got = pack_rgb565a8(raster_rgba(svg_path(name), 14))
        if got != packed[name]:
            print("FAIL %s.svg raster != catnip_icon_bits.h" % name, file=sys.stderr)
            failed = True
    if failed:
        die("SVG / catnip_icon_bits.h disagree")
    print("ok: SVG and catnip_icon_bits.h agree")
    return 0


def main(argv):
    if argv[1:] == ["--check"]:
        return check()
    if argv[1:] not in ([], ["--emit"]):
        die("usage: gen_icons.py [--emit | --check]")

    per_size = {px: [] for px in SIZES}
    rasters_14 = []
    for name in IDS:
        for px in SIZES:
            rows = raster_rgba(svg_path(name), px)
            blob = pack_rgb565a8(rows)
            per_size[px].append((name, blob))
            if px == 14:
                rasters_14.append(rows)

    write_text(BITS_H, emit_bits_h(per_size))
    write_text(IMG_C, emit_img_c())
    write_text(IMG_H, emit_img_h())
    emit_sheet(rasters_14)
    print("ok: %d SVGs -> RGB565A8 %s, img dsc, contact-sheet"
          % (len(IDS), "/".join(str(px) for px in SIZES)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
