"""Rebuild boot-animation blobs from the PNGs, and drop leftover frames.

The firmware embeds rgb565, not the PNGs. Updating f00.png without rewriting
catnip_splash_320x240.rgb565 leaves the *old* rest pose in flash - that is how
an extra giant-arm frame survived `make install`. This script is the single
place that keeps the three current frames and deletes everything else.

    python3 firmware/tools/sync_boot_art.py

Called from `meowkit.sh install` before PlatformIO builds. Not imported by
gen_assets.py: that script only turns committed rgb565 into C arrays, and
the device build must not require Pillow.
"""
from __future__ import print_function

import glob
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(TOOLS)
REPO = os.path.dirname(FIRMWARE)
ASSETS = os.path.join(REPO, "docs", "assets", "meowkit")
ANIM = os.path.join(ASSETS, "catnip_animimg")
GENERATED = os.path.join(FIRMWARE, "src", "generated")

sys.path.insert(0, TOOLS)
from png_to_rgb565 import convert  # noqa: E402


# PNG -> committed rgb565 blob. f00 is both the rest pose and the splash.
FRAMES = (
    ("f00.png", "catnip_splash_320x240.rgb565"),
    ("f01.png", "catnip_anim_f01_320x240.rgb565"),
    ("f02.png", "catnip_anim_f02_320x240.rgb565"),
)

KEEP_HEADERS = frozenset(
    ("splash_rgb565.h", "anim_f01_rgb565.h", "anim_f02_rgb565.h")
)


def _rel(path):
    return os.path.relpath(path, REPO)


def sync_rgb565():
    """Write each rgb565 from its PNG when the PNG is newer or the blob is missing."""
    for png_name, blob_name in FRAMES:
        png = os.path.join(ANIM, png_name)
        blob = os.path.join(ASSETS, blob_name)
        if not os.path.exists(png):
            raise SystemExit("missing animation frame: %s" % _rel(png))
        if os.path.exists(blob) and os.path.getmtime(blob) >= os.path.getmtime(png):
            continue
        convert(png, blob)
        print("sync_boot_art: wrote %s from %s" % (_rel(blob), _rel(png)))


def drop_stale_blobs():
    """Remove rgb565 / PNG frames that are no longer in the three-frame wave."""
    keep_blobs = set(blob for _, blob in FRAMES)
    keep_pngs = set(png for png, _ in FRAMES)
    for path in glob.glob(os.path.join(ASSETS, "catnip_anim_f*.rgb565")):
        if os.path.basename(path) not in keep_blobs:
            os.remove(path)
            print("sync_boot_art: removed leftover %s" % _rel(path))
    for path in glob.glob(os.path.join(ANIM, "f*.png")):
        if os.path.basename(path) not in keep_pngs:
            os.remove(path)
            print("sync_boot_art: removed leftover %s" % _rel(path))


def drop_stale_headers():
    if not os.path.isdir(GENERATED):
        return
    for name in os.listdir(GENERATED):
        if not name.endswith(".h"):
            continue
        if name in KEEP_HEADERS:
            continue
        path = os.path.join(GENERATED, name)
        os.remove(path)
        print("sync_boot_art: removed leftover %s" % _rel(path))


def drop_pio_generated():
    """PlatformIO copies headers into the build tree; stale ones still compile."""
    pio_gen = os.path.join(FIRMWARE, ".pio", "build", "meowkit", "src", "generated")
    if not os.path.isdir(pio_gen):
        return
    for name in os.listdir(pio_gen):
        if name.endswith(".h") and name not in KEEP_HEADERS:
            os.remove(os.path.join(pio_gen, name))
            print("sync_boot_art: removed leftover %s" % os.path.relpath(
                os.path.join(pio_gen, name), REPO
            ))


def main():
    os.makedirs(GENERATED, exist_ok=True)
    sync_rgb565()
    drop_stale_blobs()
    drop_stale_headers()
    drop_pio_generated()


if __name__ == "__main__":
    main()
