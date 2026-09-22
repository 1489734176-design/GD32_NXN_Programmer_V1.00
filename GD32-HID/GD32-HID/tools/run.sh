#!/usr/bin/env bash
# One-click launcher for DAPLINK File Manager (Git Bash).
# Uses the local .venv (Python 3.12); Python 3.14 alpha segfaults with PyQt5.
set -e
cd "$(dirname "$0")"

if [ ! -x ".venv/Scripts/python.exe" ]; then
    echo "[ERROR] .venv not found. Create it first:"
    echo "  py -3.12 -m venv .venv"
    echo "  .venv/Scripts/python -m pip install intelhex pyusb PyQt5 libusb-package"
    exit 1
fi

.venv/Scripts/python.exe daplink_manager.py
