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
    QButtonGroup, QCheckBox, QGridLayout
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

    def _send_cmd(self, cmd_data):
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
        self._send_cmd(bytes([CMD_CLOSE_FILE]))
        return sent

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
    """Multi-step import: ensure dir -> push program -> push opt."""
    progress = pyqtSignal(str, int, int)  # step_name, current, total
    log = pyqtSignal(str)
    finished = pyqtSignal(bool, str)

    def __init__(self, device, program_path, opt_path=None):
        super().__init__()
        self.device = device
        self.program_path = program_path
        self.opt_path = opt_path

    def run(self):
        try:
            # Step 1: ensure remote directory
            try:
                self.device.mkdir("0:/download/bin/")
            except Exception:
                pass  # may already exist

            # Step 2: push program
            prog_name = os.path.basename(self.program_path)
            self.log.emit("Uploading program: " + prog_name)
            self.device.push(self.program_path, "0:/download/bin/" + prog_name, self._cb)

            # Step 3: push opt if present
            if self.opt_path and os.path.exists(self.opt_path):
                opt_name = os.path.basename(self.opt_path)
                self.log.emit("Uploading OPT: " + opt_name)
                self.device.push(self.opt_path, "0:/download/bin/" + opt_name, self._cb)

            self.finished.emit(True, "Import complete")
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
        self._opt_widgets = {}

        self.setWindowTitle("CMSIS-DAPLINK File Manager")
        self.setMinimumSize(900, 600)
        self.resize(1000, 650)

        self._init_ui()
        self._apply_style()
        self._update_toolbar_state()

    def _init_ui(self):
        # ── Toolbar ──
        self.toolbar = QToolBar("Main Toolbar")
        self.toolbar.setIconSize(QSize(20, 20))
        self.toolbar.setMovable(False)
        self.addToolBar(self.toolbar)

        self.btn_connect = QAction("Connect", self)
        self.btn_connect.triggered.connect(self._on_connect)
        self.toolbar.addAction(self.btn_connect)

        self.btn_disconnect = QAction("Disconnect", self)
        self.btn_disconnect.triggered.connect(self._on_disconnect)
        self.toolbar.addAction(self.btn_disconnect)

        self.toolbar.addSeparator()

        self.btn_upload = QAction("Upload", self)
        self.btn_upload.triggered.connect(self._on_upload)
        self.toolbar.addAction(self.btn_upload)

        self.btn_download = QAction("Download", self)
        self.btn_download.triggered.connect(self._on_download)
        self.toolbar.addAction(self.btn_download)

        self.toolbar.addSeparator()

        self.btn_mkdir = QAction("New Folder", self)
        self.btn_mkdir.triggered.connect(self._on_mkdir)
        self.toolbar.addAction(self.btn_mkdir)

        self.btn_delete = QAction("Delete", self)
        self.btn_delete.triggered.connect(self._on_delete)
        self.toolbar.addAction(self.btn_delete)

        self.toolbar.addSeparator()

        self.btn_refresh = QAction("Refresh", self)
        self.btn_refresh.triggered.connect(self._on_refresh)
        self.toolbar.addAction(self.btn_refresh)

        self.toolbar.addSeparator()

        self.btn_import = QAction("导入程序", self)
        self.btn_import.triggered.connect(self._on_import_program)
        self.toolbar.addAction(self.btn_import)

        # ── Central Widget ──
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # ── Connection Status ──
        self.status_frame = QFrame()
        self.status_frame.setFrameShape(QFrame.StyledPanel)
        status_layout = QVBoxLayout(self.status_frame)
        status_layout.setContentsMargins(10, 6, 10, 6)
        status_layout.setSpacing(2)
        # Row 1: status icon + text
        row1 = QHBoxLayout()
        self.lbl_status_icon = QLabel("●")
        self.lbl_status_icon.setStyleSheet("color: red; font-size: 16px;")
        self.lbl_status_text = QLabel("Disconnected")
        self.lbl_status_text.setFont(QFont("Segoe UI", 10))
        row1.addWidget(self.lbl_status_icon)
        row1.addWidget(self.lbl_status_text)
        row1.addStretch()
        status_layout.addLayout(row1)
        # Row 2: path
        self.lbl_path = QLabel("Path: /")
        self.lbl_path.setFont(QFont("Consolas", 9))
        self.lbl_path.setStyleSheet("color: #555;")
        self.lbl_path.setWordWrap(True)
        status_layout.addWidget(self.lbl_path)
        layout.addWidget(self.status_frame)

        # ── Chip & OPT Selection Bar ──
        self.chip_opt_frame = QFrame()
        self.chip_opt_frame.setFrameShape(QFrame.StyledPanel)
        chip_opt_layout = QVBoxLayout(self.chip_opt_frame)
        chip_opt_layout.setContentsMargins(8, 4, 8, 4)
        chip_opt_layout.setSpacing(4)

        # Row: chip model combo + OPT toggle
        chip_bar = QHBoxLayout()
        chip_bar.addWidget(QLabel("芯片型号："))
        self.cmb_chip = QComboBox()
        self.cmb_chip.setMinimumWidth(180)
        self.cmb_chip.addItem("")  # empty default
        for m in self.opt_config.get_chip_models():
            self.cmb_chip.addItem(m)
        self.cmb_chip.currentTextChanged.connect(self._on_chip_changed)
        chip_bar.addWidget(self.cmb_chip)
        chip_bar.addSpacing(8)
        self.btn_opt_toggle = QPushButton("OPT 配置 ▼")
        self.btn_opt_toggle.setCheckable(True)
        self.btn_opt_toggle.clicked.connect(self._toggle_opt_panel)
        chip_bar.addWidget(self.btn_opt_toggle)
        chip_bar.addStretch()
        chip_opt_layout.addLayout(chip_bar)

        # OPT config panel (scrollable, initially hidden)
        self.opt_scroll = QScrollArea()
        self.opt_scroll.setWidgetResizable(True)
        self.opt_scroll.setMaximumHeight(350)
        self.opt_scroll.setVisible(False)
        self.opt_content = QWidget()
        self.opt_scroll.setWidget(self.opt_content)
        chip_opt_layout.addWidget(self.opt_scroll)

        layout.addWidget(self.chip_opt_frame)

        # ── Splitter: Local + Remote ──
        splitter = QSplitter(Qt.Horizontal)

        # Local files
        local_frame = QFrame()
        local_frame.setFrameShape(QFrame.StyledPanel)
        local_layout = QVBoxLayout(local_frame)
        local_layout.setContentsMargins(4, 4, 4, 4)
        lbl_local = QLabel("Local Computer")
        lbl_local.setFont(QFont("Segoe UI", 10, QFont.Bold))
        local_layout.addWidget(lbl_local)

        self.local_tree = QTreeWidget()
        self.local_tree.setHeaderLabels(["Name", "Size", "Type"])
        self.local_tree.setRootIsDecorated(False)
        self.local_tree.setAlternatingRowColors(True)
        self.local_tree.header().setSectionResizeMode(0, QHeaderView.Stretch)
        self.local_tree.header().resizeSection(1, 80)
        self.local_tree.header().resizeSection(2, 60)
        self.local_tree.itemDoubleClicked.connect(self._on_local_double_click)
        local_layout.addWidget(self.local_tree)

        self.local_path_edit = QLabel("Select a folder...")
        self.local_path_edit.setFont(QFont("Consolas", 8))
        self.local_path_edit.setStyleSheet("color: #888; padding: 2px;")
        local_layout.addWidget(self.local_path_edit)

        btn_browse = QPushButton("Browse Folder...")
        btn_browse.clicked.connect(self._on_browse_local)
        local_layout.addWidget(btn_browse)

        splitter.addWidget(local_frame)

        # Remote files
        remote_frame = QFrame()
        remote_frame.setFrameShape(QFrame.StyledPanel)
        remote_layout = QVBoxLayout(remote_frame)
        remote_layout.setContentsMargins(4, 4, 4, 4)
        lbl_remote = QLabel("Device (SPI Flash)")
        lbl_remote.setFont(QFont("Segoe UI", 10, QFont.Bold))
        remote_layout.addWidget(lbl_remote)

        self.remote_tree = QTreeWidget()
        self.remote_tree.setHeaderLabels(["Name", "Size", "Type"])
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
        nav_layout.addWidget(QLabel("Path:"))
        self.remote_path_input = QLineEdit("/")
        self.remote_path_input.setFont(QFont("Consolas", 9))
        self.remote_path_input.returnPressed.connect(self._on_go_path)
        nav_layout.addWidget(self.remote_path_input, 1)
        btn_go = QPushButton("Go")
        btn_go.setFixedWidth(40)
        btn_go.clicked.connect(self._on_go_path)
        nav_layout.addWidget(btn_go)
        btn_up = QPushButton("Up")
        btn_up.setFixedWidth(40)
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
        self.statusBar().showMessage("Ready")

    def _apply_style(self):
        self.setStyleSheet("""
            QMainWindow { background: #f5f5f5; }
            QToolBar { background: #fff; border-bottom: 1px solid #ddd; spacing: 6px; padding: 4px; }
            QToolBar QToolButton { padding: 6px 12px; border: 1px solid #ccc; border-radius: 4px; background: #fafafa; }
            QToolBar QToolButton:hover { background: #e8f0fe; border-color: #4285f4; }
            QFrame { background: #fff; border-radius: 6px; }
            QTreeWidget { border: 1px solid #ddd; border-radius: 4px; font-size: 12px; }
            QTreeWidget::item { padding: 3px; }
            QTreeWidget::item:selected { background: #e8f0fe; color: #000; }
            QHeaderView::section { background: #f0f0f0; border: none; border-bottom: 1px solid #ddd; padding: 4px; font-weight: bold; }
            QProgressBar { border: 1px solid #ccc; border-radius: 4px; text-align: center; height: 22px; }
            QProgressBar::chunk { background: #4285f4; border-radius: 3px; }
            QPushButton { padding: 6px 16px; border: 1px solid #ccc; border-radius: 4px; background: #fafafa; }
            QPushButton:hover { background: #e8f0fe; border-color: #4285f4; }
        """)

    def _update_toolbar_state(self):
        connected = self.device.is_connected()
        self.btn_connect.setEnabled(not connected)
        self.btn_disconnect.setEnabled(connected)
        self.btn_upload.setEnabled(connected)
        self.btn_download.setEnabled(connected)
        self.btn_mkdir.setEnabled(connected)
        self.btn_delete.setEnabled(connected)
        self.btn_refresh.setEnabled(connected)
        self.btn_import.setEnabled(connected)

    # ── Connection ──

    def _on_connect(self):
        self.statusBar().showMessage("Connecting...")
        self.btn_connect.setEnabled(False)
        self.worker = ConnectWorker(self.device)
        self.worker.finished.connect(self._connect_done)
        self.worker.start()

    def _connect_done(self, ok, msg):
        if ok:
            self.lbl_status_icon.setStyleSheet("color: #34a853; font-size: 16px;")
            self.lbl_status_text.setText("Connected")
            self.statusBar().showMessage("Device connected")
            self._navigate_remote("/")
        else:
            self.lbl_status_icon.setStyleSheet("color: red; font-size: 16px;")
            self.lbl_status_text.setText("Disconnected")
            self.statusBar().showMessage(f"Connection failed: {msg}")
        self._update_toolbar_state()

    def _on_disconnect(self):
        self.device.disconnect()
        self.lbl_status_icon.setStyleSheet("color: red; font-size: 16px;")
        self.lbl_status_text.setText("Disconnected")
        self.remote_tree.clear()
        self.statusBar().showMessage("Disconnected")
        self._update_toolbar_state()

    # ── Remote Navigation ──

    def _navigate_remote(self, path):
        if not self.device.is_connected():
            return
        self.current_remote_path = path
        self.lbl_path.setText(f"Path: {path}")
        self.remote_path_input.setText(path)
        self.statusBar().showMessage(f"Listing {path}...")
        self.worker = ListDirWorker(self.device, path)
        self.worker.finished.connect(self._list_done)
        self.worker.start()

    def _list_done(self, entries, error):
        self.remote_tree.clear()
        if error:
            self.statusBar().showMessage(f"Error: {error}")
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

        self.statusBar().showMessage(f"{len(entries)} items in {self.current_remote_path}")

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
            QMessageBox.information(self, "Info", "Select a file on the device first.")
            return
        entry_type, name = item.data(0, Qt.UserRole)
        if entry_type != 'FILE':
            QMessageBox.information(self, "Info", "Select a file (not a directory).")
            return

        save_path, _ = QFileDialog.getSaveFileName(self, "Save As", name)
        if not save_path:
            return
        remote_path = self.current_remote_path.rstrip('/') + '/' + name
        self._start_transfer('pull', remote_path, save_path)

    def _start_transfer(self, mode, src, dst):
        self.progress.setVisible(True)
        self.progress.setValue(0)
        name = os.path.basename(src if mode == 'push' else dst)
        direction = "Uploading" if mode == 'push' else "Downloading"
        self.statusBar().showMessage(f"{direction} {name}...")

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
            self.statusBar().showMessage(f"Transfer failed: {error}")
            QMessageBox.warning(self, "Error", f"Transfer failed:\n{error}")
        else:
            self.statusBar().showMessage(f"Transfer complete: {self._format_size(nbytes)}")
            self._navigate_remote(self.current_remote_path)

    # ── Mkdir / Delete ──

    def _on_mkdir(self):
        if not self.device.is_connected():
            return
        name, ok = QInputDialog.getText(self, "New Folder", "Folder name:")
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
        reply = QMessageBox.question(self, "Confirm Delete", f"Delete '{name}'?",
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
            act_download = menu.addAction("Download")
            act_download.triggered.connect(self._on_download)
        act_delete = menu.addAction("Delete")
        act_delete.triggered.connect(self._on_delete)
        menu.exec_(self.remote_tree.viewport().mapToGlobal(pos))

    def _action_done(self, ok, msg):
        if ok:
            self.statusBar().showMessage("Done")
        else:
            self.statusBar().showMessage(f"Error: {msg}")
        self._navigate_remote(self.current_remote_path)

    # ── OPT Configuration ──

    def _toggle_opt_panel(self):
        visible = self.btn_opt_toggle.isChecked()
        self.opt_scroll.setVisible(visible)
        self.btn_opt_toggle.setText("OPT 配置 ▲" if visible else "OPT 配置 ▼")

    def _on_chip_changed(self, model_name):
        if not model_name:
            self.current_chip = None
            self._clear_opt_ui()
            return
        self.current_chip = self.opt_config.get_chip(model_name)
        self._build_opt_ui()
        # auto-expand panel when chip selected
        if not self.opt_scroll.isVisible():
            self.btn_opt_toggle.setChecked(True)
            self._toggle_opt_panel()

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
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        # OPT binary preview (at top, always visible)
        preview_grp = QGroupBox("OPT Bin 预览")
        preview_layout = QHBoxLayout(preview_grp)
        self.lbl_opt_preview = QLineEdit()
        self.lbl_opt_preview.setReadOnly(True)
        self.lbl_opt_preview.setFont(QFont("Consolas", 10))
        self.lbl_opt_preview.setStyleSheet("background: #f0f0f0; color: #333;")
        preview_layout.addWidget(self.lbl_opt_preview)
        layout.addWidget(preview_grp)

        for w in self.current_chip.get('opt_words', []):
            grp = QGroupBox(f"{w['name']}  ({w['address']})")
            grp_layout = QVBoxLayout(grp)
            grp_layout.setSpacing(2)

            for field in w.get('fields', []):
                widget = self._build_field_widget(field)
                label = QLabel(field['name'])
                label.setMinimumWidth(120)
                row = QHBoxLayout()
                row.addWidget(label)
                row.addWidget(widget)
                row.addStretch()
                grp_layout.addLayout(row)
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
            words = self.current_chip['opt_words']
            addresses = [int(w['address'], 16) for w in words]
            base_addr = min(addresses)
            end_addr = max(addresses) + 2
            buf = bytearray([0xFF] * (end_addr - base_addr))

            for w in words:
                offset = int(w['address'], 16) - base_addr
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

                high_byte = (~low_byte) & 0xFF
                value = (high_byte << 8) | low_byte
                buf[offset:offset + 2] = struct.pack('<H', value)

            hex_str = ' '.join(f'{b:02X}' for b in buf)
            self.lbl_opt_preview.setText(hex_str)
        except Exception:
            pass

    def _build_field_widget(self, field):
        options = field.get('options', [])
        default = field.get('default')

        if field.get('byte'):
            combo = QComboBox()
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
                container = QWidget()
                hbox = QHBoxLayout(container)
                hbox.setContentsMargins(0, 0, 0, 0)
                hbox.setSpacing(8)
                group = QButtonGroup(container)
                for opt in options:
                    radio = QRadioButton(opt['label'], container)
                    group.addButton(radio, opt['value'])
                    hbox.addWidget(radio)
                    if default is not None and opt['value'] == default:
                        radio.setChecked(True)
                container._btn_group = group
                return container
            else:
                cb = QCheckBox()
                if default is not None:
                    cb.setChecked(default != 0)
                return cb

        return QLabel("")

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
        if not self.device.is_connected():
            QMessageBox.warning(self, "Warning", "请先连接设备。")
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

        opt_path = None
        if self.current_chip:
            opt_path = self._generate_opt_bin(file_path)

        self.progress.setVisible(True)
        self.progress.setValue(0)
        self.statusBar().showMessage("Importing...")

        self.worker = ImportWorker(self.device, file_path, opt_path)
        self.worker.progress.connect(lambda step, cur, total: self._transfer_progress(cur, total))
        self.worker.log.connect(lambda msg: self.statusBar().showMessage(msg))
        self.worker.finished.connect(self._import_done)
        self.worker.start()

    def _generate_opt_bin(self, program_path):
        chip = self.current_chip
        words = chip['opt_words']

        addresses = [int(w['address'], 16) for w in words]
        base_addr = min(addresses)
        end_addr = max(addresses) + 2
        buf = bytearray([0xFF] * (end_addr - base_addr))

        for w in words:
            offset = int(w['address'], 16) - base_addr

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

            high_byte = (~low_byte) & 0xFF
            value = (high_byte << 8) | low_byte
            buf[offset:offset + 2] = struct.pack('<H', value)

        base_name = os.path.splitext(program_path)[0]
        opt_path = base_name + '_opt.bin'
        with open(opt_path, 'wb') as f:
            f.write(buf)

        return opt_path

    def _import_done(self, ok, msg):
        self.progress.setVisible(False)
        if ok:
            self.statusBar().showMessage("Import complete: " + msg)
            self._navigate_remote(self.current_remote_path)
        else:
            self.statusBar().showMessage(f"Import failed: {msg}")
            QMessageBox.warning(self, "Import Error", f"Import failed:\n{msg}")

    # ── Close ──

    def closeEvent(self, event):
        self.device.disconnect()
        event.accept()


# ─── Entry Point ───────────────────────────────────────────────────────────────

def main():
    app = QApplication(sys.argv)
    app.setApplicationName("DAPLINK File Manager")
    app.setStyle("Fusion")

    window = DAPLinkManager()
    window.show()

    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
