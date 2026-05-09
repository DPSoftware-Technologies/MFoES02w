cmake_minimum_required(VERSION 3.20)

project(mfoes02w
    VERSION 1.0.0
    LANGUAGES CXX
)

#  Standards 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS ON)

#  Output directories 
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#  Options
option(STATIC_LINK    "Link statically (required for Alpine musl)" ON)
option(ENABLE_TESTS   "Build unit tests" OFF)
option(ENABLE_LOGGING "Enable debug logging" OFF)
option(DESKTOP_BUILD  "Build for desktop with SDL graphics" OFF)
# GFX_DRM_SUPPORT: use kernel DRM/KMS dumb-buffer page-flip instead of /dev/fb0.
# Requires vc4-kms-v3d overlay in config.txt and /dev/dri/card0 on the Pi.
# No Mesa/libdrm runtime dep — uses raw Linux kernel ioctls only.
option(GFX_DRM_SUPPORT "GFX: DRM/KMS backend (raw kernel, no Mesa)" OFF)

#  Static linking for musl 
if(STATIC_LINK)
    add_link_options(
        -static-libgcc
        -static-libstdc++
        -Wl,--gc-sections
    )
    set(BUILD_SHARED_LIBS OFF)
    message(STATUS "Static linking ENABLED (musl-compatible)")
endif()

#  Compiler flags 
add_compile_options(
    -w
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
)

if(NOT DESKTOP_BUILD)
    add_compile_options(-march=armv8-a+simd)
endif()

# Strip dead code on release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_link_options(-Wl,--gc-sections -s)
endif()

if(ENABLE_LOGGING)
    add_compile_definitions(ENABLE_LOGGING)
endif()

if(DESKTOP_BUILD)
    add_compile_definitions(DESKTOP GFXSDL)
    message(STATUS "Desktop build ENABLED with SDL graphics")
endif()

# Forward GFX_DRM_SUPPORT into the libgfx sub-project.
# Mutually exclusive with DESKTOP_BUILD.
if(GFX_DRM_SUPPORT AND NOT DESKTOP_BUILD)
    set(GFX_DRM_SUPPORT ON CACHE BOOL "" FORCE)
    message(STATUS "GFX DRM/KMS backend ENABLED")
elseif(GFX_DRM_SUPPORT AND DESKTOP_BUILD)
    message(WARNING "GFX_DRM_SUPPORT ignored — DESKTOP_BUILD takes priority")
    set(GFX_DRM_SUPPORT OFF CACHE BOOL "" FORCE)
endif()


# --------------library--------------------

add_subdirectory(libs/libgfx)
add_subdirectory(libs/hwinterface)
add_subdirectory(libs/uisys)
add_subdirectory(libs/linfo)
add_subdirectory(libs/UIDisplayWidgets)

#  Subdirectories 
add_executable(mfoes02w
    src/main.cpp
    src/usb_handling.cpp
    src/ui.cpp
    src/app.cpp
)

target_include_directories(mfoes02w
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# Link pthread statically if needed
find_package(Threads REQUIRED)
target_link_libraries(mfoes02w
    PRIVATE
        Threads::Threads
        libgfx
        pthread
        hwinterface 
        uisys
        linfo
        UIDisplayWidgets
)

if(NOT DESKTOP_BUILD)
    # OTA Daemon - Completely static, self-contained
    add_executable(otad
        services/otad.cpp
        libs/hwinterface/usbd_client.cpp
        libs/libgfx/GFX.cpp
    )

    target_include_directories(otad
        PRIVATE
            ${PROJECT_SOURCE_DIR}/libs
    )

    target_link_libraries(otad
        PRIVATE
            pthread
    )

    target_link_options(otad PRIVATE -static)

    # USB Daemon - Completely static, self-contained
    add_executable(usbd
        services/usbd.cpp
    )

    target_include_directories(usbd
        PRIVATE
    )

    target_link_libraries(usbd
        PRIVATE
            pthread
    )

    target_link_options(usbd PRIVATE -static)
endif()

#  Install rules 
if(DESKTOP_BUILD)
    install(TARGETS mfoes02w
        RUNTIME DESTINATION bin
    )
else()
    install(TARGETS mfoes02w otad usbd
        RUNTIME DESTINATION bin
    )
endif()

#  Summary 
message(STATUS "")
message(STATUS "=== alpine-rpi-cmake build config ===")
message(STATUS "  Target arch   : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Static link   : ${STATIC_LINK}")
message(STATUS "  Tests         : ${ENABLE_TESTS}")
message(STATUS "  Logging       : ${ENABLE_LOGGING}")
message(STATUS "  Desktop build : ${DESKTOP_BUILD}")
message(STATUS "  DRM backend   : ${GFX_DRM_SUPPORT}")
message(STATUS "======================================")
message(STATUS "")