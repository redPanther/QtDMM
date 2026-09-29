#!/usr/bin/env python3
# Copyright (c) 2026 The QtDMM developers
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds assets/macos/qtdmm.icns from the PNG application icons.

The largest drawn icon is 128 px. macOS wants up to 1024 px, so 256, 512
and 1024 are scaled up with nearest neighbour: the pixel look stays sharp
instead of blurring. A real high-resolution icon needs a redrawn SVG.

Needs Pillow. Run from the repository root:  python3 tools/make_icns.py
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "assets" / "icons"
OUT = ROOT / "assets" / "macos" / "qtdmm.icns"


def main():
    imgs = {n: Image.open(ICONS / f"qtdmm_{n}.png").convert("RGBA") for n in (32, 64, 128)}
    imgs[16] = imgs[32].resize((16, 16), Image.LANCZOS)
    for n in (256, 512, 1024):
        imgs[n] = imgs[128].resize((n, n), Image.NEAREST)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    imgs[1024].save(OUT, append_images=[imgs[n] for n in (16, 32, 64, 128, 256, 512)])
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
