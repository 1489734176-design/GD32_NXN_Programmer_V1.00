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

# Qt 5.15 的插件加载器走 Win32 ANSI API，路径含中文时解析成 "???"，
# 导致 qwindows.dll 找不到。显式指定 Unicode 真实路径来规避。
export QT_PLUGIN_PATH="$(pwd)/.venv/Lib/site-packages/PyQt5/Qt5/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$(pwd)/.venv/Lib/site-packages/PyQt5/Qt5/plugins/platforms"

.venv/Scripts/python.exe daplink_manager.py
