@echo off
cd /d "%~dp0"
if exist ".venv\Scripts\pythonw.exe" (
    ".venv\Scripts\pythonw.exe" daplink_manager.py
    exit /b
)
if exist "..\..\..\GD32-HID\GD32-HID\tools\.venv\Scripts\pythonw.exe" (
    "..\..\..\GD32-HID\GD32-HID\tools\.venv\Scripts\pythonw.exe" daplink_manager.py
    exit /b
)
echo Python 3.12 environment required:
echo py -3.12 -m venv .venv
echo .venv\Scripts\python -m pip install intelhex pyusb PyQt5 libusb-package
pause
