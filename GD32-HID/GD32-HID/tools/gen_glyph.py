#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
One-shot glyph generator for the OLED 16x16 Chinese font (font.h string_N arrays).

Strategy:
  1. Parse the existing string_3 / string_4 arrays out of font.h.
  2. Brute-force candidate characters + rendering parameters (font, offset,
     bit order, column/row order) until the rendered bytes EXACTLY reproduce
     a known array. This both discovers the real characters and locks the
     exact bit order used by OLED_ShowChinese().
  3. Using the verified parameters, render the target string and print a
     C array literal ready to paste into font.h as string_5.

Run: tools/.venv/Scripts/python.exe tools/gen_glyph.py
"""
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_H = os.path.join(HERE, "..", "USER", "BSP", "font.h")

FONTS = [
    "C:/Windows/Fonts/simhei.ttf",
    "C:/Windows/Fonts/simsun.ttc",
    "C:/Windows/Fonts/msyh.ttc",
]

# Candidate characters that likely appear in string_3/string_4 (algorithm / file labels)
CANDIDATES = list("算法文件：:固读程序烧录开始下上一个选择确认")

TARGET = "开始烧录"          # what we ultimately want
TARGET_INDEX = 5             # string_5


def parse_array(name, text):
    """Extract byte rows of `const unsigned char NAME[..][32] = { {..},{..} };`."""
    m = re.search(name + r"\s*\[\s*\d+\s*\]\s*\[\s*32\s*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        return None
    body = m.group(1)
    rows = re.findall(r"\{([^}]*)\}", body)
    out = []
    for r in rows:
        vals = [int(v, 16) for v in re.findall(r"0[xX]([0-9A-Fa-f]{1,2})", r)]
        if len(vals) == 32:
            out.append(vals)
    return out


def render_char(ch, font, y_off, x_off, thr):
    """Render one char into a 16x16 monochrome grid (list of 16 rows x 16 cols, 1=on)."""
    img = Image.new("L", (16, 16), 0)
    d = ImageDraw.Draw(img)
    d.text((x_off, y_off), ch, fill=255, font=font)
    px = img.load()
    return [[1 if px[x, y] > thr else 0 for x in range(16)] for y in range(16)]


def pack_like_oled(grid, bit_lsb_left, right_half_first):
    """Replicate OLED_ShowChinese byte consumption (ROW-major).

    Code: for j in 0..31: temp=arr[j]; for z in 0..7: draw(x1,y1); temp>>=1; x1++.
          after a byte y1++ ; at j==15 x+=8, reset y.
    => byte = 8 HORIZONTAL pixels of one row. temp&0x01 first => bit0 = leftmost.
       bytes 0..15 = left 8 cols, rows 0..15 (one byte per row);
       bytes 16..31 = right 8 cols, rows 0..15.
    """
    halves = [(0, 8), (8, 16)]
    if right_half_first:
        halves = [(8, 16), (0, 8)]
    out = []
    for (c0, c1) in halves:
        for row in range(16):
            b = 0
            for i, x in enumerate(range(c0, c1)):
                if grid[row][x]:
                    if bit_lsb_left:
                        b |= (1 << i)          # bit0 = leftmost pixel
                    else:
                        b |= (1 << (7 - i))
            out.append(b)
    return out


def find_params(known_rows):
    """Find (font,size,y_off,x_off,thr,bit_lsb_left,right_half_first,chars) reproducing
    as many known glyph rows byte-exact as possible. Caches renders for speed."""
    best = None
    for fpath in FONTS:
        if not os.path.exists(fpath):
            continue
        for size in (16, 15, 14, 12):
            try:
                font = ImageFont.truetype(fpath, size)
            except Exception:
                continue
            for thr in (96, 128, 64):
                for y_off in range(-3, 4):
                    for x_off in range(-2, 3):
                        grids = {ch: render_char(ch, font, y_off, x_off, thr) for ch in CANDIDATES}
                        for bit_lsb_left in (True, False):
                            for right_half_first in (False, True):
                                lut = {}
                                for ch, g in grids.items():
                                    lut[tuple(pack_like_oled(g, bit_lsb_left, right_half_first))] = ch
                                matched = [lut.get(tuple(row)) for row in known_rows]
                                score = sum(1 for m in matched if m)
                                if best is None or score > best[0]:
                                    best = (score, fpath, size, y_off, x_off, thr,
                                            bit_lsb_left, right_half_first, matched)
                                if score == len(known_rows):
                                    return best
    return best


def fmt_array(name, chars, font, y_off, x_off, thr, bit_lsb_left, right_half_first):
    lines = [f"const unsigned char {name}[{len(chars)}][32] =", "{"]
    for ch in chars:
        g = render_char(ch, font, y_off, x_off, thr)
        b = pack_like_oled(g, bit_lsb_left, right_half_first)
        hexs = ",".join(f"0X{v:02X}" for v in b)
        lines.append(f"{{{hexs}}},//\"{ch}\"")
    lines.append("};")
    return "\n".join(lines)


def main():
    with open(FONT_H, "r", encoding="latin-1") as f:
        text = f.read()

    s3 = parse_array("string_3", text)
    s4 = parse_array("string_4", text)
    if not s3 or not s4:
        print("ERROR: could not parse string_3/string_4 from font.h")
        sys.exit(1)

    known = s3 + s4
    print(f"Parsed {len(s3)} rows from string_3, {len(s4)} rows from string_4.")
    print("Brute-forcing render params to reproduce known glyphs...")
    best = find_params(known)
    if not best:
        print("ERROR: no fonts available")
        sys.exit(1)

    score, fpath, size, y_off, x_off, thr, lsb, rhf, matched = best
    print(f"\nBest match: {score}/{len(known)} known glyphs reproduced EXACTLY")
    print(f"  font={os.path.basename(fpath)} size={size} y_off={y_off} x_off={x_off} thr={thr}")
    print(f"  bit_lsb_left={lsb} right_half_first={rhf}")
    print(f"  identified chars: {matched}")

    if score < len(known):
        print("\n[WARN] Not all known glyphs matched byte-exact.")
        print("       Original font differs; generated glyphs use the code-derived")
        print("       bit order with the best-available typeface. Verify on device.")

    font = ImageFont.truetype(fpath, size)
    print("\n// ---- paste into font.h ----")
    print(fmt_array(f"string_{TARGET_INDEX}", list(TARGET),
                    font, y_off, x_off, thr, lsb, rhf))


if __name__ == "__main__":
    main()
