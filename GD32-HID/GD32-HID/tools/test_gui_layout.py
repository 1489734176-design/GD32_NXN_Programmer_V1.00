#!/usr/bin/env python3
"""Native Qt layout/interaction checks; never connects to USB or writes bindings.

Run with the tools Python 3.12 venv. Optional: --screenshots <directory>.
Repeat with QT_SCALE_FACTOR=1.25 or 1.5 to check scaled layouts.
"""
import argparse
from pathlib import Path

from PyQt5.QtCore import QPoint, Qt
from PyQt5.QtGui import QFont, QPalette
from PyQt5.QtTest import QTest
from PyQt5.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QGroupBox, QLabel, QStyle,
    QStyleOptionGroupBox,
)

from daplink_manager import DAPLinkManager


def settle():
    QApplication.processEvents()
    QTest.qWait(40)


def check_layout(window):
    controls = (window.cmb_chip, window.algorithm_edit,
                window.btn_bind_algorithm, window.btn_opt_toggle)
    assert {c.height() for c in controls} == {window.control_height}
    assert window.cmb_chip.size() == window.algorithm_edit.size()
    assert window.btn_bind_algorithm.size() == window.btn_opt_toggle.size()
    assert window.cmb_chip.y() == window.btn_opt_toggle.y()
    assert window.algorithm_edit.y() == window.btn_bind_algorithm.y()
    assert window.cmb_chip.x() == window.algorithm_edit.x()
    for i, control in enumerate(controls):
        for other in controls[i + 1:]:
            assert not control.geometry().intersects(other.geometry())
        assert window.chip_opt_frame.rect().contains(control.geometry())

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
                for j, button in enumerate(buttons):
                    assert button.width() >= button.minimumSizeHint().width()
                    assert child.rect().contains(button.geometry())
                    for other in buttons[j + 1:]:
                        assert not button.geometry().intersects(other.geometry())

    bar = window.opt_scroll.verticalScrollBar()
    assert bar.maximum() > 0
    bar.setValue(bar.maximum())
    settle()
    last = groups[-1]
    bottom = last.mapTo(window.opt_scroll.viewport(), last.rect().bottomLeft())
    assert bottom.y() < window.opt_scroll.viewport().height()
    bar.setValue(0)


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
    window = DAPLinkManager()
    window.show()
    try:
        models = window.opt_config.get_chip_models()
        for width, height in ((900, 600), (1000, 650), (1280, 800)):
            window.resize(width, height)
            for model in models:
                window.cmb_chip.setCurrentText(model)
                settle()
                assert window.width() == width and window.height() == height
                check_layout(window)
                if width == 1000:
                    check_preview(window)
                    window._build_opt_ui()
                    settle()
                    if args.screenshots:
                        window.grab().save(str(args.screenshots / f'{model}.png'))
                        window.opt_scroll.verticalScrollBar().setValue(170)
                        settle()
                        window.grab().save(str(args.screenshots / f'{model}-fields.png'))
                    window.opt_scroll.verticalScrollBar().setValue(0)
                    combo = next(w for w in window._opt_widgets.values()
                                 if isinstance(w, QComboBox))
                    window.opt_scroll.ensureWidgetVisible(combo)
                    settle()
                    check_popup(combo, args.screenshots, f'{model}-opt-popup')
                window.btn_opt_toggle.click()
                settle()
                assert not window.opt_scroll.isVisible()
                window.btn_opt_toggle.click()
                settle()
                assert window.opt_scroll.isVisible()
        window.cmb_chip.setCurrentIndex(1)
        settle()
        check_popup(window.cmb_chip, args.screenshots, 'chip-popup')
        window.cmb_chip.setCurrentIndex(0)
        settle()
        assert not window._opt_widgets
        assert not window.btn_opt_toggle.isEnabled()
        assert not window.device.is_connected()
        print(f'PASS {len(models)} chips x 3 window sizes; aligned controls, titles, rows and scrolling')
        print('PASS OPT defaults, field changes, inverse bytes, expand/collapse and chip switching')
        print('PASS chip/OPT popup colors, mouse selection and keyboard navigation')
        print(f'Qt platform: {app.platformName()}, device pixel ratio: {window.devicePixelRatioF()}')
        if args.screenshots:
            print(f'Screenshots: {args.screenshots}')
    finally:
        window.close()


if __name__ == '__main__':
    main()
