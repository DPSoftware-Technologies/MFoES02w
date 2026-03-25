@echo off
echo Running build on WSL2
: wsl rm -rf build-aarch64
echo compiling app
wsl ./build-cross.sh Release
echo compiling service
wsl aarch64-linux-gnu-g++ -w -O2 -static -pthread -o build-aarch64/usbd services/usbd.cpp

echo Copying to SD card...
copy build-aarch64\bin\mfoes02w H:\mfoes02w /Y
xcopy /Y /I build-aarch64\lib\ H:\mfoes02w\libs\
: copy services
copy build-aarch64\usbd H:\ /Y

echo Done