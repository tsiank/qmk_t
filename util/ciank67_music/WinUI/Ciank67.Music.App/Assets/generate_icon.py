# Copyright 2026 QMK Contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regenerate the original keyboard/spectrum icon: python -m pip install Pillow."""
from pathlib import Path

from PIL import Image, ImageDraw


def render(size):
    # Draw each frame separately at 4x resolution so small Windows icons stay sharp.
    scale = size * 4 / 256
    image = Image.new('RGBA', (size * 4, size * 4))
    draw = ImageDraw.Draw(image)

    def box(bounds, radius, color):
        draw.rounded_rectangle(tuple(round(v * scale) for v in bounds),
                               radius=round(radius * scale), fill=color)

    box((8, 8, 248, 248), 52, '#12243A')
    heights = (32, 60, 86, 106, 74, 48, 26)
    colors = ('#37DCC4', '#37DCC4', '#43D4EC', '#43D4EC', '#739FFF', '#A394FF', '#C39AFF')
    for column, (height, color) in enumerate(zip(heights, colors)):
        x = 42 + column * 25
        box((x, 139 - height, x + 17, 139), 8, color)

    box((32, 155, 224, 216), 13, '#DCECF6')
    # Two simple key rows remain readable at 16 px.
    for row in range(2):
        for column in range(7):
            x, y = 44 + column * 25, 166 + row * 18
            box((x, y, x + 17, y + 11), 3, '#203B54')
    return image.resize((size, size), Image.Resampling.LANCZOS)


if __name__ == '__main__':
    sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
    frames = [render(size) for size in sizes]
    frames[-1].save(Path(__file__).with_name('Ciank67Music.ico'), format='ICO',
                    sizes=[(s, s) for s in sizes], append_images=frames[:-1])
