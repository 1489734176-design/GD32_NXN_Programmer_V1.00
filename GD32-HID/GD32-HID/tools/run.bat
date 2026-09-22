@echo off
REM One-click launcher for DAPLINK File Manager.
REM Uses the local .venv (Python 3.12) so it does NOT run under the
REM incompatible Python 3.14 alpha that causes a segfault.

cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
    echo [ERROR] .venv not found. Create it first:
    echo   py -3.12 -m venv .venv
    echo   .venv\Scripts\python -m pip install intelhex pyusb PyQt5 libusb-package
    pause
    exit /b 1
)

".venv\Scripts\pythonw.exe" daplink_manager.py
