#!/usr/bin/env python3
"""镜像索引布局/CRC/选择逻辑主机侧测试（不连接设备）。

C 端布局由 image_task.h 的编译期 sizeof 断言保证；本脚本用同一套常量
和 Python struct 复算，覆盖 bank 选择、CRC 损坏、current=NONE、100 槽边界。
"""
import binascii
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "USER" / "APP" / "image_task.h"

HEADER_FMT = "<IHHIBBH"     # magic, version, slots, seq, current, flags, reserved
RECORD_FMT = "<BBI32s32s16s24s2s"
CRC_FMT = "<I"

MAGIC = 0x31474D49
VERSION = 0x0001
BANK_VALID = 0x01
REC_VALID = 0x01
REC_OPT = 0x02
REC_SUM = 0x04
NONE = 0xFF


def parse_defines(text):
    defs = {}
    for name, value in re.findall(r"^#define\s+(IMAGE_[A-Z0-9_]+)\s+([0-9xA-Fa-f]+)U?L?",
                                  text, re.M):
        try:
            defs[name] = int(value, 0)
        except ValueError:
            pass
    return defs


def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF


def empty_record(record_id):
    return struct.pack(RECORD_FMT, record_id, 0, 0,
                       b"", b"", b"", b"", b"\x00\x00")


def make_record(record_id, algo=b"MM32G001-OPT_ext.bin", program=b"demo.bin",
                chip=b"MM32G0001A", desc=b"IMAGE 000", checksum=0,
                opt=False, sum_valid=False):
    flags = REC_VALID
    if opt:
        flags |= REC_OPT
    if sum_valid:
        flags |= REC_SUM
    return struct.pack(RECORD_FMT, record_id, flags, checksum,
                       algo[:32].ljust(32, b"\x00"),
                       program[:32].ljust(32, b"\x00"),
                       chip[:16].ljust(16, b"\x00"),
                       desc[:24].ljust(24, b"\x00"),
                       b"\x00\x00")


def make_bank(seq, current, records, slots=100):
    header = struct.pack(HEADER_FMT, MAGIC, VERSION, slots, seq,
                         current, BANK_VALID, 0)
    body = bytearray(header)
    for i in range(slots):
        body += records.get(i, empty_record(i))
    body += b"\xFF" * 0  # 逻辑 bank 不含 4KB 填充
    data = bytes(body)
    return data + struct.pack(CRC_FMT, crc32(data))


def parse_bank(bank):
    if len(bank) < 20:
        return None
    header = struct.unpack_from(HEADER_FMT, bank, 0)
    magic, version, slots, seq, current, flags, _ = header
    if magic != MAGIC or version != VERSION or slots != 100 or not (flags & BANK_VALID):
        return None
    if current != NONE and current >= slots:
        return None
    body_len = len(bank) - 4
    stored = struct.unpack_from(CRC_FMT, bank, body_len)[0]
    if crc32(bank[:body_len]) != stored:
        return None
    records = {}
    for i in range(slots):
        off = 16 + i * 112
        r = struct.unpack_from(RECORD_FMT, bank, off)
        records[i] = r
    return {"seq": seq, "current": current, "records": records}


def newer(a, b):
    return struct.unpack("<i", struct.pack("<I", (a - b) & 0xFFFFFFFF))[0] > 0


def select(a, b):
    va, vb = parse_bank(a) if a else None, parse_bank(b) if b else None
    if va and vb:
        return "B" if newer(vb["seq"], va["seq"]) else "A"
    if va:
        return "A"
    if vb:
        return "B"
    return None


def main():
    text = HEADER.read_text(encoding="utf-8", errors="replace")
    d = parse_defines(text)
    checks = []

    def expect(name, cond, detail=""):
        checks.append((name, bool(cond), detail))

    expect("IMAGE_SLOTS", d.get("IMAGE_SLOTS") == 100, d.get("IMAGE_SLOTS"))
    expect("IMAGE_RECORD_SIZE", d.get("IMAGE_RECORD_SIZE") == 112, d.get("IMAGE_RECORD_SIZE"))
    expect("IMAGE_BANK_HEADER_SIZE", d.get("IMAGE_BANK_HEADER_SIZE") == 16)
    expect("IMAGE_BANK_DATA_SIZE", d.get("IMAGE_BANK_DATA_SIZE") == 11216)
    expect("IMAGE_BANK_SIZE", d.get("IMAGE_BANK_SIZE") == 11220)
    expect("bank size formula",
           d.get("IMAGE_BANK_HEADER_SIZE", 0) + d.get("IMAGE_SLOTS", 0) * d.get("IMAGE_RECORD_SIZE", 0) + 4
           == d.get("IMAGE_BANK_SIZE", -1))
    expect("IMAGE_BANK_A_ADDR", d.get("IMAGE_BANK_A_ADDR") == 0x00FF0000)
    expect("IMAGE_BANK_B_ADDR", d.get("IMAGE_BANK_B_ADDR") == 0x00FF4000)
    expect("IMAGE_BANK_RESERVE", d.get("IMAGE_BANK_RESERVE") == 0x4000)
    expect("IMAGE_BANK_MAGIC", d.get("IMAGE_BANK_MAGIC") == MAGIC)
    expect("IMAGE_BANK_VERSION", d.get("IMAGE_BANK_VERSION") == VERSION)
    expect("IMAGE_FW_MAX", d.get("IMAGE_FW_MAX") == 102400)

    expect("header struct size", struct.calcsize(HEADER_FMT) == 16)
    expect("record struct size", struct.calcsize(RECORD_FMT) == 112)
    expect("crc32 test vector", crc32(b"123456789") == 0xCBF43926)
    expect("A 4KB aligned", 0x00FF0000 % 4096 == 0)
    expect("B 4KB aligned", 0x00FF4000 % 4096 == 0)
    expect("A/B separated >= bank", 0x00FF4000 - 0x00FF0000 >= 11220)
    expect("B + 3 sectors <= 16MB", 0x00FF4000 + 3 * 4096 <= 0x01000000)

    rec0 = make_record(0, opt=True, sum_valid=True, checksum=0x12345678)
    bank_a = make_bank(5, 0, {0: rec0})
    bank_b = make_bank(6, NONE, {1: make_record(1)})
    expect("parse A", parse_bank(bank_a) is not None)
    expect("seq newer B", select(bank_a, bank_b) == "B")
    expect("seq equal prefers A", select(bank_a, make_bank(5, 0, {})) == "A")
    expect("seq wrap", select(make_bank(0xFFFFFFFF, 0, {}), make_bank(0, 0, {})) == "B")
    expect("current NONE accepted", parse_bank(bank_b) is not None)

    bad = bytearray(bank_a)
    bad[100] ^= 0x01
    expect("corrupt bank rejected", parse_bank(bytes(bad)) is None)
    expect("current 100 rejected", parse_bank(make_bank(1, 100, {})) is None)
    expect("bad magic rejected",
           parse_bank(struct.pack(HEADER_FMT, 0, VERSION, 100, 1, 0, 1, 0) + b"\xFF" * 11200
                      + struct.pack(CRC_FMT, 0)) is None)

    r = struct.unpack_from(RECORD_FMT, bank_a, 16)
    expect("record id", r[0] == 0)
    expect("record flags", r[1] == (REC_VALID | REC_OPT | REC_SUM))
    expect("record checksum", r[2] == 0x12345678)
    expect("record algo", r[3].split(b"\x00")[0] == b"MM32G001-OPT_ext.bin")

    for slot in (0, 99):
        expect(f"record {slot} offset in bank",
               16 + slot * 112 + 112 <= 11216)
    expect("record 100 out of range", 16 + 100 * 112 >= 11216)

    failed = [c for c in checks if not c[1]]
    for name, ok, detail in checks:
        print(("PASS " if ok else "FAIL ") + name + (f" ({detail})" if detail else ""))
    print(f"\n{len(checks) - len(failed)}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
