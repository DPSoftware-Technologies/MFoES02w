@echo off
rem Fetches an Alpine aarch64 sysroot (musl libc, libstdc++, gcc crt objects,
rem Linux headers) directly onto Windows -- no WSL/VM required. Used by the
rem clang cross toolchain (cmake/aarch64-alpine-musl-clang-windows.cmake).
rem
rem Usage: fetch-sysroot.cmd [AlpineVersion] [SysrootDir]
rem   fetch-sysroot.cmd
rem   fetch-sysroot.cmd v3.20 E:\sysroots\alpine-aarch64

setlocal enabledelayedexpansion

set "ALPINE_VERSION=v3.20"
if not "%~1"=="" set "ALPINE_VERSION=%~1"

set "SYSROOT_DIR=E:\sysroots\alpine-aarch64"
if not "%~2"=="" set "SYSROOT_DIR=%~2"

set "MIRROR=https://dl-cdn.alpinelinux.org/alpine/%ALPINE_VERSION%/main/aarch64"
set "PACKAGES=musl musl-dev libgcc libstdc++ libstdc++-dev linux-headers gcc"

set "WORKDIR=%TEMP%\alpine-sysroot-fetch"
if not exist "%WORKDIR%" mkdir "%WORKDIR%"
if not exist "%SYSROOT_DIR%" mkdir "%SYSROOT_DIR%"

echo Fetching APKINDEX from %MIRROR% ...
curl -fsSL -o "%WORKDIR%\APKINDEX.tar.gz" "%MIRROR%/APKINDEX.tar.gz" || goto :err
tar -xzf "%WORKDIR%\APKINDEX.tar.gz" -C "%WORKDIR%" APKINDEX || goto :err

set "CUR_NAME="
set "CUR_VER="

rem NOTE: `for /f` silently skips blank lines, so APKINDEX's blank-line block
rem separators never fire as loop iterations. Flush the previous block when a
rem new "P:" line starts instead, plus once more after the loop for the last one.
for /f "usebackq delims=" %%L in ("%WORKDIR%\APKINDEX") do (
    set "LINE=%%L"
    if "!LINE:~0,2!"=="P:" (
        call :maybe_fetch "!CUR_NAME!" "!CUR_VER!"
        set "CUR_NAME=!LINE:~2!"
        set "CUR_VER="
    ) else if "!LINE:~0,2!"=="V:" (
        set "CUR_VER=!LINE:~2!"
    )
)
call :maybe_fetch "!CUR_NAME!" "!CUR_VER!"

echo.
echo Sysroot populated at %SYSROOT_DIR%
echo Verify: %SYSROOT_DIR%\usr\include\stdio.h and %SYSROOT_DIR%\usr\lib\libstdc++.a should exist.
exit /b 0

:maybe_fetch
set "NAME=%~1"
set "VER=%~2"
if "%NAME%"=="" exit /b 0
for %%P in (%PACKAGES%) do (
    if "%%P"=="%NAME%" (
        set "APK=%NAME%-%VER%.apk"
        echo Downloading !APK! ...
        curl -fsSL -o "%WORKDIR%\!APK!" "%MIRROR%/!APK!" || goto :err
        echo Extracting !APK! -^> %SYSROOT_DIR%
        tar -xzf "%WORKDIR%\!APK!" -C "%SYSROOT_DIR%" --exclude=".SIGN*" --exclude=".PKGINFO" || goto :err
    )
)
exit /b 0

:err
echo ERROR: fetch-sysroot failed.
exit /b 1
