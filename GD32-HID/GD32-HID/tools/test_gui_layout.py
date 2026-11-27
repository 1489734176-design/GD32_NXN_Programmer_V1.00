#!/usr/bin/env python3
"""Native Qt layout/interaction checks; never connects to USB or writes bindings.

Run with the tools Python 3.12 venv. Optional: --screenshots <directory>.
Repeat with QT_SCALE_FACTOR=1.25 or 1.5 to check scaled layouts.
"""
import argparse
from pathlib import Path
from unittest.mock import patch

# Qt 插件路径修复（必须在 import PyQt5 之前）：工程路径含中文时 Qt 的 ANSI 路径解析
# 会变成 "???"，导致 "no Qt platform plugin could be initialized"。
import os as _os
def _fix_qt_plugin_path():
    try:
        import PyQt5 as _pyqt5
        _pkg = _os.path.dirname(_os.path.abspath(_pyqt5.__file__))
        _plugins = _os.path.join(_pkg, "Qt5", "plugins")
        _platforms = _os.path.join(_plugins, "platforms")
        if _os.path.isdir(_platforms):
            _os.environ["QT_PLUGIN_PATH"] = _plugins
            _os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = _platforms
    except Exception:
        pass

_fix_qt_plugin_path()

from PyQt5.QtCore import QEvent, QPoint, QPointF, Qt
from PyQt5.QtGui import QFont, QMouseEvent, QPalette, QWheelEvent
from PyQt5.QtTest import QTest
from PyQt5.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QGroupBox, QLabel, QSplitter,
    QStyle, QStyleOptionGroupBox, QWidget,
)

from daplink_manager import DAPLinkManager


def settle():
    QApplication.processEvents()
    QTest.qWait(40)


def check_layout(window):
    controls = (window.cmb_chip, window.btn_opt_toggle, window.chk_opt)
    assert window.cmb_chip.height() == window.btn_opt_toggle.height() == window.control_height
    assert window.cmb_chip.y() == window.btn_opt_toggle.y()
    for i, control in enumerate(controls):
        for other in controls[i + 1:]:
            assert not control.geometry().intersects(other.geometry())
        assert window.chip_opt_frame.rect().contains(control.geometry())

    for control in controls:
        assert window.chk_channel_b.y() > control.geometry().bottom()
    assert window.chk_channel_b.geometry().bottom() < window.chk_quota.y()
    assert window.chk_quota.geometry().bottom() < window.cmb_image.y()
    assert window.cmb_image.geometry().center().y() == window.edit_image_desc.geometry().center().y()
    assert window.cmb_image.geometry().right() < window.edit_image_desc.x()
    assert window.edit_image_desc.geometry().bottom() < window.opt_scroll.y()
    assert window.chip_opt_frame.rect().contains(window.opt_scroll.geometry())
    assert window.centralWidget().rect().contains(window.chip_opt_frame.geometry())
    groups = window.opt_content.findChildren(QGroupBox)
    previous_bottom = -1
    for group in groups:
        assert group.y() > previous_bottom
        previous_bottom = group.geometry().bottom()
        assert group.font().pointSize() == window.font().pointSize()
        option = QStyleOptionGroupBox()
        group.initStyleOption(option)
        title_rect = group.style().subControlRect(
            QStyle.CC_GroupBox, option, QStyle.SC_GroupBoxLabel, group)
        radio_columns = None
        for i in range(group.layout().count()):
            child = group.layout().itemAt(i).widget()
            if child is None:
                continue
            assert child.y() > title_rect.bottom(), group.title()
            assert group.rect().contains(child.geometry()), group.title()
            assert child.height() >= window.control_height
            if isinstance(child, QLabel):
                assert child.width() >= child.fontMetrics().horizontalAdvance(child.text())
            if hasattr(child, '_btn_group'):
                buttons = child._btn_group.buttons()
                columns = tuple(sorted(button.mapTo(group, QPoint()).x() for button in buttons))
                assert len(columns) == 2 and columns[0] < columns[1]
                if radio_columns is None:
                    radio_columns = columns
                assert columns == radio_columns, (group.title(), columns, radio_columns)
                for j, button in enumerate(buttons):
                    assert button.width() >= button.minimumSizeHint().width()
                    assert child.rect().contains(button.geometry())
                    for other in buttons[j + 1:]:
                        assert not button.geometry().intersects(other.geometry())

    bar = window.opt_scroll.verticalScrollBar()
    assert bar.maximum() > 0
    preview = window.lbl_opt_preview
    position = preview.mapTo(window, QPoint())
    bar.setValue(bar.maximum())
    settle()
    assert preview.mapTo(window, QPoint()) == position, 'OPT 预览不能随配置列表滚走'
    assert preview.visibleRegion().contains(preview.rect().center())
    assert window.lbl_rdp_state.visibleRegion().contains(window.lbl_rdp_state.rect().center())
    last = groups[-1]
    bottom = last.mapTo(window.opt_scroll.viewport(), last.rect().bottomLeft())
    assert bottom.y() < window.opt_scroll.viewport().height()
    field = window._opt_widgets['WRP0_bit0']
    before = bytes.fromhex(preview.text())
    assert field.visibleRegion().contains(field.rect().center())
    field.click()
    after = bytes.fromhex(preview.text())
    assert after == before[:-2] + bytes(value ^ 1 for value in before[-2:])
    assert bar.value() == bar.maximum() and preview.mapTo(window, QPoint()) == position
    field.click()
    assert bytes.fromhex(preview.text()) == before
    bar.setValue(0)


def check_rpo_wheel(window):
    combo = window._opt_widgets['RPO']
    bar = window.opt_scroll.verticalScrollBar()
    native = QApplication.platformName() == 'windows'
    for focused, index, delta in ((True, 0, -120), (False, 1, 120)):
        combo.setCurrentIndex(index)
        (combo if focused else window.cmb_chip).setFocus()
        window.opt_scroll.ensureWidgetVisible(combo)
        pos = combo.rect().center()
        QTest.mouseMove(combo, pos)
        settle()
        assert combo.hasFocus() == focused
        before = bar.value()
        expected_bytes = window.lbl_opt_preview.text()
        if native:
            # sendEvent(widget, wheel) is non-spontaneous: Qt doesn't bubble it to its scroll area.
            import ctypes
            from ctypes import wintypes
            pointer = wintypes.POINT()
            user32 = ctypes.windll.user32
            assert user32.GetCursorPos(ctypes.byref(pointer))
            assert QApplication.widgetAt(combo.mapToGlobal(pos)) is combo
            xy = ((pointer.y & 0xffff) << 16) | (pointer.x & 0xffff)
            user32.SendMessageW(wintypes.HWND(int(window.winId())), 0x020A,  # WM_MOUSEWHEEL
                                wintypes.WPARAM((delta & 0xffff) << 16), wintypes.LPARAM(xy))
        else:
            event = QWheelEvent(QPointF(pos), QPointF(combo.mapToGlobal(pos)), QPoint(),
                                QPoint(0, delta), Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase, False)
            QApplication.sendEvent(combo, event)
            assert not event.isAccepted()
        settle()
        assert combo.currentIndex() == index and window.lbl_opt_preview.text() == expected_bytes
        if native:
            assert (bar.value() - before) * delta < 0, (before, bar.value(), delta)
    combo.setCurrentIndex(0)


def check_splitters(window):
    splitters = window.findChildren(QSplitter)
    assert {s.orientation() for s in splitters} == {Qt.Vertical, Qt.Horizontal}
    for splitter in splitters:
        vertical = splitter.orientation() == Qt.Vertical
        handle = splitter.handle(1)
        assert handle.cursor().shape() == (Qt.SplitVCursor if vertical else Qt.SplitHCursor)
        assert handle.isVisible()
        sizes = splitter.sizes()
        pos = handle.rect().center()
        end = handle.mapToGlobal(pos) + (QPoint(0, 24) if vertical else QPoint(24, 0))
        QTest.mousePress(handle, Qt.LeftButton, pos=pos)
        move = QMouseEvent(QEvent.MouseMove, QPointF(handle.mapFromGlobal(end)), QPointF(end),
                           Qt.NoButton, Qt.LeftButton, Qt.NoModifier)
        QApplication.sendEvent(handle, move)
        QTest.mouseRelease(handle, Qt.LeftButton, pos=handle.mapFromGlobal(end))
        settle()
        first, second = splitter.sizes()
        assert first > sizes[0] and second < sizes[1], (sizes, splitter.sizes())
        splitter.setSizes(sizes)
        settle()


def check_brand_and_quota(window):
    assert window.windowTitle() == 'NX Writer'
    labels = [label.text() for label in window.banner.findChildren(QLabel)]
    assert 'NX Writer' in labels and not any('CMSIS' in text for text in labels)
    assert not window.windowIcon().pixmap(32, 32).isNull()
    assert [window.cmb_chip.itemText(i) for i in range(1, window.cmb_chip.count())] == ['NX32F001T', 'NX32F002TS']
    assert all('算法' not in w.text() for w in window.chip_opt_frame.findChildren(QLabel))
    for control in (window.chk_quota, window.spin_quota, window.cmb_image, window.edit_image_desc):
        assert window.chip_opt_frame.isAncestorOf(control)
    assert window.cmb_image.count() == 101 and window.cmb_image.currentData() is None
    assert not window.edit_image_desc.isEnabled()
    for slot in (0, 99):
        window.cmb_image.setCurrentIndex(window.cmb_image.findData(slot))
        assert window.cmb_image.currentText() == f'{slot:03d}' and window.edit_image_desc.isEnabled()
    window.cmb_image.setCurrentIndex(0)
    assert not window.device.is_connected() and window.chk_quota.isEnabled()
    assert not window.chk_quota.isChecked() and not window.spin_quota.isEnabled()
    QTest.mouseClick(window.chk_quota, Qt.LeftButton)
    assert window.spin_quota.isEnabled()
    spin = window.spin_quota
    spin.selectAll()
    QTest.keyClicks(spin, '123')
    QTest.keyClick(spin, Qt.Key_Return)
    assert spin.value() == 123
    QTest.mouseClick(window.chk_quota, Qt.LeftButton)
    assert not spin.isEnabled()


def check_preview(window):
    for word in window.current_chip['opt_words']:
        for field in word.get('fields', []):
            assert window._get_field_value(field['name']) == field['default']
            widget = window._opt_widgets[field['name']]
            before = window.lbl_opt_preview.text()
            if isinstance(widget, QComboBox) and widget.count() > 1:
                widget.setCurrentIndex((widget.currentIndex() + 1) % widget.count())
            elif isinstance(widget, QCheckBox):
                widget.click()
            elif hasattr(widget, '_btn_group'):
                next(b for b in widget._btn_group.buttons() if not b.isChecked()).click()
            else:
                continue
            settle()
            assert window.lbl_opt_preview.text() != before, field['name']
            data = bytes.fromhex(window.lbl_opt_preview.text())
            assert all(a ^ b == 0xFF for a, b in zip(data[::2], data[1::2]))


def check_popup(combo, screenshots=None, name='popup'):
    combo.showPopup()
    settle()
    view = combo.view()
    target = (combo.currentIndex() + 1) % combo.count()
    index = combo.model().index(target, 0)
    view.scrollTo(index)
    QTest.mouseMove(view.viewport(), view.visualRect(index).center())
    settle()
    for state in (QPalette.Active, QPalette.Inactive):
        palette = view.palette()
        assert palette.color(state, QPalette.Highlight).name() == '#2677bd'
        assert palette.color(state, QPalette.HighlightedText).name() == '#ffffff'
    if screenshots:
        assert view.window().grab().save(str(screenshots / f'{name}.png'))
    QTest.mouseClick(view.viewport(), Qt.LeftButton, pos=view.visualRect(index).center())
    settle()
    assert combo.currentIndex() == target
    combo.showPopup()
    settle()
    QTest.keyClick(combo.view(), Qt.Key_Up if target else Qt.Key_Down)
    QTest.keyClick(combo.view(), Qt.Key_Return)
    settle()
    assert combo.currentIndex() == (target - 1 if target else 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    app = QApplication([])
    app.setStyle('Fusion')
    # Reproduce a large system font instead of relying on a friendly default.
    app.setFont(QFont('SimSun', 18))
    with patch('daplink_manager.usb.core.find', side_effect=AssertionError('unexpected USB access')) as find:
        run_checks(app, args.screenshots)
        find.assert_not_called()


def run_checks(app, screenshots):
    window = DAPLinkManager()
    window.show()
    window.activateWindow()
    settle()
    try:
        check_brand_and_quota(window)
        models = window.opt_config.get_chip_models()
        for width, height in ((900, 600), (1000, 650), (1280, 800)):
            window.resize(width, height)
            for model in models:
                window.cmb_chip.setCurrentIndex(window.cmb_chip.findData(model))
                assert window.current_model == model and window.current_chip == window.opt_config.get_chip(model)
                settle()
                assert window.width() == width and window.height() == height
                check_layout(window)
                if width == 1000:
                    check_rpo_wheel(window)
                    check_splitters(window)
                    check_preview(window)
                    window._build_opt_ui()
                    settle()
                    if screenshots:
                        window.grab().save(str(screenshots / f'{model}.png'))
                        window.opt_scroll.verticalScrollBar().setValue(170)
                        settle()
                        window.grab().save(str(screenshots / f'{model}-fields.png'))
                    window.opt_scroll.verticalScrollBar().setValue(0)
                    combo = window._opt_widgets['RPO']
                    window.opt_scroll.ensureWidgetVisible(combo)
                    settle()
                    check_popup(combo, screenshots, f'{model}-opt-popup')
                window.btn_opt_toggle.click()
                settle()
                assert not window.opt_scroll.isVisible() and not window.lbl_opt_preview.isVisible()
                window.btn_opt_toggle.click()
                settle()
                assert window.opt_scroll.isVisible() and window.lbl_opt_preview.isVisible()
        window.cmb_chip.setCurrentIndex(1)
        settle()
        check_popup(window.cmb_chip, screenshots, 'chip-popup')
        window.cmb_chip.setCurrentIndex(0)
        settle()
        assert not window._opt_widgets
        assert not window.lbl_opt_preview.isVisible() and not window.lbl_opt_preview.text()
        assert not window.btn_opt_toggle.isEnabled()
        assert not window.device.is_connected()
        print(f'PASS {len(models)} chips x 3 window sizes; aligned controls, titles, rows and scrolling')
        print('PASS pinned, live OPT bytes at the last register, defaults, expand/collapse and chip switching')
        print('PASS chip/OPT popup colors, mouse selection and keyboard navigation')
        print('PASS RPO wheel safety, both splitter drags, radio columns, channel row, offline quota and branding')
        print(f'Qt platform: {app.platformName()}, device pixel ratio: {window.devicePixelRatioF()}')
        if screenshots:
            print(f'Screenshots: {screenshots}')
    finally:
        window.close()


if __name__ == '__main__':
    main()
