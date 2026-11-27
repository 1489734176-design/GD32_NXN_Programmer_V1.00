# 固件次数独立保存到设备；重新导入只加载当前导入文件的次数，不影响其他固件。
#!/usr/bin/env python3
"""
NX Writer
PyQt5 GUI for transferring files to/from GD32 DAPLINK device.

Usage:
    python daplink_manager.py

Requires: PyQt5, pyusb, libusb-package
    pip install PyQt5 pyusb libusb-package
"""

import sys
import os
import struct
import threading
import tempfile
import re

# ─── Qt 平台插件路径修复（必须在 import PyQt5 之前执行）───────────────────────
# 问题：工程路径含中文（6.烧录器）。Qt 5.15 的插件加载器使用 Win32 ANSI(非Unicode)
# 文件系统 API，解析出的插件路径会把中文变成 "???"，导致 qwindows.dll 加载失败，
# 弹出 "This application failed to start because no Qt platform plugin could be initialized"。
# 方案：显式指定 QT_PLUGIN_PATH / QT_QPA_PLATFORM_PLUGIN_PATH 为真实 Unicode 路径。
def _fix_qt_plugin_path():
    try:
        # 用 find_spec 定位 PyQt5 安装目录，绝不能在设置环境变量前 import PyQt5：
        # 某些 Python/PyQt5 组合会在 import 时提前加载 QtCore 并缓存坏插件路径。
        import importlib.util
        spec = importlib.util.find_spec("PyQt5")
        if spec is None or not spec.submodule_search_locations:
            return
        _pkg_dir = os.path.abspath(next(iter(spec.submodule_search_locations)))
        _plugins = os.path.join(_pkg_dir, "Qt5", "plugins")
        _platforms = os.path.join(_plugins, "platforms")
        if os.path.isdir(_platforms):
            # 无条件覆盖：Qt 会优先读这两个变量，覆盖它自己算出的 ANSI 坏路径。
            os.environ["QT_PLUGIN_PATH"] = _plugins
            os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = _platforms
    except Exception:
        pass  # 交由 Qt 自己处理，不阻断启动


_fix_qt_plugin_path()

from intelhex import IntelHex

import usb.core
import usb.util

# Use libusb-package as backend
try:
    import libusb_package
    _original_find = usb.core.find
    def _find_with_backend(*args, **kwargs):
        kwargs.setdefault('backend', libusb_package.get_libusb1_backend())
        return _original_find(*args, **kwargs)
    usb.core.find = _find_with_backend
except ImportError:
    pass

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QTreeWidget, QTreeWidgetItem, QToolBar, QAction,
    QPushButton, QLabel, QProgressBar, QStatusBar, QFileDialog,
    QMessageBox, QInputDialog, QHeaderView, QFrame, QStyle,
    QLineEdit, QComboBox, QScrollArea, QGroupBox, QRadioButton,
    QButtonGroup, QCheckBox, QGridLayout, QListView, QLayout, QSizePolicy, QSpinBox
)
from PyQt5.QtCore import Qt, QThread, QTimer, pyqtSignal, QSize
from PyQt5.QtGui import QIcon, QFont, QColor, QPixmap

from opt_config import OptConfig, CHIP_DISPLAY_NAMES
from image_tool import (
    CMD_IMAGE_INFO, CMD_IMAGE_COMMIT, CMD_IMAGE_ABORT, CMD_QUOTA_PEEK,
    CMD_GET_FREE, CMD_IMAGE_GET_RECORD, IMAGE_TIMEOUT, IMAGE_RECORD_SIZE,
    IMAGE_SLOTS, IMAGE_FW_MAX, IMAGE_ID_NONE, ImageCommitError, ImageTarget,
    unique_firmware_name, opt_name_for,
)

# ─── USB Protocol Constants ────────────────────────────────────────────────────

VID = 0x28E9
PID = 0x128A
CUSTOMBULK_INTERFACE = 1
EP_OUT = 0x02
EP_IN = 0x82
PACKET_SIZE = 64
MAX_DATA = 56
TIMEOUT = 5000
BURN_QUOTA_MAX = 100000  # 次数输入、设备响应和控件共用同一上限。
PREPARE_TIMEOUT = 30000  # 设备需要回读主BIN计算SUM32，大文件准备响应允许等待30秒。

CMD_LIST_DIR    = 0x01
CMD_OPEN_WRITE  = 0x02
CMD_WRITE_DATA  = 0x03
CMD_CLOSE_FILE  = 0x04
CMD_OPEN_READ   = 0x05
CMD_READ_DATA   = 0x06
CMD_DELETE_FILE = 0x07
CMD_MKDIR       = 0x08
CMD_GET_STATUS  = 0x09
CMD_RENAME      = 0x0A
CMD_STAGE_ALGORITHM = 0x0B
CMD_PREPARE_DOWNLOAD = 0x0C
CMD_GET_BURN_QUOTA = 0x0D
CMD_SET_BURN_QUOTA = 0x0E

STATUS_OK   = 0x00
STATUS_ERR  = 0x01
STATUS_BUSY = 0x02
STATUS_EOF  = 0x03


# ─── USB Device Driver ─────────────────────────────────────────────────────────

class DAPLinkDevice:
    def __init__(self):
        self.dev = None
        self.ep_out = None
        self.ep_in = None
        self._command_lock = threading.RLock()
        self.identity = None

    def connect(self):
        with self._command_lock:
            self.disconnect()
            try:
                self._open()
                self.check_status()
                if self.identity is None:
                    self.identity = self._identity(self.dev)
            except Exception:
                self.disconnect()
                raise

    @staticmethod
    def _identity(dev):
        try:
            serial = dev.serial_number
        except (ValueError, usb.core.USBError, NotImplementedError):
            serial = None
        if serial:
            return ('serial', serial)
        if dev.port_numbers:
            return ('port', dev.bus, tuple(dev.port_numbers))
        return None

    def _open(self):
        match = (lambda d: self._identity(d) == self.identity) if self.identity else None
        self.dev = usb.core.find(idVendor=VID, idProduct=PID, custom_match=match)
        if self.dev is None:
            raise ConnectionError("未找到烧录器，请检查供电和 USB 连接。")
        try:
            if self.dev.is_kernel_driver_active(CUSTOMBULK_INTERFACE):
                self.dev.detach_kernel_driver(CUSTOMBULK_INTERFACE)
        except (usb.core.USBError, NotImplementedError):
            pass
        try:
            cfg = self.dev.get_active_configuration()
        except usb.core.USBError:
            self.dev.set_configuration()
            cfg = self.dev.get_active_configuration()
        intf = cfg[(CUSTOMBULK_INTERFACE, 0)]

        self.ep_out = usb.util.find_descriptor(
            intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.ep_in = usb.util.find_descriptor(
            intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        if self.ep_out is None or self.ep_in is None:
            self.disconnect()
            raise RuntimeError("Endpoints not found on Interface 1. Check WinUSB driver.")

    def is_connected(self):
        return self.dev is not None and self.ep_out is not None and self.ep_in is not None

    def disconnect(self):
        with self._command_lock:
            dev = self.dev
            self.dev = self.ep_out = self.ep_in = None
            if dev is not None:
                try:
                    usb.util.dispose_resources(dev)
                except usb.core.USBError:
                    pass  # Re-enumeration can already have invalidated the handle.

    def check_status(self):
        resp = self._send_cmd(bytes([CMD_GET_STATUS]), timeout=700)
        if resp != bytes([STATUS_BUSY]) and not (len(resp) == 10 and resp[0] == STATUS_OK):
            raise ConnectionError("设备状态响应无效")

    def ensure_connection(self):
        if self.is_connected():
            try:
                self.check_status()
                return
            except OSError:
                pass
        self.connect()

    @staticmethod
    def _fix_path(path):
        git_prefix = 'C:/Program Files/Git'
        if path.startswith(git_prefix):
            path = path[len(git_prefix):]
        if path.startswith('0:'):
            return path
        return '0:' + path.lstrip('/')

    # 锁住单条USB发送/接收，确保同一指令与其响应配对。
    def _send_cmd(self, cmd_data, timeout=TIMEOUT):
        with self._command_lock:
            if not self.is_connected():
                raise ConnectionError("设备未连接")
            if cmd_data and cmd_data[0] == CMD_PREPARE_DOWNLOAD:
                response_timeout = PREPARE_TIMEOUT
            elif cmd_data and cmd_data[0] in (CMD_IMAGE_COMMIT, CMD_IMAGE_ABORT):
                response_timeout = IMAGE_TIMEOUT
            else:
                response_timeout = timeout
            try:
                self.ep_out.write(cmd_data, timeout=timeout)
                return bytes(self.ep_in.read(PACKET_SIZE, timeout=response_timeout))
            except usb.core.USBError:
                self.disconnect()  # A lost acknowledgement must not replay a write.
                raise

    def list_dir(self, path="/"):
        resp = self._send_cmd(bytes([CMD_LIST_DIR]) + self._fix_path(path).encode('utf-8'))
        if resp[0] == STATUS_ERR:
            raise RuntimeError(f"List failed: {path}")
        data = resp[1:].decode('utf-8', errors='replace').strip()
        result = []
        for entry in data.split('\n'):
            entry = entry.strip()
            if not entry:
                continue
            if entry.endswith('/'):
                result.append(('DIR', entry[:-1]))
            else:
                result.append(('FILE', entry))
        return result

    def push(self, local_path, remote_path, callback=None):
        file_size = os.path.getsize(local_path)
        size_bytes = struct.pack('<I', file_size)
        resp = self._send_cmd(bytes([CMD_OPEN_WRITE]) + size_bytes + self._fix_path(remote_path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Open write failed: {remote_path}")
        sent = 0
        with open(local_path, 'rb') as f:
            while sent < file_size:
                chunk = f.read(MAX_DATA)
                if not chunk:
                    break
                resp = self._send_cmd(bytes([CMD_WRITE_DATA]) + chunk)
                if resp[0] != STATUS_OK:
                    raise RuntimeError(f"Write error at {sent}")
                sent += len(chunk)
                if callback:
                    callback(sent, file_size)
        close_resp = self._send_cmd(bytes([CMD_CLOSE_FILE]))
        if not close_resp or close_resp[0] != STATUS_OK:
            raise RuntimeError(f"Close write failed: {remote_path}")
        if sent != file_size:
            raise RuntimeError(f"Incomplete write: {sent}/{file_size} bytes")
        return sent

    def file_exists(self, remote_path):
        resp = self._send_cmd(bytes([CMD_OPEN_READ]) + self._fix_path(remote_path).encode('utf-8'))
        if not resp or resp[0] != STATUS_OK:
            return False
        close_resp = self._send_cmd(bytes([CMD_CLOSE_FILE]))
        if not close_resp or close_resp[0] != STATUS_OK:
            raise RuntimeError(f"Close failed after checking: {remote_path}")
        return True

    def file_size(self, remote_path):
        """OPEN_READ 返回的文件大小；打不开返回 None，随后 CLOSE 幂等。"""
        resp = self._send_cmd(bytes([CMD_OPEN_READ]) + self._fix_path(remote_path).encode('utf-8'))
        if not resp or resp[0] != STATUS_OK or len(resp) < 5:
            return None
        size = struct.unpack_from('<I', resp, 1)[0]
        try:
            self._send_cmd(bytes([CMD_CLOSE_FILE]))
        except Exception:
            pass
        return size

    def stage_algorithm(self, algorithm_name):
        if len(algorithm_name.encode('ascii')) > 31:  # 空名称用于取消失败的导入会话。
            raise ValueError("Algorithm filename must be 1-31 ASCII characters")
        resp = self._send_cmd(bytes([CMD_STAGE_ALGORITHM]) + algorithm_name.encode('ascii'))
        if not resp or resp[0] != STATUS_OK:
            raise RuntimeError("烧录器未接受当前芯片配置，请检查设备状态。")

    def stage_image(self, target, algorithm_name):
        resp = self._send_cmd(target.stage_request(algorithm_name))
        if resp == bytes([STATUS_BUSY]):
            raise RuntimeError("设备正忙，暂不能导入镜像。")
        if resp != bytes([STATUS_OK]):
            raise RuntimeError("设备未接受指定镜像号。请确认烧录器固件已更新。")

    def prepare_download(self, program_name, opt_required, checksum=None, channel_b=False, image=False):
        if not program_name or len(program_name.encode('ascii')) > 31:
            raise ValueError("Program filename must be 1-31 ASCII characters")
        flags = 1 if opt_required else 0
        if channel_b:
            flags |= 4
        if image:
            flags |= 8  # 必须提交之前暂存的单槽镜像，不能退化为普通导入。
        checksum_data = b''
        if checksum is not None:
            # 校验值0也是有效值，不能用真假判断；只接受无符号32位整数。
            if type(checksum) is not int or not 0 <= checksum <= 0xFFFFFFFF:
                raise ValueError("Checksum(SUM32) 必须为 0–FFFFFFFF 的整数。")
            flags |= 2
            checksum_data = struct.pack('<I', checksum)
        resp = self._send_cmd(bytes([CMD_PREPARE_DOWNLOAD, flags]) + checksum_data + program_name.encode('ascii'))
        if not resp:
            raise RuntimeError("设备准备响应为空，无法确认 Checksum(SUM32)。")
        if resp[0] == STATUS_BUSY:
            raise RuntimeError("设备正在传输或烧录，请完成后再导入。")
        if resp[0] != STATUS_OK:
            raise RuntimeError("设备拒绝导入准备，请检查程序、算法、OPT文件和 Checksum(SUM32)。")
        # 旧调用允许旧固件仅返回OK；请求校验时必须收到完整5字节及匹配的值。
        if len(resp) == 1 and checksum is None:
            return None
        if len(resp) != 5:
            raise RuntimeError("设备未返回完整 Checksum(SUM32)，请更新固件后重试。")
        actual_checksum = struct.unpack('<I', resp[1:5])[0]
        if checksum is not None and actual_checksum != checksum:
            raise RuntimeError(f"Checksum(SUM32) 不一致：上位机 {checksum:08X}，设备 {actual_checksum:08X}。")
        return actual_checksum

    @staticmethod
    # 验证设备次数响应的状态、版本、长度与范围，返回启用/异常/剩余次数字段。
    def _decode_quota(resp):
        if resp and resp[0] == STATUS_BUSY:
            raise RuntimeError("设备正在传输或烧录，请完成后再设置次数。")
        if not resp or resp[0] != STATUS_OK:
            raise RuntimeError("设备不支持次数限制或保存失败，请更新固件后重试。")
        if len(resp) != 8:
            raise RuntimeError("设备次数响应无效。")
        if resp[1] != 2:
            raise RuntimeError("设备固件不支持按固件文件独立保存次数，请更新设备固件后重试。")
        if resp[2] > 1 or resp[3] > 1:
            raise RuntimeError("设备次数响应无效。")
        remaining = struct.unpack('<I', resp[4:8])[0]
        if remaining > BURN_QUOTA_MAX or (not resp[2] and remaining):
            raise RuntimeError(f"设备剩余次数无效（应为 0–{BURN_QUOTA_MAX}）。")
        return dict(enabled=bool(resp[2]), fault=bool(resp[3]), remaining=remaining)

    @staticmethod
    def _quota_name_bytes(program_name):
        if program_name is None:
            return b''  # 不附文件名时，设备使用其当前选择的主固件。
        try:
            name = program_name.encode('ascii')
        except (AttributeError, UnicodeEncodeError) as exc:
            raise ValueError("固件文件名必须为 1–31 个 ASCII 字符。") from exc
        if (not 1 <= len(name) <= 31 or program_name in ('.', '..') or
                name[-1:] in (b'.', b' ') or
                any(c < 0x20 or c > 0x7E for c in name) or
                any(c in program_name for c in '/\\:*?"<>|')):
            raise ValueError("固件文件名无效，需为 1–31 个 ASCII 字符的文件名。")
        return name

    # 发送0x0D读取指定固件次数；省略文件名时读取设备当前选择的固件。
    def get_burn_quota(self, program_name=None):
        request = bytes([CMD_GET_BURN_QUOTA]) + self._quota_name_bytes(program_name)
        return self._decode_quota(self._send_cmd(request))

    # 发送0x0E指令设置次数并检查设备回传，取消限制时额度归零但恢复不限。
    def set_burn_quota(self, enabled, remaining, program_name=None):
        if not isinstance(remaining, int) or not 0 <= remaining <= BURN_QUOTA_MAX:
            raise ValueError(f"烧录次数必须为 0–{BURN_QUOTA_MAX}。")
        remaining = remaining if enabled else 0
        request = (bytes([CMD_SET_BURN_QUOTA, int(bool(enabled))]) + struct.pack('<I', remaining) +
                   self._quota_name_bytes(program_name))
        result = self._decode_quota(self._send_cmd(request))
        if result['fault'] or result['enabled'] != bool(enabled) or result['remaining'] != remaining:
            raise RuntimeError("设备次数保存校验失败。")
        return result

    def pull(self, remote_path, local_path, callback=None):
        resp = self._send_cmd(bytes([CMD_OPEN_READ]) + self._fix_path(remote_path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Open read failed: {remote_path}")
        file_size = struct.unpack('<I', resp[1:5])[0]
        received = 0
        try:
            with open(local_path, 'wb') as f:
                while received < file_size:
                    req_len = min(MAX_DATA, file_size - received)
                    resp = self._send_cmd(bytes([CMD_READ_DATA, req_len]))
                    if resp[0] == STATUS_ERR:
                        raise RuntimeError(f"Read error at {received}")
                    data = resp[1:]
                    f.write(data)
                    received += len(data)
                    if callback:
                        callback(received, file_size)
                    if resp[0] == STATUS_EOF:
                        break
        finally:
            # 新固件在 EOF 时自动关闭；旧固件需要显式 CLOSE。关闭失败不掩盖读取结果。
            try:
                self._send_cmd(bytes([CMD_CLOSE_FILE]))
            except Exception:
                pass
        return received

    def mkdir(self, path):
        resp = self._send_cmd(bytes([CMD_MKDIR]) + self._fix_path(path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Mkdir failed: {path}")

    def rm(self, path):
        resp = self._send_cmd(bytes([CMD_DELETE_FILE]) + self._fix_path(path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Delete failed: {path}")

    # ── Image task protocol (0x0F-0x15) ─────────────────────────────────

    def image_info(self):
        resp = self._send_cmd(bytes([CMD_IMAGE_INFO]))
        if not resp:
            raise RuntimeError("镜像 INFO 响应为空")
        if resp[0] == STATUS_BUSY:
            raise RuntimeError("设备正在传输或烧录")
        if resp[0] != STATUS_OK:
            raise RuntimeError("设备不支持镜像功能，请升级固件")
        if len(resp) < 9:
            raise RuntimeError("镜像 INFO 响应无效")
        return dict(version=resp[1], slots=resp[2], current=resp[3],
                    flags=resp[4], seq=struct.unpack_from('<I', resp, 5)[0])

    def image_get_record(self, index):
        if not 0 <= index < IMAGE_SLOTS:
            return None
        out = bytearray()
        for offset in (0, 56):
            resp = self._send_cmd(bytes([CMD_IMAGE_GET_RECORD, index, offset]))
            if not resp or resp[0] != STATUS_OK:
                return None
            n = resp[1]
            data = resp[2:2 + n]
            if len(data) != n:
                return None
            out.extend(data)
        return bytes(out) if len(out) == IMAGE_RECORD_SIZE else None

    def image_commit(self, expected_seq, stage_crc):
        request = bytes([CMD_IMAGE_COMMIT]) + struct.pack('<II', int(expected_seq), int(stage_crc))
        resp = self._send_cmd(request)
        if not resp:
            raise RuntimeError("镜像提交响应为空")
        if resp[0] == STATUS_BUSY:
            raise RuntimeError("设备正在传输或烧录，镜像提交被拒绝")
        if resp[0] != STATUS_OK:
            raise ImageCommitError(resp[1] if len(resp) > 1 else None)
        if len(resp) < 11:
            raise RuntimeError("镜像提交响应无效")
        return dict(detail=resp[1],
                    new_seq=struct.unpack_from('<I', resp, 2)[0],
                    current=resp[6],
                    bank_crc=struct.unpack_from('<I', resp, 7)[0])

    def image_abort(self):
        resp = self._send_cmd(bytes([CMD_IMAGE_ABORT]))
        if not resp or resp[0] != STATUS_OK:
            raise RuntimeError("镜像恢复失败")
        return dict(detail=resp[1] if len(resp) > 1 else 0,
                    current=resp[2] if len(resp) > 2 else IMAGE_ID_NONE)

    def quota_peek(self, program_name):
        name = self._quota_name_bytes(program_name)
        resp = self._send_cmd(bytes([CMD_QUOTA_PEEK]) + name)
        if not resp or resp[0] != STATUS_OK or len(resp) < 7:
            return None
        return dict(enabled=bool(resp[1]), known=bool(resp[2]),
                    remaining=struct.unpack_from('<I', resp, 3)[0])

    def get_free(self):
        resp = self._send_cmd(bytes([CMD_GET_FREE]))
        if not resp or resp[0] != STATUS_OK or len(resp) < 9:
            raise RuntimeError("设备不支持剩余空间查询")
        return dict(free_kb=struct.unpack_from('<I', resp, 1)[0],
                    total_kb=struct.unpack_from('<I', resp, 5)[0])


# ─── Worker Threads ─────────────────────────────────────────────────────────────

class ConnectionWorker(QThread):
    def __init__(self, device):
        super().__init__()
        self.device = device
        self.probed_handle = device.dev
        self.probe = device.is_connected()
        self.error = ''

    def run(self):
        try:
            if self.probe:
                self.device.check_status()
            else:
                self.device.connect()
        except Exception as exc:
            self.error = str(exc) or type(exc).__name__


class ListDirWorker(QThread):
    finished = pyqtSignal(list, str)  # entries, error
    def __init__(self, device, path):
        super().__init__()
        self.device = device
        self.path = path

    def run(self):
        try:
            entries = self.device.list_dir(self.path)
            self.finished.emit(entries, "")
        except Exception as e:
            self.finished.emit([], str(e))


class TransferWorker(QThread):
    progress = pyqtSignal(int, int)  # current, total
    finished = pyqtSignal(int, str)  # bytes, error
    def __init__(self, device, mode, src, dst):
        super().__init__()
        self.device = device
        self.mode = mode  # 'push' or 'pull'
        self.src = src
        self.dst = dst
    # 后台上传/下载文件并报告进度，完成后返回传输字节数。
    def run(self):
        try:
            def cb(cur, total):
                self.progress.emit(cur, total)
            if self.mode == 'push':
                n = self.device.push(self.src, self.dst, cb)
            else:
                n = self.device.pull(self.src, self.dst, cb)
            self.finished.emit(n, "")
        except Exception as e:
            self.finished.emit(0, str(e))


class ActionWorker(QThread):
    finished = pyqtSignal(bool, str)
    def __init__(self, device, action, path, path2=None):
        super().__init__()
        self.device = device
        self.action = action
        self.path = path
        self.path2 = path2
    # 后台执行设备目录创建或文件删除操作。
    def run(self):
        try:
            if self.action == 'mkdir':
                self.device.mkdir(self.path)
            elif self.action == 'rm':
                self.device.rm(self.path)
            self.finished.emit(True, "")
        except Exception as e:
            self.finished.emit(False, str(e))


class ImportWorker(QThread):
    """上传程序与可选OPT，重新加载本次次数设置后提交离线烧录准备。"""
    progress = pyqtSignal(str, int, int)  # step_name, current, total
    log = pyqtSignal(str)
    finished = pyqtSignal(bool, str)

    def __init__(self, device, program_path, algorithm_name, opt_path=None, quota_setting=None, channel_b=False,
                 image_target=None):
        super().__init__()
        self.image_target = image_target
        self.device = device
        self.program_path = program_path
        self.algorithm_name = algorithm_name
        self.opt_path = opt_path
        self.channel_b = bool(channel_b)
        self.checksum = None  # 只在设备确认主BIN的SUM32后保留值，供GUI显示。
        # 主界面传入本次导入的次数快照，后台线程不读取控件；None兼容旧的独立调用。
        self.quota_setting = tuple(quota_setting) if quota_setting is not None else None
        self.quota_result = None  # 只保存设备确认成功的次数，供上位机显示实际已写入的值。
        self.quota_error = ""  # 区分次数加载失败与程序上传失败，避免错误提示误导用户。

    # 后台导入固件、算法和可选OPT文件，确认准备完成后设备转入离线烧录。
    def run(self):
        self.checksum = None  # 每次导入开始清除此前结果，失败不能沿用旧校验值。
        self.quota_result = None
        self.quota_error = ""
        session_started = False
        try:
            # Reopening is safe here, never after a file or quota write has started.
            prog_name = os.path.basename(self.program_path)
            if self.image_target is not None:
                if not 0 < os.path.getsize(self.program_path) <= IMAGE_FW_MAX:
                    raise ValueError(f"镜像固件不能为空，且不能超过 {IMAGE_FW_MAX // 1024} KB。")
                prog_name = unique_firmware_name(self.image_target.slot, base_hint=prog_name)
            self.device.ensure_connection()
            self.device.get_burn_quota()
            if self.image_target is not None:
                # 旧固件拒绝 0x16 时不写文件、不改次数，也不发会清空原选择的取消指令。
                self._stage()
                session_started = True
            try:
                self.device.mkdir("0:/download/bin/")
            except Exception:
                pass

            if self.image_target is None:
                session_started = True
                self._stage()
            # 只累加最终上传主BIN的全部字节；HEX填洞已在转换时写入BIN，OPT/算法和尾页填充不计。
            # 上传前确定期望值；若本地文件或传输内容变化，由设备prepare校验拒绝提交。
            expected_checksum = 0
            with open(self.program_path, 'rb') as program_file:
                for chunk in iter(lambda: program_file.read(65536), b''):
                    expected_checksum = (expected_checksum + sum(chunk)) & 0xFFFFFFFF
            self.log.emit("Uploading program: " + prog_name)
            written = self.device.push(
                self.program_path,
                "0:/download/bin/" + prog_name,
                self._cb)
            if written != os.path.getsize(self.program_path):
                raise RuntimeError(f"Program upload incomplete: {written} bytes")

            opt_required = self.opt_path is not None
            if opt_required:
                opt_name = opt_name_for(prog_name) if self.image_target is not None else os.path.basename(self.opt_path)
                self.log.emit("Uploading OPT: " + opt_name)
                written = self.device.push(
                    self.opt_path,
                    "0:/download/bin/" + opt_name,
                    self._cb)
                if written != os.path.getsize(self.opt_path):
                    raise RuntimeError(f"OPT upload incomplete: {written} bytes")

            self.log.emit("正在确认烧录准备")
            self._stage()
            if self.quota_setting is not None:
                # 暂存算法仍锁住自动烧录；必须先成功写入次数，再发0x0C最终提交指令。
                # 用最终上传的主BIN文件名指定次数，暂存期间设备当前选择已清空。
                self.log.emit("正在重新加载该固件烧录次数：" + prog_name)
                try:
                    self.quota_result = self.device.set_burn_quota(*self.quota_setting, program_name=prog_name)
                except Exception as exc:
                    self.quota_error = str(exc)
                    raise RuntimeError("烧录次数重新加载失败：" + str(exc)) from exc
                remaining_text = f"{self.quota_result['remaining']:03d}次" if self.quota_result['enabled'] else "不限"
                self.log.emit("该固件烧录次数已重新加载：" + remaining_text)
            self.checksum = self.device.prepare_download(
                prog_name, opt_required, checksum=expected_checksum,
                channel_b=self.channel_b, image=self.image_target is not None)
            self.log.emit(f"Checksum(SUM32): {self.checksum:08X}")  # 已由设备确认，按8位大写十六进制显示。
            self.finished.emit(True, "Device ready for offline programming")
        except Exception as e:
            # 导入失败取消暂存，恢复设备待机；断线时取消失败交由设备断线清理。
            if session_started:
                try:
                    self.device.stage_algorithm("")
                except Exception:
                    pass
            self.finished.emit(False, str(e))

    def _stage(self):
        if self.image_target is None:
            self.device.stage_algorithm(self.algorithm_name)
        else:
            self.device.stage_image(self.image_target, self.algorithm_name)

    def _cb(self, cur, total):
        self.progress.emit("", cur, total)


# ─── Main Window ───────────────────────────────────────────────────────────────

class ScrollSafeCombo(QComboBox):
    def wheelEvent(self, event):
        event.ignore()  # 留给外层滚动区；滚轮不提交配置变更。


class DAPLinkManager(QMainWindow):
    def __init__(self):
        super().__init__()
        self.device = DAPLinkDevice()
        self.current_remote_path = "/"
        self.worker = None

        # OPT configuration
        self.opt_config = OptConfig()
        self.current_chip = None
        self.current_model = ''
        self.import_in_progress = False
        self._watch_connection = False
        self._connection_job = None
        self._usb_busy = False
        self._upload_queue = []
        self._opt_widgets = {}
        self._connection_timer = QTimer(self)
        self._connection_timer.setInterval(2000)
        self._connection_timer.timeout.connect(self._poll_connection)

        self.setWindowTitle("NX Writer")
        self.setFont(QFont("Microsoft YaHei", 10))
        self.control_height = max(36, self.fontMetrics().height() + 16)
        self.setMinimumSize(900, 600)
        self.resize(1000, 760)

        self._init_ui()
        self._apply_style()
        self._update_toolbar_state()

    def _init_ui(self):
        # ── Toolbar ──
        self.toolbar = QToolBar("Main Toolbar")
        self.toolbar.setIconSize(QSize(20, 20))
        self.toolbar.setMovable(False)
        self.addToolBar(self.toolbar)

        self.btn_connect = QAction("🔌 连接", self)
        self.btn_connect.triggered.connect(self._on_connect)
        self.toolbar.addAction(self.btn_connect)

        self.btn_disconnect = QAction("⏏ 断开", self)
        self.btn_disconnect.triggered.connect(self._on_disconnect)
        self.toolbar.addAction(self.btn_disconnect)

        self.toolbar.addSeparator()

        self.btn_upload = QAction("⬆ 上传", self)
        self.btn_upload.triggered.connect(self._on_upload)
        self.toolbar.addAction(self.btn_upload)

        self.btn_download = QAction("⬇ 下载", self)
        self.btn_download.triggered.connect(self._on_download)
        self.toolbar.addAction(self.btn_download)

        self.toolbar.addSeparator()

        self.btn_mkdir = QAction("📁 新建文件夹", self)
        self.btn_mkdir.triggered.connect(self._on_mkdir)
        self.toolbar.addAction(self.btn_mkdir)

        self.btn_delete = QAction("🗑 删除", self)
        self.btn_delete.triggered.connect(self._on_delete)
        self.toolbar.addAction(self.btn_delete)

        self.toolbar.addSeparator()

        self.btn_refresh = QAction("🔄 刷新", self)
        self.btn_refresh.triggered.connect(self._on_refresh)
        self.toolbar.addAction(self.btn_refresh)

        self.toolbar.addSeparator()

        self.btn_import = QAction("⚡ 导入程序", self)
        self.btn_import.triggered.connect(self._on_import_program)
        self.toolbar.addAction(self.btn_import)

        # ── Central Widget ──
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # ── Title Banner ──
        self.banner = QFrame()
        self.banner.setObjectName("banner")
        banner_layout = QHBoxLayout(self.banner)
        banner_layout.setContentsMargins(14, 4, 14, 4)
        wordmark = QPixmap(os.path.join(os.path.dirname(__file__), 'nxn_brand.png'))
        logo = wordmark.copy(0, 0, wordmark.height(), wordmark.height())
        self.setWindowIcon(QIcon(logo))
        dpr = self.devicePixelRatioF()
        logo = logo.scaledToHeight(round(44 * dpr), Qt.SmoothTransformation)
        logo.setDevicePixelRatio(dpr)
        brand = QLabel()
        brand.setPixmap(logo)
        banner_layout.addWidget(brand)
        product = QLabel(self.windowTitle())
        product.setObjectName("productTitle")
        banner_layout.addWidget(product)
        banner_layout.addStretch()
        status_layout = QVBoxLayout()
        status_layout.setSpacing(1)
        row1 = QHBoxLayout()
        row1.addStretch()
        self.lbl_status_icon = QLabel("●")
        self.lbl_status_icon.setStyleSheet("color: #e74c3c; font-size: 16px;")
        self.lbl_status_text = QLabel("未连接")
        self.lbl_status_text.setFont(QFont("Microsoft YaHei", 10))
        row1.addWidget(self.lbl_status_icon)
        row1.addWidget(self.lbl_status_text)
        status_layout.addLayout(row1)
        self.lbl_checksum = QLabel("Checksum(SUM32): --------")
        self.lbl_checksum.setFont(QFont("Consolas", 10))
        self.lbl_checksum.setAlignment(Qt.AlignVCenter | Qt.AlignRight)
        self.lbl_checksum.setToolTip("最近成功导入主BIN的全部字节相加，取低32位；HEX填洞计入，OPT和算法不计。")
        status_layout.addWidget(self.lbl_checksum)
        banner_layout.addLayout(status_layout)
        layout.addWidget(self.banner)

        # ── Chip & OPT Selection Bar ──
        self.chip_opt_frame = QFrame()
        self.chip_opt_frame.setFrameShape(QFrame.StyledPanel)
        self.chip_opt_frame.setProperty("panel", True)
        chip_opt_layout = QVBoxLayout(self.chip_opt_frame)
        chip_opt_layout.setContentsMargins(12, 8, 12, 8)  # 减少竖向留白，900×600时芯片框不能覆盖配额框。
        chip_opt_layout.setSpacing(12)
        chip_opt_layout.setSizeConstraint(QLayout.SetMinimumSize)

        # 芯片显示别名和内部配置键分开，切换型号本身不访问设备。
        chip_bar = QGridLayout()
        chip_bar.setHorizontalSpacing(12)
        chip_bar.setVerticalSpacing(10)
        chip_bar.setColumnStretch(1, 1)
        chip_bar.addWidget(QLabel("芯片型号："), 0, 0)
        self.cmb_chip = ScrollSafeCombo()
        self.cmb_chip.setView(QListView())
        self.cmb_chip.setMinimumWidth(220)
        self.cmb_chip.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.cmb_chip.addItem("")  # empty default
        for model in self.opt_config.get_chip_models():
            self.cmb_chip.addItem(CHIP_DISPLAY_NAMES.get(model, model), model)
        self.cmb_chip.currentIndexChanged.connect(self._on_chip_changed)
        chip_bar.addWidget(self.cmb_chip, 0, 1)
        self.btn_opt_toggle = QPushButton("OPT 配置 ▲")
        self.btn_opt_toggle.setCheckable(True)
        self.btn_opt_toggle.setChecked(True)
        self.btn_opt_toggle.clicked.connect(self._toggle_opt_panel)
        chip_bar.addWidget(self.btn_opt_toggle, 0, 2)
        button_width = max(120, self.fontMetrics().horizontalAdvance("OPT 配置 ▼") + 32)
        for control in (self.cmb_chip, self.btn_opt_toggle):
            control.setFixedHeight(self.control_height)
        self.btn_opt_toggle.setFixedWidth(button_width)
        self.chk_opt = QCheckBox("上传 OPT")
        self.chk_opt.setChecked(True)
        self.chk_opt.setEnabled(False)
        self.chk_opt.setMinimumHeight(self.control_height)
        chip_bar.addWidget(self.chk_opt, 0, 3)
        self.chk_channel_b = QCheckBox("启用通道 B（双通道烧录）")
        self.chk_channel_b.setToolTip("不勾选时仅使用通道 A；勾选后两路可独立烧录。")
        chip_bar.addWidget(QLabel("烧录通道："), 1, 0)
        chip_bar.addWidget(self.chk_channel_b, 1, 1, 1, 3)

        chip_bar.addWidget(QLabel("烧录次数："), 2, 0)
        quota_row = QHBoxLayout()
        quota_row.setSpacing(10)
        self.chk_quota = QCheckBox("启用次数限制")
        self.spin_quota = QSpinBox()
        self.spin_quota.setRange(0, BURN_QUOTA_MAX)
        self.spin_quota.setValue(100)
        self.spin_quota.setSuffix(" 次")
        self.spin_quota.setToolTip("随程序导入；0 次表示额度已用尽，不勾选表示不限。")
        self.spin_quota.setMinimumWidth(100)
        self.chk_quota.toggled.connect(self._update_toolbar_state)
        quota_row.addWidget(self.chk_quota)
        quota_row.addWidget(self.spin_quota)
        quota_row.addStretch()
        chip_bar.addLayout(quota_row, 2, 1, 1, 3)

        chip_bar.addWidget(QLabel("镜像号："), 3, 0)
        image_row = QHBoxLayout()
        image_row.setSpacing(10)
        self.cmb_image = ScrollSafeCombo()
        self.cmb_image.setView(QListView())
        self.cmb_image.setAccessibleName("镜像号")
        self.cmb_image.addItem("不指定（普通导入）", None)
        for slot in range(IMAGE_SLOTS):
            self.cmb_image.addItem(f"{slot:03d}", slot)
        self.cmb_image.setToolTip("指定后，导入程序将覆盖此镜像号并切换到该镜像；其他镜像不变。")
        self.cmb_image.currentIndexChanged.connect(self._update_toolbar_state)
        image_row.addWidget(self.cmb_image)
        image_row.addWidget(QLabel("烧录说明："))
        self.edit_image_desc = QLineEdit()
        self.edit_image_desc.setAccessibleName("烧录说明")
        self.edit_image_desc.setMaxLength(23)
        self.edit_image_desc.setPlaceholderText("选填，最多 23 个 ASCII 字符（英文、数字或符号）")
        image_row.addWidget(self.edit_image_desc, 1)
        chip_bar.addLayout(image_row, 3, 1, 1, 3)
        chip_opt_layout.addLayout(chip_bar)

        # OPT config panel (scrollable, expanded by default so RDP 选择一目了然)
        self.opt_scroll = QScrollArea()
        self.opt_scroll.setObjectName("optScroll")
        self.opt_scroll.setWidgetResizable(True)
        self.opt_scroll.setMinimumHeight(48)  # 小窗口及上传进度条出现时允许滚动查看OPT，保持配额控件完整尺寸。
        self.opt_scroll.setVisible(True)
        self.opt_content = QWidget()
        self.opt_scroll.setWidget(self.opt_content)
        chip_opt_layout.addWidget(self.opt_scroll)

        self.workspace = QSplitter(Qt.Vertical)
        self.workspace.setChildrenCollapsible(False)
        self.workspace.addWidget(self.chip_opt_frame)
        layout.addWidget(self.workspace, 1)
        # ── Splitter: Local + Remote ──
        splitter = QSplitter(Qt.Horizontal)

        # Local files
        local_frame = QFrame()
        local_frame.setFrameShape(QFrame.StyledPanel)
        local_frame.setProperty("panel", True)
        local_layout = QVBoxLayout(local_frame)
        local_layout.setContentsMargins(4, 4, 4, 4)
        lbl_local = QLabel("💻 本地电脑")
        lbl_local.setFont(QFont("Microsoft YaHei", 10, QFont.Bold))
        local_layout.addWidget(lbl_local)

        self.local_tree = QTreeWidget()
        self.local_tree.setMinimumHeight(32)  # 文件区可在小窗口收缩，给OPT和配额区域留出空间。
        self.local_tree.setHeaderLabels(["名称", "大小", "类型"])
        self.local_tree.setRootIsDecorated(False)
        self.local_tree.setAlternatingRowColors(True)
        self.local_tree.header().setSectionResizeMode(0, QHeaderView.Stretch)
        self.local_tree.header().resizeSection(1, 80)
        self.local_tree.header().resizeSection(2, 60)
        self.local_tree.itemDoubleClicked.connect(self._on_local_double_click)
        local_layout.addWidget(self.local_tree)

        self.local_path_edit = QLabel("请选择文件夹...")
        self.local_path_edit.setFont(QFont("Consolas", 8))
        self.local_path_edit.setStyleSheet("color: #888; padding: 2px;")
        local_layout.addWidget(self.local_path_edit)

        btn_browse = QPushButton("📂 浏览文件夹...")
        btn_browse.clicked.connect(self._on_browse_local)
        local_layout.addWidget(btn_browse)

        splitter.addWidget(local_frame)

        # Remote files
        remote_frame = QFrame()
        remote_frame.setFrameShape(QFrame.StyledPanel)
        remote_frame.setProperty("panel", True)
        remote_layout = QVBoxLayout(remote_frame)
        remote_layout.setContentsMargins(4, 4, 4, 4)
        lbl_remote = QLabel("🔧 设备 (SPI Flash)")
        lbl_remote.setFont(QFont("Microsoft YaHei", 10, QFont.Bold))
        remote_layout.addWidget(lbl_remote)

        self.remote_tree = QTreeWidget()
        self.remote_tree.setMinimumHeight(32)  # 保持两侧文件区一致，不再以默认最小高度挤压上方配置框。
        self.remote_tree.setHeaderLabels(["名称", "大小", "类型"])
        self.remote_tree.setRootIsDecorated(False)
        self.remote_tree.setAlternatingRowColors(True)
        self.remote_tree.header().setSectionResizeMode(0, QHeaderView.Stretch)
        self.remote_tree.header().resizeSection(1, 80)
        self.remote_tree.header().resizeSection(2, 60)
        self.remote_tree.itemDoubleClicked.connect(self._on_remote_double_click)
        self.remote_tree.setContextMenuPolicy(Qt.CustomContextMenu)
        self.remote_tree.customContextMenuRequested.connect(self._on_remote_context_menu)
        remote_layout.addWidget(self.remote_tree)

        # Remote navigation: path input + buttons
        nav_layout = QHBoxLayout()
        nav_layout.addWidget(QLabel("路径:"))
        self.remote_path_input = QLineEdit("/")
        self.remote_path_input.setFont(QFont("Consolas", 9))
        self.remote_path_input.returnPressed.connect(self._on_go_path)
        nav_layout.addWidget(self.remote_path_input, 1)
        btn_go = QPushButton("转到")
        btn_go.setMinimumWidth(48)
        btn_go.clicked.connect(self._on_go_path)
        nav_layout.addWidget(btn_go)
        btn_up = QPushButton("上级")
        btn_up.setMinimumWidth(48)
        btn_up.clicked.connect(self._on_remote_up)
        nav_layout.addWidget(btn_up)
        btn_root = QPushButton("/")
        btn_root.setFixedWidth(30)
        btn_root.clicked.connect(lambda: self._navigate_remote("/"))
        nav_layout.addWidget(btn_root)
        remote_layout.addLayout(nav_layout)

        splitter.addWidget(remote_frame)
        splitter.setChildrenCollapsible(False)
        splitter.setSizes([450, 450])
        self.workspace.addWidget(splitter)
        self._expanded_sizes = [320, 240]
        self.btn_opt_toggle.setChecked(False)
        self._toggle_opt_panel()
        self.workspace.handle(1).setToolTip("上下拖动，调整配置区与文件区高度")
        splitter.handle(1).setToolTip("左右拖动，调整本地与设备文件区宽度")

        # ── Progress Bar ──
        self.progress = QProgressBar()
        self.progress.setVisible(False)
        self.progress.setTextVisible(True)
        layout.addWidget(self.progress)

        # ── Status Bar ──
        self.statusBar().showMessage("就绪")

    def _apply_style(self):
        self.setStyleSheet("""
            QWidget { font-family: 'Microsoft YaHei'; font-size: 10pt; }
            QMainWindow { background: #eef2f7; }
            QFrame#banner { background: #ffffff; border: 1px solid #dde4ec; border-radius: 8px; }
            QLabel#productTitle { color: #24364b; font-size: 12pt; font-weight: bold; }
            QSplitter::handle { background: #d5dfe9; border-radius: 3px; }
            QSplitter::handle:vertical { height: 7px; margin: 1px 0; }
            QSplitter::handle:horizontal { width: 7px; margin: 0 1px; }
            QSplitter::handle:hover, QSplitter::handle:pressed { background: #4784b5; }
            QToolBar {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #ffffff, stop:1 #e9eef4);
                border: none;
                border-bottom: 2px solid #4e9fd1;
                spacing: 6px;
                padding: 5px;
            }
            QToolBar QToolButton {
                padding: 6px 14px;
                border: 1px solid #c3cede;
                border-radius: 6px;
                background: #ffffff;
                font-family: 'Microsoft YaHei';
                font-size: 13px;
                color: #2b3a4a;
            }
            QToolBar QToolButton:hover {
                background: #4e9fd1;
                border-color: #2b5876;
                color: #ffffff;
            }
            QToolBar QToolButton:pressed { background: #2b5876; color: #ffffff; }
            QToolBar QToolButton:disabled { color: #aeb8c4; background: #f3f5f8; }
            QToolBar::separator { background: #d0d8e3; width: 1px; margin: 4px 2px; }
            QFrame[panel="true"] {
                background: #ffffff; border-radius: 8px; border: 1px solid #dde4ec;
            }
            QLabel { border: none; background: transparent; color: #2b3a4a; }
            QScrollArea#optScroll { border: none; background: #ffffff; }
            QScrollArea#optScroll > QWidget > QWidget { background: #ffffff; }
            QTreeWidget {
                border: 1px solid #dde4ec; border-radius: 6px;
                font-size: 12px; background: #fbfcfe;
                alternate-background-color: #f2f6fb;
            }
            QTreeWidget::item { padding: 4px; }
            QTreeWidget::item:selected { background: #4e9fd1; color: #ffffff; }
            QHeaderView::section {
                background: #eaf1f8; border: none;
                border-bottom: 2px solid #4e9fd1; padding: 5px;
                font-weight: bold; color: #2b5876;
            }
            QProgressBar {
                border: 1px solid #c3cede; border-radius: 6px;
                text-align: center; height: 22px; background: #f3f5f8;
                color: #2b3a4a;
            }
            QProgressBar::chunk {
                border-radius: 5px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #43cea2, stop:1 #4e9fd1);
            }
            QPushButton {
                padding: 4px 10px; border: 1px solid #c3cede;
                border-radius: 6px; background: #ffffff; color: #2b3a4a;
            }
            QPushButton:hover { background: #e8f2fc; border-color: #2677bd; color: #174e7d; }
            QPushButton:pressed, QPushButton:checked { background: #2677bd; color: #ffffff; }
            QPushButton:disabled { color: #8593a3; background: #f3f5f8; }
            QComboBox, QLineEdit {
                border: 1px solid #c3cede; border-radius: 5px;
                padding: 4px 8px; background: #ffffff; color: #24364b;
                selection-background-color: #2677bd; selection-color: #ffffff;
            }
            QComboBox { padding-right: 28px; }
            QComboBox:focus, QLineEdit:focus { border-color: #2677bd; }
            QComboBox:disabled, QLineEdit:disabled { color: #8593a3; background: #f3f5f8; }
            QComboBox QAbstractItemView {
                background: #ffffff; color: #24364b;
                border: 1px solid #b5c6da; padding: 4px;
                selection-background-color: #2677bd; selection-color: #ffffff;
                outline: none;
            }
            QComboBox QAbstractItemView::item {
                min-height: 28px; padding: 4px 10px;
                border: none; color: #24364b; background: #ffffff;
            }
            QComboBox QAbstractItemView::item:hover {
                background: #e8f2fc; color: #174e7d;
            }
            QComboBox QAbstractItemView::item:selected {
                background: #2677bd; color: #ffffff;
            }
            QGroupBox {
                border: 1px solid #dde4ec; border-radius: 6px;
                margin-top: 1.2em; padding-top: 0; color: #2b5876;
            }
            QGroupBox::title {
                subcontrol-origin: margin; subcontrol-position: top left;
                left: 12px; padding: 0 4px; color: #2b5876;
                background: #ffffff;
            }
            QCheckBox, QRadioButton { color: #24364b; spacing: 6px; }
            QCheckBox:disabled, QRadioButton:disabled { color: #8593a3; }
            QStatusBar { background: #e9eef4; color: #2b3a4a; }
        """)

    def _update_toolbar_state(self):
        idle = not self.import_in_progress and not self._usb_busy
        ready = self._can_use_usb()
        self.btn_connect.setEnabled(not self._watch_connection and self._connection_job is None and idle)
        self.btn_disconnect.setEnabled(self._watch_connection and idle)
        for action in (self.btn_upload, self.btn_download, self.btn_mkdir, self.btn_delete,
                       self.btn_refresh, self.btn_import):
            action.setEnabled(ready)
        self.remote_tree.setEnabled(ready)
        self.remote_path_input.setEnabled(ready)
        self.cmb_chip.setEnabled(idle)
        self.btn_opt_toggle.setEnabled(idle and self.current_chip is not None)
        self.chk_opt.setEnabled(idle and self.current_chip is not None)
        self.chk_channel_b.setEnabled(idle)
        self.opt_content.setEnabled(idle)
        self.chk_quota.setEnabled(idle)
        self.spin_quota.setEnabled(idle and self.chk_quota.isChecked())
        self.cmb_image.setEnabled(idle)
        slot = self.cmb_image.currentData()
        self.edit_image_desc.setEnabled(idle and slot is not None)
        self.btn_import.setToolTip("普通导入，不写镜像槽。" if slot is None else
                                   f"导入到镜像 {slot:03d}，覆盖该槽的原有内容。")

    def _set_import_state(self, active):
        self.import_in_progress = active
        if active:
            self.lbl_checksum.setText("Checksum(SUM32): --------")  # 新导入开始即清除旧值，上传期间不显示未验证值。
        self.remote_tree.setEnabled(not active)
        self.remote_path_input.setEnabled(not active)
        self._update_toolbar_state()

    def _start_worker(self, worker, callback):
        self._usb_busy = True
        self.worker = worker
        self._worker_callback = callback
        worker.finished.connect(self._worker_done, Qt.QueuedConnection)
        self._update_toolbar_state()
        worker.start()

    def _worker_done(self, *result):
        worker = self.worker
        worker.wait()  # The result signal is emitted just before run() returns.
        self._usb_busy = False
        self._update_toolbar_state()
        self._worker_callback(*result)
        if self.worker is worker:
            self.worker = None
        worker.deleteLater()

    def _poll_connection(self):
        if (not self._watch_connection or self._connection_job is not None or self._usb_busy
                or self.import_in_progress or QApplication.activeModalWidget() is not None):
            return
        self._connection_job = ConnectionWorker(self.device)
        self._connection_job.finished.connect(self._connection_done)
        self._update_toolbar_state()
        self._connection_job.start()

    def _can_use_usb(self):
        return (self._watch_connection and self.device.is_connected()
                and (self._connection_job is None or self._connection_job.probe)
                and not self._usb_busy and not self.import_in_progress)

    def _on_connect(self):
        self._watch_connection = True
        self._connection_timer.start()
        self.statusBar().showMessage("正在连接烧录器…")
        self._poll_connection()

    def _connection_done(self):
        job = self._connection_job
        self._connection_job = None
        job.deleteLater()
        if not self._watch_connection:
            self.device.disconnect()
            self.device.identity = None
        elif job.error:
            # A click can start an operation after its idle probe has been sent.
            if job.probe and ((self.device.is_connected() and self.device.dev is not job.probed_handle)
                              or self._usb_busy or self.import_in_progress or QApplication.activeModalWidget() is not None):
                self._update_toolbar_state()
                return
            self.device.disconnect()
            self._show_offline("等待烧录器重新连接…")
            self.lbl_status_text.setToolTip(job.error)
        elif not job.probe:
            self.lbl_status_icon.setStyleSheet("color: #21814d; font-size: 16px;")
            self.lbl_status_text.setText("已连接")
            self.lbl_status_text.setToolTip("断电后会自动重新连接")
            self.statusBar().showMessage("烧录器已连接")
            self._navigate_remote("/")
        self._update_toolbar_state()

    def _show_offline(self, text):
        self.lbl_status_icon.setStyleSheet("color: #a36616; font-size: 16px;")
        self.lbl_status_text.setText(text)
        self.lbl_status_text.setToolTip("")
        self.remote_tree.clear()
        self.lbl_checksum.setText("Checksum(SUM32): --------")
        self.statusBar().showMessage(text)

    def _on_disconnect(self):
        self._watch_connection = False
        self._connection_timer.stop()
        if self._connection_job is None:
            self.device.disconnect()
            self.device.identity = None
        self._show_offline("已断开")
        self._update_toolbar_state()

    # ── Remote Navigation ──

    def _navigate_remote(self, path):
        if not self._can_use_usb():
            return
        self.current_remote_path = path
        self.remote_path_input.setText(path)
        self.statusBar().showMessage(f"正在列出 {path} ...")
        worker = ListDirWorker(self.device, path)
        self._start_worker(worker, self._list_done)

    def _list_done(self, entries, error):
        self.remote_tree.clear()
        if error:
            self.statusBar().showMessage(f"错误: {error}")
            return

        # Add parent directory entry
        if self.current_remote_path != "/":
            item = QTreeWidgetItem(["..", "", "DIR"])
            item.setData(0, Qt.UserRole, ("DIR", ".."))
            item.setForeground(0, QColor("#4285f4"))
            self.remote_tree.addTopLevelItem(item)

        for entry_type, name in entries:
            if entry_type == 'DIR':
                item = QTreeWidgetItem([name, "", "DIR"])
                item.setForeground(0, QColor("#4285f4"))
                item.setForeground(2, QColor("#4285f4"))
            else:
                item = QTreeWidgetItem([name, "", "FILE"])
            item.setData(0, Qt.UserRole, (entry_type, name))
            self.remote_tree.addTopLevelItem(item)

        self.statusBar().showMessage(f"{self.current_remote_path} 共 {len(entries)} 项")

    def _on_remote_double_click(self, item, col):
        entry_type, name = item.data(0, Qt.UserRole)
        if entry_type == 'DIR':
            if name == "..":
                self._on_remote_up()
            else:
                new_path = self.current_remote_path.rstrip('/') + '/' + name
                self._navigate_remote(new_path)

    def _on_go_path(self):
        path = self.remote_path_input.text().strip()
        if not path:
            path = "/"
        if not path.startswith("/"):
            path = "/" + path
        self._navigate_remote(path)

    def _on_remote_up(self):
        if self.current_remote_path == "/":
            return
        parts = self.current_remote_path.rstrip('/').split('/')
        parent = '/'.join(parts[:-1]) or '/'
        self._navigate_remote(parent)

    def _on_refresh(self):
        self._navigate_remote(self.current_remote_path)

    # ── Local Browser ──

    def _on_browse_local(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Local Folder")
        if folder:
            self._load_local_folder(folder)

    def _load_local_folder(self, folder):
        self.local_path_edit.setText(folder)
        self.local_tree.clear()
        try:
            entries = sorted(os.listdir(folder), key=lambda x: (not os.path.isdir(os.path.join(folder, x)), x.lower()))
            for name in entries:
                full = os.path.join(folder, name)
                if os.path.isdir(full):
                    item = QTreeWidgetItem([name, "", "DIR"])
                    item.setForeground(0, QColor("#4285f4"))
                    item.setData(0, Qt.UserRole, ("DIR", full))
                else:
                    size = os.path.getsize(full)
                    size_str = self._format_size(size)
                    item = QTreeWidgetItem([name, size_str, "FILE"])
                    item.setData(0, Qt.UserRole, ("FILE", full))
                self.local_tree.addTopLevelItem(item)
        except Exception as e:
            self.statusBar().showMessage(f"Error listing local folder: {e}")

    def _on_local_double_click(self, item, col):
        entry_type, path = item.data(0, Qt.UserRole)
        if entry_type == 'DIR':
            self._load_local_folder(path)

    @staticmethod
    def _format_size(size):
        if size < 1024:
            return f"{size} B"
        elif size < 1024 * 1024:
            return f"{size/1024:.1f} KB"
        else:
            return f"{size/1024/1024:.1f} MB"

    # ── Upload / Download ──

    def _on_upload(self):
        if not self._can_use_usb():
            return
        files, _ = QFileDialog.getOpenFileNames(self, "选择要上传的文件")
        if files:
            self._upload_queue = [(p, self.current_remote_path.rstrip('/') + '/' + os.path.basename(p)) for p in files]
            self._start_transfer('push', *self._upload_queue.pop(0))

    def _on_download(self):
        if not self.device.is_connected():
            return
        item = self.remote_tree.currentItem()
        if not item:
            QMessageBox.information(self, "提示", "请先在设备上选择一个文件。")
            return
        entry_type, name = item.data(0, Qt.UserRole)
        if entry_type != 'FILE':
            QMessageBox.information(self, "提示", "请选择文件(不是目录)。")
            return

        save_path, _ = QFileDialog.getSaveFileName(self, "另存为", name)
        if not save_path:
            return
        remote_path = self.current_remote_path.rstrip('/') + '/' + name
        self._start_transfer('pull', remote_path, save_path)

    def _start_transfer(self, mode, src, dst):
        self.progress.setVisible(True)
        self.progress.setValue(0)
        name = os.path.basename(src if mode == 'push' else dst)
        direction = "上传" if mode == 'push' else "下载"
        self.statusBar().showMessage(f"正在{direction} {name} ...")

        worker = TransferWorker(self.device, mode, src, dst)
        worker.progress.connect(self._transfer_progress)
        self._start_worker(worker, self._transfer_done)

    def _transfer_progress(self, current, total):
        if total > 0:
            pct = current * 100 // total
            self.progress.setValue(pct)
            self.progress.setFormat(f"{self._format_size(current)} / {self._format_size(total)} ({pct}%)")

    def _transfer_done(self, nbytes, error):
        self.progress.setVisible(False)
        if error:
            self._upload_queue.clear()
            self.statusBar().showMessage(f"传输失败: {error}")
            QMessageBox.warning(self, "错误", f"传输失败:\n{error}")
        elif self._upload_queue:
            self._start_transfer('push', *self._upload_queue.pop(0))
        else:
            self.statusBar().showMessage(f"传输完成: {self._format_size(nbytes)}")
            self._navigate_remote(self.current_remote_path)

    # ── Mkdir / Delete ──

    def _on_mkdir(self):
        if not self.device.is_connected():
            return
        name, ok = QInputDialog.getText(self, "新建文件夹", "文件夹名称:")
        if ok and name:
            path = self.current_remote_path.rstrip('/') + '/' + name
            self._start_worker(ActionWorker(self.device, 'mkdir', path), self._action_done)

    def _on_delete(self):
        if not self.device.is_connected():
            return
        item = self.remote_tree.currentItem()
        if not item:
            return
        entry_type, name = item.data(0, Qt.UserRole)
        if name == "..":
            return
        reply = QMessageBox.question(self, "确认删除", f"确定删除 '{name}' ？",
                                     QMessageBox.Yes | QMessageBox.No)
        if reply == QMessageBox.Yes:
            path = self.current_remote_path.rstrip('/') + '/' + name
            self._start_worker(ActionWorker(self.device, 'rm', path), self._action_done)

    def _on_remote_context_menu(self, pos):
        item = self.remote_tree.itemAt(pos)
        if not item:
            return
        entry_type, name = item.data(0, Qt.UserRole)
        if name == "..":
            return

        menu = QApplication.instance().style()  # just use a simple approach
        from PyQt5.QtWidgets import QMenu
        menu = QMenu(self)
        if entry_type == 'FILE':
            act_download = menu.addAction("下载")
            act_download.triggered.connect(self._on_download)
        act_delete = menu.addAction("删除")
        act_delete.triggered.connect(self._on_delete)
        menu.exec_(self.remote_tree.viewport().mapToGlobal(pos))

    def _action_done(self, ok, msg):
        if ok:
            self.statusBar().showMessage("完成")
        else:
            self.statusBar().showMessage(f"错误: {msg}")
        self._navigate_remote(self.current_remote_path)

    # ── OPT Configuration ──

    def _toggle_opt_panel(self):
        visible = self.btn_opt_toggle.isChecked()
        if not visible and self.workspace.isVisible() and not self.opt_scroll.isHidden():
            self._expanded_sizes = self.workspace.sizes()
        self.opt_scroll.setVisible(visible)
        if visible:
            self.chip_opt_frame.setMaximumHeight(16777215)  # QWIDGETSIZE_MAX
            self.workspace.setSizes(self._expanded_sizes)
        else:
            self.chip_opt_frame.setMaximumHeight(self.chip_opt_frame.sizeHint().height())
        self.btn_opt_toggle.setText("OPT 配置 ▲" if visible else "OPT 配置 ▼")

    @staticmethod
    def _valid_name(name, require_bin=False):
        try:
            encoded = name.encode('ascii')
        except UnicodeEncodeError:
            return False
        if not 1 <= len(encoded) <= 31:
            return False
        if name in ('.', '..') or any(c in name for c in '/\\:'):
            return False
        return not require_bin or name.lower().endswith('.bin')

    def _on_chip_changed(self, index):
        self.current_model = self.cmb_chip.itemData(index) or ''
        if not self.current_model:
            self.current_chip = None
            self.chk_opt.setChecked(False)
            self.btn_opt_toggle.setChecked(False)
            self._toggle_opt_panel()
            self._clear_opt_ui()
            self._update_toolbar_state()
            return
        self.current_chip = self.opt_config.get_chip(self.current_model)
        self.chk_opt.setChecked(True)
        self._build_opt_ui()
        # auto-expand panel when chip selected
        if not self.opt_scroll.isVisible():
            self.btn_opt_toggle.setChecked(True)
            self._toggle_opt_panel()
        self._update_toolbar_state()

    def _clear_opt_ui(self):
        self._opt_widgets.clear()
        if self.opt_content:
            self.opt_content.deleteLater()
        self.opt_content = QWidget()
        self.opt_scroll.setWidget(self.opt_content)

    def _build_opt_ui(self):
        self._clear_opt_ui()
        if not self.current_chip:
            return

        layout = QVBoxLayout(self.opt_content)
        layout.setContentsMargins(4, 4, 8, 4)
        layout.setSpacing(12)
        layout.setSizeConstraint(QLayout.SetMinimumSize)

        # Let the scroll area scroll rather than squeeze the field rows.
        preview_grp = QGroupBox("OPT 数据预览", self.opt_content)
        preview_grp.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        preview_layout = QHBoxLayout(preview_grp)
        preview_layout.setContentsMargins(12, 12, 12, 12)
        self.lbl_opt_preview = QLineEdit(preview_grp)
        self.lbl_opt_preview.setReadOnly(True)
        self.lbl_opt_preview.setFixedHeight(self.control_height)
        self.lbl_opt_preview.setFont(QFont("Consolas", 10))
        self.lbl_opt_preview.setStyleSheet(
            "font-family: Consolas; font-size: 10pt; background: #f3f6fa; color: #24364b;")
        preview_layout.addWidget(self.lbl_opt_preview)
        self.lbl_rdp_state = QLabel("", preview_grp)
        self.lbl_rdp_state.setMinimumWidth(self.fontMetrics().horizontalAdvance("读保护已开启") + 16)
        self.lbl_rdp_state.setToolTip("当前 OPT 配置预览，不是芯片回读结果。")
        self.lbl_rdp_state.setAlignment(Qt.AlignCenter)
        self.lbl_rdp_state.setMinimumHeight(self.control_height)
        preview_layout.addWidget(self.lbl_rdp_state)
        layout.addWidget(preview_grp)

        words = self.current_chip.get('opt_words', [])
        label_width = max([120] + [
            self.fontMetrics().horizontalAdvance(field['name']) + 12
            for word in words for field in word.get('fields', [])
        ])
        for w in words:
            grp = QGroupBox(f"{w['name']}  ({w['address']})", self.opt_content)
            grp.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
            grp_layout = QGridLayout(grp)
            grp_layout.setContentsMargins(12, 12, 12, 12)
            grp_layout.setHorizontalSpacing(16)
            grp_layout.setVerticalSpacing(10)
            grp_layout.setColumnStretch(1, 1)

            radio_rows = []
            for row, field in enumerate(w.get('fields', [])):
                widget = self._build_field_widget(field, grp)
                if hasattr(widget, '_btn_group'):
                    radio_rows.append(widget._btn_group.buttons())
                widget.setMinimumHeight(self.control_height)
                label = QLabel(field['name'], grp)
                label.setFixedWidth(label_width)
                label.setMinimumHeight(self.control_height)
                grp_layout.addWidget(label, row, 0, Qt.AlignVCenter)
                grp_layout.addWidget(widget, row, 1)
                self._opt_widgets[field['name']] = widget
                # Connect change signals for live preview
                self._connect_opt_signal(widget)

            # Match the widest first choice in the group, not the text in each row.
            if radio_rows:
                for column in zip(*radio_rows):
                    width = max(button.sizeHint().width() for button in column)
                    for button in column:
                        button.setFixedWidth(width)
            layout.addWidget(grp)

        layout.addStretch()
        self._update_opt_preview()

    def _connect_opt_signal(self, widget):
        """Connect widget change signals to update OPT preview."""
        if isinstance(widget, QComboBox):
            widget.currentIndexChanged.connect(self._update_opt_preview)
        elif isinstance(widget, QCheckBox):
            widget.stateChanged.connect(self._update_opt_preview)
        elif isinstance(widget, QWidget) and hasattr(widget, '_btn_group'):
            widget._btn_group.buttonClicked.connect(self._update_opt_preview)

    def _update_opt_preview(self, *args):
        """Compute current OPT bin and show hex in preview."""
        if not self.current_chip:
            return
        try:
            buf = self._build_opt_bytes()
            hex_str = ' '.join(f'{b:02X}' for b in buf)
            self.lbl_opt_preview.setText(hex_str)

            # 直接把 RDP 结果标出来: A5 5A = 不加密, 7F 80 = 加密
            if hasattr(self, 'lbl_rdp_state'):
                rdp = (buf[0] | (buf[1] << 8)) if len(buf) >= 2 else 0xFFFF
                if rdp == 0x5AA5:
                    self.lbl_rdp_state.setText("读保护关闭")
                    self.lbl_rdp_state.setStyleSheet(
                        "color:#b03a2e; font-weight:bold; border:1px solid #e6b0aa;"
                        "border-radius:3px; background:#fdedec;")
                elif rdp == 0x807F:
                    self.lbl_rdp_state.setText("读保护开启")
                    self.lbl_rdp_state.setStyleSheet(
                        "color:#1e8449; font-weight:bold; border:1px solid #a9dfbf;"
                        "border-radius:3px; background:#eafaf1;")
                else:
                    self.lbl_rdp_state.setText("RDP 异常")
                    self.lbl_rdp_state.setStyleSheet(
                        "color:#b9770e; font-weight:bold; border:1px solid #f5cba7;"
                        "border-radius:3px; background:#fef5e7;")
        except Exception:
            pass

    def _build_field_widget(self, field, parent):
        options = field.get('options', [])
        default = field.get('default')

        if field.get('byte'):
            combo = ScrollSafeCombo(parent)
            combo.setView(QListView())
            combo.setFixedHeight(self.control_height)
            default_idx = 0
            for i, opt in enumerate(options):
                combo.addItem(opt['label'], opt['value'])
                if default is not None and opt['value'] == default:
                    default_idx = i
            combo.setCurrentIndex(default_idx)
            return combo

        if 'bit' in field:
            has_labels = any(o['label'] not in ('0', '1') for o in options)
            if has_labels:
                # Wrap radio buttons in a container widget for proper parent chain
                container = QWidget(parent)
                hbox = QHBoxLayout(container)
                hbox.setContentsMargins(0, 0, 0, 0)
                hbox.setSpacing(20)
                group = QButtonGroup(container)
                for opt in options:
                    radio = QRadioButton(opt['label'], container)
                    group.addButton(radio, opt['value'])
                    hbox.addWidget(radio)
                    if default is not None and opt['value'] == default:
                        radio.setChecked(True)
                hbox.addStretch()
                container._btn_group = group
                return container
            else:
                cb = QCheckBox(parent)
                if default is not None:
                    cb.setChecked(default != 0)
                return cb

        return QLabel("", parent)

    def _get_field_value(self, field_name):
        widget = self._opt_widgets.get(field_name)
        if widget is None:
            return None
        if isinstance(widget, QComboBox):
            return widget.currentData()
        if hasattr(widget, '_btn_group'):
            btn = widget._btn_group.checkedButton()
            if btn:
                return widget._btn_group.id(btn)
            return None
        if isinstance(widget, QCheckBox):
            return 1 if widget.isChecked() else 0
        return None

    @staticmethod
    def _safe_filename(path):
        """Return path with ASCII-only basename; copy file if renamed.
        Returns (safe_path, original_was_unsafe).
        """
        dir_name = os.path.dirname(path)
        base = os.path.basename(path)
        name, ext = os.path.splitext(base)
        safe = re.sub(r'[^\x00-\x7F]', '', name).strip(' _-')
        if not safe:
            safe = 'firmware'
        if safe + ext == base:
            return path, False
        safe_path = os.path.join(dir_name, safe + ext)
        if os.path.exists(safe_path):
            os.remove(safe_path)
        os.rename(path, safe_path)
        return safe_path, True

    # ── Import Program ──

    def _on_import_program(self):
        if not self._can_use_usb():
            return
        if not self.current_model:
            QMessageBox.warning(self, "Warning", "请先选择芯片型号。")
            return
        algorithm_name = self.opt_config.get_algorithm(self.current_model)
        if not self._valid_name(algorithm_name, require_bin=True):
            QMessageBox.warning(self, "芯片配置", "所选芯片的配置无效。")
            return
        slot = self.cmb_image.currentData()
        image_target = None
        if slot is not None:
            image_target = ImageTarget(slot, self.current_model, self.edit_image_desc.text().strip())
            try:
                image_target.stage_request(algorithm_name)
            except ValueError as exc:
                QMessageBox.warning(self, "镜像设置", str(exc))
                return
        file_path, _ = QFileDialog.getOpenFileName(
            self, "选择程序文件", "",
            "Binary/Hex Files (*.bin *.hex);;Binary Files (*.bin);;Hex Files (*.hex);;All Files (*)")
        if not file_path:
            return

        self.lbl_checksum.setText("Checksum(SUM32): --------")  # 已选定新文件；HEX转换或文件检查失败也不能沿用旧值。

        scratch = None
        try:
            if image_target is not None:
                scratch = tempfile.TemporaryDirectory(prefix="nx-writer-")
            if file_path.lower().endswith('.hex'):
                ih = IntelHex(file_path)
                if image_target is not None and (not len(ih) or ih.maxaddr() - ih.minaddr() + 1 > IMAGE_FW_MAX):
                    raise ValueError(f"镜像固件不能为空，HEX 展开后不能超过 {IMAGE_FW_MAX // 1024} KB。")
                stem = os.path.splitext(os.path.basename(file_path))[0]
                safe = re.sub(r'[^\x00-\x7F]', '', stem).strip(' _-') or 'firmware'
                bin_path = os.path.join(scratch.name if scratch else os.path.dirname(file_path), safe + '.bin')
                ih.tobinfile(bin_path)
                file_path = bin_path
            if image_target is None:
                file_path, _ = self._safe_filename(file_path)
                if not self._valid_name(os.path.basename(file_path), require_bin=True):
                    raise ValueError("程序文件名需为 1–31 个 ASCII 字符的 .bin。")
                opt_base = file_path
            else:
                if not file_path.lower().endswith('.bin') or not 0 < os.path.getsize(file_path) <= IMAGE_FW_MAX:
                    raise ValueError(f"镜像固件需为非空 BIN 或 HEX，最大 {IMAGE_FW_MAX // 1024} KB。")
                opt_base = os.path.join(scratch.name, 'options.bin')
            opt_path = self._generate_opt_bin(opt_base) if self.chk_opt.isChecked() else None
        except Exception as exc:
            if scratch is not None:
                scratch.cleanup()
            QMessageBox.warning(self, "无法导入程序", str(exc))
            return

        # 主线程一次性获取导入设置；后续控件锁定，后台线程仅使用此次数快照。
        quota_setting = (self.chk_quota.isChecked(), self.spin_quota.value())
        self.progress.setVisible(True)
        self.progress.setValue(0)
        self.statusBar().showMessage("正在导入...")
        self._set_import_state(True)

        worker = ImportWorker(self.device, file_path, algorithm_name, opt_path, quota_setting=quota_setting,
                              channel_b=self.chk_channel_b.isChecked(), image_target=image_target)
        if scratch is not None:
            worker.finished.connect(lambda *_: scratch.cleanup())
        worker.progress.connect(lambda step, cur, total: self._transfer_progress(cur, total))
        worker.log.connect(lambda msg: self.statusBar().showMessage(msg))
        self._start_worker(worker, self._import_done)

    def _build_opt_bytes(self):
        """按当前 UI 值构造 OPT 二进制（长度 = 配置里定义的 word 范围）。"""
        chip = self.current_chip
        words = chip['opt_words']

        addresses = [int(w['address'], 16) for w in words]
        base_addr = min(addresses)
        end_addr = max(addresses) + 2
        # 文件长度按实际定义的 word 生成(MM32G0001 为 10 字节: OPT0~WRP0)。
        # 算法会按 opt_size(16B) 写入, 但固件已用 0xFF 预填缓冲区,
        # 超出文件的部分写进去就是"未编程", 安全; 而校验也只比对文件长度,
        # 不会去读 OPT 区后面那些读不回来的保留字节。
        total = end_addr - base_addr
        buf = bytearray([0xFF] * total)

        for w in words:
            offset = int(w['address'], 16) - base_addr
            if offset + 2 > total:
                continue

            byte_fields = [f for f in w.get('fields', []) if f.get('byte')]
            bit_fields = [f for f in w.get('fields', []) if 'bit' in f]

            if byte_fields:
                user_val = self._get_field_value(byte_fields[0]['name'])
                low_byte = user_val if user_val is not None else byte_fields[0].get('default', 0xFF)
            else:
                low_byte = 0xFF
                for f in bit_fields:
                    user_val = self._get_field_value(f['name'])
                    if user_val is None:
                        user_val = f.get('default', 1)
                    if user_val:
                        low_byte |= (1 << f['bit'])
                    else:
                        low_byte &= ~(1 << f['bit'])

            # 半字格式 = 低字节(值) + 高字节(值的反码)。
            # 高字节必须严格等于 ~低字节, 芯片会按这个规则校验/规范化:
            # 实测低字节 0xFF 时, 写 0xFFFF 读回是 0x00FF —— 反码位被纠正为 0x00。
            # 所以这里统一按反码规则算, 不要特判 0xFF。
            value = (((~low_byte) & 0xFF) << 8) | low_byte
            buf[offset:offset + 2] = struct.pack('<H', value)

        return buf

    def _generate_opt_bin(self, program_path):
        buf = self._build_opt_bytes()

        base_name = os.path.splitext(program_path)[0]
        opt_path = base_name + '_opt.bin'
        with open(opt_path, 'wb') as f:
            f.write(buf)

        return opt_path

    def _import_done(self, ok, msg):
        self.progress.setVisible(False)
        quota_result = getattr(self.worker, 'quota_result', None)
        if ok:
            # 兼容既有完成回调；只有新导入线程保留的已验证校验值才显示。
            checksum = getattr(self.worker, 'checksum', None)
            if checksum is not None:
                self.lbl_checksum.setText(f"Checksum(SUM32): {checksum:08X}")
            target = getattr(self.worker, 'image_target', None)
            ready = (f"镜像 {target.slot:03d} 已写入并设为当前镜像。" if target is not None
                     else "程序已导入，设备已进入烧录界面。")
            self.statusBar().showMessage(ready + ("烧录次数已重新加载。" if quota_result is not None else ""))
            QMessageBox.information(self, "导入完成", ready + "\n"
                                    "自动模式检测到芯片后开始烧录；手动模式按中键开始。\n"
                                    "本次次数设置已随程序导入。")
        else:
            self.lbl_checksum.setText("Checksum(SUM32): --------")  # 导入失败不得保留此前成功值。
            self.statusBar().showMessage(f"导入失败: {msg}")
            QMessageBox.warning(self, "导入错误", f"导入失败:\n{msg}")
        self._set_import_state(False)

    # ── Close ──

    def closeEvent(self, event):
        if self._usb_busy or self.import_in_progress:
            self.statusBar().showMessage("请等待设备操作完成后关闭窗口。")
            event.ignore()
            return
        self._connection_timer.stop()
        self._watch_connection = False
        if self._connection_job is not None:
            self._connection_job.wait()
        self.device.disconnect()
        event.accept()


# ─── Entry Point ───────────────────────────────────────────────────────────────

def main():
    app = QApplication(sys.argv)
    app.setApplicationName("NX Writer")
    app.setStyle("Fusion")

    window = DAPLinkManager()
    window.show()

    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
