@echo off
echo Running build on WSL2
: wsl rm -rf build-aarch64
echo compiling app
wsl ./build-cross.sh Release
echo compiling service
wsl aarch64-linux-gnu-g++ -w -O2 -static -pthread -o build-aarch64/usbd services/usbd.cpp

echo Copying to SD card...
copy build-aarch64\bin\mfoes02w H:\ /Y
copy build-aarch64\usbd H:\ /Y
: xcopy /E /I /Y build-aarch64\libs H:\libs

echo Done