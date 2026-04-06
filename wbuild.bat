@echo off
echo Running build on WSL2
: wsl rm -rf build-aarch64
echo compiling app and services (usbd, otad)
wsl ./build-cross.sh Release

uploadsd.bat
echo Done