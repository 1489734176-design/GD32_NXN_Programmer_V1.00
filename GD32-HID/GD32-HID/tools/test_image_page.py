#!/usr/bin/env python3
"""镜像页静态检查：100 槽分页、GBK 字模、mode5/UI_ImageItem 入口。

不连接设备、不运行 FVP；像素验证由实机与 UI 预览负责。
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUN = (ROOT / "USER/APP/run.c").read_text(encoding="utf-8", errors="replace")
OLED = (ROOT / "USER/BSP/bsp_oled.c").read_text(encoding="utf-8", errors="replace")
OLED_H = (ROOT / "USER/BSP/bsp_oled.h").read_text(encoding="utf-8", errors="replace")
IMAGE_H = (ROOT / "USER/APP/image_task.h").read_text(encoding="utf-8", errors="replace")
FONT = (ROOT / "USER/BSP/ui_chinese_font.h").read_text(encoding="utf-8", errors="replace")

checks = []


def check(name, cond, detail=""):
    checks.append((name, bool(cond), detail))


def define(text, name):
    m = re.search(r"^#define\s+" + name + r"\s+([0-9]+)U?", text, re.M)
    return int(m.group(1)) if m else None


slots = define(IMAGE_H, "IMAGE_SLOTS")
rows = define(RUN, "FILE_LIST_ROWS")
check("IMAGE_SLOTS", slots == 100, slots)
check("FILE_LIST_ROWS", rows == 12, rows)
pages = math.ceil(slots / rows)
last_rows = slots - (pages - 1) * rows
check("page count", pages == 9, pages)
check("last page rows", last_rows == 4, last_rows)

check("mode5 defined", re.search(r"^void mode5\(void\)", RUN, re.M) is not None)
check("scan_image_page defined", "static void scan_image_page(void)" in RUN)
check("image_move defined", "static void image_move(int8_t delta)" in RUN)
check("leave_image_list defined", "static void leave_image_list(void)" in RUN)
check("run_task dispatch case 5", re.search(r"case\s+5\s*:\s*mode5\(\)", RUN) is not None)
check("head=3 entry", "app_data.head == 3" in RUN and "app_data.mode = 5" in RUN)
check("cursor id10", "UI_FieldColor(10, 0, 48, 8, 16" in RUN)
check("UI_ImageItem declared", "UI_ImageItem" in OLED_H)
check("UI_ImageItem defined", re.search(r"uint16_t UI_ImageItem\(", OLED) is not None)
check("UI_ImageItem three digits", '"%03u"' in OLED)
check("row text 24 char cap", '"%.24s"' in RUN)
check("long press jump 10", "image_move(-10)" in RUN and "image_move(10)" in RUN)

codes = {int(x, 16) for x in re.findall(r"\{0x([0-9A-Fa-f]{4}),", FONT)}
for text in ("镜像选择", "无镜像", "中键确认", "长按中键返回"):
    missing = []
    for ch in text:
        b = ch.encode("gbk")
        code = (b[0] << 8) | b[1]
        if code not in codes:
            missing.append(f"{ch}(0x{code:04X})")
    check(f"glyphs: {text}", not missing, ",".join(missing))

failed = [c for c in checks if not c[1]]
for name, ok, detail in checks:
    print(("PASS " if ok else "FAIL ") + name + (f" ({detail})" if detail else ""))
print(f"\n{len(checks) - len(failed)}/{len(checks)} passed")
sys.exit(1 if failed else 0)
