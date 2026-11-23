#!/usr/bin/env python3
"""镜像表/事务主机侧测试（不接设备）。

覆盖：表布局 round-trip、CRC、唯一文件名、OPT 字节、下载/切换/删除事务、
提交失败时自动 ABORT。
"""
import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from image_tool import (  # noqa: E402
    IMAGE_SLOTS, IMAGE_REC_VALID, IMAGE_REC_OPT, IMAGE_REC_SUM,
    IMAGE_BANK_MAGIC, IMAGE_BANK_VERSION, IMAGE_ID_NONE,
    build_bank, parse_bank, pack_record, empty_record, unpack_record,
    crc32, sum32, unique_firmware_name, opt_name_for, build_opt_bytes,
    ImageSession, ImageCommitError,
)

ROOT = Path(__file__).resolve().parent
checks = []


def check(name, cond, detail=""):
    checks.append((name, bool(cond), detail))


class FakeDevice:
    def __init__(self, commit_error=None):
        self.records = {0: pack_record(0, IMAGE_REC_VALID | IMAGE_REC_OPT | IMAGE_REC_SUM,
                                       0x12345678, "MM32G001-OPT_ext.bin", "old.bin",
                                       "MM32G0001A", "OLD")}
        self.staged = None
        self.pushed = []
        self.aborted = 0
        self.quota = None
        self.commit_error = commit_error
        self.commit_seq = None
        self.commit_crc = None

    def image_info(self):
        return dict(version=1, slots=IMAGE_SLOTS, current=0, flags=1, seq=5)

    def image_get_record(self, index):
        return self.records.get(index, empty_record(index))

    def file_exists(self, path):
        return True

    def stage_algorithm(self, algo):
        self.algo = algo

    def file_size(self, path):
        return 11220 if path == "0:/download/stg.dat" else None

    def push(self, local, remote, callback=None):
        data = open(local, "rb").read()
        self.pushed.append((remote, data))
        if remote == "0:/download/stg.dat":
            self.staged = data
        if callback:
            callback(len(data), len(data))
        return len(data)

    def set_burn_quota(self, enabled, remaining, name):
        self.quota = (enabled, remaining, name)
        return {}

    def image_commit(self, expected_seq, stage_crc):
        b = parse_bank(self.staged)
        self.commit_seq = expected_seq
        self.commit_crc = stage_crc
        check("commit expected seq", b["seq"] == expected_seq + 1)
        check("commit crc", crc32(self.staged[:-4]) == stage_crc)
        if self.commit_error is not None:
            raise ImageCommitError(self.commit_error)
        return dict(detail=0, new_seq=b["seq"], current=b["current"], bank_crc=stage_crc)

    def image_abort(self):
        self.aborted += 1
        return {}

    def mkdir(self, path):
        pass

    def get_free(self):
        return dict(free_kb=15000, total_kb=16000)

    def quota_peek(self, name):
        return dict(enabled=False, remaining=0, known=1)


def make_records():
    recs = [empty_record(i) for i in range(IMAGE_SLOTS)]
    recs[0] = pack_record(0, IMAGE_REC_VALID, 0x11111111, "ALGO.bin", "a.bin", "CHIP", "A")
    return recs


def main():
    recs = make_records()
    bank = build_bank(7, 0, recs)
    check("bank size", len(bank) == 11220, len(bank))
    parsed = parse_bank(bank)
    check("bank seq", parsed["seq"] == 7)
    check("bank current", parsed["current"] == 0)
    check("record valid", unpack_record(parsed["records"][0])["valid"])
    check("empty record", not unpack_record(parsed["records"][99])["valid"])

    bad = bytearray(bank)
    bad[100] ^= 1
    try:
        parse_bank(bytes(bad))
        check("bad crc rejected", False)
    except ValueError:
        check("bad crc rejected", True)

    name = unique_firmware_name(1, b"abc")
    named = unique_firmware_name(1, b"abc", "SPIN0230_DEMO.bin")
    check("unique name prefix/suffix",
          name.startswith("001_") and name.endswith(".bin") and len(name) <= 27, name)
    check("unique name readable base",
          named == "001_SPIN0230_DEMO.bin" and len(named) <= 27, named)
    check("opt name <=31", len(opt_name_for(named)) <= 31, opt_name_for(named))

    cfg = json.loads((ROOT / "chip_config.json").read_text(encoding="utf-8"))
    chip = cfg["MM32G0001A"]
    enc = build_opt_bytes(chip, True)
    plain = build_opt_bytes(chip, False)
    check("opt encrypted RDP", enc[:2] == b"\x7F\x80", enc[:2].hex())
    check("opt plain RDP", plain[:2] == b"\xA5\x5A", plain[:2].hex())
    check("opt length 10", len(enc) == 10, len(enc))

    fw = bytes((i * 7) & 0xFF for i in range(20000))
    fd, fw_path = tempfile.mkstemp(suffix=".bin")
    try:
        os.write(fd, fw)
        os.close(fd)
        dev = FakeDevice()
        sess = ImageSession(dev)
        result = sess.download(1, fw_path, "MM32G0001A", chip,
                               "MM32G001-OPT_ext.bin", True, "TEST", False, 0, True)
        b = parse_bank(dev.staged)
        rec = unpack_record(b["records"][1])
        check("download current", b["current"] == 1)
        check("download seq", b["seq"] == 6)
        check("record id", rec["id"] == 1)
        check("record flags", rec["flags"] == (IMAGE_REC_VALID | IMAGE_REC_OPT | IMAGE_REC_SUM))
        check("record sum", rec["checksum"] == sum32(fw))
        check("record bin", rec["bin"] == result["remote"])
        check("opt uploaded", any(p[0].endswith("_opt.bin") for p in dev.pushed))
        check("quota unlimited", dev.quota == (False, 0, result["remote"]), dev.quota)
        check("no abort on success", dev.aborted == 0)

        dev2 = FakeDevice()
        sess2 = ImageSession(dev2)
        sess2.switch(0)
        check("switch current", parse_bank(dev2.staged)["current"] == 0)
        dev3 = FakeDevice()
        sess3 = ImageSession(dev3)
        sess3.delete(0)
        b3 = parse_bank(dev3.staged)
        check("delete current -> next/none",
              b3["current"] == IMAGE_ID_NONE or not unpack_record(b3["records"][b3["current"]])["valid"])

        dev4 = FakeDevice(commit_error=3)
        sess4 = ImageSession(dev4)
        try:
            sess4.switch(0)
            check("commit failure raises", False)
        except RuntimeError:
            check("commit failure raises", True)
        check("commit failure aborts", dev4.aborted == 1)
    finally:
        try:
            os.unlink(fw_path)
        except OSError:
            pass

    failed = [c for c in checks if not c[1]]
    for name, ok, detail in checks:
        print(("PASS " if ok else "FAIL ") + name + (f" ({detail})" if detail else ""))
    print(f"\n{len(checks) - len(failed)}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
