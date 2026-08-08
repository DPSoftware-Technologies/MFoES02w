@echo off
rem Cross-compile mfoes02w for aarch64-linux-musl (RPi Zero 2W) natively on
rem Windows using LLVM/clang -- no WSL/VM required.
rem
rem Usage: build-cross-windows.cmd [BuildType]
rem   build-cross-windows.cmd
rem   build-cross-windows.cmd Debug

setlocal

set "BUILD_TYPE=Release"
if not "%~1"=="" set "BUILD_TYPE=%~1"

set "BUILD_DIR=build-aarch64"

echo Cross-compiling -^> aarch64 musl (native Windows, clang) ^| type=%BUILD_TYPE%

cmake -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-alpine-musl-clang-windows.cmake ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DSTATIC_LINK=ON ^
    -DGFX_DRM_SUPPORT=OFF ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 exit /b 1

copy /y "%BUILD_DIR%\compile_commands.json" ".\compile_commands.json" >nul

echo.
echo Binary : %BUILD_DIR%\bin\mfoes02w
echo Also   : %BUILD_DIR%\bin\otad, %BUILD_DIR%\bin\usbd
