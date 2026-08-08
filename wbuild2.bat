@echo off
call build-cross-windows.bat
if errorlevel 1 (
    echo Build failed - aborting upload.
    exit /b 1
)
REM One-time bootstrap of a working otad: put SD in PC, uncomment next line.
REM call deploy-windows.bat J:
python tools/ota_usb_upload.py --version "1.0"