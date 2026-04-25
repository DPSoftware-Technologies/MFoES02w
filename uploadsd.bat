echo Copying to SD card...
copy build-aarch64\bin\mfoes02w J:\mfoes02w /Y
xcopy /Y /I build-aarch64\lib\ J:\mfoes02w\libs\
: copy service
copy build-aarch64\bin\usbd J:\ /Y
copy build-aarch64\bin\otad J:\ /Y