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
    QLineEdit
)
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QSize
from PyQt5.QtGui import QIcon, QFont, QColor

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


# ─── Main Window ───────────────────────────────────────────────────────────────

class DAPLinkManager(QMainWindow):
    def __init__(self):
        super().__init__()
        self.device = DAPLinkDevice()
        self.current_remote_path = "/"
        self.worker = None

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
