@echo off
echo Running build on WSL2
: wsl rm -rf build-aarch64
wsl ./build-cross.sh Release

echo Copying to SD card...
copy build-aarch64\bin\mfoes02w H:\ /Y
: xcopy /E /I /Y build-aarch64\libs H:\libs

echo Done