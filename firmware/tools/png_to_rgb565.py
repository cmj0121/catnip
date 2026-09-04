"""Flatten a 320x240 PNG to the raw little-endian RGB565 blob the build embeds.

    python3 tools/png_to_rgb565.py in.png out.rgb565

Run by hand when artwork changes; the result is committed next to the PNG so
the firmware build itself needs no image library.

Quantising 8-bit gradients to 5/6 bits bands visibly on a dark background, so
the conversion dithers. It uses an ordered (Bayer) pattern rather than error
diffusion on purpose: the pattern depends only on pixel position, so the same
colour lands on the same value in every animation frame and nothing shimmers
between them.
"""
import struct
import sys

from PIL import Image

W, H = 320, 240

# 4x4 Bayer thresholds, 0..15, applied as a fraction of one quantisation step.
BAYER = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]


def quant(v, bits, t):
    """Map an 8-bit channel to `bits` bits, nudged by threshold t in 0..1."""
    levels = (1 << bits) - 1
    q = int(v / 255.0 * levels + t)
    return min(levels, q)


def convert(src, dst):
    im = Image.open(src).convert("RGB")
    if im.size != (W, H):
        raise SystemExit("%s is %dx%d, expected %dx%d" % (src, im.size[0], im.size[1], W, H))
    data = im.tobytes()
    out = []
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 3
            r, g, b = data[i], data[i + 1], data[i + 2]
            t = BAYER[y & 3][x & 3] / 16.0
            out.append((quant(r, 5, t) << 11) | (quant(g, 6, t) << 5) | quant(b, 5, t))
    with open(dst, "wb") as f:
        f.write(struct.pack("<%dH" % len(out), *out))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(sys.argv[1], sys.argv[2])
