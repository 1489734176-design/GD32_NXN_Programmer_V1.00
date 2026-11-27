"""USB power-cycle, import and GUI regression tests; all USB endpoints are simulated."""
import os
import struct
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

import daplink_manager as host
from image_tool import CMD_STAGE_IMAGE
from PyQt5.QtTest import QSignalSpy, QTest
from PyQt5.QtWidgets import QApplication, QFileDialog


class SimulatedUSB:
    def __init__(self):
        self.powered = True
        self.generation = 1
        self.packets = []
        self.status_reply = bytes(10)
        self.drop_reply = None
        self.hold_command = None
        self.stage_image_reply = bytes([host.STATUS_OK])
        self.images = {}
        self.pending_image = None
        self.held = threading.Event()
        self.release = threading.Event()

    def find(self, **kwargs):
        if not self.powered:
            return iter(()) if kwargs.get('find_all') else None
        generation = self.generation
        packet = b''

        def check_handle():
            if not self.powered or generation != self.generation:
                raise host.usb.core.USBError('stale endpoint', errno=5)

        def write(data, timeout):
            nonlocal packet
            check_handle()
            packet = bytes(data)
            self.packets.append((generation, packet))
            return len(packet)

        def read(size, timeout):
            check_handle()
            cmd = packet[0]
            if cmd == self.drop_reply:
                self.drop_reply = None
                self.generation += 1  # Device received the write, then powered off before ACK.
                check_handle()
            if cmd == self.hold_command:
                self.held.set()
                if not self.release.wait(5):
                    raise AssertionError('test did not release transfer')
            if cmd == host.CMD_GET_STATUS:
                return self.status_reply
            if cmd == host.CMD_LIST_DIR:
                return b'\x00download/'
            if cmd == host.CMD_IMAGE_INFO:
                return bytes([host.STATUS_OK, 1, 100, 255, 0]) + bytes(4)
            if cmd == host.CMD_GET_BURN_QUOTA:
                return bytes([host.STATUS_OK, 2, 0, 0]) + bytes(4)
            if cmd == host.CMD_SET_BURN_QUOTA:
                return bytes([host.STATUS_OK, 2, packet[1], 0]) + packet[2:6]
            if cmd == CMD_STAGE_IMAGE:
                if self.stage_image_reply == bytes([host.STATUS_OK]):
                    self.pending_image = packet
                return self.stage_image_reply
            if cmd == host.CMD_PREPARE_DOWNLOAD:
                if packet[1] & 8:
                    if self.pending_image is None:
                        return bytes([host.STATUS_ERR])
                    self.images[self.pending_image[1]] = (self.pending_image, packet)
                    self.pending_image = None
                return b'\x00' + packet[2:6]
            return bytes([host.STATUS_OK])

        endpoints = [Mock(bEndpointAddress=host.EP_OUT, write=write),
                     Mock(bEndpointAddress=host.EP_IN, read=read)]
        dev = Mock(serial_number='TEST-01', bus=1, port_numbers=(4, 1))
        dev.is_kernel_driver_active.return_value = False
        dev.get_active_configuration.return_value = {(host.CUSTOMBULK_INTERFACE, 0): endpoints}
        return iter([dev]) if kwargs.get('find_all') else dev

    def requests(self, command, generation=None):
        return [p for g, p in self.packets if p[0] == command and (generation is None or g == generation)]


class HostReconnectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.usb = SimulatedUSB()
        self.discovery = self.enterContext(patch.object(host.usb.core, 'find', side_effect=self.usb.find))
        self.dispose = self.enterContext(patch.object(host.usb.util, 'dispose_resources'))
        self.device = host.DAPLinkDevice()
        self.bin = Path(self.enterContext(tempfile.TemporaryDirectory())) / 'demo.bin'
        self.bin.write_bytes(b'host test')

    def until(self, predicate):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            self.app.processEvents()
            if predicate():
                return
            QTest.qWait(5)
        self.fail('timed out waiting for Qt/USB condition')

    def test_status_requires_a_valid_reply_but_busy_is_alive(self):
        self.device.connect()
        for resp in (bytes(10), bytes([host.STATUS_BUSY])):
            self.usb.status_reply = resp
            self.device.ensure_connection()
        self.assertEqual(self.discovery.call_count, 1)
        for resp in (b'', bytes(1), bytes(11), bytes([host.STATUS_ERR]), bytes([host.STATUS_BUSY, 0])):
            with self.subTest(resp=resp), self.assertRaises(ConnectionError):
                self.usb.status_reply = resp
                self.device.check_status()
        self.assertTrue(all(p == bytes([host.CMD_GET_STATUS]) for _, p in self.usb.packets))

    def import_program(self, quota, channel_b=True, image_target=None, opt_path=None):
        worker = host.ImportWorker(self.device, str(self.bin), 'algo.bin', opt_path=opt_path,
                                   quota_setting=quota, channel_b=channel_b, image_target=image_target)
        results = QSignalSpy(worker.finished)
        worker.start()
        self.assertTrue(worker.wait(3000))
        self.until(lambda: len(results) == 1)
        return tuple(results[0])

    def test_first_import_after_reboot_uses_new_handle_and_commits_local_settings(self):
        self.device.connect()
        for enabled, count, channel_b in ((True, 37, True), (False, 88, False), (True, 0, True)):
            with self.subTest(enabled=enabled, count=count):
                old_handle = self.device.dev
                self.usb.generation += 1
                ok, message = self.import_program((enabled, count), channel_b)
                self.assertTrue(ok, message)
                self.dispose.assert_any_call(old_handle)
                packets = [p for gen, p in self.usb.packets if gen == self.usb.generation]
                self.assertEqual(packets[0], bytes([host.CMD_GET_STATUS]))
                limit = bytes([host.CMD_SET_BURN_QUOTA, enabled]) + struct.pack('<I', count if enabled else 0) + b'demo.bin'
                prep = bytes([host.CMD_PREPARE_DOWNLOAD, 2 | (4 if channel_b else 0)]) + struct.pack('<I', sum(self.bin.read_bytes())) + b'demo.bin'
                self.assertEqual(packets.count(limit), 1)
                self.assertEqual(packets[-1], prep)
                self.assertEqual(packets.count(bytes([host.CMD_WRITE_DATA]) + self.bin.read_bytes()), 1)

    def test_import_replaces_only_the_chosen_slot_without_querying_the_table(self):
        self.usb.images[42] = ('unrelated',)
        opt = self.bin.with_name('custom-options.bin')
        opt.write_bytes(b'\xa5\x5a\x00\xff')
        for slot, desc, channel in ((0, 'A', False), (99, '', True), (0, 'A rev2', True)):
            target = host.ImageTarget(slot, 'MM32G0001A', desc)
            self.usb.packets.clear()
            ok, message = self.import_program((True, 12), channel, target, str(opt))
            self.assertTrue(ok, message)
            stages = self.usb.requests(CMD_STAGE_IMAGE)
            self.assertEqual(stages, [bytes([CMD_STAGE_IMAGE, slot]) + b'algo.bin\0MM32G0001A\0' + desc.encode()] * 2)
            program = f'{slot:03d}_demo.bin'.encode()
            checksum = struct.pack('<I', sum(self.bin.read_bytes()))
            self.assertEqual(self.usb.images[slot], (stages[0], bytes([host.CMD_PREPARE_DOWNLOAD, 11 | (4 if channel else 0)]) + checksum + program))
            self.assertEqual(self.usb.requests(host.CMD_SET_BURN_QUOTA)[0][6:], program)
            files = self.usb.requests(host.CMD_OPEN_WRITE)
            self.assertEqual([p[5:].split(b'/')[-1] for p in files], [program, f'{slot:03d}_demo_opt.bin'.encode()])
            self.assertIn(bytes([host.CMD_WRITE_DATA]) + opt.read_bytes(), self.usb.requests(host.CMD_WRITE_DATA))
            for cmd in (host.CMD_IMAGE_INFO, host.CMD_IMAGE_GET_RECORD, host.CMD_IMAGE_COMMIT):
                self.assertFalse(self.usb.requests(cmd))
        self.assertEqual(set(self.usb.images), {0, 42, 99})
        self.assertEqual(self.usb.images[42], ('unrelated',))
        self.assertTrue(self.usb.images[0][0].endswith(b'A rev2'))

    def test_numbered_import_rejects_bad_metadata_or_size_before_writing(self):
        for desc, size in [('A' * 24, 1), ('测试', 1), ('', 0), ('', host.IMAGE_FW_MAX + 1)]:
            with self.subTest(desc=desc, size=size):
                self.bin.write_bytes(bytes(size))
                self.usb.packets.clear()
                ok, message = self.import_program((False, 0), image_target=host.ImageTarget(99, 'MM32SPIN0230', desc))
                self.assertFalse(ok, message)
                for cmd in (CMD_STAGE_IMAGE, host.CMD_STAGE_ALGORITHM, host.CMD_OPEN_WRITE, host.CMD_SET_BURN_QUOTA):
                    self.assertFalse(self.usb.requests(cmd))

    def test_unsupported_image_import_never_writes_or_falls_back(self):
        self.usb.stage_image_reply = bytes([host.STATUS_ERR])
        ok, message = self.import_program((True, 12), image_target=host.ImageTarget(0, 'MM32G0001A'))
        self.assertFalse(ok)
        self.assertIn('固件', message)
        for cmd in (host.CMD_STAGE_ALGORITHM, host.CMD_OPEN_WRITE, host.CMD_SET_BURN_QUOTA, host.CMD_PREPARE_DOWNLOAD):
            self.assertFalse(self.usb.requests(cmd))

    def test_gui_imports_hex_with_selected_model_description_and_exact_opt(self):
        window = host.DAPLinkManager()
        try:
            window.btn_connect.trigger()
            self.until(lambda: window.btn_import.isEnabled() and window.remote_tree.topLevelItemCount())
            window.cmb_chip.setCurrentText('NX32F002TS')
            window.cmb_image.setCurrentText('006')
            note = ' Board/rev.B (003) #19 '  # 23 printable bytes, including surrounding spaces.
            window.edit_image_desc.setText(note)
            window.chk_channel_b.setChecked(True)
            window.chk_quota.setChecked(True)
            window.spin_quota.setValue(39)
            rpo = window._opt_widgets['RPO']
            rpo.setCurrentIndex((rpo.currentIndex() + 1) % rpo.count())
            opt = window._build_opt_bytes()
            ih = host.IntelHex()
            ih[0x8000000], ih[0x8000002] = 0x12, 0x34
            hex_file = self.bin.with_suffix('.hex')
            ih.write_hex_file(str(hex_file))
            original_bin = self.bin.read_bytes()
            with patch.object(QFileDialog, 'getOpenFileName', return_value=(str(hex_file), '')), \
                    patch.object(host.QMessageBox, 'information') as done, \
                    patch.object(host.QMessageBox, 'warning') as warning:
                window.btn_import.trigger()
                self.until(lambda: not window._usb_busy)
                self.assertFalse(warning.called, warning.call_args)
                self.assertIn('006', done.call_args.args[2])
            expected_stage = bytes([CMD_STAGE_IMAGE, 6]) + b'MM32SPIN0230-OPT_ext.bin\0MM32SPIN0230\0' + note.encode('ascii')
            self.assertEqual(self.usb.requests(CMD_STAGE_IMAGE), [expected_stage] * 2)
            self.assertEqual(self.usb.requests(host.CMD_WRITE_DATA), [b'\x03\x12\xff\x34', b'\x03' + opt])
            self.assertIn('00000145', window.lbl_checksum.text())
            self.assertEqual(self.usb.images[6][1][1], 15)
            self.assertEqual(self.usb.requests(host.CMD_SET_BURN_QUOTA)[0][1:6], b'\x01' + struct.pack('<I', 39))
            self.assertEqual(self.bin.read_bytes(), original_bin)
            self.assertFalse(self.usb.requests(host.CMD_IMAGE_INFO))
            self.assertFalse(self.usb.requests(host.CMD_IMAGE_GET_RECORD))
        finally:
            window.close()
            self.app.processEvents()

    def test_lost_ack_does_not_replay_writes(self):
        for command in (host.CMD_WRITE_DATA, host.CMD_SET_BURN_QUOTA, host.CMD_PREPARE_DOWNLOAD):
            with self.subTest(command=command):
                self.device.connect()
                self.usb.packets.clear()
                self.discovery.reset_mock()
                self.usb.drop_reply = command
                ok, message = self.import_program((True, 9))
                self.assertFalse(ok, message)
                self.assertTrue(message)
                self.assertEqual(len(self.usb.requests(command)), 1)
                self.discovery.assert_not_called()
                if command != host.CMD_PREPARE_DOWNLOAD:
                    self.assertFalse(self.usb.requests(host.CMD_PREPARE_DOWNLOAD))

    def test_gui_power_cycle_transfer_and_manual_disconnect(self):
        window = host.DAPLinkManager()
        window._connection_timer.setInterval(40)
        try:
            window.btn_connect.trigger()
            self.until(lambda: window.btn_import.isEnabled() and window.remote_tree.topLevelItemCount() == 1)
            window.chk_quota.setChecked(True)
            window.spin_quota.setValue(63)
            window.cmb_image.setCurrentIndex(window.cmb_image.findData(0))
            window.edit_image_desc.setText('REV-A')
            self.usb.powered = False
            self.until(lambda: not window.device.is_connected() and not window.btn_import.isEnabled())
            self.assertEqual(window.remote_tree.topLevelItemCount(), 0)
            self.usb.generation += 1
            self.usb.powered = True
            self.until(lambda: window.btn_upload.isEnabled() and window.remote_tree.topLevelItemCount() == 1)
            self.assertTrue(window.chk_quota.isChecked())
            self.assertEqual(window.spin_quota.value(), 63)
            self.assertEqual(window.cmb_image.currentData(), 0)
            self.assertEqual(window.edit_image_desc.text(), 'REV-A')
            self.assertFalse(self.usb.requests(host.CMD_IMAGE_INFO))
            self.assertFalse(self.usb.requests(host.CMD_IMAGE_GET_RECORD))

            self.usb.hold_command = host.CMD_WRITE_DATA
            with patch.object(QFileDialog, 'getOpenFileNames', return_value=([str(self.bin)], '')):
                window.btn_upload.trigger()
            self.until(self.usb.held.is_set)
            probes = len(self.usb.requests(host.CMD_GET_STATUS))
            ticks = QSignalSpy(window._connection_timer.timeout)
            self.until(lambda: len(ticks) >= 2)
            self.assertEqual(len(self.usb.requests(host.CMD_GET_STATUS)), probes)
            self.assertFalse(window.btn_import.isEnabled())
            self.usb.release.set()
            self.until(lambda: window.btn_disconnect.isEnabled() and window.btn_upload.isEnabled())
            window.btn_disconnect.trigger()
            attempts = self.discovery.call_count
            QTest.qWait(150)
            self.assertFalse(window.device.is_connected())
            self.assertEqual(self.discovery.call_count, attempts)
        finally:
            self.usb.release.set()
            window._connection_timer.stop()
            self.until(lambda: not window._usb_busy)
            window.close()
            self.app.processEvents()


if __name__ == '__main__':
    unittest.main(verbosity=2)
