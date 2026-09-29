#!/usr/bin/env python3
# Copyright (c) 2026 The QtDMM developers
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the platform icons from assets/icons/qtdmm.svg.

- assets/macos/qtdmm.icns: all sizes from the SVG, drawn inside the macOS
  icon grid (an 824 px body on a 1024 px canvas), so QtDMM is as large in
  the Dock as other apps.
- assets/windows/qtdmm.ico: the drawn 16 to 128 px entries stay as they are
  (the small ones are pixel-tuned by hand); a 256 px entry from the SVG is
  added for large Explorer views.

Needs Pillow and rsvg-convert (librsvg). Run from anywhere:
    python3 tools/make_icons.py
"""

import io
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "assets" / "icons" / "qtdmm.svg"
ICNS = ROOT / "assets" / "macos" / "qtdmm.icns"
ICO = ROOT / "assets" / "windows" / "qtdmm.ico"

MAC_BODY = 824 / 1024   # share of the canvas the icon body takes on macOS


def render(size):
    """The SVG rendered to size x size pixels."""
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), str(SVG)],
                         check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def mac_icon(size):
    body = round(size * MAC_BODY)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    offset = (size - body) // 2
    canvas.alpha_composite(render(body), (offset, offset))
    return canvas


def make_icns():
    sizes = (16, 32, 64, 128, 256, 512, 1024)
    imgs = {n: mac_icon(n) for n in sizes}
    ICNS.parent.mkdir(parents=True, exist_ok=True)
    imgs[1024].save(ICNS, append_images=[imgs[n] for n in sizes[:-1]])
    print(f"wrote {ICNS.relative_to(ROOT)}")


def make_ico():
    old = Image.open(ICO)
    entries = []
    for size in sorted(s for s in old.info["sizes"] if s[0] < 256):
        old.size = size
        entries.append(old.copy().convert("RGBA"))
    entries.append(render(256))
    entries[-1].save(ICO, sizes=[e.size for e in entries], append_images=entries[:-1])
    print(f"wrote {ICO.relative_to(ROOT)}")


if __name__ == "__main__":
    make_icns()
    make_ico()
