cmake_minimum_required(VERSION 3.16)
project(remote_viewer LANGUAGES CXX)

# ============================================================================
# remote_viewer — host-side renderer for the board's DrawReplay stream.
#
# Built separately from the firmware: the root project cross-compiles for
# aarch64, while this runs on a desktop and needs SDL2 and libusb.
#
#   cmake -B build -S hostTools/remote_viewer
#   cmake --build build
#
# On MSYS2 use the UCRT64 shell — that is where libusb lives:
#   pacman -S mingw-w64-ucrt-x86_64-{SDL2,libusb}
#
# Without libusb, configure with -DUSBPROTO_WITH_LIBUSB=OFF and use --file to
# replay a capture.
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MFOES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The viewer needs a real window, so force the SDL backend.
set(GFX_SDL_SUPPORT ON  CACHE BOOL "" FORCE)
set(GFX_DRM_SUPPORT OFF CACHE BOOL "" FORCE)

# Talk to the board directly unless the user turns it off.
option(USBPROTO_WITH_LIBUSB "Build the host-side libusb transport" ON)

# Lets the viewer share the USB link with other local tools instead of locking
# them out — libusb claims the interface exclusively.
option(USBPROTO_WITH_BROKER "Build the local TCP channel broker" ON)

add_subdirectory(${MFOES_ROOT}/libs/libgfx   libgfx_build)
add_subdirectory(${MFOES_ROOT}/libs/usbproto usbproto_build)

add_executable(remote_viewer main.cpp)

target_link_libraries(remote_viewer PRIVATE libgfx usbproto)

if(MINGW)
    # SDL2 on MinGW pulls in the Windows multimedia bits.
    target_link_libraries(remote_viewer PRIVATE mingw32)
endif()

message(STATUS "remote_viewer: libusb transport = ${USBPROTO_WITH_LIBUSB}")
