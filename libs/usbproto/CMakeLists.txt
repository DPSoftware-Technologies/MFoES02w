cmake_minimum_required(VERSION 3.16)
project(usbproto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# usbproto — the MFoES USB channel wire format.
#
# The core (framing, channel demux, record reassembly, Session) is plain C++
# with no platform dependency, so the firmware, the host tools and the tests
# can all link it.
#
# The device side needs no transport of its own: an app on the Pi talks to
# usbd through UsbdClient (libs/hwinterface), and usbd does the MUBD framing.
# Only the host needs to speak the wire format directly, which is what the
# optional libusb transport is for.
# ============================================================================
option(USBPROTO_WITH_LIBUSB "Build the host-side libusb transport" OFF)
option(USBPROTO_WITH_BROKER "Build the local TCP channel broker"   OFF)

set(USBPROTO_SOURCES
    usbproto.cpp
    usbproto_session.cpp
    usbproto_file.cpp
)

if(USBPROTO_WITH_BROKER)
    list(APPEND USBPROTO_SOURCES usbproto_broker.cpp)
endif()

if(USBPROTO_WITH_LIBUSB)
    list(APPEND USBPROTO_SOURCES usbproto_libusb.cpp)
endif()

add_library(usbproto ${USBPROTO_SOURCES})

target_include_directories(usbproto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(USBPROTO_WITH_BROKER)
    target_compile_definitions(usbproto PUBLIC USBPROTO_WITH_BROKER)
    if(WIN32 OR MINGW)
        target_link_libraries(usbproto PUBLIC ws2_32)
    endif()
    message(STATUS "usbproto: local TCP broker ENABLED")
endif()

if(USBPROTO_WITH_LIBUSB)
    target_compile_definitions(usbproto PUBLIC USBPROTO_WITH_LIBUSB)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB libusb-1.0)
    endif()

    if(LIBUSB_FOUND)
        target_include_directories(usbproto PUBLIC ${LIBUSB_INCLUDE_DIRS})
        target_link_directories(usbproto PUBLIC ${LIBUSB_LIBRARY_DIRS})
        target_link_libraries(usbproto PUBLIC ${LIBUSB_LIBRARIES})
    else()
        # No pkg-config (common on MSYS2/Windows) — fall back to a plain search.
        find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
        find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
        if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "usbproto: USBPROTO_WITH_LIBUSB=ON but libusb-1.0 was not found.\n"
                "  MSYS2/UCRT64 : pacman -S mingw-w64-ucrt-x86_64-libusb\n"
                "  Debian/Ubuntu: sudo apt install libusb-1.0-0-dev")
        endif()
        target_include_directories(usbproto PUBLIC ${LIBUSB_INCLUDE_DIR})
        target_link_libraries(usbproto PUBLIC ${LIBUSB_LIBRARY})
    endif()

    message(STATUS "usbproto: libusb (host) transport ENABLED")
endif()
