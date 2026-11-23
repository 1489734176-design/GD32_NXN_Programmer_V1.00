#!/usr/bin/env python3
"""镜像任务协议/表格式与上位机事务（不依赖 PyQt5）。

镜像表布局必须与固件 USER/APP/image_task.h 完全一致：
  header 16B + 100 × 112B record + CRC32 4B = 11,220B。
"""
import binascii
import os
import struct
import tempfile

# ── Wire protocol commands (0x0F-0x15) ──────────────────────────────────────
CMD_IMAGE_INFO = 0x0F
CMD_IMAGE_COMMIT = 0x10
CMD_IMAGE_ABORT = 0x12
CMD_QUOTA_PEEK = 0x13
CMD_GET_FREE = 0x14
CMD_IMAGE_GET_RECORD = 0x15

IMAGE_TIMEOUT = 60000

# ── Table format ────────────────────────────────────────────────────────────
IMAGE_SLOTS = 100
IMAGE_RECORD_SIZE = 112
IMAGE_BANK_HEADER_SIZE = 16
IMAGE_BANK_DATA_SIZE = 11216
IMAGE_BANK_SIZE = 11220
IMAGE_BANK_MAGIC = 0x31474D49
IMAGE_BANK_VERSION = 0x0001
IMAGE_BANK_VALID = 0x01
IMAGE_REC_VALID = 0x01
IMAGE_REC_OPT = 0x02
IMAGE_REC_SUM = 0x04
IMAGE_FW_MAX = 102400
IMAGE_ID_NONE = 0xFF

HEADER_FMT = "<IHHIBBH"
RECORD_FMT = "<BBI32s32s16s24s2s"

STATUS_OK = 0x00
STATUS_ERR = 0x01
STATUS_BUSY = 0x02
STATUS_EOF = 0x03


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def sum32(data: bytes) -> int:
    return sum(data) & 0xFFFFFFFF


def ascii_pad(value, size, what):
    if isinstance(value, str):
        try:
            raw = value.encode("ascii")
        except UnicodeEncodeError as exc:
            raise ValueError(f"{what}只能使用 ASCII 字符") from exc
    else:
        raw = bytes(value)
    if len(raw) > size:
        raise ValueError(f"{what}最长 {size} 个 ASCII 字符")
    return raw.ljust(size, b"\x00")


def empty_record(index: int) -> bytes:
    return struct.pack(RECORD_FMT, index, 0, 0, b"", b"", b"", b"", b"\x00\x00")


def pack_record(index, flags, checksum, algo, program, chip, desc):
    return struct.pack(RECORD_FMT, index, flags, checksum & 0xFFFFFFFF,
                       ascii_pad(algo, 32, "算法文件名"),
                       ascii_pad(program, 32, "固件文件名"),
                       ascii_pad(chip, 16, "芯片名"),
                       ascii_pad(desc, 24, "镜像说明"),
                       b"\x00\x00")


def unpack_record(data: bytes):
    if len(data) != IMAGE_RECORD_SIZE:
        raise ValueError("镜像记录长度错误")
    index, flags, checksum, algo, program, chip, desc, _ = struct.unpack(RECORD_FMT, data)
    def text(raw):
        return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
    return dict(id=index, flags=flags, checksum=checksum, algo=text(algo),
                bin=text(program), chip=text(chip), desc=text(desc),
                valid=bool(flags & IMAGE_REC_VALID and text(program)))


def record_bytes(records, index):
    data = records[index]
    if len(data) != IMAGE_RECORD_SIZE:
        raise ValueError("镜像表记录长度错误")
    return data


def build_bank(seq: int, current: int, records) -> bytes:
    if len(records) != IMAGE_SLOTS:
        raise ValueError("镜像表必须包含 100 条记录")
    if current != IMAGE_ID_NONE and not 0 <= current < IMAGE_SLOTS:
        raise ValueError("当前镜像号越界")
    header = struct.pack(HEADER_FMT, IMAGE_BANK_MAGIC, IMAGE_BANK_VERSION,
                         IMAGE_SLOTS, seq & 0xFFFFFFFF, current, IMAGE_BANK_VALID, 0)
    body = bytearray(header)
    for i in range(IMAGE_SLOTS):
        data = record_bytes(records, i)
        rec = unpack_record(data)
        if rec["id"] != i:
            raise ValueError(f"记录 {i} 的 id 不匹配")
        body += data
    assert len(body) == IMAGE_BANK_DATA_SIZE
    return bytes(body) + struct.pack("<I", crc32(body))


def bank_crc(bank: bytes) -> int:
    """返回 bank 尾部存储的整表 CRC（覆盖前 11,216B）。"""
    if len(bank) != IMAGE_BANK_SIZE:
        raise ValueError("镜像表长度错误")
    return struct.unpack_from("<I", bank, IMAGE_BANK_DATA_SIZE)[0]


def parse_bank(data: bytes):
    if len(data) != IMAGE_BANK_SIZE:
        raise ValueError("镜像表长度错误")
    magic, version, slots, seq, current, flags, _ = struct.unpack_from(HEADER_FMT, data, 0)
    if magic != IMAGE_BANK_MAGIC or version != IMAGE_BANK_VERSION or slots != IMAGE_SLOTS:
        raise ValueError("镜像表头无效")
    if not flags & IMAGE_BANK_VALID:
        raise ValueError("镜像表未标记有效")
    stored = struct.unpack_from("<I", data, IMAGE_BANK_DATA_SIZE)[0]
    if crc32(data[:IMAGE_BANK_DATA_SIZE]) != stored:
        raise ValueError("镜像表 CRC 错误")
    records = [data[IMAGE_BANK_HEADER_SIZE + i * IMAGE_RECORD_SIZE:
                    IMAGE_BANK_HEADER_SIZE + (i + 1) * IMAGE_RECORD_SIZE]
               for i in range(IMAGE_SLOTS)]
    return dict(seq=seq, current=current, records=records)


def unique_firmware_name(slot: int, data: bytes, base_hint: str = "") -> str:
    """物理文件名 = 三位镜像号 + '_' + 真实文件名（清洗/截断）+ '.bin'。

    例：001_SPIN0230_DEMO.bin；OPT 为 001_SPIN0230_DEMO_opt.bin（≤31B）。
    镜像号前缀保证不同镜像的物理文件天然不同；文件管理里手动覆盖/删除
    某个镜像的文件不会影响其他镜像。同一槽再次下载同名固件会覆盖该槽
    自己的旧文件（只影响该镜像）。"""
    stem = ""
    if base_hint:
        stem = os.path.splitext(os.path.basename(base_hint))[0]
        stem = "".join(c if (c.isalnum() or c in "_-") else "_" for c in stem)
        try:
            stem.encode("ascii")
        except UnicodeEncodeError:
            stem = "fw"
        stem = stem.strip("_-")[:19].strip("_-")
    if not stem:
        stem = "fw"
    return f"{slot:03d}_{stem}.bin"


def opt_name_for(program: str) -> str:
    dot = program.rfind(".")
    base = program[:dot] if dot > 0 else program
    return base + "_opt.bin"


def build_opt_bytes(chip: dict, encrypt: bool) -> bytes:
    """RPO 加密/不加密 + 其余字段使用 chip_config.json 的默认值。"""
    words = chip.get("opt_words", [])
    if not words:
        raise ValueError("芯片没有 OPT 配置")
    addresses = [int(w["address"], 16) for w in words]
    base_addr = min(addresses)
    total = max(addresses) + 2 - base_addr
    if total <= 0 or total > 128:
        raise ValueError("OPT 长度无效")
    buf = bytearray([0xFF] * total)
    for word in words:
        offset = int(word["address"], 16) - base_addr
        if offset + 2 > total:
            continue
        byte_fields = [f for f in word.get("fields", []) if f.get("byte")]
        bit_fields = [f for f in word.get("fields", []) if "bit" in f]
        if byte_fields:
            field = byte_fields[0]
            if field.get("name") == "RPO":
                low = 127 if encrypt else 165
            else:
                low = field.get("default", 0xFF)
        else:
            low = 0xFF
            for field in bit_fields:
                value = field.get("default", 1)
                if value:
                    low |= 1 << field["bit"]
                else:
                    low &= ~(1 << field["bit"])
        value = (((~low) & 0xFF) << 8) | (low & 0xFF)
        struct.pack_into("<H", buf, offset, value)
    return bytes(buf)


class ImageCommitError(RuntimeError):
    def __init__(self, detail):
        self.detail = detail
        super().__init__(commit_detail_text(detail))


def commit_detail_text(detail) -> str:
    if detail is None:
        return "设备拒绝提交"
    if detail & 0x80:
        err = detail & 0x7F
        reasons = {
            1: "镜像表 CRC 错误",
            2: "镜像记录格式错误",
            3: "算法/固件文件缺失或为空",
            4: "OPT 文件缺失或算法不是扩展格式",
            5: "固件 SUM32 与记录不一致",
            6: "次数账本绑定失败",
            7: "选择记录保存失败",
            8: "设备忙",
            9: "镜像索引不可用",
            10: "镜像表 current 写入失败",
        }
        return "镜像表已提交，但应用镜像失败：" + reasons.get(err, f"IMG_ERR {err}")
    reasons = {
        1: "暂存表尺寸/格式/CRC 错误",
        2: "暂存表读取失败",
        3: "镜像表已被其他操作修改（seq 冲突），请刷新后重试",
        4: "写入镜像 bank 后校验失败",
        5: "设备忙",
        6: "暂存表文件不存在或无法打开（/download/stg.dat）",
    }
    return reasons.get(detail, f"提交失败（detail={detail}）")


class ImageSession:
    """一次镜像表读取/下载/切换/删除事务；device 需实现 image_* 方法。"""

    def __init__(self, device, log=None, progress=None):
        self.device = device
        self.log = log or (lambda _msg: None)
        self.progress = progress or (lambda _step, _cur, _total: None)

    def read_table(self):
        self.log("读取镜像表…")
        info = self.device.image_info()
        valid = bool(info.get("flags", 0) & 1)
        records = []
        for i in range(IMAGE_SLOTS):
            data = self.device.image_get_record(i) if valid else None
            records.append(data if data and len(data) == IMAGE_RECORD_SIZE else empty_record(i))
            if (i + 1) % 10 == 0:
                self.progress("读取镜像表", i + 1, IMAGE_SLOTS)
        return info, records

    def _ensure_img_dir(self):
        try:
            self.device.mkdir("0:/img/")
        except Exception:
            pass  # 目录已存在时设备回 ERR；随后 push 会给出真实错误。

    def _check_space(self, need):
        try:
            free = int(self.device.get_free().get("free_kb", 0)) * 1024
        except Exception:
            return
        if free and free < need:
            raise RuntimeError(f"设备剩余空间不足：需要约 {need // 1024}KB，剩余 {free // 1024}KB")

    def _push_stage(self, bank):
        self._ensure_img_dir()
        fd, path = tempfile.mkstemp(prefix="imgstg-", suffix=".bin")
        try:
            with os.fdopen(fd, "wb") as f:
                f.write(bank)
            self.device.push(path, "0:/download/stg.dat",
                             lambda cur, total: self.progress("写入暂存表", cur, total))
            size = self.device.file_size("0:/download/stg.dat")
            self.log(f"设备暂存表大小：{size} B")
            if size != IMAGE_BANK_SIZE:
                raise RuntimeError(f"暂存表写入后大小异常：{size}，期望 {IMAGE_BANK_SIZE}")
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass

    def _safe_abort(self):
        try:
            self.device.image_abort()
            self.log("已请求设备恢复原镜像选择")
        except Exception as exc:
            self.log(f"恢复失败：{exc}")

    def download(self, slot, local_path, chip_model, chip_cfg, algorithm,
                 encrypt, desc, quota_enabled, quota_count, switch):
        if not 0 <= slot < IMAGE_SLOTS:
            raise ValueError("镜像号必须为 000–099")
        with open(local_path, "rb") as f:
            firmware = f.read()
        if not firmware:
            raise ValueError("固件文件为空")
        if len(firmware) > IMAGE_FW_MAX:
            raise ValueError(f"每槽固件上限 {IMAGE_FW_MAX // 1024}KB，当前 {len(firmware) // 1024}KB")
        checksum = sum32(firmware)
        opt_bytes = build_opt_bytes(chip_cfg, True) if encrypt else b""
        self.log(f"固件 {os.path.basename(local_path)}：{len(firmware)}B，SUM32={checksum:08X}")
        info, records = self.read_table()
        self._check_space(len(firmware) + len(opt_bytes) + IMAGE_BANK_SIZE + 64 * 1024)
        algo_path = "0:/download/firmware/" + algorithm
        if not algorithm:
            raise ValueError("该芯片没有配套算法")
        if not self.device.file_exists(algo_path):
            raise RuntimeError(f"设备缺少算法 {algorithm}，请先在文件管理里上传到 /download/firmware/")
        remote = unique_firmware_name(slot, firmware, local_path)
        if not 0 < len(remote) <= 31 or len(opt_name_for(remote)) > 31:
            raise RuntimeError("生成的固件文件名过长")
        committed = False
        try:
            self.device.stage_algorithm(algorithm)
            self.log(f"上传固件到 /download/bin/{remote}")
            self.device.push(local_path, "0:/download/bin/" + remote,
                             lambda cur, total: self.progress("上传固件", cur, total))
            if encrypt:
                fd, opt_path = tempfile.mkstemp(prefix="imgopt-", suffix=".bin")
                try:
                    with os.fdopen(fd, "wb") as f:
                        f.write(opt_bytes)
                    self.log(f"上传 OPT 到 /download/bin/{opt_name_for(remote)}（加密）")
                    self.device.push(opt_path, "0:/download/bin/" + opt_name_for(remote),
                                     lambda cur, total: self.progress("上传 OPT", cur, total))
                finally:
                    try:
                        os.unlink(opt_path)
                    except OSError:
                        pass
            self.device.stage_algorithm(algorithm)
            self.log("设置该镜像次数：" + ("不限" if not quota_enabled else f"{quota_count} 次"))
            self.device.set_burn_quota(bool(quota_enabled), int(quota_count), remote)
            flags = IMAGE_REC_VALID | IMAGE_REC_SUM | (IMAGE_REC_OPT if encrypt else 0)
            records[slot] = pack_record(slot, flags, checksum, algorithm, remote,
                                        chip_model, desc)
            if switch:
                current = slot
            else:
                current = info.get("current", IMAGE_ID_NONE)
                if current >= IMAGE_SLOTS:
                    current = IMAGE_ID_NONE
            seq = (int(info.get("seq", 0)) + 1) & 0xFFFFFFFF
            bank = build_bank(seq, current, records)
            self._push_stage(bank)
            result = self.device.image_commit(int(info.get("seq", 0)), bank_crc(bank))
            committed = True
            detail = int(result.get("detail", 0))
            if detail:
                raise RuntimeError(commit_detail_text(detail))
            self.log(f"镜像 {slot:03d} 已提交并应用（seq={result.get('new_seq', seq)}）")
            return dict(remote=remote, seq=result.get("new_seq", seq),
                        current=result.get("current", current))
        except Exception:
            if not committed:
                self._safe_abort()
            raise

    def switch(self, slot):
        if not 0 <= slot < IMAGE_SLOTS:
            raise ValueError("镜像号必须为 000–099")
        info, records = self.read_table()
        rec = unpack_record(records[slot])
        if not rec["valid"]:
            raise RuntimeError(f"镜像 {slot:03d} 不是有效任务")
        seq = (int(info.get("seq", 0)) + 1) & 0xFFFFFFFF
        bank = build_bank(seq, slot, records)
        committed = False
        try:
            self._push_stage(bank)
            result = self.device.image_commit(int(info.get("seq", 0)), bank_crc(bank))
            committed = True
            if int(result.get("detail", 0)):
                raise RuntimeError(commit_detail_text(result["detail"]))
            self.log(f"已切换到镜像 {slot:03d}")
            return result
        except Exception:
            if not committed:
                self._safe_abort()
            raise

    def delete(self, slot):
        if not 0 <= slot < IMAGE_SLOTS:
            raise ValueError("镜像号必须为 000–099")
        info, records = self.read_table()
        records[slot] = empty_record(slot)
        current = info.get("current", IMAGE_ID_NONE)
        if current == slot:
            current = IMAGE_ID_NONE
            for i in range(IMAGE_SLOTS):
                if i != slot and unpack_record(records[i])["valid"]:
                    current = i
                    break
        seq = (int(info.get("seq", 0)) + 1) & 0xFFFFFFFF
        bank = build_bank(seq, current, records)
        committed = False
        try:
            self._push_stage(bank)
            result = self.device.image_commit(int(info.get("seq", 0)), bank_crc(bank))
            committed = True
            if int(result.get("detail", 0)):
                raise RuntimeError(commit_detail_text(result["detail"]))
            self.log(f"已删除镜像 {slot:03d}（文件和次数账本保留）")
            return result
        except Exception:
            if not committed:
                self._safe_abort()
            raise

    def abort(self):
        return self.device.image_abort()
