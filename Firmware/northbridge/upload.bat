@echo off

REM Compile the project
C:\Users\dharmvee/.pico-sdk/ninja/v1.13.2/ninja.exe -C E:\MFoES02w\Firmware\northbridge\build

REM Upload the compiled binary to the board
C:\Users\dharmvee\.pico-sdk\picotool\2.3.0\picotool\picotool.exe load E:\MFoES02w\Firmware\northbridge\build\northbridge.uf2 -fx
