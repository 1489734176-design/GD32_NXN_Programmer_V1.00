# 中文注释补充（2026-10-26）：新增烧录次数设置/读取界面及USB命令；额度保存到设备，重新导入程序不会自动恢复次数。
#!/usr/bin/env python3
"""
CMSIS-DAPLINK File Manager
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
import re

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
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QSize
from PyQt5.QtGui import QIcon, QFont, QColor

from opt_config import OptConfig

# ─── USB Protocol Constants ────────────────────────────────────────────────────

VID = 0x28E9
PID = 0x128A
CUSTOMBULK_INTERFACE = 1
EP_OUT = 0x02
EP_IN = 0x82
PACKET_SIZE = 64
MAX_DATA = 56
TIMEOUT = 5000

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
        self._command_lock = threading.Lock()

    def connect(self):
        # Release any previous resources first
        self.disconnect()

        self.dev = usb.core.find(idVendor=VID, idProduct=PID)
        if self.dev is None:
            raise RuntimeError("Device not found. Check USB connection.")

        # Detach kernel drivers on all interfaces
        for i in range(4):
            try:
                if self.dev.is_kernel_driver_active(i):
                    self.dev.detach_kernel_driver(i)
            except (usb.core.USBError, NotImplementedError):
                pass

        # Try to set configuration (may already be set)
        try:
            self.dev.set_configuration()
        except usb.core.USBError:
            pass  # Already configured

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
        return self.dev is not None and self.ep_out is not None

    def disconnect(self):
        if self.dev:
            usb.util.dispose_resources(self.dev)
            self.dev = None
            self.ep_out = None
            self.ep_in = None

    @staticmethod
    def _fix_path(path):
        git_prefix = 'C:/Program Files/Git'
        if path.startswith(git_prefix):
            path = path[len(git_prefix):]
        if path.startswith('0:'):
            return path
        return '0:' + path.lstrip('/')

    # 锁住单条USB发送/接收，确保同一指令与其响应配对。
    def _send_cmd(self, cmd_data):
        with self._command_lock:
            self.ep_out.write(cmd_data, timeout=TIMEOUT)
            data = self.ep_in.read(PACKET_SIZE, timeout=TIMEOUT)
            return bytes(data)

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

    def stage_algorithm(self, algorithm_name):
        if not algorithm_name or len(algorithm_name.encode('ascii')) > 31:
            raise ValueError("Algorithm filename must be 1-31 ASCII characters")
        resp = self._send_cmd(bytes([CMD_STAGE_ALGORITHM]) + algorithm_name.encode('ascii'))
        if not resp or resp[0] != STATUS_OK:
            raise RuntimeError(f"Algorithm selection rejected: {algorithm_name}")

    def prepare_download(self, program_name, opt_required):
        if not program_name or len(program_name.encode('ascii')) > 31:
            raise ValueError("Program filename must be 1-31 ASCII characters")
        flags = 1 if opt_required else 0
        resp = self._send_cmd(bytes([CMD_PREPARE_DOWNLOAD, flags]) + program_name.encode('ascii'))
        if not resp:
            raise RuntimeError("Prepare response was empty")
        if resp[0] == STATUS_BUSY:
            raise RuntimeError("Device is busy")
        if resp[0] != STATUS_OK:
            raise RuntimeError("Device rejected download preparation")

    @staticmethod
    # 验证设备次数响应的状态、版本、长度与范围，返回启用/异常/剩余次数字段。
    def _decode_quota(resp):
        if resp and resp[0] == STATUS_BUSY:
            raise RuntimeError("设备正在传输或烧录，请完成后再设置次数。")
        if not resp or resp[0] != STATUS_OK:
            raise RuntimeError("设备不支持次数限制或保存失败，请更新固件后重试。")
        if len(resp) != 8 or resp[1] != 1 or resp[2] > 1 or resp[3] > 1:
            raise RuntimeError("设备次数响应无效。")
        remaining = struct.unpack('<I', resp[4:8])[0]
        if remaining > 999 or (not resp[2] and remaining):
            raise RuntimeError("设备剩余次数无效。")
        return dict(enabled=bool(resp[2]), fault=bool(resp[3]), remaining=remaining)

    # 发送0x0D指令读取设备剩余次数。
    def get_burn_quota(self):
        return self._decode_quota(self._send_cmd(bytes([CMD_GET_BURN_QUOTA])))

    # 发送0x0E指令设置次数并检查设备回传，取消限制时额度归零但恢复不限。
    def set_burn_quota(self, enabled, remaining):
        if not isinstance(remaining, int) or not 0 <= remaining <= 999:
            raise ValueError("烧录次数必须为 0–999。")
        remaining = remaining if enabled else 0
        request = bytes([CMD_SET_BURN_QUOTA, int(bool(enabled))]) + struct.pack('<I', remaining)
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
        return received

    def mkdir(self, path):
        resp = self._send_cmd(bytes([CMD_MKDIR]) + self._fix_path(path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Mkdir failed: {path}")

    def rm(self, path):
        resp = self._send_cmd(bytes([CMD_DELETE_FILE]) + self._fix_path(path).encode('utf-8'))
        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Delete failed: {path}")


# ─── Worker Threads ─────────────────────────────────────────────────────────────

class ConnectWorker(QThread):
    finished = pyqtSignal(bool, str)
    def __init__(self, device):
        super().__init__()
        self.device = device
    # 后台连接设备，将成功或错误通知主界面。
    def run(self):
        try:
            self.device.connect()
            self.finished.emit(True, "Connected")
        except Exception as e:
            self.finished.emit(False, str(e))


class ListDirWorker(QThread):
    finished = pyqtSignal(list, str)  # entries, error
    def __init__(self, device, path):
        super().__init__()
        self.device = device
        self.path = path
    # 后台读取设备目录，返回文件列表或错误。
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


class QuotaWorker(QThread):
    finished = pyqtSignal(object, str)

    def __init__(self, device, setting=None):
        super().__init__()
        self.device = device
        self.setting = setting

    # 后台保存或读取烧录次数，将设备回传额度交给主界面。
    def run(self):
        try:
            if self.setting is None:
                result = self.device.get_burn_quota()
            else:
                result = self.device.set_burn_quota(*self.setting)
            self.finished.emit(result, "")
        except Exception as exc:
            self.finished.emit(None, str(exc))


class ImportWorker(QThread):
    """Upload a program batch and commit it for offline programming."""
    progress = pyqtSignal(str, int, int)  # step_name, current, total
    log = pyqtSignal(str)
    finished = pyqtSignal(bool, str)

    def __init__(self, device, program_path, algorithm_name, opt_path=None):
        super().__init__()
        self.device = device
        self.program_path = program_path
        self.algorithm_name = algorithm_name
        self.opt_path = opt_path

    # 后台导入固件、算法和可选OPT文件，确认准备完成后设备转入离线烧录。
    def run(self):
        try:
            try:
                self.device.mkdir("0:/download/bin/")
            except Exception:
                pass

            prog_name = os.path.basename(self.program_path)
            self.log.emit("Uploading program: " + prog_name)
            written = self.device.push(
                self.program_path,
                "0:/download/bin/" + prog_name,
                self._cb)
            if written != os.path.getsize(self.program_path):
                raise RuntimeError(f"Program upload incomplete: {written} bytes")

            opt_required = bool(self.opt_path and os.path.exists(self.opt_path))
            if opt_required:
                opt_name = os.path.basename(self.opt_path)
                self.log.emit("Uploading OPT: " + opt_name)
                written = self.device.push(
                    self.opt_path,
                    "0:/download/bin/" + opt_name,
                    self._cb)
                if written != os.path.getsize(self.opt_path):
                    raise RuntimeError(f"OPT upload incomplete: {written} bytes")

            self.log.emit("Preparing offline programming")
            self.device.stage_algorithm(self.algorithm_name)
            self.device.prepare_download(prog_name, opt_required)
            self.finished.emit(True, "Device ready for offline programming")
        except Exception as e:
            self.finished.emit(False, str(e))

    def _cb(self, cur, total):
        self.progress.emit("", cur, total)


# ─── Main Window ───────────────────────────────────────────────────────────────

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
        self.quota_in_progress = False
        self.quota_worker = None
        self._opt_widgets = {}

        self.setWindowTitle("CMSIS-DAPLINK 脱机编程器 · 文件管理")
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
        self.banner = QLabel("⚡  CMSIS-DAPLINK 脱机编程器")
        self.banner.setObjectName("banner")
        self.banner.setAlignment(Qt.AlignVCenter | Qt.AlignLeft)
        self.banner.setFixedHeight(46)
        layout.addWidget(self.banner)

        # ── Connection Status ──
        self.status_frame = QFrame()
        self.status_frame.setFrameShape(QFrame.StyledPanel)
        self.status_frame.setProperty("panel", True)
        status_layout = QVBoxLayout(self.status_frame)
        status_layout.setContentsMargins(10, 6, 10, 6)
        status_layout.setSpacing(2)
        # Row 1: status icon + text
        row1 = QHBoxLayout()
        self.lbl_status_icon = QLabel("●")
        self.lbl_status_icon.setStyleSheet("color: #e74c3c; font-size: 16px;")
        self.lbl_status_text = QLabel("未连接")
        self.lbl_status_text.setFont(QFont("Microsoft YaHei", 10))
        row1.addWidget(self.lbl_status_icon)
        row1.addWidget(self.lbl_status_text)
        row1.addStretch()
        status_layout.addLayout(row1)
        # Row 2: path
        self.lbl_path = QLabel("路径: /")
        self.lbl_path.setFont(QFont("Consolas", 9))
        self.lbl_path.setStyleSheet("color: #555;")
        self.lbl_path.setWordWrap(True)
        status_layout.addWidget(self.lbl_path)
        layout.addWidget(self.status_frame)

        # ── Chip & OPT Selection Bar ──
        self.chip_opt_frame = QFrame()
        self.chip_opt_frame.setFrameShape(QFrame.StyledPanel)
        self.chip_opt_frame.setProperty("panel", True)
        chip_opt_layout = QVBoxLayout(self.chip_opt_frame)
        chip_opt_layout.setContentsMargins(12, 12, 12, 12)
        chip_opt_layout.setSpacing(12)
        chip_opt_layout.setSizeConstraint(QLayout.SetMinimumSize)

        # Two rows share the same input and button columns.
        chip_bar = QGridLayout()
        chip_bar.setHorizontalSpacing(12)
        chip_bar.setVerticalSpacing(10)
        chip_bar.setColumnStretch(1, 1)
        chip_bar.addWidget(QLabel("芯片型号："), 0, 0)
        self.cmb_chip = QComboBox()
        self.cmb_chip.setView(QListView())
        self.cmb_chip.setMinimumWidth(220)
        self.cmb_chip.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.cmb_chip.addItem("")  # empty default
        for m in self.opt_config.get_chip_models():
            self.cmb_chip.addItem(m)
        self.cmb_chip.currentTextChanged.connect(self._on_chip_changed)
        chip_bar.addWidget(self.cmb_chip, 0, 1)
        chip_bar.addWidget(QLabel("算法文件："), 1, 0)
        self.algorithm_edit = QLineEdit()
        self.algorithm_edit.setPlaceholderText("设备 /download/firmware/ 中的算法文件名")
        self.algorithm_edit.setMinimumWidth(220)
        chip_bar.addWidget(self.algorithm_edit, 1, 1)
        self.btn_bind_algorithm = QPushButton("保存算法")
        self.btn_bind_algorithm.clicked.connect(self._bind_algorithm)
        chip_bar.addWidget(self.btn_bind_algorithm, 1, 2)
        self.btn_opt_toggle = QPushButton("OPT 配置 ▲")
        self.btn_opt_toggle.setCheckable(True)
        self.btn_opt_toggle.setChecked(True)
        self.btn_opt_toggle.clicked.connect(self._toggle_opt_panel)
        chip_bar.addWidget(self.btn_opt_toggle, 0, 2)
        button_width = max(120, self.fontMetrics().horizontalAdvance("OPT 配置 ▼") + 32)
        for control in (self.cmb_chip, self.algorithm_edit,
                        self.btn_bind_algorithm, self.btn_opt_toggle):
            control.setFixedHeight(self.control_height)
        for button in (self.btn_bind_algorithm, self.btn_opt_toggle):
            button.setFixedWidth(button_width)
        self.chk_opt = QCheckBox("上传 OPT")
        self.chk_opt.setChecked(True)
        self.chk_opt.setEnabled(False)
        self.chk_opt.setMinimumHeight(self.control_height)
        chip_bar.addWidget(self.chk_opt, 0, 3)
        chip_opt_layout.addLayout(chip_bar)

        # OPT config panel (scrollable, expanded by default so RDP 选择一目了然)
        self.opt_scroll = QScrollArea()
        self.opt_scroll.setObjectName("optScroll")
        self.opt_scroll.setWidgetResizable(True)
        self.opt_scroll.setMinimumHeight(100)
        self.opt_scroll.setMaximumHeight(350)
        self.opt_scroll.setVisible(True)
        self.opt_content = QWidget()
        self.opt_scroll.setWidget(self.opt_content)
        chip_opt_layout.addWidget(self.opt_scroll)

        layout.addWidget(self.chip_opt_frame)

        # Persistent quota is saved separately; importing a program never replenishes it.
        self.quota_frame = QGroupBox("烧录次数限制")
        quota_layout = QGridLayout(self.quota_frame)
        quota_layout.setContentsMargins(12, 2, 12, 4)
        quota_layout.setHorizontalSpacing(10)
        self.chk_quota = QCheckBox("启用次数限制")
        self.spin_quota = QSpinBox()
        self.spin_quota.setRange(0, 999)
        self.spin_quota.setValue(100)
        self.spin_quota.setSuffix(" 次")
        self.spin_quota.setMinimumWidth(100)
        self.btn_quota_save = QPushButton("保存到设备")
        self.btn_quota_read = QPushButton("读取剩余次数")
        self.lbl_quota = QLabel("剩余次数：请连接设备后读取")
        self.lbl_quota.setWordWrap(True)
        self.chk_quota.toggled.connect(lambda _: self._update_toolbar_state())
        self.btn_quota_save.clicked.connect(lambda: self._start_quota(True))
        self.btn_quota_read.clicked.connect(lambda: self._start_quota(False))
        for control in (self.chk_quota, self.spin_quota, self.btn_quota_save, self.btn_quota_read):
            control.setMinimumHeight(self.control_height)
        quota_layout.addWidget(self.chk_quota, 0, 0)
        quota_layout.addWidget(self.spin_quota, 0, 1)
        quota_layout.addWidget(self.btn_quota_save, 0, 2)
        quota_layout.addWidget(self.btn_quota_read, 0, 3)
        quota_layout.addWidget(self.lbl_quota, 0, 4)
        quota_layout.setColumnStretch(4, 1)
        self.quota_frame.setToolTip("保存会把设备剩余次数设为输入值；失败不扣次数，烧录中断电按一次计。取消勾选并保存可不限次数。")
        layout.addWidget(self.quota_frame)

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
        btn_go.setFixedWidth(48)
        btn_go.clicked.connect(self._on_go_path)
        nav_layout.addWidget(btn_go)
        btn_up = QPushButton("上级")
        btn_up.setFixedWidth(48)
        btn_up.clicked.connect(self._on_remote_up)
        nav_layout.addWidget(btn_up)
        btn_root = QPushButton("/")
        btn_root.setFixedWidth(30)
        btn_root.clicked.connect(lambda: self._navigate_remote("/"))
        nav_layout.addWidget(btn_root)
        remote_layout.addLayout(nav_layout)

        splitter.addWidget(remote_frame)
        splitter.setSizes([450, 450])
        layout.addWidget(splitter)

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
            QLabel#banner {
                color: #ffffff;
                font-family: 'Microsoft YaHei';
                font-size: 17px;
                font-weight: bold;
                padding-left: 16px;
                border-radius: 8px;
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #2b5876, stop:1 #4e9fd1);
            }
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
        connected = self.device.is_connected()
        idle = not self.import_in_progress and not self.quota_in_progress
        self.btn_connect.setEnabled(not connected and idle)
        self.btn_disconnect.setEnabled(connected and idle)
        self.btn_upload.setEnabled(connected and idle)
        self.btn_download.setEnabled(connected and idle)
        self.btn_mkdir.setEnabled(connected and idle)
        self.btn_delete.setEnabled(connected and idle)
        self.btn_refresh.setEnabled(connected and idle)
        self.btn_import.setEnabled(connected and idle)
        self.cmb_chip.setEnabled(idle)
        self.algorithm_edit.setEnabled(idle)
        self.btn_bind_algorithm.setEnabled(idle)
        self.btn_opt_toggle.setEnabled(idle and self.current_chip is not None)
        self.chk_opt.setEnabled(idle and self.current_chip is not None)
        self.chk_quota.setEnabled(connected and idle)
        self.spin_quota.setEnabled(connected and idle and self.chk_quota.isChecked())
        self.btn_quota_save.setEnabled(connected and idle)
        self.btn_quota_read.setEnabled(connected and idle)

    def _set_import_state(self, active):
        self.import_in_progress = active
        self.remote_tree.setEnabled(not active)
        self.remote_path_input.setEnabled(not active)
        self._update_toolbar_state()

    # 启动后台保存/读取次数，暂时锁住其他USB操作，避免响应或文件传输互相干扰。
    def _start_quota(self, save=False):
        if not self.device.is_connected() or self.import_in_progress or self.quota_in_progress:
            return
        if self.worker is not None and self.worker.isRunning():
            self.statusBar().showMessage("请等待当前设备操作完成，再设置或读取次数。")
            return
        self.quota_in_progress = True
        self.remote_tree.setEnabled(False)
        self.remote_path_input.setEnabled(False)
        self._update_toolbar_state()
        setting = (self.chk_quota.isChecked(), self.spin_quota.value()) if save else None
        self.statusBar().showMessage("正在保存烧录次数..." if save else "正在读取剩余次数...")
        self.quota_worker = QuotaWorker(self.device, setting)
        self.quota_worker.finished.connect(lambda result, error: self._quota_done(result, error, save))
        self.quota_worker.start()

    # 恢复控件状态，显示三位剩余次数或错误；保存的新额度独立于导入程序。
    def _quota_done(self, result, error, saved):
        self.quota_in_progress = False
        self.remote_tree.setEnabled(True)
        self.remote_path_input.setEnabled(True)
        if error:
            self.lbl_quota.setText("剩余次数：读取或保存失败")
            self.statusBar().showMessage(error)
        else:
            self.chk_quota.setChecked(result['enabled'])
            if result['enabled']:
                self.spin_quota.setValue(result['remaining'])
            if result['fault']:
                self.lbl_quota.setText("次数记录异常：请重新设置并保存到设备")
            else:
                text = f"{result['remaining']:03d}次" if result['enabled'] else "不限"
                self.lbl_quota.setText("剩余次数：" + text)
            self.statusBar().showMessage("烧录次数已保存到设备，断电后保留。" if saved else "已读取设备剩余次数。")
        self._update_toolbar_state()

    def _on_connect(self):
        self.statusBar().showMessage("正在连接...")
        self.btn_connect.setEnabled(False)
        self.worker = ConnectWorker(self.device)
        self.worker.finished.connect(self._connect_done)
        self.worker.start()

    def _connect_done(self, ok, msg):
        if ok:
            self.lbl_status_icon.setStyleSheet("color: #2ecc71; font-size: 16px;")
            self.lbl_status_text.setText("已连接")
            self.statusBar().showMessage("设备已连接；可设置或读取烧录次数")
            self.lbl_quota.setText("剩余次数：请点击“读取剩余次数”")
            self._navigate_remote("/")
        else:
            self.lbl_status_icon.setStyleSheet("color: #e74c3c; font-size: 16px;")
            self.lbl_status_text.setText("未连接")
            self.statusBar().showMessage(f"连接失败: {msg}")
        self._update_toolbar_state()

    def _on_disconnect(self):
        self.device.disconnect()
        self.lbl_status_icon.setStyleSheet("color: #e74c3c; font-size: 16px;")
        self.lbl_status_text.setText("未连接")
        self.remote_tree.clear()
        self.statusBar().showMessage("已断开")
        self.lbl_quota.setText("剩余次数：设备未连接")
        self._update_toolbar_state()

    # ── Remote Navigation ──

    def _navigate_remote(self, path):
        if self.quota_in_progress or not self.device.is_connected():
            return
        self.current_remote_path = path
        self.lbl_path.setText(f"路径: {path}")
        self.remote_path_input.setText(path)
        self.statusBar().showMessage(f"正在列出 {path} ...")
        self.worker = ListDirWorker(self.device, path)
        self.worker.finished.connect(self._list_done)
        self.worker.start()

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
        if not self.device.is_connected():
            return
        files, _ = QFileDialog.getOpenFileNames(self, "Select Files to Upload")
        if not files:
            return
        for local_path in files:
            filename = os.path.basename(local_path)
            remote_path = self.current_remote_path.rstrip('/') + '/' + filename
            self._start_transfer('push', local_path, remote_path)

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

        self.worker = TransferWorker(self.device, mode, src, dst)
        self.worker.progress.connect(self._transfer_progress)
        self.worker.finished.connect(self._transfer_done)
        self.worker.start()

    def _transfer_progress(self, current, total):
        if total > 0:
            pct = current * 100 // total
            self.progress.setValue(pct)
            self.progress.setFormat(f"{self._format_size(current)} / {self._format_size(total)} ({pct}%)")

    def _transfer_done(self, nbytes, error):
        self.progress.setVisible(False)
        if error:
            self.statusBar().showMessage(f"传输失败: {error}")
            QMessageBox.warning(self, "错误", f"传输失败:\n{error}")
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
            self.worker = ActionWorker(self.device, 'mkdir', path)
            self.worker.finished.connect(self._action_done)
            self.worker.start()

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
            self.worker = ActionWorker(self.device, 'rm', path)
            self.worker.finished.connect(self._action_done)
            self.worker.start()

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
        self.opt_scroll.setVisible(visible)
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

    def _bind_algorithm(self):
        if not self.current_model:
            QMessageBox.warning(self, "Warning", "请先选择芯片型号。")
            return
        name = self.algorithm_edit.text().strip()
        if not self._valid_name(name, require_bin=True):
            QMessageBox.warning(self, "Algorithm Error", "算法文件名必须是 1-31 个 ASCII 字符的 .bin 文件名。")
            return
        self.opt_config.set_algorithm(self.current_model, name)
        self.algorithm_edit.setText(name)
        self.statusBar().showMessage(f"已保存 {self.current_model} 的算法绑定: {name}")

    def _on_chip_changed(self, model_name):
        self.current_model = model_name or ''
        self.algorithm_edit.setText(
            self.opt_config.get_algorithm(self.current_model) if self.current_model else '')
        if not model_name:
            self.current_chip = None
            self.chk_opt.setChecked(False)
            self._clear_opt_ui()
            self._update_toolbar_state()
            return
        self.current_chip = self.opt_config.get_chip(model_name)
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
        self.lbl_rdp_state.setFixedWidth(76)
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

            for row, field in enumerate(w.get('fields', [])):
                widget = self._build_field_widget(field, grp)
                widget.setMinimumHeight(self.control_height)
                label = QLabel(field['name'], grp)
                label.setFixedWidth(label_width)
                label.setMinimumHeight(self.control_height)
                grp_layout.addWidget(label, row, 0, Qt.AlignVCenter)
                grp_layout.addWidget(widget, row, 1)
                self._opt_widgets[field['name']] = widget
                # Connect change signals for live preview
                self._connect_opt_signal(widget)

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
                    self.lbl_rdp_state.setText("未加密")
                    self.lbl_rdp_state.setStyleSheet(
                        "color:#b03a2e; font-weight:bold; border:1px solid #e6b0aa;"
                        "border-radius:3px; background:#fdedec;")
                elif rdp == 0x807F:
                    self.lbl_rdp_state.setText("已加密")
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
            combo = QComboBox(parent)
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
        if self.quota_in_progress:
            return
        if not self.device.is_connected():
            QMessageBox.warning(self, "Warning", "请先连接设备。")
            return
        if not self.current_model:
            QMessageBox.warning(self, "Warning", "请先选择芯片型号。")
            return
        algorithm_name = self.opt_config.get_algorithm(self.current_model)
        if not algorithm_name:
            QMessageBox.warning(self, "Warning",
                                f"芯片 {self.current_model} 尚未绑定算法文件，请先在“算法文件”中填写并保存。")
            return
        if not self._valid_name(algorithm_name, require_bin=True):
            QMessageBox.warning(self, "Warning",
                                f"已保存的算法文件名无效: {algorithm_name}")
            return
        file_path, _ = QFileDialog.getOpenFileName(
            self, "选择程序文件", "",
            "Binary/Hex Files (*.bin *.hex);;Binary Files (*.bin);;Hex Files (*.hex);;All Files (*)")
        if not file_path:
            return

        # Convert .hex to .bin if needed
        if file_path.lower().endswith('.hex'):
            try:
                ih = IntelHex(file_path)
                dir_name = os.path.dirname(file_path)
                base_name = os.path.splitext(os.path.basename(file_path))[0]
                safe_name = re.sub(r'[^\x00-\x7F]', '', base_name).strip(' _-')
                if not safe_name:
                    safe_name = 'firmware'
                bin_path = os.path.join(dir_name, safe_name + '.bin')
                ih.tobinfile(bin_path)
                self.statusBar().showMessage(f"HEX -> BIN: {os.path.basename(bin_path)}")
                file_path = bin_path
            except Exception as e:
                QMessageBox.warning(self, "HEX Error", f"HEX 转换失败:\n{e}")
                return

        # Sanitize filename for FATFS compatibility
        file_path, renamed = self._safe_filename(file_path)
        if renamed:
            self.statusBar().showMessage(f"文件名已过滤: {os.path.basename(file_path)}")

        prog_name = os.path.basename(file_path)
        if not self._valid_name(prog_name, require_bin=True):
            QMessageBox.warning(self, "Warning",
                                f"程序文件名无效(需 1-31 个 ASCII 字符的 .bin): {prog_name}")
            return

        opt_path = None
        if self.chk_opt.isChecked():
            opt_path = self._generate_opt_bin(file_path)

        self.progress.setVisible(True)
        self.progress.setValue(0)
        self.statusBar().showMessage("正在导入...")
        self._set_import_state(True)

        self.worker = ImportWorker(self.device, file_path, algorithm_name, opt_path)
        self.worker.progress.connect(lambda step, cur, total: self._transfer_progress(cur, total))
        self.worker.log.connect(lambda msg: self.statusBar().showMessage(msg))
        self.worker.finished.connect(self._import_done)
        self.worker.start()

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
        if ok:
            # Device switches to offline mode and drops USB on purpose.
            self.device.disconnect()
            self.lbl_status_icon.setStyleSheet("color: red; font-size: 16px;")
            self.lbl_status_text.setText("Disconnected")
            self.remote_tree.clear()
            self.statusBar().showMessage("导入完成，设备已准备离线烧录。再次导入前请让设备回到在线模式。")
            QMessageBox.information(self, "导入完成",
                                    "程序与算法已导入，设备已切换到离线烧录选择页，光标停在“开始烧录”。\n"
                                    "在设备上按一下中键即可烧录。\n\n"
                                    "设备已按预期断开 USB。再次导入前请让它回到在线模式。")
        else:
            self.statusBar().showMessage(f"导入失败: {msg}")
            QMessageBox.warning(self, "导入错误", f"导入失败:\n{msg}")
        self._set_import_state(False)

    # ── Close ──

    def closeEvent(self, event):
        if self.quota_in_progress or self.import_in_progress:
            self.statusBar().showMessage("请等待设备操作完成后关闭窗口。")
            event.ignore()
            return
        self.device.disconnect()
        event.accept()


# ─── Entry Point ───────────────────────────────────────────────────────────────

def main():
    app = QApplication(sys.argv)
    app.setApplicationName("DAPLINK 脱机编程器")
    app.setStyle("Fusion")

    window = DAPLinkManager()
    window.show()

    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
