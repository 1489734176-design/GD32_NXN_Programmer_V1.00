@echo off
REM One-click launcher for DAPLINK File Manager.
REM Uses the local .venv (Python 3.12) so it does NOT run under the
REM incompatible Python 3.14 alpha that causes a segfault.

chcp 65001 >nul
cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
    echo [ERROR] .venv not found. Create it first:
    echo   py -3.12 -m venv .venv
    echo   .venv\Scripts\python -m pip install intelhex pyusb PyQt5 libusb-package
    pause
    exit /b 1
)

REM Qt 5.15 的插件加载器走 Win32 ANSI API，工程路径含中文时会把路径解析成 "???"，
REM 导致 qwindows.dll 找不到（"no Qt platform plugin could be initialized"）。
REM 显式指定 Unicode 真实路径来规避。
set "QT_PLUGIN_PATH=%~dp0.venv\Lib\site-packages\PyQt5\Qt5\plugins"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0.venv\Lib\site-packages\PyQt5\Qt5\plugins\platforms"

".venv\Scripts\pythonw.exe" daplink_manager.py
