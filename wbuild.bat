@echo off
echo Running build on WSL2
wsl --cd ~/MFoES02w ./build-cross.sh Release

echo Copying to SD card...
copy build-aarch64\bin\mfoes02w H:\mfoes02w /Y
xcopy /Y /I build-aarch64\lib\ H:\mfoes02w\libs\
: copy service
copy build-aarch64\bin\usbd H:\ /Y
copy build-aarch64\bin\otad H:\ /Y

echo Done