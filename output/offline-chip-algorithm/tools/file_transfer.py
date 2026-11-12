#!/usr/bin/env python3
"""
CMSIS-DAPLINK File Transfer Tool
Communicates with GD32 DAPLINK device via WinUSB custom bulk interface.
Transfers files to/from the device's SPI Flash (FatFs file system).

Usage:
    python file_transfer.py list [path]
    python file_transfer.py push <local_file> <remote_path>
    python file_transfer.py pull <remote_path> <local_file>
    python file_transfer.py mkdir <path>
    python file_transfer.py rm <path>
    python file_transfer.py info

Requires: pyusb (pip install pyusb)
On Windows: WinUSB driver must be installed (automatic on Win8.1+ with BOS descriptor)
On Linux: may need udev rules for non-root access
"""

import sys
import os
import time
import struct
import usb.core
import usb.util

# Use libusb-package as backend (bundled libusb DLL)
try:
    import libusb_package
    _original_find = usb.core.find
    def _find_with_backend(*args, **kwargs):
        kwargs.setdefault('backend', libusb_package.get_libusb1_backend())
        return _original_find(*args, **kwargs)
    usb.core.find = _find_with_backend
except ImportError:
    pass

# USB device identifiers
VID = 0x28E9
PID = 0x128A

# Custom Bulk interface number (IF1)
CUSTOMBULK_INTERFACE = 1

# Endpoint addresses
EP_OUT = 0x02  # EP2 OUT
EP_IN = 0x82   # EP2 IN

# Packet size
PACKET_SIZE = 64

# Protocol command IDs
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

# Response status codes
STATUS_OK  = 0x00
STATUS_ERR = 0x01
STATUS_BUSY = 0x02
STATUS_EOF = 0x03

# Max data payload per packet
MAX_DATA = 56

# Timeout (ms)
TIMEOUT = 5000


class DAPLinkDevice:
    def __init__(self):
        self.dev = None
        self.ep_out = None
        self.ep_in = None

    def connect(self):
        """Find and connect to the DAPLINK device."""
        self.dev = usb.core.find(idVendor=VID, idProduct=PID)
        if self.dev is None:
            raise RuntimeError(
                f"DAPLINK device not found (VID={VID:#06x}, PID={PID:#06x}).\n"
                "Make sure the device is connected and powered."
            )

        # Detach kernel driver if needed (Linux)
        try:
            if self.dev.is_kernel_driver_active(CUSTOMBULK_INTERFACE):
                self.dev.detach_kernel_driver(CUSTOMBULK_INTERFACE)
        except (usb.core.USBError, NotImplementedError):
            pass

        # Set configuration
        self.dev.set_configuration()

        # Get the custom bulk interface
        cfg = self.dev.get_active_configuration()
        intf = cfg[(CUSTOMBULK_INTERFACE, 0)]

        # Find endpoints
        self.ep_out = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        )
        self.ep_in = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
        )

        if self.ep_out is None or self.ep_in is None:
            raise RuntimeError("Could not find custom bulk endpoints. Is the firmware correct?")

        print(f"Connected to DAPLINK device (VID={VID:#06x}, PID={PID:#06x})")
        print(f"  EP OUT: {self.ep_out.bEndpointAddress:#04x}, EP IN: {self.ep_in.bEndpointAddress:#04x}")

    def _send(self, data):
        """Send data to the device."""
        if len(data) > PACKET_SIZE:
            data = data[:PACKET_SIZE]
        self.ep_out.write(data, timeout=TIMEOUT)

    def _recv(self):
        """Receive response from the device."""
        data = self.ep_in.read(PACKET_SIZE, timeout=TIMEOUT)
        return bytes(data)

    def _send_cmd(self, cmd_data):
        """Send a command and receive the response."""
        self._send(cmd_data)
        return self._recv()

    @staticmethod
    def _fix_path(path):
        """Convert path to FatFs format for device.
        Handles Git Bash path conversion and adds '0:' prefix.
        """
        git_prefix = 'C:/Program Files/Git'
        if path.startswith(git_prefix):
            path = path[len(git_prefix):]
        if path.startswith('0:'):
            return path
        return '0:' + path.lstrip('/')

    def list_dir(self, path="/"):
        """List files in a directory on the device."""
        path_bytes = self._fix_path(path).encode('utf-8')
        resp = self._send_cmd(bytes([CMD_LIST_DIR]) + path_bytes)

        status = resp[0]
        if status == STATUS_ERR:
            raise RuntimeError(f"Failed to list directory: {path}")

        # Parse file list (newline-separated)
        data = resp[1:]
        entries = data.decode('utf-8', errors='replace').strip().split('\n')
        result = []
        for entry in entries:
            entry = entry.strip()
            if not entry:
                continue
            if entry.endswith('/'):
                result.append(('DIR', entry[:-1]))
            else:
                result.append(('FILE', entry))
        return result

    def push(self, local_path, remote_path):
        """Upload a file to the device."""
        file_size = os.path.getsize(local_path)
        print(f"Uploading: {local_path} -> {remote_path} ({file_size} bytes)")

        # Open file for writing on device
        path_bytes = self._fix_path(remote_path).encode('utf-8')
        size_bytes = struct.pack('<I', file_size)
        resp = self._send_cmd(bytes([CMD_OPEN_WRITE]) + size_bytes + path_bytes)

        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Failed to open file for writing: {remote_path}")

        # Send data in chunks
        sent = 0
        with open(local_path, 'rb') as f:
            while sent < file_size:
                chunk = f.read(MAX_DATA)
                if not chunk:
                    break

                resp = self._send_cmd(bytes([CMD_WRITE_DATA]) + chunk)
                if resp[0] != STATUS_OK:
                    raise RuntimeError(f"Write error at byte {sent}")

                sent += len(chunk)
                # Progress
                pct = sent * 100 // file_size
                bar = '#' * (pct // 5) + '-' * (20 - pct // 5)
                print(f"\r  [{bar}] {pct}% ({sent}/{file_size})", end='', flush=True)

        # Close file
        resp = self._send_cmd(bytes([CMD_CLOSE_FILE]))
        print(f"\n  Done. {sent} bytes written.")

    def pull(self, remote_path, local_path):
        """Download a file from the device."""
        path_bytes = self._fix_path(remote_path).encode('utf-8')
        resp = self._send_cmd(bytes([CMD_OPEN_READ]) + path_bytes)

        if resp[0] != STATUS_OK:
            raise RuntimeError(f"Failed to open file for reading: {remote_path}")

        file_size = struct.unpack('<I', resp[1:5])[0]
        print(f"Downloading: {remote_path} -> {local_path} ({file_size} bytes)")

        received = 0
        with open(local_path, 'wb') as f:
            while received < file_size:
                # Request data chunk
                req_len = min(MAX_DATA, file_size - received)
                resp = self._send_cmd(bytes([CMD_READ_DATA, req_len]))

                status = resp[0]
                data = resp[1:]

                if status == STATUS_ERR:
                    raise RuntimeError(f"Read error at byte {received}")

                f.write(data)
                received += len(data)

                # Progress
                pct = received * 100 // file_size if file_size > 0 else 100
                bar = '#' * (pct // 5) + '-' * (20 - pct // 5)
                print(f"\r  [{bar}] {pct}% ({received}/{file_size})", end='', flush=True)

                if status == STATUS_EOF:
                    break

        print(f"\n  Done. {received} bytes received.")

    def mkdir(self, path):
        """Create a directory on the device."""
        path_bytes = self._fix_path(path).encode('utf-8')
        resp = self._send_cmd(bytes([CMD_MKDIR]) + path_bytes)
        if resp[0] == STATUS_OK:
            print(f"Directory created: {path}")
        else:
            raise RuntimeError(f"Failed to create directory: {path}")

    def rm(self, path):
        """Delete a file on the device."""
        path_bytes = self._fix_path(path).encode('utf-8')
        resp = self._send_cmd(bytes([CMD_DELETE_FILE]) + path_bytes)
        if resp[0] == STATUS_OK:
            print(f"Deleted: {path}")
        else:
            raise RuntimeError(f"Failed to delete: {path}")

    def get_status(self):
        """Get device status."""
        resp = self._send_cmd(bytes([CMD_GET_STATUS]))
        if resp[0] != STATUS_OK:
            raise RuntimeError("Failed to get status")

        file_opened = resp[1]
        bytes_transferred = struct.unpack('<I', resp[2:6])[0]
        file_size = struct.unpack('<I', resp[6:10])[0]

        state = {0: 'Closed', 1: 'Reading', 2: 'Writing'}.get(file_opened, 'Unknown')
        print(f"Device status:")
        print(f"  File state: {state}")
        print(f"  Bytes transferred: {bytes_transferred}")
        print(f"  File size: {file_size}")

    def close(self):
        """Release the device."""
        if self.dev is not None:
            usb.util.dispose_resources(self.dev)
            self.dev = None


def print_usage():
    print("CMSIS-DAPLINK File Transfer Tool")
    print()
    print("Usage:")
    print("  python file_transfer.py list [path]              List directory")
    print("  python file_transfer.py push <local> <remote>    Upload file")
    print("  python file_transfer.py pull <remote> <local>    Download file")
    print("  python file_transfer.py mkdir <path>             Create directory")
    print("  python file_transfer.py rm <path>                Delete file")
    print("  python file_transfer.py info                     Get device status")
    print()
    print("Examples:")
    print("  python file_transfer.py list /")
    print("  python file_transfer.py list /download/bin/")
    print("  python file_transfer.py push firmware.bin /download/bin/firmware.bin")
    print("  python file_transfer.py pull /download/bin/firmware.bin local.bin")
    print("  python file_transfer.py mkdir /download/newdir")


def main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    cmd = sys.argv[1].lower()

    try:
        dev = DAPLinkDevice()
        dev.connect()

        if cmd == 'list':
            path = sys.argv[2] if len(sys.argv) > 2 else '/'
            entries = dev.list_dir(path)
            if not entries:
                print(f"Directory empty or not found: {path}")
            else:
                print(f"Contents of {path}:")
                for entry_type, name in entries:
                    prefix = '  [DIR] ' if entry_type == 'DIR' else '  [FILE]'
                    print(f"{prefix} {name}")

        elif cmd == 'push':
            if len(sys.argv) < 4:
                print("Usage: file_transfer.py push <local_file> <remote_path>")
                sys.exit(1)
            local_path = sys.argv[2]
            remote_path = sys.argv[3]
            if not os.path.exists(local_path):
                print(f"Local file not found: {local_path}")
                sys.exit(1)
            dev.push(local_path, remote_path)

        elif cmd == 'pull':
            if len(sys.argv) < 4:
                print("Usage: file_transfer.py pull <remote_path> <local_file>")
                sys.exit(1)
            remote_path = sys.argv[2]
            local_path = sys.argv[3]
            dev.pull(remote_path, local_path)

        elif cmd == 'mkdir':
            if len(sys.argv) < 3:
                print("Usage: file_transfer.py mkdir <path>")
                sys.exit(1)
            dev.mkdir(sys.argv[2])

        elif cmd == 'rm':
            if len(sys.argv) < 3:
                print("Usage: file_transfer.py rm <path>")
                sys.exit(1)
            dev.rm(sys.argv[2])

        elif cmd == 'info':
            dev.get_status()

        else:
            print(f"Unknown command: {cmd}")
            print_usage()
            sys.exit(1)

    except RuntimeError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except usb.core.USBError as e:
        print(f"USB Error: {e}")
        sys.exit(1)
    finally:
        try:
            dev.close()
        except:
            pass


if __name__ == '__main__':
    main()
