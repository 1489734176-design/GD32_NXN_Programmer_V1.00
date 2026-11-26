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
            if cmd == host.CMD_PREPARE_DOWNLOAD:
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

    def import_program(self, quota, channel_b=True):
        worker = host.ImportWorker(self.device, str(self.bin), 'algo.bin', quota_setting=quota, channel_b=channel_b)
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
            self.usb.powered = False
            self.until(lambda: not window.device.is_connected() and not window.btn_import.isEnabled())
            self.assertEqual(window.remote_tree.topLevelItemCount(), 0)
            self.usb.generation += 1
            self.usb.powered = True
            self.until(lambda: window.btn_upload.isEnabled() and window.remote_tree.topLevelItemCount() == 1)
            self.assertTrue(window.chk_quota.isChecked())
            self.assertEqual(window.spin_quota.value(), 63)

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
