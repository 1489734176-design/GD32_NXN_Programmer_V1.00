#!/usr/bin/env python3
"""Generate 16x16 Chinese glyphs for OLED_ShowChinese (requires Pillow)."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT = Path("C:/Windows/Fonts/simhei.ttf")
TARGET = "开始烧录"
TARGET_INDEX = 5


def render_char(ch, font):
    image = Image.new("L", (16, 16), 0)
    left, top, right, bottom = font.getbbox(ch)
    origin = ((16 - (right - left)) // 2 - left,
              (16 - (bottom - top)) // 2 - top)
    ImageDraw.Draw(image).text(origin, ch, fill=255, font=font)
    return [[int(image.getpixel((column, row)) > 96)
             for column in range(16)] for row in range(16)]


def pack_like_oled(grid):
    # OLED x is the vertical coordinate; bit 0 is the top pixel of each page.
    return [sum(grid[page * 8 + bit][column] << bit for bit in range(8))
            for page in range(2) for column in range(16)]


def fmt_array():
    font = ImageFont.truetype(str(FONT), 16)
    lines = [f"const unsigned char string_{TARGET_INDEX}[{len(TARGET)}][32] =", "{"]
    for ch in TARGET:
        data = pack_like_oled(render_char(ch, font))
        lines.append("{" + ",".join(f"0X{value:02X}" for value in data) + "},")
    lines.append("};")
    return "\n".join(lines)


if __name__ == "__main__":
    print(fmt_array())
