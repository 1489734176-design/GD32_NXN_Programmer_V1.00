#!/usr/bin/env python3
"""Pack ui_boot_logo.h arrays into /system/boot_logo.bin.

Layout (little endian):
  0  magic "NXN1"
  4  rle_len u16
  6  logo_w u16
  8  logo_h u16
 10  caption_w u16
 12  caption_h u16
 14  caption_stride u16
 16  palette[16] u16 (RGB565)
 48  RLE bytes
 48+rle_len caption bitmap (caption_stride * caption_h, row-major MSB-first)

Usage:
  python make_boot_logo_bin.py [--preview]
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "USER/BSP/ui_boot_logo.h"
OUT = Path(__file__).resolve().parent / "boot_logo.bin"


def array_values(text, name, expect):
    m = re.search(r"static\s+const\s+uint8_t\s+" + name + r"\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit(f"{name} not found")
    declared = int(m.group(1))
    vals = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(2))]
    if len(vals) != declared or declared != expect:
        raise SystemExit(f"{name}: declared {declared}, parsed {len(vals)}, expected {expect}")
    return vals


def palette_values(text):
    m = re.search(r"static\s+const\s+uint16_t\s+boot_logo_palette\s*\[\s*16\s*\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit("palette not found")
    vals = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    if len(vals) != 16:
        raise SystemExit(f"palette: {len(vals)} values")
    return vals


def define(text, name):
    m = re.search(r"^#define\s+" + name + r"\s+(\d+)U?", text, re.M)
    if not m:
        raise SystemExit(f"{name} not found")
    return int(m.group(1))


def rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def preview(path, logo_w, logo_h, rle, palette, cap_w, cap_h, stride, caption):
    try:
        from PIL import Image
    except ImportError:
        print("PIL not available, skip preview")
        return
    img = Image.new("RGB", (240, 320), "black")
    lx = (240 - logo_w) // 2
    ly = (320 - (logo_h + 16 + cap_h)) // 2
    col = row = 0
    for token in rle:
        span = (token & 15) + 1
        color = token >> 4
        if color and row < logo_h:
            for dx in range(span):
                if col + dx < logo_w:
                    img.putpixel((lx + col + dx, ly + row), rgb565(palette[color]))
        col += span
        if col >= logo_w:
            col = 0
            row += 1
    cx = (240 - cap_w) // 2
    cy = ly + logo_h + 16
    for y in range(cap_h):
        for x in range(cap_w):
            byte = caption[y * stride + x // 8]
            if byte & (0x80 >> (x & 7)):
                img.putpixel((cx + x, cy + y), (255, 0, 0))
    img.save(path)
    print(f"preview -> {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    text = HEADER.read_text(encoding="utf-8", errors="replace")
    logo_w = define(text, "BOOT_LOGO_WIDTH")
    logo_h = define(text, "BOOT_LOGO_HEIGHT")
    cap_w = define(text, "BOOT_CAPTION_WIDTH")
    cap_h = define(text, "BOOT_CAPTION_HEIGHT")
    stride = define(text, "BOOT_CAPTION_STRIDE")
    rle = array_values(text, "boot_logo_rle", 1890)
    caption = array_values(text, "boot_caption_bitmap", stride * cap_h)
    palette = palette_values(text)

    hdr = struct.pack("<4sHHHHHH16H", b"NXN1", len(rle), logo_w, logo_h,
                      cap_w, cap_h, stride, *palette)
    data = hdr + bytes(rle) + bytes(caption)
    OUT.write_bytes(data)
    print(f"wrote {OUT} ({len(data)} bytes, header {len(hdr)})")
    if args.preview:
        preview(Path(__file__).resolve().parent / "boot_logo_preview.png",
                logo_w, logo_h, rle, palette, cap_w, cap_h, stride, caption)


if __name__ == "__main__":
    sys.exit(main())
