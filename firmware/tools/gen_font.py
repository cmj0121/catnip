"""Generate the device's faces, out of the typeface in assets/fonts (#74).

Every size the platform draws in, cut from one typeface and committed as C.

**Monaspace Neon, and it is monospaced on purpose.** Nearly everything this
device shows is a value beside a label: a channel next to a name, a strength
next to both, a heap size under a version. In a proportional face those columns
line up only by accident - the number after a short name sits somewhere else
than the number after a long one - and the page has to be read across instead of
down. A monospaced face makes the columns a property of the type rather than of
the strings that happen to be in them.

**The typeface is vendored**, in assets/fonts, with its OFL licence beside it.
That is a cost and it is the first one this repository carries: the face this
replaced arrived inside the pinned LVGL dependency, so nobody here owned it.
Monaspace is SIL OFL 1.1, which permits it; the licence file is not optional
paperwork but the condition under which the .otf may be here at all.

**The output is committed**, the way the icons are: a build on a bare runner
must not need a font renderer, and this script is not in the build. What keeps
the generated file honest is the check target, which notices drift.

**One face has digits and nothing else.** `display` is for a quantity that has
earned the whole panel, and a face with letters in it would be reached for as
"big text" within a week - at which point the five prose roles would no longer
be the only way to set words. Restricting the glyphs is not an economy, it is
what makes the rule enforceable: ask for `display` with letters and you get
missing-glyph boxes, at the size you asked for, which is the loudest possible
way to be told.

Usage:  python3 tools/gen_font.py
        python3 tools/gen_font.py --check   (does the committed file still match?)
"""
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - the message is the whole handling
    sys.exit(
        "gen_font: needs Pillow (pip install pillow). The generated .c is "
        "committed, so only someone changing the faces needs this."
    )

FIRMWARE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OTF = os.path.join(FIRMWARE, "assets", "fonts", "MonaspaceNeon-Regular.otf")
OUT = os.path.join(FIRMWARE, "src", "device", "catnip_font.c")

BPP = 4

# Printable ASCII. Not a smaller set: these faces set prose, and prose that
# silently loses a bracket or a slash is worse than one that is a kilobyte
# larger. Not a larger one either - anything past ASCII is a decision about
# which language this device speaks, and it does not have one yet.
PROSE = "".join(chr(c) for c in range(0x20, 0x7F))

# `0-9` and the three separators a readout needs: a clock's colon, a decimal
# point, a minus. A space, so a caller can pad without falling off the cmap.
DIGITS = " -.0123456789:"

FACES = [
    ("catnip_font_10", 10, PROSE),
    ("catnip_font_16", 16, PROSE),
    ("catnip_font_20", 20, PROSE),
    ("catnip_font_24", 24, PROSE),
    ("catnip_font_display", 96, DIGITS),
]


def render(font, ch, canvas, origin):
    """The glyph's ink, as (image, bbox) in canvas coordinates.

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


def cut(name, size, glyphs):
    """One face, as the block of C that declares it."""
    font = ImageFont.truetype(OTF, size)
    ascent, descent = font.getmetrics()
    canvas = (size * 3, (ascent + descent) * 3)
    origin = (size, ascent + size // 2)

    bitmap = bytearray()
    dsc = [(0, 0, 0, 0, 0, 0)]  # id 0 is reserved, and LVGL expects it present
    for ch in glyphs:
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
    # Unicode, and " -." then a gap then "0-9:" is not. One cmap entry per run
    # keeps the lookup a subtraction rather than a search.
    runs = []
    start = 0
    for i in range(1, len(glyphs) + 1):
        if i == len(glyphs) or ord(glyphs[i]) != ord(glyphs[i - 1]) + 1:
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
            "    }" % (ord(glyphs[first]), length, first + 1)
        )

    body = [
        "/* ---- %s: %d px, %d glyphs ---------------------------------- */" % (
            name, size, len(glyphs)),
        "",
        "static LV_ATTRIBUTE_LARGE_CONST const uint8_t %s_bitmap[] = {" % name,
        hexrow(bitmap),
        "};",
        "",
        "static const lv_font_fmt_txt_glyph_dsc_t %s_dsc[] = {" % name,
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
        "static const lv_font_fmt_txt_cmap_t %s_cmaps[] = {" % name,
        ",\n".join(cmaps),
        "};",
        "",
        "static const lv_font_fmt_txt_dsc_t %s_font_dsc = {" % name,
        "    .glyph_bitmap = %s_bitmap," % name,
        "    .glyph_dsc = %s_dsc," % name,
        "    .cmaps = %s_cmaps," % name,
        "    /* No kerning. The face is monospaced, so every pair is already the",
        "     * same width apart; a kerning table would be a list of zeroes with",
        "     * a lookup in front of it. */",
        "    .kern_dsc = NULL,",
        "    .kern_scale = 0,",
        "    .cmap_num = %d," % len(cmaps),
        "    .bpp = %d," % BPP,
        "    .kern_classes = 0,",
        "    .bitmap_format = 0,",
        "};",
        "",
        "const lv_font_t %s = {" % name,
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
        "    .line_height = %d," % (ascent + descent),
        "    .base_line = %d," % descent,
        "    .subpx = LV_FONT_SUBPX_NONE,",
        "    .underline_position = %d," % -(max(size // 12, 1)),
        "    .underline_thickness = %d," % max(1, size // 24),
        "    .dsc = &%s_font_dsc," % name,
        "};",
        "",
    ]
    return "\n".join(body), len(bitmap)


def main():
    check = "--check" in sys.argv[1:]

    if not os.path.exists(OTF):
        sys.exit(
            "gen_font: %s is missing.\n"
            "It is vendored in this repository - see assets/fonts." % OTF
        )

    head = [
        "/*",
        " * Generated by tools/gen_font.py - do not edit by hand.",
        " *",
        " * Every face the platform draws in, cut from",
        " * assets/fonts/MonaspaceNeon-Regular.otf at %d bpp." % BPP,
        " *",
        " * Committed rather than generated at build time, the way the icons",
        " * are: a build on a bare runner must not need a font renderer.",
        " */",
        "#include <lvgl.h>",
        "",
        "#include \"catnip_font.h\"",
        "",
        # Tables of numbers, laid out to be read as a grid. clang-format would
        # reflow them into a paragraph and then disagree with this generator
        # forever after - the same bargain gen_icons.py makes with its own
        # tables, and for the same reason.
        "/* clang-format off */",
        "",
    ]
    blocks = []
    sizes = []
    for name, size, glyphs in FACES:
        block, nbytes = cut(name, size, glyphs)
        blocks.append(block)
        sizes.append((name, nbytes))
    text = "\n".join(head + blocks + ["/* clang-format on */", ""])

    if check:
        # The committed file is the artefact and this script is its source, so
        # the two disagreeing means somebody edited the artefact.
        have = ""
        if os.path.exists(OUT):
            with open(OUT, "r", encoding="utf-8") as f:
                have = f.read()
        if have != text:
            sys.exit(
                "gen_font: %s does not match what this script produces.\n"
                "Run: python3 tools/gen_font.py"
                % os.path.relpath(OUT, FIRMWARE)
            )
        print("gen_font: %s is up to date" % os.path.relpath(OUT, FIRMWARE))
        return

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    total = sum(n for _, n in sizes)
    for name, n in sizes:
        print("gen_font:   %-22s %7d bytes" % (name, n))
    print("gen_font: %d bytes of bitmap -> %s"
          % (total, os.path.relpath(OUT, FIRMWARE)))


main()
