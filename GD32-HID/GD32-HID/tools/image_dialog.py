#!/usr/bin/env python3
"""镜像任务对话框：读取/下载/切换/删除 100 槽镜像。"""
import os

from PyQt5.QtCore import Qt, QThread, pyqtSignal
from PyQt5.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox, QLabel,
    QPushButton, QLineEdit, QComboBox, QSpinBox, QCheckBox, QFileDialog,
    QTableWidget, QTableWidgetItem, QAbstractItemView, QHeaderView,
    QMessageBox, QProgressBar, QTextEdit, QSplitter, QWidget,
)

from image_tool import (
    IMAGE_SLOTS, IMAGE_ID_NONE, IMAGE_REC_VALID, IMAGE_REC_OPT,
    IMAGE_FW_MAX, unpack_record, commit_detail_text, ImageSession,
)


class ImageWorker(QThread):
    progress = pyqtSignal(str, int, int)
    log = pyqtSignal(str)
    finished_ok = pyqtSignal(bool, str, object)

    def __init__(self, device, mode, kwargs=None):
        super().__init__()
        self.device = device
        self.mode = mode
        self.kwargs = kwargs or {}

    def run(self):
        session = ImageSession(self.device, log=self.log.emit, progress=self.progress.emit)
        try:
            if self.mode == "read":
                result = session.read_table()
            elif self.mode == "download":
                result = session.download(**self.kwargs)
            elif self.mode == "switch":
                result = session.switch(self.kwargs["slot"])
            elif self.mode == "delete":
                result = session.delete(self.kwargs["slot"])
            elif self.mode == "abort":
                result = session.abort()
            else:
                raise ValueError(self.mode)
            self.finished_ok.emit(True, "完成", result)
        except Exception as exc:
            self.finished_ok.emit(False, str(exc), None)


class ImageTaskDialog(QDialog):
    def __init__(self, device, opt_config, parent=None, auto_refresh=True):
        super().__init__(parent)
        self.device = device
        self.opt_config = opt_config
        self.info = {}
        self.records = []
        self.worker = None
        self.last_mode = ""
        self.setWindowTitle("镜像任务（100 槽）")
        self.resize(940, 660)
        self._build_ui()
        if auto_refresh:
            self._refresh()

    # ── UI ──────────────────────────────────────────────────────────────
    def _build_ui(self):
        root = QVBoxLayout(self)
        split = QSplitter(Qt.Horizontal)

        self.table = QTableWidget(IMAGE_SLOTS, 5)
        self.table.setHorizontalHeaderLabels(["号", "说明/固件", "芯片", "算法", "状态"])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SingleSelection)
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeToContents)
        self.table.verticalHeader().setDefaultSectionSize(20)
        self.table.currentCellChanged.connect(self._on_row_selected)
        split.addWidget(self.table)

        side = QWidget()
        side_layout = QVBoxLayout(side)
        side_layout.setContentsMargins(6, 0, 0, 0)

        self.lbl_info = QLabel("镜像表：读取中…")
        self.lbl_info.setWordWrap(True)
        side_layout.addWidget(self.lbl_info)

        form_box = QGroupBox("镜像内容")
        form = QFormLayout(form_box)
        self.spin_slot = QSpinBox()
        self.spin_slot.setRange(0, IMAGE_SLOTS - 1)
        self.spin_slot.valueChanged.connect(self._on_slot_spin)
        form.addRow("镜像号", self.spin_slot)

        self.cmb_chip = QComboBox()
        for model in self.opt_config.get_chip_models():
            self.cmb_chip.addItem(model)
        self.cmb_chip.currentTextChanged.connect(self._update_algorithm)
        form.addRow("芯片型号", self.cmb_chip)

        self.lbl_algorithm = QLabel("")
        form.addRow("算法文件", self.lbl_algorithm)

        fw_row = QHBoxLayout()
        self.edit_firmware = QLineEdit()
        self.edit_firmware.setPlaceholderText("选择本地 .bin（≤100KB）")
        self.btn_browse = QPushButton("浏览…")
        self.btn_browse.clicked.connect(self._browse_firmware)
        fw_row.addWidget(self.edit_firmware, 1)
        fw_row.addWidget(self.btn_browse)
        form.addRow("主固件", fw_row)

        self.chk_encrypt = QCheckBox("写入 OPT（读保护/加密）")
        self.chk_encrypt.setChecked(True)
        form.addRow("OPT", self.chk_encrypt)

        self.edit_desc = QLineEdit()
        self.edit_desc.setMaxLength(23)
        self.edit_desc.setPlaceholderText("ASCII 短说明，可空（≤23 字符）")
        form.addRow("镜像说明", self.edit_desc)

        quota_row = QHBoxLayout()
        self.chk_quota = QCheckBox("限制次数")
        self.spin_quota = QSpinBox()
        self.spin_quota.setRange(0, 100000)
        self.spin_quota.setValue(100000)
        self.spin_quota.setSuffix(" 次")
        self.spin_quota.setEnabled(False)
        self.chk_quota.toggled.connect(self.spin_quota.setEnabled)
        quota_row.addWidget(self.chk_quota)
        quota_row.addWidget(self.spin_quota)
        quota_row.addStretch()
        form.addRow("烧录次数", quota_row)

        self.chk_switch = QCheckBox("提交后切换为当前镜像")
        self.chk_switch.setChecked(True)
        form.addRow("", self.chk_switch)
        side_layout.addWidget(form_box)

        btn_row = QHBoxLayout()
        self.btn_download = QPushButton("下载到此镜像号")
        self.btn_switch = QPushButton("仅切换")
        self.btn_delete = QPushButton("删除镜像")
        self.btn_abort = QPushButton("取消/恢复")
        self.btn_refresh = QPushButton("刷新")
        self.btn_download.clicked.connect(self._download)
        self.btn_switch.clicked.connect(self._switch)
        self.btn_delete.clicked.connect(self._delete)
        self.btn_abort.clicked.connect(self._abort)
        self.btn_refresh.clicked.connect(self._refresh)
        for btn in (self.btn_download, self.btn_switch, self.btn_delete,
                    self.btn_abort, self.btn_refresh):
            btn_row.addWidget(btn)
        side_layout.addLayout(btn_row)

        self.progress = QProgressBar()
        self.progress.setVisible(False)
        side_layout.addWidget(self.progress)
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMaximumHeight(120)
        side_layout.addWidget(self.txt_log)
        side_layout.addStretch()
        split.addWidget(side)
        split.setSizes([560, 360])
        root.addWidget(split)
        self._update_algorithm()

    def _update_algorithm(self):
        model = self.cmb_chip.currentText()
        algo = self.opt_config.get_algorithm(model) if model else ""
        self.lbl_algorithm.setText(algo or "（该芯片没有配套算法）")

    def _browse_firmware(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择主固件", "", "BIN 文件 (*.bin);;所有文件 (*)")
        if path:
            self.edit_firmware.setText(path)
            current = self.edit_desc.text().strip()
            if not current or current == "IMAGE 000":
                base = os.path.splitext(os.path.basename(path))[0]
                try:
                    base.encode("ascii")
                    if 0 < len(base) <= 23:
                        self.edit_desc.setText(base)
                except UnicodeEncodeError:
                    pass

    # ── Table ───────────────────────────────────────────────────────────
    def _fill_table(self):
        self.table.blockSignals(True)
        current = self.info.get("current", IMAGE_ID_NONE)
        for i in range(IMAGE_SLOTS):
            rec = unpack_record(self.records[i]) if i < len(self.records) else None
            if rec and rec["valid"]:
                desc = rec["desc"]
                if not desc or desc == "IMAGE 000":  # 首启迁移占位说明不显示
                    desc = rec["bin"]
                    prefix = f"{i:03d}_"
                    if desc.startswith(prefix):
                        desc = desc[len(prefix):]
                values = [f"{i:03d}", desc, rec["chip"], rec["algo"],
                          "当前 ●" if i == current else "有效"]
            else:
                values = [f"{i:03d}", "", "", "", "空"]
            for col, text in enumerate(values):
                item = QTableWidgetItem(text)
                if col == 0:
                    item.setTextAlignment(Qt.AlignCenter)
                if col == 4:
                    item.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(i, col, item)
        self.table.blockSignals(False)
        row = current if 0 <= current < IMAGE_SLOTS else 0
        self.table.selectRow(row)
        seq = self.info.get("seq", 0)
        valid = bool(self.info.get("flags", 0) & 1)
        self.lbl_info.setText(
            f"镜像表：{'有效' if valid else '空/不可用'}  seq={seq}  "
            f"当前={('-' if current >= IMAGE_SLOTS else f'{current:03d}')}")

    def _on_slot_spin(self, value):
        if 0 <= value < self.table.rowCount():
            self.table.setCurrentCell(value, 0)

    def _on_row_selected(self, row, _col, _prev_row, _prev_col):
        if row < 0 or row >= len(self.records):
            return
        rec = unpack_record(self.records[row])
        self.spin_slot.setValue(row)
        self.edit_desc.setText(rec["desc"])
        self.chk_encrypt.setChecked(bool(rec["flags"] & IMAGE_REC_OPT))
        if rec["algo"]:
            for idx in range(self.cmb_chip.count()):
                model = self.cmb_chip.itemText(idx)
                if self.opt_config.get_algorithm(model) == rec["algo"]:
                    self.cmb_chip.setCurrentIndex(idx)
                    break
        elif rec["chip"]:
            idx = self.cmb_chip.findText(rec["chip"])
            if idx >= 0:
                self.cmb_chip.setCurrentIndex(idx)

    # ── Worker plumbing ─────────────────────────────────────────────────
    def _set_busy(self, busy):
        for w in (self.table, self.spin_slot, self.cmb_chip, self.edit_firmware,
                  self.btn_browse, self.chk_encrypt, self.edit_desc, self.chk_quota,
                  self.spin_quota, self.chk_switch, self.btn_download, self.btn_switch,
                  self.btn_delete, self.btn_abort, self.btn_refresh):
            w.setEnabled(not busy)
        if not busy:
            self.spin_quota.setEnabled(self.chk_quota.isChecked())
        self.progress.setVisible(busy)
        if busy:
            self.progress.setRange(0, 0)

    def _refresh(self):
        if self.worker is not None:
            return
        self._start_worker("read")

    def _start_worker(self, mode, **kwargs):
        if self.worker is not None:
            return
        self.last_mode = mode
        self._set_busy(True)
        if mode != "read":
            self.txt_log.append(f"──── {mode} ────")
        self.worker = ImageWorker(self.device, mode, kwargs)
        self.worker.progress.connect(self._on_progress)
        self.worker.log.connect(self._log)
        self.worker.finished_ok.connect(self._on_worker_done)
        self.worker.start()

    def _on_progress(self, step, cur, total):
        if total:
            self.progress.setRange(0, total)
            self.progress.setValue(cur)
        self.progress.setFormat(f"{step}  %v/%m")

    def _log(self, msg):
        self.txt_log.append(msg)

    def _on_worker_done(self, ok, msg, result):
        mode = self.last_mode
        self.worker = None
        self._set_busy(False)
        if not ok:
            self.lbl_info.setText("失败：" + msg)
            QMessageBox.warning(self, "镜像任务失败", msg)
            if mode != "read":
                self._refresh()
            return
        if mode == "read":
            self.info, self.records = result
            self._fill_table()
            self.lbl_info.setText(self.lbl_info.text())  # keep table summary
        else:
            self._log("完成")
            self._refresh()

    # ── Actions ─────────────────────────────────────────────────────────
    def _download(self):
        path = self.edit_firmware.text().strip()
        if not path or not os.path.isfile(path):
            QMessageBox.warning(self, "缺少固件", "请选择主固件 .bin 文件")
            return
        size = os.path.getsize(path)
        if not 0 < size <= IMAGE_FW_MAX:
            QMessageBox.warning(self, "固件过大", f"每槽上限 {IMAGE_FW_MAX // 1024}KB")
            return
        try:
            desc = self.edit_desc.text().strip()
            desc.encode("ascii")
        except UnicodeEncodeError:
            QMessageBox.warning(self, "说明无效", "镜像说明只能使用 ASCII 字符")
            return
        slot = self.spin_slot.value()
        model = self.cmb_chip.currentText()
        chip_cfg = self.opt_config.get_chip(model) if model else None
        algorithm = self.opt_config.get_algorithm(model) if model else ""
        if not chip_cfg or not algorithm:
            QMessageBox.warning(self, "芯片无效", "请选择有配套算法的芯片型号")
            return
        if slot < len(self.records):
            rec = unpack_record(self.records[slot])
            if rec["valid"]:
                quota = None
                try:
                    quota = self.device.quota_peek(rec["bin"])
                except Exception:
                    pass
                extra = ""
                if quota and quota.get("enabled"):
                    extra = f"\n该镜像剩余次数：{quota['remaining']} 次。"
                if QMessageBox.question(
                        self, "覆盖镜像",
                        f"镜像 {slot:03d} 已有任务：\n{rec['desc'] or rec['bin']}{extra}\n\n覆盖？",
                        QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
                    return
        self._start_worker(
            "download", slot=slot, local_path=path, chip_model=model,
            chip_cfg=chip_cfg, algorithm=algorithm,
            encrypt=self.chk_encrypt.isChecked(), desc=desc,
            quota_enabled=self.chk_quota.isChecked(),
            quota_count=self.spin_quota.value(),
            switch=self.chk_switch.isChecked())

    def _switch(self):
        slot = self.spin_slot.value()
        if QMessageBox.question(self, "切换镜像", f"切换当前镜像为 {slot:03d}？",
                                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        self._start_worker("switch", slot=slot)

    def _delete(self):
        slot = self.spin_slot.value()
        if QMessageBox.question(self, "删除镜像",
                                f"删除镜像 {slot:03d}？\n固件文件与次数账本会保留。",
                                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        self._start_worker("delete", slot=slot)

    def _abort(self):
        if QMessageBox.question(self, "取消/恢复", "删除暂存表并恢复当前镜像？",
                                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return
        self._start_worker("abort")

    def closeEvent(self, event):
        if self.worker is not None:
            QMessageBox.information(self, "请稍候", "镜像事务进行中，完成后再关闭。")
            event.ignore()
            return
        super().closeEvent(event)
