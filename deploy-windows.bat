@echo off
rem Copies cross-compiled binaries to the SD card, natively on Windows
rem (SD card already has a normal drive letter -- no drvfs mount step needed).
rem
rem Usage: deploy-windows.cmd [Drive]
rem   deploy-windows.cmd
rem   deploy-windows.cmd H:

setlocal

set "DRIVE=J:"
if not "%~1"=="" set "DRIVE=%~1"

if not exist "%DRIVE%\" (
    echo %DRIVE% not found. Is the SD card plugged in?
    exit /b 1
)

set "BUILD_DIR=build-aarch64"

if not exist "%DRIVE%\mfoes02w\libs" mkdir "%DRIVE%\mfoes02w\libs"

copy /y "%BUILD_DIR%\bin\mfoes02w" "%DRIVE%\mfoes02w\" >nul
copy /y "%BUILD_DIR%\bin\usbd" "%DRIVE%\" >nul
copy /y "%BUILD_DIR%\bin\otad" "%DRIVE%\" >nul
xcopy /y /e /i "%BUILD_DIR%\lib\*" "%DRIVE%\mfoes02w\libs\" >nul

rem Optional target-side test tools (skipped silently if not built)
if exist "%BUILD_DIR%\bin\nb_link_test" copy /y "%BUILD_DIR%\bin\nb_link_test" "%DRIVE%\" >nul
if exist "%BUILD_DIR%\bin\cam_capture" copy /y "%BUILD_DIR%\bin\cam_capture" "%DRIVE%\" >nul

echo Done! Deployed to %DRIVE%
