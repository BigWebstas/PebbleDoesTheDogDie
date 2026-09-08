#!/usr/bin/env python3
"""Generate the app icon + loading logo (run with the pebble-tool python, which
bundles Pillow):

    ~/.local/share/uv/tools/pebble-tool/bin/python tools/make_images.py

A dog paw print - black on transparent, reads at 25 px 1-bit and at logo size.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(__file__), "..", "resources", "images")
os.makedirs(ROOT, exist_ok=True)

BLACK = (0, 0, 0, 255)
CLEAR = (0, 0, 0, 0)


def paw(size, toe_r=0.15, pad_w=0.46, pad_h=0.40):
    """A four-toe paw print on a square canvas `size` px wide."""
    S = size * 8  # supersample, then downscale for clean edges
    img = Image.new("RGBA", (S, S), CLEAR)
    d = ImageDraw.Draw(img)

    def ellipse(cx, cy, rx, ry):
        d.ellipse([(cx - rx) * S, (cy - ry) * S, (cx + rx) * S, (cy + ry) * S], fill=BLACK)

    # main pad, lower-centre
    ellipse(0.50, 0.66, pad_w / 2, pad_h / 2)

    # four toes across the top, outer pair set lower and wider
    r = toe_r
    ellipse(0.24, 0.34, r, r * 1.15)
    ellipse(0.42, 0.20, r, r * 1.15)
    ellipse(0.60, 0.20, r, r * 1.15)
    ellipse(0.78, 0.34, r, r * 1.15)

    return img.resize((size, size), Image.LANCZOS)


def save(img, name):
    path = os.path.join(ROOT, name)
    img.save(path)
    print("wrote", os.path.relpath(path))


# Launcher menu icon: small + 1-bit, keep the shapes chunky.
save(paw(25, toe_r=0.16, pad_w=0.50, pad_h=0.44), "menu_icon.png")

# Loading-screen logo: larger, a touch more slender.
save(paw(72), "logo.png")
