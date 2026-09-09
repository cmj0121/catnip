"""Generate the `display` role's face: digits, large (#74).

The largest font LVGL builds in is Montserrat 48, and 48 px is not a clock face
on a 320x240 panel - it is a line of text that happens to be bigger. This makes
one at whatever size the role actually wants, out of the same typeface LVGL's
own faces are cut from.

**The typeface is not vendored.** `lvgl/scripts/built_in_font/Montserrat-Medium.ttf`
is the file lv_font_conv was pointed at to produce every `lv_font_montserrat_*.c`
in the library, and PlatformIO already fetches and pins that library. Copying it
into this repository would be a second copy of something we already have, under
a licence we would then have to carry ourselves.

**The output is committed**, the way `catnip_icon_img.c` is: a build on a bare
runner must not need a TTF, a font renderer, or this script. What keeps the
generated file honest is the same thing that keeps the icons honest - the
generator is the source of truth and the check target notices drift.

**Digits and nothing else.** `display` is for a quantity that has earned the
whole panel, and a face with letters in it would be reached for as "big text"
within a week - at which point the five prose roles would no longer be the only
way to set words. Restricting the glyphs is not an economy, it is what makes the
rule enforceable: ask for `display` with letters and you get missing-glyph
boxes, at the size you asked for, which is the loudest possible way to be told.

Usage:  python3 tools/gen_font.py [size]
        python3 tools/gen_font.py --check   (does the committed file still match?)
"""
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - the message is the whole handling
    sys.exit(
        "gen_font: needs Pillow (pip install pillow). The generated .c is "
        "committed, so only someone changing the face needs this."
    )

FIRMWARE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The typeface, inside the pinned LVGL dependency. Looked for rather than
# assumed, because PlatformIO puts libdeps under the build directory and a clean
# tree has not fetched them yet.
TTF = os.path.join(
    FIRMWARE, ".pio", "libdeps", "meowkit", "lvgl", "scripts", "built_in_font",
    "Montserrat-Medium.ttf",
)

OUT = os.path.join(FIRMWARE, "src", "device", "catnip_font_display.c")

# `0-9` and the three separators a readout needs: a clock's colon, a decimal
# point, a minus. A space, so a caller can pad without falling off the cmap.
GLYPHS = " -.0123456789:"

DEFAULT_SIZE = 96
BPP = 4


def render(font, ch, canvas, origin):
    """The glyph's ink, as (bbox, pixel-getter) in canvas coordinates.

    Drawn at a known baseline origin so the offsets below are measured against
    the baseline rather than against wherever PIL happened to put the box.
    """
    img = Image.new("L", canvas, 0)
    ImageDraw.Draw(img).text(origin, ch, font=font, fill=255, anchor="ls")
    return img, img.getbbox()


def pack(img, box):
    """The box, as a continuous 4-bit-per-pixel stream.

    Rows are *not* padded to byte boundaries: LVGL's plain format reads the
    glyph as one bit stream, which the library's own faces confirm - a 7x34
    glyph occupies exactly 7*34*4/8 = 119 bytes there, not 34 rows of 4.
    """
    l, t, r, b = box
    out = bytearray()
    acc = 0
    nbits = 0
    px = img.load()
    for y in range(t, b):
        for x in range(l, r):
            acc = (acc << BPP) | (px[x, y] >> (8 - BPP))
            nbits += BPP
            if nbits == 8:
                out.append(acc & 0xFF)
                acc = 0
                nbits = 0
    if nbits:
        out.append((acc << (8 - nbits)) & 0xFF)
    return bytes(out)


def hexrow(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("    " + " ".join("0x%02x," % b for b in data[i:i + per_line]))
    return "\n".join(lines)


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    size = int(args[0]) if args else DEFAULT_SIZE

    if not os.path.exists(TTF):
        sys.exit(
            "gen_font: %s is missing.\n"
            "It arrives with the pinned LVGL dependency - run a device build "
            "first (make device), which fetches it." % TTF
        )

    font = ImageFont.truetype(TTF, size)
    ascent, descent = font.getmetrics()
    canvas = (size * 3, (ascent + descent) * 3)
    origin = (size, ascent + size // 2)

    bitmap = bytearray()
    dsc = [(0, 0, 0, 0, 0, 0)]  # id 0 is reserved, and LVGL expects it present
    for ch in GLYPHS:
        img, box = render(font, ch, canvas, origin)
        adv = int(round(font.getlength(ch) * 16))
        if box is None:  # a space has advance and no ink
            dsc.append((len(bitmap), adv, 0, 0, 0, 0))
            continue
        l, t, r, b = box
        data = pack(img, box)
        dsc.append((len(bitmap), adv, r - l, b - t, l - origin[0], origin[1] - b))
        bitmap.extend(data)

    # A single contiguous range would need the glyphs to be contiguous in
    # Unicode, and " -." then a gap then "0-9:" is not. Two ranges cost two cmap
    # entries and keep the lookup a subtraction rather than a search.
    runs = []
    start = 0
    for i in range(1, len(GLYPHS) + 1):
        if i == len(GLYPHS) or ord(GLYPHS[i]) != ord(GLYPHS[i - 1]) + 1:
            runs.append((start, i - start))
            start = i
    cmaps = []
    for first, length in runs:
        cmaps.append(
            "    {\n"
            "        .range_start = %d, .range_length = %d, .glyph_id_start = %d,\n"
            "        .unicode_list = NULL, .glyph_id_ofs_list = NULL, "
            ".list_length = 0,\n"
            "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n"
            "    }" % (ord(GLYPHS[first]), length, first + 1)
        )

    body = [
        "/*",
        " * Generated by tools/gen_font.py - do not edit by hand.",
        " *",
        " * Size: %d px, %d bpp, from the Montserrat-Medium.ttf that ships inside" % (size, BPP),
        " * the pinned LVGL dependency - the same file its own built-in faces are",
        " * cut from. Glyphs: %s" % repr(GLYPHS),
        " *",
        " * Committed rather than generated at build time, the way the icons are:",
        " * a build on a bare runner must not need a TTF or a font renderer.",
        " */",
        "#include <lvgl.h>",
        "",
        "#include \"catnip_font_display.h\"",
        "",
        # Tables of numbers, laid out to be read as a grid. clang-format would
        # reflow them into a paragraph and then disagree with this generator
        # forever after - the same bargain gen_icons.py makes with its own
        # tables, and for the same reason.
        "/* clang-format off */",
        "",
        "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {",
        hexrow(bitmap),
        "};",
        "",
        "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {",
    ]
    for i, (bi, adv, bw, bh, ox, oy) in enumerate(dsc):
        body.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
            ".ofs_x = %d, .ofs_y = %d}%s" % (bi, adv, bw, bh, ox, oy,
                                             "," if i < len(dsc) - 1 else "")
        )
    body += [
        "};",
        "",
        "static const lv_font_fmt_txt_cmap_t cmaps[] = {",
        ",\n".join(cmaps),
        "};",
        "",
        "static const lv_font_fmt_txt_dsc_t font_dsc = {",
        "    .glyph_bitmap = glyph_bitmap,",
        "    .glyph_dsc = glyph_dsc,",
        "    .cmaps = cmaps,",
        "    /* No kerning: the pairs this face can form are digits against",
        "     * digits, and a clock whose colon crept left when the minute",
        "     * changed would be worse than one that never moves. */",
        "    .kern_dsc = NULL,",
        "    .kern_scale = 0,",
        "    .cmap_num = %d," % len(cmaps),
        "    .bpp = %d," % BPP,
        "    .kern_classes = 0,",
        "    .bitmap_format = 0,",
        "};",
        "",
        "const lv_font_t catnip_font_display = {",
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
        "    .line_height = %d," % (ascent + descent),
        "    .base_line = %d," % descent,
        "    .subpx = LV_FONT_SUBPX_NONE,",
        "    .underline_position = %d," % -(size // 12),
        "    .underline_thickness = %d," % max(1, size // 24),
        "    .dsc = &font_dsc,",
        "};",
        "",
        "/* clang-format on */",
        "",
    ]

    text = "\n".join(body)

    if check:
        # The committed file is the artefact and this script is its source, so
        # the two disagreeing means somebody edited the artefact. Said here
        # rather than only promised in a comment, which is what it was.
        have = ""
        if os.path.exists(OUT):
            with open(OUT, "r", encoding="utf-8") as f:
                have = f.read()
        if have != text:
            sys.exit(
                "gen_font: %s does not match what this script produces.\n"
                "Run: python3 tools/gen_font.py %d"
                % (os.path.relpath(OUT, FIRMWARE), size)
            )
        print("gen_font: %s is up to date" % os.path.relpath(OUT, FIRMWARE))
        return

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(
        "gen_font: %d px, %d glyphs, %d bytes of bitmap -> %s"
        % (size, len(GLYPHS), len(bitmap), os.path.relpath(OUT, FIRMWARE))
    )


main()
