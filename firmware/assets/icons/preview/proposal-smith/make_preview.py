#!/usr/bin/env python3
"""Compose the proposal sheet: current vs proposed, plain vs selected.

Throwaway, and deliberately not in tools/: it renders into this directory only
and writes nothing another author owns.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(FW, "tools"))
import gen_icons as g  # noqa: E402

BG = (0x00, 0x00, 0x00)      # kColBg
PRIMARY = (0x00, 0xB0, 0xFF) # kColPrimary - the selected row's 2px border
TEXT = (0xFF, 0xFF, 0xFF)
FAINT = (0x7B, 0x7D, 0x7B)

ROW_W, ROW_H = 148, 34   # the real row: 34 tall, leading slot 20 (icon 14 + 6)
ICON_X, SLOT = 6, 20
ZOOM = 6


def blank(w, h, col):
    return [[col for _ in range(w)] for _ in range(h)]


def blit(dst, src_rgba, x0, y0):
    """Alpha-composite a rasterised icon onto the sheet."""
    for j, row in enumerate(src_rgba):
        for i, (r, gr, b, a) in enumerate(row):
            if a == 0:
                continue
            br, bg_, bb = dst[y0 + j][x0 + i]
            dst[y0 + j][x0 + i] = (
                (r * a + br * (255 - a)) // 255,
                (gr * a + bg_ * (255 - a)) // 255,
                (b * a + bb * (255 - a)) // 255,
            )


def rect(dst, x, y, w, h, col):
    for i in range(x, x + w):
        dst[y][i] = col
        dst[y + h - 1][i] = col
    for j in range(y, y + h):
        dst[j][x] = col
        dst[j][x + w - 1] = col


def row(svg, selected):
    """One row exactly as the device draws it: icon in the leading slot, a bar
    standing in for the label, and a 2px primary border when selected."""
    px = blank(ROW_W, ROW_H, BG)
    icon = g.raster_rgba(svg, 14)
    blit(px, icon, ICON_X, (ROW_H - 14) // 2)
    for j in range(ROW_H // 2 - 4, ROW_H // 2 + 4):      # the label, as a bar
        for i in range(ICON_X + SLOT, ROW_W - 10):
            px[j][i] = TEXT if selected else FAINT
    if selected:
        rect(px, 0, 0, ROW_W, ROW_H, PRIMARY)
        rect(px, 1, 1, ROW_W - 2, ROW_H - 2, PRIMARY)   # 2 px
    return px


def zoom(px, n):
    return [[c for c in r for _ in range(n)] for r in px for _ in range(n)]


def paste(sheet, px, x0, y0):
    for j, r in enumerate(px):
        for i, c in enumerate(r):
            sheet[y0 + j][x0 + i] = c


def main():
    names = ("file", "audio", "edit")
    pad = 8
    cell_w, cell_h = ROW_W * ZOOM, ROW_H * ZOOM
    sheet_w = pad + (cell_w + pad) * 2
    sheet_h = pad + (cell_h + pad) * len(names) * 2
    sheet = blank(sheet_w, sheet_h, (0x18, 0x1A, 0x18))

    y = pad
    for n in names:
        for svg in (
            os.path.join(FW, "assets", "icons", n + ".svg"),   # current
            os.path.join(HERE, n + ".svg"),                    # proposed
        ):
            paste(sheet, zoom(row(svg, False), ZOOM), pad, y)
            paste(sheet, zoom(row(svg, True), ZOOM), pad * 2 + cell_w, y)
            y += cell_h + pad

    flat = []
    for r in sheet:
        for c in r:
            flat.extend(c)
    out = os.path.join(HERE, "proposal.png")
    g.write_png(out, sheet_w, sheet_h, flat)
    print("wrote %s (%dx%d)" % (out, sheet_w, sheet_h))
    print("rows top-to-bottom: file current/proposed, audio, edit")
    print("columns: unselected | selected (2px #00B0FF border)")


main()
